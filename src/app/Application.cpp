#include "Application.h"
#include "TrayIcon.h"
#include "../ui/MainWindow.h"
#include "../ui/PopupWindow.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../hotkeys/HotkeyManager.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

Application* Application::s_instance = nullptr;
static constexpr UINT_PTR kResizeRenderTimerId = 1;
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace {

bool SetUnicodeClipboardText(HWND owner, const wchar_t* text, size_t chars) {
    if (!OpenClipboard(owner))
        return false;

    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (chars + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }

    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    std::memcpy(data, text, chars * sizeof(wchar_t));
    static_cast<wchar_t*>(data)[chars] = L'\0';
    GlobalUnlock(mem);

    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

} // namespace

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
        if (HasRenderableUi() || m_appearanceDirty)
            RenderFrame();
        else
            WaitMessage();
    }
    return 0;
}

void Application::ShowMainWindow() {
    m_mainVisible = true;

    if (IsIconic(m_hwnd))
        ShowWindow(m_hwnd, SW_RESTORE);
    else
        ShowWindow(m_hwnd, SW_SHOWNORMAL);

    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    BringWindowToTop(m_hwnd);
    SetFocus(m_hwnd);
}

void Application::HideMainWindow() {
    m_mainVisible = false;
    ShowWindow(m_hwnd, SW_HIDE);
}

void Application::ShowPopup() {
    if (m_popup) m_popup->Show();
}

void Application::RequestAppearance(const AppearanceSettings& settings) {
    m_appearance = settings;
    std::filesystem::path imported = ConfigStore::ImportFontFile(m_appearance.fontPath);
    if (!imported.empty())
        m_appearance.fontPath = imported.u8string();
    m_config.appearance = m_appearance;
    m_appearanceDirty = true;
    SaveConfig();
}

void Application::SetPopupOpacity(float opacity) {
    m_appearance.popupOpacity = std::clamp(opacity, 0.1f, 1.0f);
    m_config.appearance = m_appearance;
    m_appearanceDirty = true;
    SaveConfig();
}

void Application::RequestHotkeySettings(const HotkeySettings& settings) {
    m_hotkeySettings = settings;
    m_config.hotkeys = settings;
    if (m_hotkeys)
        m_hotkeys->ApplySettings(m_hotkeySettings);
    SaveConfig();
}

void Application::SetNewItemsAtTop(bool value) {
    m_config.newItemsAtTop = value;
    if (m_history)
        m_history->SetNewItemsAtTop(value);
    SaveConfig();
}

void Application::SetAppendNewlineAfterPaste(bool value) {
    m_config.appendNewlineAfterPaste = value;
    if (m_popup)
        m_popup->SetAppendNewlineAfterPaste(value);
    SaveConfig();
}

ClipboardHistory::MoveTarget Application::GetPasteMoveTarget() const {
    switch (m_config.pasteMoveTarget) {
    case 1: return ClipboardHistory::MoveTarget::Top;
    case 2: return ClipboardHistory::MoveTarget::Bottom;
    default: return ClipboardHistory::MoveTarget::None;
    }
}

void Application::SetPasteMoveTarget(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::Top:    m_config.pasteMoveTarget = 1; break;
    case ClipboardHistory::MoveTarget::Bottom: m_config.pasteMoveTarget = 2; break;
    default:                                   m_config.pasteMoveTarget = 0; break;
    }
    if (m_popup)
        m_popup->SetPasteMoveTarget(target);
    SaveConfig();
}

void Application::SaveConfig() {
    ConfigStore::Save(m_config);
}

void Application::ApplyLoadedConfig(const AppConfig& config) {
    m_config = config;
    m_appearance = m_config.appearance;
    m_hotkeySettings = m_config.hotkeys;
    m_appearanceDirty = true;

    if (m_history)
        m_history->SetNewItemsAtTop(m_config.newItemsAtTop);
    if (m_popup) {
        m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
        m_popup->SetPasteMoveTarget(GetPasteMoveTarget());
    }
    if (m_hotkeys)
        m_hotkeys->ApplySettings(m_hotkeySettings);
}

