#include "TextEditorWindow.h"
#include "../app/Application.h"
#include "../util/Win32Util.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

constexpr wchar_t kEditorClass[] = L"ClipboardPlusPlus_TextEditor";
constexpr UINT_PTR kResizeRenderTimerId = 4;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

float UiScale(const AppearanceSettings& appearance) {
    return EffectiveUiScale(appearance);
}

float S(float value, const AppearanceSettings& appearance) {
    return value * UiScale(appearance);
}

ImU32 U32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

int CountLines(const std::string& text) {
    if (text.empty())
        return 1;
    int lines = 1;
    for (char c : text) {
        if (c == '\n')
            ++lines;
    }
    return lines;
}

std::string ExtensionForMode(int mode) {
    switch (mode) {
    case 1: return ".ps1";
    case 2: return ".cmd";
    case 3: return ".json";
    case 4: return ".md";
    default: return ".txt";
    }
}

std::string ModeName(int mode) {
    switch (mode) {
    case 1: return "PowerShell";
    case 2: return "Batch";
    case 3: return "JSON";
    case 4: return "Markdown";
    default: return "Plain text";
    }
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

} // namespace

bool TextEditorWindow::Create(HINSTANCE hInstance,
                              ID3D11Device* device,
                              ID3D11DeviceContext* context) {
    m_hInstance = hInstance;
    m_device = device;
    m_context = context;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kEditorClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kEditorClass,
        L"Clipboard++ Editor",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
        0, 0, m_width, m_height,
        nullptr, nullptr, hInstance,
        static_cast<LPVOID>(this));

    if (!m_hwnd)
        return false;

    ApplyWindowChrome();
    if (!CreateSwapChain()) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyThemeStyle(m_appearance, true);
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);
    RebuildFontAtlas(io, m_appearance);

    ImGui::SetCurrentContext(prevCtx);
    return true;
}

void TextEditorWindow::Destroy() {
    Hide();

    ImGuiContext* destroyedCtx = m_imguiCtx;
    if (m_imguiCtx) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_imguiCtx);
        m_imguiCtx = nullptr;
        ImGui::SetCurrentContext(prevCtx == destroyedCtx ? nullptr : prevCtx);
    }

    DestroySwapChain();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    if (m_hInstance)
        UnregisterClassW(kEditorClass, m_hInstance);
}

void TextEditorWindow::Show() {
    if (!m_hwnd)
        return;

    if (m_settings.openWithClipboard && !m_visible && !m_dirty)
        LoadClipboardText();

    m_width = std::max(520, m_settings.width);
    m_height = std::max(360, m_settings.height);
    PositionInitial();
    ResizeSwapChainToClient();
    ApplyWindowRegion();

    const HWND insertAfter = m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_TOP;
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetWindowPos(m_hwnd, insertAfter, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    m_visible = true;
    m_focusEditorOnOpen = true;
}

void TextEditorWindow::Hide() {
    if (!m_visible)
        return;

    if (m_settings.copyOnClose)
        CopyTextToClipboard();

    m_visible = false;
    m_pendingClose = false;
    m_maximized = false;
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
}

void TextEditorWindow::ApplyAppearance(const AppearanceSettings& settings) {
    if (!m_imguiCtx)
        return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    m_appearance = settings;
    m_appearance.dpiScale = win32util::DpiScaleForWindow(m_hwnd);
    ApplyThemeStyle(m_appearance, true);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    RebuildFontAtlas(ImGui::GetIO(), m_appearance);
    ImGui_ImplDX11_CreateDeviceObjects();
    InvalidateWindowRegion();
    ApplyWindowChrome();
    if (m_visible)
        ApplyWindowRegion();

    ImGui::SetCurrentContext(prevCtx);
}

void TextEditorWindow::ApplySettings(const EditorSettings& settings) {
    m_settings = settings;
    m_width = std::max(520, settings.width);
    m_height = std::max(360, settings.height);
    if (m_visible && m_hwnd) {
        SetWindowPos(m_hwnd, settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, m_width, m_height, SWP_NOMOVE | SWP_NOOWNERZORDER);
        ResizeSwapChainToClient();
        ApplyWindowRegion();
    }
}

SIZE TextEditorWindow::GetCurrentSize() const {
    RECT rc{};
    if (m_hwnd && GetWindowRect(m_hwnd, &rc))
        return {rc.right - rc.left, rc.bottom - rc.top};
    return {m_width, m_height};
}

void TextEditorWindow::Render() {
    if (!m_visible || !m_hwnd || !m_imguiCtx || !m_renderTarget)
        return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({static_cast<float>(rc.right), static_cast<float>(rc.bottom)},
                             ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##text_editor_popup", nullptr, flags);
    DrawTitleBar();
    DrawToolbar();
    DrawEditor();
    DrawStatusBar();

    if (m_pendingClose) {
        ImGui::OpenPopup("Unsaved editor text");
        m_pendingClose = false;
    }
    ConfirmClose();

    ImGui::End();
    ImGui::Render();

    const AppearanceSettings effective = m_appearance.customColors
        ? m_appearance
        : ThemeDefaults(m_appearance.theme);
    const float clear[4] = {
        effective.windowBg.x,
        effective.windowBg.y,
        effective.windowBg.z,
        effective.windowBg.w
    };
    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_context->ClearRenderTargetView(m_renderTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);

    ImGui::SetCurrentContext(prevCtx);
}

bool TextEditorWindow::CreateSwapChain() {
    IDXGIDevice* dxgiDev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;

    if (FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice),
                                        reinterpret_cast<void**>(&dxgiDev))))
        return false;
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const UINT width = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    const UINT height = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);

    factory->Release();
    adapter->Release();
    dxgiDev->Release();

    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

