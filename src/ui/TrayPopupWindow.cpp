#include "TrayPopupWindow.h"
#include "../app/Application.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <functional>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

constexpr wchar_t kTrayPopupClass[] = L"ClipboardPlusPlus_TrayPopup";
constexpr int kTrayPopupBaseWidth = 230;
constexpr int kTrayPopupBaseHeight = 248;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

void DrawActionButton(const char* label, const ImVec2& size, const std::function<void()>& action) {
    if (ImGui::Button(label, size))
        action();
}

} // namespace

bool TrayPopupWindow::Create(HINSTANCE hInstance,
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
    wc.lpszClassName = kTrayPopupClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        kTrayPopupClass, nullptr,
        WS_POPUP,
        0, 0, m_width, m_height,
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

    ApplyThemeStyle(m_appearance, true);
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);
    RebuildFontAtlas(io, m_appearance);

    ImGui::SetCurrentContext(prevCtx);
    ApplyWindowChrome();
    SetLayeredWindowAttributes(m_hwnd, 0, 245, LWA_ALPHA);
    return true;
}

void TrayPopupWindow::Destroy() {
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
        UnregisterClassW(kTrayPopupClass, m_hInstance);
}

void TrayPopupWindow::ShowAtCursor() {
    if (!m_hwnd)
        return;

    PositionNearCursor();
    ApplyWindowChrome();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_visible = true;
    GetAsyncKeyState(VK_LBUTTON);
    GetAsyncKeyState(VK_RBUTTON);
}

void TrayPopupWindow::Hide() {
    if (!m_visible)
        return;

    m_visible = false;
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
}

void TrayPopupWindow::ApplyAppearance(const AppearanceSettings& settings) {
    if (!m_imguiCtx)
        return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    m_appearance = settings;
    m_appearance.dpiScale = 1.0f;
    ApplyThemeStyle(m_appearance, true);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    RebuildFontAtlas(ImGui::GetIO(), m_appearance);
    ImGui_ImplDX11_CreateDeviceObjects();
    m_width = kTrayPopupBaseWidth;
    m_height = kTrayPopupBaseHeight;
    if (m_visible) {
        PositionNearCursor();
        ResizeSwapChainToClient();
    }

    ImGui::SetCurrentContext(prevCtx);
}

void TrayPopupWindow::Render() {
    if (!m_visible || !m_hwnd || !m_renderTarget)
        return;

    CloseWhenClickedOutside();
    if (!m_visible)
        return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(m_width), static_cast<float>(m_height)),
                             ImGuiCond_Always);
    ImGui::Begin("TrayPopup", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoSavedSettings);
    DrawMenu();
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

bool TrayPopupWindow::CreateSwapChain() {
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

void TrayPopupWindow::ResizeSwapChainToClient() {
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

void TrayPopupWindow::DestroySwapChain() {
    DestroyRenderTarget();
    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
}

void TrayPopupWindow::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void TrayPopupWindow::DestroyRenderTarget() {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
}

void TrayPopupWindow::ApplyWindowChrome() {
    const int pref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
}

void TrayPopupWindow::PositionNearCursor() {
    POINT pt{};
    GetCursorPos(&pt);

    int x = pt.x;
    int y = pt.y - m_height;

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left),
                       static_cast<int>(mi.rcWork.right) - m_width);
        if (y < mi.rcWork.top)
            y = pt.y;
        y = std::clamp(y, static_cast<int>(mi.rcWork.top),
                       static_cast<int>(mi.rcWork.bottom) - m_height);
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_width, m_height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    ResizeSwapChainToClient();
}

void TrayPopupWindow::CloseWhenClickedOutside() {
    const bool clicked = (GetAsyncKeyState(VK_LBUTTON) & 1) != 0 ||
                         (GetAsyncKeyState(VK_RBUTTON) & 1) != 0;
    if (!clicked)
        return;

    POINT pt{};
    RECT rc{};
    GetCursorPos(&pt);
    GetWindowRect(m_hwnd, &rc);
    if (!PtInRect(&rc, pt))
        Hide();
}

void TrayPopupWindow::DrawMenu() {
    Application* app = Application::Get();

    ImGui::TextUnformatted("Clipboard++");
    ImGui::Separator();

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 buttonSize(width, 28.0f);

    DrawActionButton("Open Clipboard++", buttonSize, [this, app]() {
        if (app)
            app->OpenSettingsWindow();
        Hide();
    });

    DrawActionButton("Show Popup", buttonSize, [this, app]() {
        if (app)
            app->ShowPopup();
        Hide();
    });

    DrawActionButton("Open Editor", buttonSize, [this, app]() {
        if (app)
            app->ShowEditorPopup();
        Hide();
    });

    const bool incognito = app && app->IsIncognito();
    const char* incognitoLabel = incognito ? "Incognito: On" : "Incognito: Off";
    DrawActionButton(incognitoLabel, buttonSize, [app]() {
        if (app)
            app->ToggleIncognito();
    });

    ImGui::Separator();

    DrawActionButton("About", buttonSize, [this]() {
        MessageBoxW(m_hwnd,
            L"Clipboard++ v0.1\n\nA lean, modern clipboard manager for Windows.",
            L"About Clipboard++", MB_OK | MB_ICONINFORMATION);
        Hide();
    });

    DrawActionButton("Exit", buttonSize, [this, app]() {
        if (app)
            PostMessageW(app->GetHwnd(), WM_DESTROY, 0, 0);
        Hide();
    });
}

LRESULT CALLBACK TrayPopupWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* window = reinterpret_cast<TrayPopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (window && window->m_imguiCtx)
        ImGui::SetCurrentContext(window->m_imguiCtx);

    const bool imguiHandled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;

    if (window && window->m_imguiCtx)
        ImGui::SetCurrentContext(prevCtx);

    if (imguiHandled)
        return TRUE;

    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_SIZE:
        if (window && window->m_device && wParam != SIZE_MINIMIZED) {
            window->m_width = LOWORD(lParam);
            window->m_height = HIWORD(lParam);
            window->ResizeSwapChainToClient();
        }
        return 0;
    case WM_KILLFOCUS:
        if (window)
            window->Hide();
        return 0;
    case WM_CLOSE:
        if (window)
            window->Hide();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