bool Application::HandleClipboardTextCommand(const COPYDATASTRUCT& cds) {
    if (!m_history || cds.dwData != CD_CLIPBOARD_TEXT ||
        cds.cbData < sizeof(ClipboardTextCommand))
        return false;

    const auto* cmd = static_cast<const ClipboardTextCommand*>(cds.lpData);
    const size_t headerBytes = offsetof(ClipboardTextCommand, text);
    const size_t textBytes = cds.cbData - headerBytes;
    if (textBytes < sizeof(wchar_t))
        return false;

    const size_t maxChars = textBytes / sizeof(wchar_t);
    const size_t chars = wcsnlen_s(cmd->text, maxChars);
    if (chars == 0)
        return false;

    int utf8Bytes = WideCharToMultiByte(CP_UTF8, 0, cmd->text, static_cast<int>(chars),
                                        nullptr, 0, nullptr, nullptr);
    if (utf8Bytes <= 0)
        return false;

    ClipboardItem item;
    item.type = ContentType::Text;
    item.text.resize(static_cast<size_t>(utf8Bytes));
    WideCharToMultiByte(CP_UTF8, 0, cmd->text, static_cast<int>(chars),
                        item.text.data(), utf8Bytes, nullptr, nullptr);
    item.sourceProcess = "clipboardpp.exe";
    item.tags = ContentDetector::DetectTags(item.text);

    size_t index = 0;
    if (cmd->position == -1) {
        index = m_history->Size();
    } else if (cmd->position > 0) {
        index = static_cast<size_t>(std::clamp(cmd->position, 1, 500) - 1);
    }

    if (cmd->setSystemClipboard) {
        if (m_monitor)
            m_monitor->SuppressNextUpdate();
        SetUnicodeClipboardText(m_hwnd, cmd->text, chars);
    }

    m_history->Insert(std::move(item), index);
    return true;
}

// ── Private: initialisation ───────────────────────────────────────────────────

bool Application::Init() {
    ApplyLoadedConfig(ConfigStore::Load());

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInstance;
    wc.hIcon         = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));   // IDI_APPLICATION
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514)); // IDC_ARROW
    wc.hbrBackground = nullptr; // D3D owns the background — prevents white flash on resize
    wc.lpszClassName = L"ClipboardPlusPlus_Main";
    RegisterClassExW(&wc);

    // WS_POPUP removes native chrome; WS_THICKFRAME keeps resize hit-testing.
    // WS_EX_APPWINDOW ensures we still appear in the taskbar.
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"ClipboardPlusPlus_Main",
        L"Clipboard++",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 750,
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // DWM drop-shadow for borderless window (1px inset on all sides is enough)
    MARGINS shadow = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(m_hwnd, &shadow);
    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR,
                          &noBorder, sizeof(noBorder));

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
    io.IniFilename  = nullptr;

    ApplyThemeStyle(m_appearance.theme, false);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_d3dDevice, m_d3dContext);

    RebuildFontAtlas(io, m_appearance);

    m_tray = std::make_unique<TrayIcon>(m_hwnd, m_hInstance);
    if (!m_tray->Create()) return false;

    // Clipboard history + monitor
    m_history = std::make_unique<ClipboardHistory>(500);
    m_history->SetNewItemsAtTop(m_config.newItemsAtTop);
    m_monitor = std::make_unique<ClipboardMonitor>();
    m_monitor->Start(m_hInstance, [this](ClipboardItem item) {
        m_history->Push(std::move(item));
    });

    m_popup = std::make_unique<PopupWindow>();
    if (!m_popup->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_popup->ApplyAppearance(m_appearance);
    m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
    m_popup->SetPasteMoveTarget(GetPasteMoveTarget());

    m_hotkeys = std::make_unique<HotkeyManager>();
    m_hotkeys->Install(m_hwnd);
    m_hotkeys->ApplySettings(m_hotkeySettings);

    // TODO (Milestone 5): only show on first launch
    ShowMainWindow();

    m_running = true;
    return true;
}

void Application::Shutdown() {
    if (m_hotkeys) m_hotkeys->Uninstall();
    if (m_popup)   m_popup->Destroy();
    if (m_monitor) m_monitor->Stop();
    if (m_tray)    m_tray->Destroy();

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
    if (m_appearanceDirty)
        ApplyAppearanceNow();

    const bool renderMain = m_mainVisible && m_hwnd && !IsIconic(m_hwnd);
    if (renderMain) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        MainWindow::Draw(m_mainVisible);

        ImGui::Render();

        constexpr float bg[4] = {0.118f, 0.118f, 0.118f, 1.0f}; // #1e1e1e
        m_d3dContext->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_d3dContext->ClearRenderTargetView(m_renderTarget, bg);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        m_swapChain->Present(1, 0);
    }

    // Popup has its own context + swap chain — rendered separately
    if (m_popup) m_popup->Render();
}

bool Application::HasRenderableUi() const {
    const bool mainRenderable = m_mainVisible && m_hwnd && !IsIconic(m_hwnd);
    const bool popupRenderable = m_popup && m_popup->IsVisible();
    return mainRenderable || popupRenderable;
}

void Application::ApplyAppearanceNow() {
    m_appearanceDirty = false;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ApplyThemeStyle(m_appearance.theme, false);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    RebuildFontAtlas(ImGui::GetIO(), m_appearance);
    ImGui_ImplDX11_CreateDeviceObjects();

    if (m_popup)
        m_popup->ApplyAppearance(m_appearance);

    ImGui::SetCurrentContext(prevCtx);
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

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 2, D3D11_SDK_VERSION,
        &sd, &m_swapChain, &m_d3dDevice, &level, &m_d3dContext);

    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            0, levels, 2, D3D11_SDK_VERSION,
            &sd, &m_swapChain, &m_d3dDevice, &level, &m_d3dContext);

    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

