#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../util/Win32Util.h"
#include "Appearance.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include "../hotkeys/HotkeyManager.h"  // kClipboardPasteMagic

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static constexpr wchar_t kPopupClass[] = L"CPPPopupWnd";
static constexpr UINT_PTR kPopupResizeRenderTimerId = 1;
static constexpr int kPopupResizeBorder = 8;
static constexpr int kPopupTitlePad = 8;
static constexpr int kPopupTitleButton = 22;
static constexpr int kPopupTitleGap = 4;
static constexpr int kPopupTitleHeight = kPopupTitlePad + kPopupTitleButton + 8;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

static const char* PasteMoveLabel(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::Top:    return "Move: Top";
    case ClipboardHistory::MoveTarget::Bottom: return "Move: Bottom";
    default:                                   return "Move: Keep";
    }
}

static ClipboardHistory::MoveTarget NextPasteMoveTarget(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::None:   return ClipboardHistory::MoveTarget::Top;
    case ClipboardHistory::MoveTarget::Top:    return ClipboardHistory::MoveTarget::Bottom;
    case ClipboardHistory::MoveTarget::Bottom: return ClipboardHistory::MoveTarget::None;
    default:                                   return ClipboardHistory::MoveTarget::None;
    }
}

static std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

static std::wstring UrlEncodeWide(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else if (c == ' ') {
            out.push_back(L'+');
        } else {
            out.push_back(L'%');
            out.push_back(static_cast<wchar_t>(kHex[c >> 4]));
            out.push_back(static_cast<wchar_t>(kHex[c & 0x0F]));
        }
    }
    return out;
}

// -- Create / Destroy ----------------------------------------------------------

bool PopupWindow::Create(HINSTANCE hInstance,
                          ID3D11Device* device,
                          ID3D11DeviceContext* context) {
    m_hInstance = hInstance;
    m_device    = device;
    m_context   = context;

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;  // D3D owns the background - prevents white flash on resize
    wc.lpszClassName = kPopupClass;
    RegisterClassExW(&wc);

    // WS_EX_TOPMOST  - always above other windows
    // WS_EX_LAYERED  - needed for SetLayeredWindowAttributes (opacity)
    // WS_EX_NOACTIVATE - don't steal focus from the app being pasted into.
    //                    Keyboard input for the search bar will be handled
    //                    via WH_KEYBOARD_LL in Milestone 4.
    // WS_POPUP + WS_THICKFRAME - borderless but resizable
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        kPopupClass, nullptr,
        WS_POPUP | WS_THICKFRAME,
        0, 0, m_width, m_height,
        nullptr, nullptr, hInstance,
        static_cast<LPVOID>(this));

    if (!m_hwnd) return false;

    if (!CreateSwapChain()) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    // ImGui context - independent from the main window's context
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();

    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io  = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyThemeStyle(ThemeId::DarkDefault, true);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    // Restore the main context so we don't interfere with Application init
    ImGui::SetCurrentContext(prevCtx);
    return true;
}

void PopupWindow::ApplyAppearance(const AppearanceSettings& settings) {
    if (!m_imguiCtx) return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    m_appearance = settings;
    ApplyThemeStyle(m_appearance, true);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    RebuildFontAtlas(ImGui::GetIO(), settings);
    ImGui_ImplDX11_CreateDeviceObjects();
    m_opacity = settings.popupOpacity;
    m_outlineStrength = settings.popupOutlineStrength;
    const float scale = EffectiveUiScale(settings);
    m_width = static_cast<int>(std::lround(settings.popupWidth * scale));
    m_height = static_cast<int>(std::lround(settings.popupHeight * scale));
    ApplyOpacity();
    if (m_visible) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_width, m_height,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ResizeSwapChainToClient();
        ApplyWindowCorners();
    }

    ImGui::SetCurrentContext(prevCtx);
}

void PopupWindow::Destroy() {
    if (m_imguiCtx) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiCtx = nullptr;
        if (prevCtx != m_imguiCtx)
            ImGui::SetCurrentContext(prevCtx);
    }
    DestroySwapChain();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClassW(kPopupClass, m_hInstance);
        m_hwnd = nullptr;
    }
}

// -- Show / Hide ---------------------------------------------------------------

void PopupWindow::Show(bool focusSearch) {
    m_prevForeground = GetForegroundWindow();
    PositionAtCursor();
    ResizeSwapChainToClient();
    ApplyWindowCorners();
    ApplyOpacity();
    ShowWindow(m_hwnd, SW_SHOWNA); // NA = no-activate
    m_visible           = true;
    m_justOpened        = true;
    m_focusSearchOnOpen = focusSearch;
    m_searchActive      = false;
    m_searchCapture     = focusSearch;
    m_keyboardCapture   = true;
    m_maximized         = false;
    m_queueMode         = false;
    m_queue.clear();
    std::memset(m_searchBuf, 0, sizeof(m_searchBuf));
}

