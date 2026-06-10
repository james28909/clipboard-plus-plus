#include "DebugWindow.h"
#include "../app/Application.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <dxgi.h>
#include <algorithm>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
constexpr wchar_t kDebugWindowClass[] = L"ClipboardPlusPlus_DebugWindow";
}

bool DebugWindow::Create(HINSTANCE hInstance,
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
    wc.lpszClassName = kDebugWindowClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kDebugWindowClass, L"Clipboard++ Debug Output",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height,
        nullptr, nullptr, hInstance,
        static_cast<LPVOID>(this));
    if (!m_hwnd)
        return false;

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

    ApplyThemeStyle(m_appearance, false);
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);
    RebuildFontAtlas(io, m_appearance);

    ImGui::SetCurrentContext(prevCtx);
    return true;
}

void DebugWindow::Destroy() {
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
        UnregisterClassW(kDebugWindowClass, m_hInstance);
}

void DebugWindow::Show() {
    if (!m_hwnd)
        return;

    PositionInitial();
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    m_visible = true;
}

void DebugWindow::Hide() {
    if (!m_visible)
        return;
    m_visible = false;
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
}

void DebugWindow::Toggle() {
    if (m_visible)
        Hide();
    else
        Show();
}

void DebugWindow::ApplyAppearance(const AppearanceSettings& settings) {
    if (!m_imguiCtx)
        return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    m_appearance = settings;
    ApplyThemeStyle(m_appearance, false);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    RebuildFontAtlas(ImGui::GetIO(), settings);
    ImGui_ImplDX11_CreateDeviceObjects();

    ImGui::SetCurrentContext(prevCtx);
}

void DebugWindow::AddLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_text += line;
    if (m_text.empty() || m_text.back() != '\n')
        m_text.push_back('\n');
    constexpr size_t kMaxLogBytes = 256 * 1024;
    if (m_text.size() > kMaxLogBytes)
        m_text.erase(0, m_text.size() - kMaxLogBytes);
    m_textDirty = true;
}

void DebugWindow::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_text.clear();
    m_textDirty = true;
}

void DebugWindow::Render() {
    if (!m_visible || !m_hwnd || !m_renderTarget || !m_imguiCtx)
        return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_textDirty) {
            m_textBuffer.assign(m_text.begin(), m_text.end());
            m_textBuffer.push_back('\0');
            m_textDirty = false;
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({static_cast<float>(rc.right), static_cast<float>(rc.bottom)}, ImGuiCond_Always);
    ImGui::Begin("##debug_output", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings);
    DrawContents();
    ImGui::End();

    ImGui::Render();
    const ImVec4 bg = m_appearance.customColors ? m_appearance.windowBg : ThemeDefaults(m_appearance.theme).windowBg;
    const float clear[4] = {bg.x, bg.y, bg.z, bg.w};
    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_context->ClearRenderTargetView(m_renderTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);

    ImGui::SetCurrentContext(prevCtx);
}

void DebugWindow::DrawContents() {
    ImGui::TextUnformatted("Clipboard++ Debug Output");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy all"))
        ImGui::SetClipboardText(m_textBuffer.empty() ? "" : m_textBuffer.data());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Follow tail", &m_followTail);
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide"))
        Hide();

    ImGui::Separator();
    ImGui::TextDisabled("Select text below and press Ctrl+C, or use Copy all.");

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly |
                                ImGuiInputTextFlags_NoUndoRedo |
                                ImGuiInputTextFlags_AllowTabInput;
    if (m_textBuffer.empty())
        m_textBuffer.push_back('\0');
    ImGui::InputTextMultiline("##debug_text",
                              m_textBuffer.data(),
                              m_textBuffer.size(),
                              {-1.0f, -1.0f},
                              flags);
    if (m_followTail && ImGui::IsItemActive() == false)
        ImGui::SetScrollHereY(1.0f);
}

bool DebugWindow::CreateSwapChain() {
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

void DebugWindow::ResizeSwapChainToClient() {
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

void DebugWindow::DestroySwapChain() {
    DestroyRenderTarget();
    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
}

void DebugWindow::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void DebugWindow::DestroyRenderTarget() {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
}

void DebugWindow::PositionInitial() {
    if (m_positioned)
        return;
    m_positioned = true;

    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(mon, &mi))
        return;

    int x = mi.rcWork.right - m_width - 32;
    int y = mi.rcWork.top + 64;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_width, m_height, SWP_NOACTIVATE);
}

LRESULT CALLBACK DebugWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* window = reinterpret_cast<DebugWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (window && window->m_imguiCtx)
        ImGui::SetCurrentContext(window->m_imguiCtx);
    const bool imguiHandled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
    if (window && window->m_imguiCtx)
        ImGui::SetCurrentContext(prevCtx);
    if (imguiHandled)
        return TRUE;

    switch (msg) {
    case WM_ERASEBKGND:
        return TRUE;
    case WM_SIZE:
        if (window && window->m_device && wParam != SIZE_MINIMIZED) {
            window->m_width = LOWORD(lParam);
            window->m_height = HIWORD(lParam);
            window->ResizeSwapChainToClient();
        }
        return 0;
    case WM_CLOSE:
        if (window)
            window->Hide();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
