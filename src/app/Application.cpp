#include "Application.h"
#include "TrayIcon.h"
#include "../ui/MainWindow.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

// ImGui's Win32 message handler — must be forward-declared
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

Application* Application::s_instance = nullptr;

// ── Construction / destruction ────────────────────────────────────────────────

Application::Application(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
    s_instance = this;
}

Application::~Application() {
    Shutdown();
    s_instance = nullptr;
}

// ── Public ────────────────────────────────────────────────────────────────────

int Application::Run() {
    if (!Init()) return 1;

    MSG msg{};
    while (m_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                m_running = false;
        }
        if (!m_running) break;
        RenderFrame();
    }
    return 0;
}

void Application::ShowMainWindow() {
    m_mainVisible = true;
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void Application::HideMainWindow() {
    m_mainVisible = false;
    ShowWindow(m_hwnd, SW_HIDE);
}

// ── Private: initialisation ───────────────────────────────────────────────────

bool Application::Init() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInstance;
    wc.hIcon         = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514)); // IDC_ARROW
    wc.lpszClassName = L"ClipboardPlusPlus_Main";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0,
        L"ClipboardPlusPlus_Main",
        L"Clipboard++",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 750,
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (!m_hwnd) return false;

    if (!CreateD3D()) {
        DestroyD3D();
        UnregisterClassW(L"ClipboardPlusPlus_Main", m_hInstance);
        return false;
    }

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = nullptr; // we manage our own config

    // Default dark style — replaced by ThemeManager in Milestone 8
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 4.0f;
    style.FrameRounding    = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding     = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_d3dDevice, m_d3dContext);

    // Load Segoe UI from the Windows fonts directory for a clean system look
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;
    const char* segoeUiPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(segoeUiPath) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(segoeUiPath, 15.0f, &fontCfg);
    else
        io.Fonts->AddFontDefault();

    // Tray icon
    m_tray = std::make_unique<TrayIcon>(m_hwnd, m_hInstance);
    if (!m_tray->Create()) return false;

    // TODO (Milestone 5): check config for first-launch flag
    // For now, always show on startup
    ShowMainWindow();

    m_running = true;
    return true;
}

void Application::Shutdown() {
    if (m_tray) m_tray->Destroy();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyD3D();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClassW(L"ClipboardPlusPlus_Main", m_hInstance);
        m_hwnd = nullptr;
    }
}

// ── Private: render ───────────────────────────────────────────────────────────

void Application::RenderFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (m_mainVisible)
        MainWindow::Draw(m_mainVisible);

    ImGui::Render();

    constexpr float bg[4] = { 0.118f, 0.118f, 0.118f, 1.0f }; // #1e1e1e VSCode bg
    m_d3dContext->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_d3dContext->ClearRenderTargetView(m_renderTarget, bg);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);
}

// ── Private: D3D11 ───────────────────────────────────────────────────────────

bool Application::CreateD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 2, D3D11_SDK_VERSION,
        &sd, &m_swapChain, &m_d3dDevice, &level, &m_d3dContext
    );

    // Fall back to WARP software renderer if hardware fails
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            0, levels, 2, D3D11_SDK_VERSION,
            &sd, &m_swapChain, &m_d3dDevice, &level, &m_d3dContext
        );

    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

void Application::DestroyD3D() {
    DestroyRenderTarget();
    if (m_swapChain)   { m_swapChain->Release();   m_swapChain   = nullptr; }
    if (m_d3dContext)  { m_d3dContext->Release();  m_d3dContext  = nullptr; }
    if (m_d3dDevice)   { m_d3dDevice->Release();   m_d3dDevice   = nullptr; }
}

void Application::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_d3dDevice->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void Application::DestroyRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

// ── Win32 message handler ─────────────────────────────────────────────────────

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return TRUE;

    Application* app = Application::Get();

    switch (msg) {
    case WM_SIZE:
        if (app && app->m_d3dDevice && wParam != SIZE_MINIMIZED) {
            app->DestroyRenderTarget();
            app->m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                             DXGI_FORMAT_UNKNOWN, 0);
            app->CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        // Intercept minimize — hide to tray instead
        if ((wParam & 0xfff0) == SC_MINIMIZE) {
            if (app) app->HideMainWindow();
            return 0;
        }
        break;

    case WM_CLOSE:
        // Hide to tray instead of destroying
        if (app) app->HideMainWindow();
        return 0;

    case WM_TRAYICON:
        if (app && app->m_tray)
            app->m_tray->HandleMessage(wParam, lParam);
        return 0;

    case WM_SHOWCPP_MAIN:
        if (app) app->ShowMainWindow();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