void PopupWindow::Hide() {
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible       = false;
    m_searchActive  = false;
    m_searchCapture = false;
    m_dialogTextCapture = false;
    m_newClipboardFocusPending = false;
    m_keyboardCapture = false;
    m_maximized = false;
    m_queueMode     = false;
    m_queue.clear();
}

void PopupWindow::OpenSettingsWindow() {
    ActivateKeyboardCapture();
    Application::Get()->ShowMainWindow();
    Hide();
}

SIZE PopupWindow::GetCurrentSize() const {
    RECT rc{};
    if (m_hwnd && GetWindowRect(m_hwnd, &rc))
        return {rc.right - rc.left, rc.bottom - rc.top};
    return {m_width, m_height};
}

void PopupWindow::RequestSearchFocus() {
    m_focusSearchOnOpen = true;
    m_searchCapture = true;
    m_keyboardCapture = true;
    m_justOpened = true;
}

// -- Render --------------------------------------------------------------------

void PopupWindow::Render() {
    if (!m_visible || !m_imguiCtx) return;

    if (Application* app = Application::Get())
        app->SyncClipboardForForegroundProcess();

    // Switch to popup ImGui context for this frame
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Full-window ImGui overlay - no title bar, fills the entire HWND
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({(float)(rc.right), (float)(rc.bottom)}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f); // opacity handled at OS level

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##popup", nullptr, flags);

    DrawTitleBar();

    // Escape closes
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::End(); ImGui::Render();
        ImGui::SetCurrentContext(prevCtx);
        Hide();
        return;
    }

    DrawSearchBar();
    ImGui::Spacing();
    DrawFilterStrip();
    ImGui::Separator();
    DrawItemList();

    const AppearanceSettings effective = m_appearance.customColors
        ? m_appearance
        : ThemeDefaults(m_appearance.theme);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max = {pos.x + size.x, pos.y + size.y};
    const float rounding = ImGui::GetStyle().WindowRounding;
    const float strength = std::clamp(m_outlineStrength, 0.0f, 1.0f);
    if (strength > 0.001f) {
        ImVec4 glow = effective.accent;
        glow.w = strength * 0.26f;
        ImVec4 soft = effective.accent;
        soft.w = strength * 0.38f;
        ImVec4 crisp = effective.accent;
        crisp.w = strength * 0.92f;
        dl->AddRect({pos.x + 1.0f, pos.y + 1.0f}, {max.x - 1.0f, max.y - 1.0f},
                    ImGui::GetColorU32(glow), rounding, 0, 1.0f + strength * 4.5f);
        dl->AddRect({pos.x + 2.0f, pos.y + 2.0f}, {max.x - 2.0f, max.y - 2.0f},
                    ImGui::GetColorU32(soft), rounding, 0, 0.75f + strength * 2.5f);
        dl->AddRect({pos.x + 0.5f, pos.y + 0.5f}, {max.x - 0.5f, max.y - 0.5f},
                    ImGui::GetColorU32(crisp), rounding, 0, 0.75f + strength * 1.0f);
    }

    ImGui::End();
    ImGui::Render();

    // Clear + present
    constexpr float bg[4] = {0.145f, 0.145f, 0.145f, 1.0f};
    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_context->ClearRenderTargetView(m_renderTarget, bg);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);

    ImGui::SetCurrentContext(prevCtx);
}

// -- Filter strip --------------------------------------------------------------