void TextEditorWindow::ResizeSwapChainToClient() {
    if (!m_swapChain)
        return;

    RECT rc{};
    if (!GetClientRect(m_hwnd, &rc))
        return;

    const UINT width = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    const UINT height = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));

    DestroyRenderTarget();
    if (SUCCEEDED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
        CreateRenderTarget();
}

void TextEditorWindow::DestroySwapChain() {
    DestroyRenderTarget();
    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
}

void TextEditorWindow::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void TextEditorWindow::DestroyRenderTarget() {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
}

void TextEditorWindow::ApplyWindowChrome() {
    const int pref = 1;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
}

void TextEditorWindow::ApplyWindowRegion() {
    RECT rc{};
    if (!GetClientRect(m_hwnd, &rc))
        return;

    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int radius = static_cast<int>(std::lround(
        std::clamp(m_appearance.popupRounding, 0.0f, 48.0f) *
        UiScale(m_appearance) * 2.0f));

    if (m_regionCacheValid &&
        m_lastRegionWidth == width &&
        m_lastRegionHeight == height &&
        m_lastRegionRadius == radius) {
        return;
    }

    if (radius <= 1 || m_maximized) {
        SetWindowRgn(m_hwnd, nullptr, TRUE);
    } else {
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
        if (region && SetWindowRgn(m_hwnd, region, TRUE) == 0)
            DeleteObject(region);
    }

    m_regionCacheValid = true;
    m_lastRegionWidth = width;
    m_lastRegionHeight = height;
    m_lastRegionRadius = radius;
}

void TextEditorWindow::InvalidateWindowRegion() {
    m_regionCacheValid = false;
}

void TextEditorWindow::PositionInitial() {
    RECT current{};
    if (m_visible && GetWindowRect(m_hwnd, &current)) {
        SetWindowPos(m_hwnd, m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                     current.left, current.top, m_width, m_height, SWP_NOOWNERZORDER);
        return;
    }

    POINT pt{};
    GetCursorPos(&pt);
    int x = pt.x - m_width / 2;
    int y = pt.y - 48;

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left),
                       static_cast<int>(mi.rcWork.right) - m_width);
        y = std::clamp(y, static_cast<int>(mi.rcWork.top),
                       static_cast<int>(mi.rcWork.bottom) - m_height);
    }

    SetWindowPos(m_hwnd, m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                 x, y, m_width, m_height, SWP_NOOWNERZORDER);
}