void Application::DestroyD3D() {
    DestroyRenderTarget();
    if (m_swapChain)  { m_swapChain->Release();  m_swapChain  = nullptr; }
    if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = nullptr; }
    if (m_d3dDevice)  { m_d3dDevice->Release();  m_d3dDevice  = nullptr; }
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

    // ── Remove all native non-client area so we own every pixel ──────────────
    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            // When maximized, Windows inflates the window rect by the border
            // thickness so it overlaps the taskbar. Compensate by shrinking
            // the client rect back to the monitor work area.
            if (IsZoomed(hwnd)) {
                NCCALCSIZE_PARAMS* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                int bx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int by = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left   += bx;
                p->rgrc[0].right  -= bx;
                p->rgrc[0].top    += by;
                p->rgrc[0].bottom -= by;
            }
            return 0;
        }
        break;

    // ── Tell Windows which part of our window each pixel belongs to ──────────
    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);

        const bool maximized = IsZoomed(hwnd) != 0;
        const int  border    = maximized ? 0 : 8;
        const int  titleH    = MainWindow::kTitleBarHeight;
        const int  btnZoneX  = rc.right - MainWindow::kTitleBtnWidth * 3;

        if (!maximized) {
            const bool onL = pt.x < border;
            const bool onR = pt.x >= rc.right  - border;
            const bool onT = pt.y < border;
            const bool onB = pt.y >= rc.bottom - border;

            if (onT && onL) return HTTOPLEFT;
            if (onT && onR) return HTTOPRIGHT;
            if (onB && onL) return HTBOTTOMLEFT;
            if (onB && onR) return HTBOTTOMRIGHT;
            if (onL)        return HTLEFT;
            if (onR)        return HTRIGHT;
            if (onT)        return HTTOP;
            if (onB)        return HTBOTTOM;
        }

        // Title bar — drag region excludes the three button slots on the right
        if (pt.y >= 0 && pt.y < titleH && pt.x < btnZoneX)
            return HTCAPTION;

        return HTCLIENT;
    }

    // ── Enforce a minimum window size ────────────────────────────────────────
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize = {800, 500};
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE; // prevent white flash during resize

    case WM_SIZE:
        if (app && app->m_d3dDevice && wParam != SIZE_MINIMIZED) {
            app->DestroyRenderTarget();
            app->m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                             DXGI_FORMAT_UNKNOWN, 0);
            app->CreateRenderTarget();
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        SetTimer(hwnd, kResizeRenderTimerId, 16, nullptr);
        return 0;

    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kResizeRenderTimerId);
        return 0;

    case WM_TIMER:
        if (app && wParam == kResizeRenderTimerId)
            app->RenderFrame();
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MINIMIZE) {
            if (app) app->HideMainWindow();
            return 0;
        }
        break;

    case WM_CLOSE:
        if (app) app->HideMainWindow();
        return 0;

    case WM_TRAYICON:
        if (app && app->m_tray)
            app->m_tray->HandleMessage(wParam, lParam);
        return 0;

    case WM_SHOWCPP_MAIN:
        if (app) app->ShowMainWindow();
        return 0;

    case WM_SHOWPOPUP:
        if (app) app->ShowPopup();
        return 0;

    case WM_RELOAD_CONFIG:
        if (app) app->ApplyLoadedConfig(ConfigStore::Load());
        return 0;

    case WM_COPYDATA:
        if (app)
            return app->HandleClipboardTextCommand(*reinterpret_cast<COPYDATASTRUCT*>(lParam)) ? TRUE : FALSE;
        return FALSE;

    case WM_HOTKEYACTION: {
        if (!app) return 0;
        auto action = static_cast<HotkeyAction>(wParam);
        int  data   = static_cast<int>(lParam);

        switch (action) {
        case HotkeyAction::TogglePopup:
            if (app->m_popup) {
                if (app->m_popup->IsVisible()) app->m_popup->Hide();
                else                           app->m_popup->Show(false);
            }
            break;
        case HotkeyAction::ShowPopupSearch:
            if (app->m_popup) {
                if (!app->m_popup->IsVisible())
                    app->m_popup->Show(true);
                else
                    app->m_popup->RequestSearchFocus();
            }
            break;
        case HotkeyAction::OpenSettings:
            app->ShowMainWindow();
            break;
        case HotkeyAction::Incognito:
            // TODO Milestone 9
            break;
        case HotkeyAction::PasteHistorySlot: {
            // Direct paste — no popup shown.
            // Capture foreground window now, before any focus changes.
            HWND target = GetForegroundWindow();
            if (app->m_popup)
                app->m_popup->PasteHistorySlot(data, target);
            break;
        }
        case HotkeyAction::PasteVisibleSlot:
            if (app->m_popup)
                app->m_popup->PasteVisibleSlot(data);
            break;
        default: break;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