void PopupWindow::DrawSearchBar() {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if ((m_justOpened && m_focusSearchOnOpen) ||
        (m_searchCapture && !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        ImGui::SetKeyboardFocusHere();
    }
    m_justOpened = false;

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    const bool enterPressed = ImGui::InputTextWithHint("##search", "  Search... Shift+Enter for web",
                                                       m_searchBuf, sizeof(m_searchBuf),
                                                       inputFlags);
    const bool searchHovered = ImGui::IsItemHovered();
    const bool searchInputActive = ImGui::IsItemActive();
    if (m_focusSearchOnOpen || ImGui::IsItemClicked()) {
        m_keyboardCapture = true;
        m_searchCapture = true;
    }

    ImGuiIO& popupIo = ImGui::GetIO();
    if (enterPressed && popupIo.KeyShift)
        LaunchWebSearch();

    m_searchActive = searchInputActive || m_searchCapture;
    m_focusSearchOnOpen = false;

    const bool anyItemActive = ImGui::IsAnyItemActive();
    const size_t searchLen = std::strlen(m_searchBuf);
    if (m_searchActive != m_lastSearchActive ||
        searchInputActive != m_lastSearchInputActive ||
        anyItemActive != m_lastAnyItemActive ||
        popupIo.WantTextInput != m_lastWantTextInput ||
        searchLen != m_lastSearchLen) {
        std::snprintf(m_searchDebug, sizeof(m_searchDebug),
                      "Search debug: active=%d capture=%d input=%d hover=%d anyActive=%d wantText=%d len=%zu text=\"%.48s\"",
                      m_searchActive ? 1 : 0,
                      m_searchCapture ? 1 : 0,
                      searchInputActive ? 1 : 0,
                      searchHovered ? 1 : 0,
                      anyItemActive ? 1 : 0,
                      popupIo.WantTextInput ? 1 : 0,
                      searchLen,
                      m_searchBuf);
        std::string line(m_searchDebug);
        line += "\n";
        OutputDebugStringA(line.c_str());
        m_lastSearchActive = m_searchActive;
        m_lastSearchInputActive = searchInputActive;
        m_lastAnyItemActive = anyItemActive;
        m_lastWantTextInput = popupIo.WantTextInput;
        m_lastSearchLen = searchLen;
    }
}

void PopupWindow::DrawTitleBar() {
    const float height = static_cast<float>(kPopupTitleButton);
    const float gap = static_cast<float>(kPopupTitleGap);
    const float knobSize = height;
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float spacerWidth = std::max(0.0f, fullWidth - height - knobSize - gap * 2.0f);
    const AppearanceSettings effective = m_appearance.customColors ? m_appearance : ThemeDefaults(m_appearance.theme);

    ImGui::PushStyleColor(ImGuiCol_Button, effective.closeButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, effective.closeButtonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, effective.closeButtonHover);
    ImGui::PushStyleColor(ImGuiCol_Text, effective.closeButtonText);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0f, 0.0f});
    if (ImGui::Button("x", {height, height})) {
        Hide();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    ImGui::InvisibleButton("##popup_adjust_knobs", {knobSize, knobSize});
    const bool knobHovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool opacityHovered = knobHovered && mouse.y < (min.y + max.y) * 0.5f;
    const bool outlineHovered = knobHovered && !opacityHovered;
    if (opacityHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        if (Application* app = Application::Get()) {
            app->SetPopupOpacity(m_opacity + ImGui::GetIO().MouseWheel * 0.05f);
        }
    }
    if (outlineHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        if (Application* app = Application::Get()) {
            app->SetPopupOutlineStrength(m_outlineStrength + ImGui::GetIO().MouseWheel * 0.05f);
        }
    }

    const ImVec2 opacityCenter = {(min.x + max.x) * 0.5f, min.y + knobSize * 0.28f};
    const ImVec2 outlineCenter = {(min.x + max.x) * 0.5f, min.y + knobSize * 0.73f};
    const float radius = knobSize * 0.18f;
    const ImVec4 ring = effective.opacityKnobRing;
    const ImVec4 fill = effective.opacityKnobFill;
    auto brighten = [](ImVec4 c, float amount) {
        c.x = std::min(1.0f, c.x + amount);
        c.y = std::min(1.0f, c.y + amount);
        c.z = std::min(1.0f, c.z + amount);
        return c;
    };
    auto drawKnob = [&](ImVec2 center, float value, bool hovered, ImVec4 knobRing, ImVec4 knobFill) {
        const ImU32 ringColor = ImGui::GetColorU32(hovered ? brighten(knobRing, 0.12f) : knobRing);
        const ImU32 fillColor = ImGui::GetColorU32(hovered ? brighten(knobFill, 0.08f) : knobFill);
        dl->AddCircleFilled(center, radius, fillColor, 18);
        dl->AddCircle(center, radius, ringColor, 18, 1.5f);
        const float angle = -1.5708f + value * 6.28318f;
        dl->AddLine(center,
                    {center.x + std::cos(angle) * radius * 0.78f,
                     center.y + std::sin(angle) * radius * 0.78f},
                    ringColor, 1.5f);
    };
    drawKnob(opacityCenter, m_opacity, opacityHovered, ring, fill);
    drawKnob(outlineCenter, m_outlineStrength, outlineHovered, effective.accent, effective.panelBg);
    if (opacityHovered)
        ImGui::SetTooltip("Window opacity %.0f%%", m_opacity * 100.0f);
    else if (outlineHovered)
        ImGui::SetTooltip("Outline %.0f%%", m_outlineStrength * 100.0f);

    Application* app = Application::Get();
    const ClipboardProfileConfig* active = app ? app->GetActiveClipboardProfile() : nullptr;
    if (active && active->id != m_lastClipboardId) {
        m_lastClipboardId = active->id;
        std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", active->name.c_str());
    }

    const float saveW = 42.0f;
    const float deleteW = 28.0f;
    const float selectorW = std::max(130.0f, spacerWidth - saveW - deleteW - gap * 2.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(selectorW - 32.0f);
    ImGui::InputText("##clipboard_name", m_clipboardName, sizeof(m_clipboardName));
    const bool clipboardNameActive = ImGui::IsItemActive();
    if (clipboardNameActive || ImGui::IsItemClicked()) {
        ActivateKeyboardCapture();
        m_dialogTextCapture = true;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("v##clipboard_dropdown", {28.0f, 0.0f}))
        ImGui::OpenPopup("##clipboard_profile_picker");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select another clipboard");

    if (ImGui::BeginPopup("##clipboard_profile_picker")) {
        if (app) {
            for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
                const bool selected = active && profile.id == active->id;
                std::string label = profile.name;
                if (!profile.processName.empty())
                    label += " (" + profile.processName + ")";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    app->SetActiveClipboardProfile(profile.id);
                    std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", profile.name.c_str());
                    m_lastClipboardId = profile.id;
                    m_dialogTextCapture = false;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (!app || !active || m_clipboardName[0] == '\0')
        ImGui::BeginDisabled();
    if (ImGui::Button("Save", {saveW, 0.0f}) && app) {
        app->RenameActiveClipboardProfile(m_clipboardName);
        if (const ClipboardProfileConfig* renamed = app->GetActiveClipboardProfile()) {
            std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", renamed->name.c_str());
            m_lastClipboardId = renamed->id;
        }
        m_dialogTextCapture = false;
    }
    if (!app || !active || m_clipboardName[0] == '\0')
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save clipboard name");

    ImGui::SameLine();
    if (app && !app->CanDeleteActiveClipboardProfile())
        ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
    if (ImGui::Button("x##delete_clipboard", {deleteW, 0.0f}) && app && active) {
        m_deleteClipboardName = active->name;
        MessageBeep(MB_ICONWARNING);
        ImGui::OpenPopup("Delete clipboard?");
    }
    ImGui::PopStyleColor(3);
    if (app && !app->CanDeleteActiveClipboardProfile())
        ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete current clipboard");

    if (ImGui::BeginPopupModal("Delete clipboard?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this clipboard?");
        ImGui::TextDisabled("%s", m_deleteClipboardName.empty() ? "Clipboard" : m_deleteClipboardName.c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
        if (ImGui::Button("Delete", {100.0f, 0.0f}) && app) {
            app->DeleteActiveClipboardProfile();
            if (const ClipboardProfileConfig* next = app->GetActiveClipboardProfile()) {
                std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", next->name.c_str());
                m_lastClipboardId = next->id;
            }
            m_deleteClipboardName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {90.0f, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_deleteClipboardName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (m_dialogTextCapture && !clipboardNameActive &&
               !ImGui::IsPopupOpen("##clipboard_profile_picker")) {
        m_dialogTextCapture = false;
    }

    ImGui::Spacing();
}

static bool PopupToggleButton(const char* label, bool active, const PopupToggleColors& colors) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? colors.on : colors.off);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? colors.onHovered : colors.offHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? colors.onActive : colors.offActive);
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(3);
    return clicked;
}

void PopupWindow::DrawFilterStrip() {
    const PopupToggleColors toggleColors = GetPopupToggleColors(m_appearance);
    struct Btn { const char* label; int mode; };
    static constexpr Btn kFilters[] = {
        {"All",0},{"Text",1},{"Image",2},{"URL",3},{"File",4},
        {"Code",5},{"Secret",6},{"JSON",7},{"Email",8},{"Color",9}
    };
    for (const auto& f : kFilters) {
        bool active = (m_filterMode == f.mode);
        if (PopupToggleButton(f.label, active, toggleColors)) {
            ActivateKeyboardCapture();
            m_filterMode = f.mode;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    bool qActive = m_queueMode;
    if (PopupToggleButton("Queue", qActive, toggleColors)) {
        ActivateKeyboardCapture();
        m_queueMode = !m_queueMode;
        m_queue.clear();
    }
    ImGui::SameLine();

    if (m_queueMode && !m_queue.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.f));
        if (ImGui::SmallButton("Paste All")) {
            ActivateKeyboardCapture();
            PasteQueue();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    bool newlineActive = m_appendNewlineAfterPaste;
    if (PopupToggleButton("Newline", newlineActive, toggleColors)) {
        ActivateKeyboardCapture();
        m_appendNewlineAfterPaste = !m_appendNewlineAfterPaste;
    }
    ImGui::SameLine();

    bool moveActive = m_pasteMoveTarget != ClipboardHistory::MoveTarget::None;
    if (PopupToggleButton(PasteMoveLabel(m_pasteMoveTarget), moveActive, toggleColors)) {
        ActivateKeyboardCapture();
        m_pasteMoveTarget = NextPasteMoveTarget(m_pasteMoveTarget);
    }
    ImGui::SameLine();

    if (ImGui::SmallButton(" @ ")) {       // gear placeholder
        OpenSettingsWindow();
    }
    ImGui::Spacing();
}

// -- Item list -----------------------------------------------------------------

bool PopupWindow::ItemPassesFilter(const ClipboardItem& item) const {
    switch (m_filterMode) {
    case 1: return item.IsText();
    case 2: return item.IsImage();
    case 3: return (item.tags & TAG_URL) != 0;
    case 4: return item.type == ContentType::FilePaths || (item.tags & (TAG_FILE | TAG_FOLDER | TAG_PATH)) != 0;
    case 5: return (item.tags & TAG_CODE) != 0;
    case 6: return (item.tags & TAG_SECRET) != 0;
    case 7: return (item.tags & TAG_JSON) != 0;
    case 8: return (item.tags & TAG_EMAIL) != 0;
    case 9: return (item.tags & TAG_HEX) != 0;
    default: return true;
    }
}

std::vector<size_t> PopupWindow::BuildVisibleHistoryIndices(bool pinnedOnly) const {
    std::vector<size_t> indices;
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return indices;

    const std::string query(m_searchBuf);
    std::string lquery = query;
    std::transform(lquery.begin(), lquery.end(), lquery.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });

    for (size_t i = 0; i < hist->Size() && indices.size() < 35; ++i) {
        const ClipboardItem* item = hist->Get(i);
        if (!item || !ItemPassesFilter(*item)) continue;
        if (item->pinned != pinnedOnly) continue;

        if (!lquery.empty()) {
            std::string lt = item->text;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lt.find(lquery) == std::string::npos) continue;
        }

        indices.push_back(i);
    }

    return indices;
}

void PopupWindow::DrawItemList() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("##items", {0.f, 0.f}, ImGuiChildFlags_None);

    const std::vector<size_t> pinned = BuildVisibleHistoryIndices(true);
    const std::vector<size_t> regular = BuildVisibleHistoryIndices(false);

    auto drawSection = [&](const char* title,
                           const std::vector<size_t>& indices,
                           bool pinnedSection) -> bool {
        if (indices.empty())
            return false;

        if (pinnedSection) {
            char header[128]{};
            std::snprintf(header, sizeof(header), "%s (%zu)", title, indices.size());
            if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                return false;
            }
        } else {
            ImGui::TextDisabled("%s (%zu)", title, indices.size());
            ImGui::Separator();
        }

        for (size_t sectionSlot = 0; sectionSlot < indices.size(); ++sectionSlot) {
            const size_t i = indices[sectionSlot];
            const ClipboardItem* item = hist->Get(i);
            if (!item) continue;

            const std::string key = HotkeyManager::SlotLabelText(static_cast<int>(sectionSlot));

            int qpos = -1;
            for (size_t q = 0; q < m_queue.size(); ++q)
                if (m_queue[q] == item->id) { qpos = (int)q + 1; break; }

            const bool isSecret = (item->tags & TAG_SECRET) != 0;
            if (isSecret)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.42f, 0.42f, 1.f));

            const float rowWidth = ImGui::GetContentRegionAvail().x;
            const int previewChars = std::max(40, static_cast<int>((rowWidth - 92.0f) / 7.0f));
            const std::string preview = item->Preview(static_cast<size_t>(previewChars));

            char label[1024]{};
            const char* pin = pinnedSection ? "[P] " : "";
            if (qpos >= 0)
                std::snprintf(label, sizeof(label), " %s [%d]  %s%s##r%zu",
                              key.c_str(), qpos, pin, preview.c_str(), i);
            else
                std::snprintf(label, sizeof(label), " %s   %s%s##r%zu",
                              key.c_str(), pin, preview.c_str(), i);

            if (ImGui::Selectable(label, qpos >= 0,
                                   ImGuiSelectableFlags_SpanAllColumns)) {
                ActivateKeyboardCapture();
                ReleaseSearchCapture();

                const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

                if (ctrlHeld) {
                    if (isSecret) ImGui::PopStyleColor();
                    const uint64_t itemId = item->id;
                    PasteItemKeepOpen(*item);
                    hist->MoveItemById(itemId, m_pasteMoveTarget);
                    return true;
                } else if (m_queueMode) {
                    if (qpos >= 0)
                        m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), item->id),
                                       m_queue.end());
                    else
                        m_queue.push_back(item->id);
                } else {
                    if (isSecret) ImGui::PopStyleColor();
                    const uint64_t itemId = item->id;
                    PasteItemKeepOpen(*item);
                    hist->MoveItemById(itemId, m_pasteMoveTarget);
                    return true;
                }
            }

            if (pinnedSection) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddCircleFilled({a.x + 7.0f, (a.y + b.y) * 0.5f},
                                    3.0f, IM_COL32(255, 196, 64, 255), 12);
            }

            if (DrawItemContextMenu(*item, qpos)) {
                if (isSecret) ImGui::PopStyleColor();
                return true;
            }
            DrawItemDragDrop(item->id, qpos);
            if (isSecret) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        return false;
    };

    if (drawSection("Pinned entries", pinned, true) ||
        drawSection("History", regular, false)) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    if (!pinned.empty() || !regular.empty()) {
        ImGui::InvisibleButton("##drop_end", {ImGui::GetContentRegionAvail().x, 8.0f});
        if (ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("CPP_HISTORY_IDS")) {
                hist->MoveItemsByIdBefore(m_dragIds, 0);
                m_queue.clear();
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (pinned.empty() && regular.empty())
        ImGui::TextDisabled("  No items match.");

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ActivateKeyboardCapture();
        ReleaseSearchCapture();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

bool PopupWindow::DrawItemContextMenu(const ClipboardItem& item, int qpos) {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist)
        return false;

    bool changed = false;
    const uint64_t itemId = item.id;

    if (ImGui::BeginPopupContextItem()) {
        ActivateKeyboardCapture();
        ReleaseSearchCapture();

        ImGui::TextDisabled("Item %llu", static_cast<unsigned long long>(itemId));
        ImGui::Separator();

        if (ImGui::MenuItem("Paste")) {
            PasteItemKeepOpen(item);
            hist->MoveItemById(itemId, m_pasteMoveTarget);
            changed = true;
        }
        if (ImGui::MenuItem("Copy to clipboard")) {
            WriteToClipboard(item);
        }

        ImGui::Separator();
        if (qpos >= 0) {
            if (ImGui::MenuItem("Remove from queue")) {
                m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), itemId),
                              m_queue.end());
                changed = true;
            }
        } else {
            if (ImGui::MenuItem("Add to queue")) {
                m_queue.push_back(itemId);
                changed = true;
            }
        }

        if (ImGui::MenuItem("Move to top")) {
            hist->MoveItemById(itemId, ClipboardHistory::MoveTarget::Top);
            changed = true;
        }
        if (ImGui::MenuItem("Move to bottom")) {
            hist->MoveItemById(itemId, ClipboardHistory::MoveTarget::Bottom);
            changed = true;
        }
        if (ImGui::MenuItem(item.pinned ? "Unpin" : "Pin")) {
            hist->SetPinnedById(itemId, !item.pinned);
            changed = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), itemId),
                          m_queue.end());
            hist->RemoveItemById(itemId);
            changed = true;
        }

        ImGui::EndPopup();
    }

    return changed;
}