void TextEditorWindow::ToggleMaximized() {
    if (m_maximized) {
        SetWindowPos(m_hwnd, m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                     m_restoreRect.left, m_restoreRect.top,
                     m_restoreRect.right - m_restoreRect.left,
                     m_restoreRect.bottom - m_restoreRect.top,
                     SWP_NOOWNERZORDER);
        m_maximized = false;
        InvalidateWindowRegion();
        ResizeSwapChainToClient();
        ApplyWindowRegion();
        return;
    }

    if (!GetWindowRect(m_hwnd, &m_restoreRect))
        return;

    HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi))
        return;

    const RECT& r = mi.rcWork;
    SetWindowPos(m_hwnd, m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                 r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOOWNERZORDER);
    m_maximized = true;
    InvalidateWindowRegion();
    ResizeSwapChainToClient();
    ApplyWindowRegion();
}

void TextEditorWindow::DrawTitleBar() {
    const float h = S(32.0f, m_appearance);
    const float bw = S(44.0f, m_appearance);
    const float w = ImGui::GetWindowSize().x;
    const AppearanceSettings effective = m_appearance.customColors
        ? m_appearance
        : ThemeDefaults(m_appearance.theme);

    ImVec2 wp = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(wp, {wp.x + w, wp.y + h}, U32(effective.titleBarBg));
    dl->AddLine({wp.x, wp.y + h}, {wp.x + w, wp.y + h}, U32(effective.titleBarBorder));

    ImGui::SetCursorPos({S(12.0f, m_appearance), (h - ImGui::GetTextLineHeight()) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Text, effective.titleBarText);
    std::string title = "Editor";
    if (!m_path.empty())
        title += " - " + m_path.filename().string();
    if (m_dirty)
        title += " *";
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();

    auto button = [&](const char* id, float x, ImU32 hover) {
        ImGui::SetCursorPos({x, 0.0f});
        ImGui::InvisibleButton(id, {bw, h});
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
            (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left));
        if (ImGui::IsItemHovered())
            dl->AddRectFilled({wp.x + x, wp.y}, {wp.x + x + bw, wp.y + h}, hover);
        return clicked;
    };

    float x = std::max(0.0f, w - bw * 3.0f);
    if (button("##editor_min", x, U32(effective.titleMinHover)))
        ShowWindow(m_hwnd, SW_MINIMIZE);
    dl->AddLine({wp.x + x + bw * 0.5f - S(5.0f, m_appearance), wp.y + h * 0.5f + 1.0f},
                {wp.x + x + bw * 0.5f + S(5.0f, m_appearance), wp.y + h * 0.5f + 1.0f},
                U32(effective.titleMinGlyph), S(1.5f, m_appearance));
    x += bw;

    if (button("##editor_max", x, U32(effective.titleMaxHover)))
        ToggleMaximized();
    dl->AddRect({wp.x + x + bw * 0.5f - S(5.0f, m_appearance), wp.y + h * 0.5f - S(5.0f, m_appearance)},
                {wp.x + x + bw * 0.5f + S(5.0f, m_appearance), wp.y + h * 0.5f + S(5.0f, m_appearance)},
                U32(effective.titleMaxGlyph), 0.0f, 0, S(1.2f, m_appearance));
    x += bw;

    if (button("##editor_close", x, U32(effective.titleCloseHover))) {
        if (m_dirty && m_settings.confirmClose)
            m_pendingClose = true;
        else
            Hide();
    }
    dl->AddLine({wp.x + x + bw * 0.5f - S(5.0f, m_appearance), wp.y + h * 0.5f - S(5.0f, m_appearance)},
                {wp.x + x + bw * 0.5f + S(5.0f, m_appearance), wp.y + h * 0.5f + S(5.0f, m_appearance)},
                U32(effective.titleCloseGlyph), S(1.5f, m_appearance));
    dl->AddLine({wp.x + x + bw * 0.5f + S(5.0f, m_appearance), wp.y + h * 0.5f - S(5.0f, m_appearance)},
                {wp.x + x + bw * 0.5f - S(5.0f, m_appearance), wp.y + h * 0.5f + S(5.0f, m_appearance)},
                U32(effective.titleCloseGlyph), S(1.5f, m_appearance));

    ImGui::SetCursorPosY(h + S(10.0f, m_appearance));
}