// -- Paste ---------------------------------------------------------------------

void PopupWindow::ReleaseSearchCapture() {
    m_searchCapture = false;
    m_searchActive = false;
    m_focusSearchOnOpen = false;
}

void PopupWindow::ActivateKeyboardCapture() {
    m_keyboardCapture = true;
}

void PopupWindow::NoteExternalMouseDown(POINT screenPoint) {
    RECT popupRect{};
    if (GetWindowRect(m_hwnd, &popupRect) &&
        PtInRect(&popupRect, screenPoint)) {
        return;
    }

    ReleaseSearchCapture();
    m_keyboardCapture = false;

    HWND clicked = WindowFromPoint(screenPoint);
    HWND main = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
    if (clicked) clicked = GetAncestor(clicked, GA_ROOT);
    if (clicked && clicked != m_hwnd && clicked != main) {
        m_prevForeground = clicked;
        if (Application* app = Application::Get())
            app->SyncClipboardForWindow(clicked);
    }
}

void PopupWindow::DrawItemDragDrop(uint64_t itemId, int qpos) {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        if (qpos >= 0)
            m_dragIds = m_queue;
        else
            m_dragIds = { itemId };

        const int count = static_cast<int>(m_dragIds.size());
        ImGui::SetDragDropPayload("CPP_HISTORY_IDS", &count, sizeof(count));
        ImGui::Text("%d item%s", count, count == 1 ? "" : "s");
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("CPP_HISTORY_IDS")) {
            hist->MoveItemsByIdBefore(m_dragIds, itemId);
            m_queue.clear();
        }
        ImGui::EndDragDropTarget();
    }
}

void PopupWindow::PasteHistorySlot(int slot, HWND targetWindow) {
    if (slot < 0) return;

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetRegularCopy(static_cast<size_t>(slot), item)) return;

    m_prevForeground = targetWindow;
    WriteToClipboard(item);
    RestoreFocusAndPaste(targetWindow);
    hist->MoveItemById(item.id, m_pasteMoveTarget);
}

void PopupWindow::PastePinnedSlot(int slot, HWND targetWindow) {
    if (slot < 0) return;

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetPinnedCopy(static_cast<size_t>(slot), item)) return;

    m_prevForeground = targetWindow;
    WriteToClipboard(item);
    RestoreFocusAndPaste(targetWindow);
    hist->MoveItemById(item.id, ClipboardHistory::MoveTarget::None);
}

void PopupWindow::PasteVisibleSlot(int slot) {
    const std::vector<size_t> regular = BuildVisibleHistoryIndices(false);
    if (slot < 0 || static_cast<size_t>(slot) >= regular.size()) return;
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetCopy(regular[static_cast<size_t>(slot)], item)) return;

    PasteItemKeepOpen(item);
    hist->MoveItemById(item.id, m_pasteMoveTarget);
}

void PopupWindow::PasteItemKeepOpen(const ClipboardItem& item) {
    WriteToClipboard(item);
    RestoreFocusAndPaste();
}