void TextEditorWindow::DrawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {S(10.0f, m_appearance), S(5.0f, m_appearance)});
    if (ImGui::Button("New")) {
        if (!m_dirty || !m_settings.confirmClose)
            ClearDocument();
        else
            m_pendingClose = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("From clipboard"))
        LoadClipboardText();
    ImGui::SameLine();
    if (ImGui::Button("Copy"))
        CopyTextToClipboard();
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        OpenSaveDialog();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        if (m_dirty && m_settings.confirmClose)
            m_pendingClose = true;
        else
            Hide();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ModeName(m_settings.mode).c_str());
    ImGui::PopStyleVar();
    ImGui::Separator();
}

void TextEditorWindow::DrawEditor() {
    const float statusH = m_settings.showStatusBar ? S(28.0f, m_appearance) : 0.0f;
    const float editorH = std::max(S(120.0f, m_appearance),
        ImGui::GetContentRegionAvail().y - statusH - S(6.0f, m_appearance));

    if (m_settings.showLineNumbers) {
        const int lines = CountLines(m_text);
        const float gutterW = std::max(S(48.0f, m_appearance),
            ImGui::CalcTextSize(std::to_string(lines).c_str()).x + S(22.0f, m_appearance));
        ImGui::BeginChild("##editor_line_numbers", {gutterW, editorH}, true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleColor(ImGuiCol_Text, (m_appearance.customColors ? m_appearance : ThemeDefaults(m_appearance.theme)).mutedText);
        for (int i = 1; i <= lines; ++i)
            ImGui::Text("%d", i);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::SameLine();
    }

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (m_settings.allowTabInput)
        flags |= ImGuiInputTextFlags_AllowTabInput;
    if (m_focusEditorOnOpen) {
        ImGui::SetKeyboardFocusHere();
        m_focusEditorOnOpen = false;
    }
    if (m_text.empty())
        m_text.reserve(4096);
    if (ImGui::InputTextMultiline("##editor_text",
                                  m_text.data(),
                                  m_text.capacity() + 1,
                                  {-1.0f, editorH},
                                  flags,
                                  TextResizeCallback,
                                  &m_text)) {
        m_dirty = true;
    }
}

void TextEditorWindow::DrawStatusBar() {
    if (!m_settings.showStatusBar)
        return;

    ImGui::Separator();
    const int lines = CountLines(m_text);
    const size_t bytes = m_text.size();
    ImGui::TextDisabled("%d line%s  |  %zu byte%s",
                        lines, lines == 1 ? "" : "s",
                        bytes, bytes == 1 ? "" : "s");
    if (!m_status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", m_status.c_str());
    }
}

void TextEditorWindow::ClearDocument() {
    m_text.clear();
    m_path.clear();
    m_dirty = false;
    m_status = "New document";
    m_focusEditorOnOpen = true;
}

void TextEditorWindow::LoadClipboardText() {
    m_text = win32util::ClipboardUnicodeText();
    m_dirty = !m_text.empty();
    m_status = m_text.empty() ? "Clipboard is empty" : "Loaded clipboard text";
    m_focusEditorOnOpen = true;
}

void TextEditorWindow::CopyTextToClipboard() {
    const std::wstring wide = win32util::Utf8ToWide(m_text);
    if (win32util::SetClipboardUnicodeText(m_hwnd, wide.c_str(), wide.size()))
        m_status = "Copied to clipboard";
    else
        m_status = "Copy failed";
}

bool TextEditorWindow::SaveAs() {
    if (m_path.empty())
        return false;
    return SaveToPath(m_path);
}

bool TextEditorWindow::SaveToPath(const std::filesystem::path& path) {
    if (!WriteTextFile(path, m_text)) {
        m_status = "Save failed";
        return false;
    }
    m_path = path;
    m_dirty = false;
    m_status = "Saved";
    return true;
}

void TextEditorWindow::OpenSaveDialog() {
    char file[MAX_PATH]{};
    std::string defaultName = "clipboard-editor" + ExtensionForMode(m_settings.mode);
    strncpy_s(file, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "Text and Script Files\0*.txt;*.ps1;*.cmd;*.bat;*.json;*.md\0"
        "All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = "Save editor text";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn))
        SaveToPath(file);
}

void TextEditorWindow::ConfirmClose() {
    if (ImGui::BeginPopupModal("Unsaved editor text", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("This editor has unsaved text.");
        ImGui::Spacing();
        if (ImGui::Button("Save", {90.0f, 0.0f})) {
            OpenSaveDialog();
            if (!m_dirty) {
                ImGui::CloseCurrentPopup();
                Hide();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {90.0f, 0.0f})) {
            m_dirty = false;
            ImGui::CloseCurrentPopup();
            Hide();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {90.0f, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

int TextEditorWindow::TextResizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

LRESULT CALLBACK TextEditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* window = reinterpret_cast<TextEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (window && window->m_imguiCtx)
        ImGui::SetCurrentContext(window->m_imguiCtx);

    const bool imguiHandled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;

    if (window && window->m_imguiCtx)
        ImGui::SetCurrentContext(prevCtx);

    if (imguiHandled)
        return TRUE;

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE)
            return 0;
        break;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        const int border = window
            ? static_cast<int>(std::lround(S(8.0f, window->m_appearance)))
            : 8;
        const int titleH = window
            ? static_cast<int>(std::lround(S(32.0f, window->m_appearance)))
            : 32;
        const int buttonZone = rc.right - (window
            ? static_cast<int>(std::lround(S(44.0f * 3.0f, window->m_appearance)))
            : 132);

        const bool onL = pt.x < border;
        const bool onR = pt.x >= rc.right - border;
        const bool onT = pt.y < border;
        const bool onB = pt.y >= rc.bottom - border;
        if (onT && onL) return HTTOPLEFT;
        if (onT && onR) return HTTOPRIGHT;
        if (onB && onL) return HTBOTTOMLEFT;
        if (onB && onR) return HTBOTTOMRIGHT;
        if (onL) return HTLEFT;
        if (onR) return HTRIGHT;
        if (onT) return HTTOP;
        if (onB) return HTBOTTOM;
        if (pt.y >= 0 && pt.y < titleH && pt.x < buttonZone)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const float dpiScale = win32util::DpiScaleForWindow(hwnd);
        mmi->ptMinTrackSize = {
            static_cast<LONG>(std::lround(520.0f * dpiScale)),
            static_cast<LONG>(std::lround(360.0f * dpiScale))
        };
        return 0;
    }
    case WM_SIZE:
        if (window && window->m_swapChain && wParam != SIZE_MINIMIZED) {
            window->m_width = LOWORD(lParam);
            window->m_height = HIWORD(lParam);
            window->ResizeSwapChainToClient();
            window->InvalidateWindowRegion();
            window->ApplyWindowRegion();
        }
        return 0;
    case WM_DPICHANGED:
        if (window) {
            window->m_appearance.dpiScale = win32util::DpiScaleForWindow(hwnd);
            ApplyThemeStyle(window->m_appearance, true);
            if (RECT* suggested = reinterpret_cast<RECT*>(lParam)) {
                SetWindowPos(hwnd, window->m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOOWNERZORDER);
            }
            window->ResizeSwapChainToClient();
            window->InvalidateWindowRegion();
            window->ApplyWindowRegion();
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        SetTimer(hwnd, kResizeRenderTimerId, 16, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kResizeRenderTimerId);
        return 0;
    case WM_TIMER:
        if (window && wParam == kResizeRenderTimerId)
            window->Render();
        return 0;
    case WM_CLOSE:
        if (window) {
            if (window->m_dirty && window->m_settings.confirmClose)
                window->m_pendingClose = true;
            else
                window->Hide();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