void PopupWindow::PasteQueue() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    std::vector<uint64_t> ids = m_queue;
    Hide();

    for (size_t qi = 0; qi < ids.size(); ++qi) {
        ClipboardItem item;
        if (!hist->GetByIdCopy(ids[qi], item)) continue;
        WriteToClipboard(item);
        RestoreFocusAndPaste();
        hist->MoveItemById(item.id, m_pasteMoveTarget);
        if (qi + 1 < ids.size() && m_queueDelayMs > 0)
            Sleep(static_cast<DWORD>(m_queueDelayMs));
    }
}

void PopupWindow::LaunchWebSearch() {
    LaunchWebSearchForText(m_searchBuf, true);
}

void PopupWindow::LaunchClipboardWebSearch() {
    LaunchWebSearchForText(win32util::ClipboardUnicodeText(), m_visible);
}

bool PopupWindow::LaunchWebSearchForText(const std::string& text, bool hideOnSuccess) {
    const std::string query = TrimAscii(text);
    if (query.empty())
        return false;

    const std::wstring url = L"https://www.google.com/search?q=" + UrlEncodeWide(query);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", url.c_str(),
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        if (hideOnSuccess)
            Hide();
        return true;
    }
    return false;
}

void PopupWindow::WriteToClipboard(const ClipboardItem& item) const {
    if (Application::Get() && Application::Get()->GetMonitor())
        Application::Get()->GetMonitor()->SuppressNextUpdate();

    std::string text = item.text;
    const bool isFileDrop = item.type == ContentType::FilePaths || (item.tags & TAG_PATH) != 0;
    if (isFileDrop) {
        std::vector<std::wstring> filePaths = win32util::ExistingPathListUtf8(text);
        if (!filePaths.empty() && win32util::SetClipboardFileDrop(nullptr, filePaths))
            return;
    }

    if (m_appendNewlineAfterPaste && !isFileDrop)
        text += "\r\n";

    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    if (item.type == ContentType::Image && !item.imageData.empty()) {
        HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, item.imageData.size());
        if (hm) {
            std::memcpy(GlobalLock(hm), item.imageData.data(), item.imageData.size());
            GlobalUnlock(hm);
            SetClipboardData(CF_DIB, hm);
        }
    } else {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        text.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE,
                                      static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
            if (hm) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1,
                                    static_cast<wchar_t*>(GlobalLock(hm)), wlen);
                GlobalUnlock(hm);
                SetClipboardData(CF_UNICODETEXT, hm);
            }
        }
    }
    CloseClipboard();
}

HWND PopupWindow::ResolvePasteTarget() const {
    HWND fg = GetForegroundWindow();
    HWND main = Application::Get() ? Application::Get()->GetHwnd() : nullptr;

    if (fg && fg != m_hwnd && fg != main)
        return fg;
    if (m_prevForeground && IsWindow(m_prevForeground))
        return m_prevForeground;
    return nullptr;
}

bool PopupWindow::WaitForForeground(HWND target, DWORD timeoutMs) const {
    if (!target) return false;

    const DWORD started = GetTickCount();
    while (GetForegroundWindow() != target) {
        if (GetTickCount() - started >= timeoutMs)
            return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
    return true;
}

void PopupWindow::RestoreFocusAndPaste(HWND preferredTarget) {
    HWND target = preferredTarget ? preferredTarget : ResolvePasteTarget();
    if (target && IsWindow(target)) {
        if (GetForegroundWindow() != target) {
            SetForegroundWindow(target);
            WaitForForeground(target, 100);
        }
    }

    // All injected events carry kClipboardPasteMagic so our LL hook ignores them.
    const bool ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    const bool altDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

    INPUT in[10]{};
    for (auto& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.dwExtraInfo = kClipboardPasteMagic;
    }

    int n = 0;
    if (altDown) {
        in[n].ki.wVk = VK_MENU;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (shiftDown) {
        in[n].ki.wVk = VK_SHIFT;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (!ctrlDown) {
        in[n].ki.wVk = VK_CONTROL;
        ++n;
    }

    in[n].ki.wVk = 'V';
    ++n;
    in[n].ki.wVk = 'V';
    in[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;

    if (!ctrlDown) {
        in[n].ki.wVk = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (shiftDown) {
        in[n].ki.wVk = VK_SHIFT;
        ++n;
    }
    if (altDown) {
        in[n].ki.wVk = VK_MENU;
        ++n;
    }

    SendInput(static_cast<UINT>(n), in, sizeof(INPUT));
}

// -- D3D11 swap chain ----------------------------------------------------------

bool PopupWindow::CreateSwapChain() {
    // Get DXGI factory from the existing device so we share the same adapter
    IDXGIDevice*  dxgiDev  = nullptr;
    IDXGIAdapter* adapter  = nullptr;
    IDXGIFactory* factory  = nullptr;

    if (FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice),
                                         reinterpret_cast<void**>(&dxgiDev))))
        return false;
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const UINT clientWidth = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    const UINT clientHeight = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = clientWidth;
    sd.BufferDesc.Height                  = clientHeight;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);

    factory->Release();
    adapter->Release();
    dxgiDev->Release();

    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void PopupWindow::ResizeSwapChainToClient() {
    if (!m_swapChain) return;

    RECT rc{};
    if (!GetClientRect(m_hwnd, &rc)) return;

    const UINT width = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    const UINT height = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));

    DestroyRenderTarget();
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height,
                                            DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr))
        CreateRenderTarget();
}

void PopupWindow::DestroySwapChain() {
    DestroyRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
}

void PopupWindow::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void PopupWindow::DestroyRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

// -- Helpers -------------------------------------------------------------------

void PopupWindow::PositionAtCursor() {
    POINT pt{};
    GetCursorPos(&pt);
    float x = static_cast<float>(pt.x);
    float y = static_cast<float>(pt.y);

    HMONITOR    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + m_width  > mi.rcWork.right)  x = static_cast<float>(mi.rcWork.right  - m_width);
        if (y + m_height > mi.rcWork.bottom) y = static_cast<float>(mi.rcWork.bottom - m_height);
        if (x < mi.rcWork.left) x = static_cast<float>(mi.rcWork.left);
        if (y < mi.rcWork.top)  y = static_cast<float>(mi.rcWork.top);
    }
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                  static_cast<int>(x), static_cast<int>(y),
                  m_width, m_height,
                  SWP_NOACTIVATE);
}

void PopupWindow::ApplyOpacity() {
    BYTE alpha = static_cast<BYTE>(m_opacity * 255.0f);
    SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
}

void PopupWindow::ApplyWindowCorners() {
    // ImGui rounds only its own drawing. DWM owns the native HWND corners.
    // Windows chooses the exact radius; custom pixel radii require region/per-pixel masks.
    const int pref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &pref, sizeof(pref));

    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR,
                          &noBorder, sizeof(noBorder));
}

// -- Win32 message handler -----------------------------------------------------

void PopupWindow::ToggleMaximized() {
    if (m_maximized) {
        SetWindowPos(m_hwnd, HWND_TOPMOST,
                     m_restoreRect.left, m_restoreRect.top,
                     m_restoreRect.right - m_restoreRect.left,
                     m_restoreRect.bottom - m_restoreRect.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        m_maximized = false;
        ResizeSwapChainToClient();
        ApplyWindowCorners();
        return;
    }

    if (!GetWindowRect(m_hwnd, &m_restoreRect))
        return;

    HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(mon, &mi))
        return;

    const RECT& r = mi.rcWork;
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 r.left, r.top,
                 r.right - r.left,
                 r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    m_maximized = true;
    ResizeSwapChainToClient();
    ApplyWindowCorners();
}

LRESULT CALLBACK PopupWindow::WndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* pw = reinterpret_cast<PopupWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // Switch to the popup's own ImGui context so WM_CHAR / WM_KEYDOWN from
    // the keyboard hook land in the right input queue (search bar).
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (pw && pw->m_imguiCtx)
        ImGui::SetCurrentContext(pw->m_imguiCtx);

    bool imgHandled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;

    if (pw && pw->m_imguiCtx)
        ImGui::SetCurrentContext(prevCtx);

    if (imgHandled) return TRUE;

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

        const bool onL = pt.x < kPopupResizeBorder;
        const bool onR = pt.x >= rc.right - kPopupResizeBorder;
        const bool onT = pt.y < kPopupResizeBorder;
        const bool onB = pt.y >= rc.bottom - kPopupResizeBorder;

        if (onT && onL) return HTTOPLEFT;
        if (onT && onR) return HTTOPRIGHT;
        if (onB && onL) return HTBOTTOMLEFT;
        if (onB && onR) return HTBOTTOMRIGHT;
        if (onL)        return HTLEFT;
        if (onR)        return HTRIGHT;
        if (onT)        return HTTOP;
        if (onB)        return HTBOTTOM;

        return HTCLIENT;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize = {360, 260};
        return 0;
    }

    case WM_SIZE:
        if (pw && pw->m_swapChain && wParam != SIZE_MINIMIZED) {
            pw->m_width = LOWORD(lParam);
            pw->m_height = HIWORD(lParam);
            pw->ResizeSwapChainToClient();
            pw->ApplyWindowCorners();
        }
        return 0;

    // WS_EX_NOACTIVATE stops Windows from sending paint messages during the
    // internal move/resize modal loop, so our D3D frames stop until mouse-up.
    // Fix: run a ~60fps timer during the modal loop and render each tick.
    case WM_ENTERSIZEMOVE:
        SetTimer(hwnd, kPopupResizeRenderTimerId, 16, nullptr);
        return 0;

    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kPopupResizeRenderTimerId);
        return 0;

    case WM_TIMER:
        if (pw && wParam == kPopupResizeRenderTimerId) pw->Render();
        return 0;

    case WM_KILLFOCUS:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
