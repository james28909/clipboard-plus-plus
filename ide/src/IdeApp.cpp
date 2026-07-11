#include "IdeApp.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <Scintilla.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

constexpr wchar_t kWndClass[] = L"ClipboardPlusPlus_IDE";
constexpr int kTitleBarHeight = 32;
constexpr int kTitleButtonWidth = 46;
constexpr int kResizeBorder = 8;
constexpr UINT_PTR kResizeRenderTimerId = 0xC1DE;
constexpr wchar_t kScintillaAppProp[] = L"ClipboardPlusPlusIdeApp";
constexpr int kMinWindowWidth = 900;
constexpr int kMinWindowHeight = 560;

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty())
        return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1)
        return {};
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty())
        return {};
    int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (chars <= 1)
        return {};
    std::wstring out(static_cast<size_t>(chars - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), chars);
    return out;
}

std::string FilenameUtf8(const std::wstring& path) {
    if (path.empty())
        return "Untitled";
    return WideToUtf8(std::filesystem::path(path).filename().wstring());
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

LanguageMode ModeFromText(std::wstring mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    if (mode == L"powershell" || mode == L"ps1") return LanguageMode::PowerShell;
    if (mode == L"batch" || mode == L"bat" || mode == L"cmd") return LanguageMode::Batch;
    if (mode == L"json") return LanguageMode::Json;
    if (mode == L"markdown" || mode == L"md") return LanguageMode::Markdown;
    if (mode == L"cpp" || mode == L"c++" || mode == L"c") return LanguageMode::Cpp;
    return LanguageMode::Text;
}

LanguageMode ModeFromPath(const std::wstring& path) {
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    if (ext == L".ps1" || ext == L".psm1") return LanguageMode::PowerShell;
    if (ext == L".bat" || ext == L".cmd") return LanguageMode::Batch;
    if (ext == L".json") return LanguageMode::Json;
    if (ext == L".md" || ext == L".markdown") return LanguageMode::Markdown;
    if (ext == L".cpp" || ext == L".h" || ext == L".hpp" || ext == L".c") return LanguageMode::Cpp;
    return LanguageMode::Text;
}

const char* ModeName(LanguageMode mode) {
    switch (mode) {
    case LanguageMode::PowerShell: return "PowerShell";
    case LanguageMode::Batch: return "Batch";
    case LanguageMode::Json: return "JSON";
    case LanguageMode::Markdown: return "Markdown";
    case LanguageMode::Cpp: return "C++";
    default: return "Plain Text";
    }
}

ImU32 Color(float r, float g, float b, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32({r, g, b, a});
}

bool TitleButton(const char* id, float x, float w, float h, ImU32 base, ImU32 hover) {
    ImGui::SetCursorPos({x, 0.0f});
    ImGui::InvisibleButton(id, {w, h});
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
        (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    if (ImGui::IsItemHovered())
        dl->AddRectFilled({wp.x + x, wp.y}, {wp.x + x + w, wp.y + h}, hover);
    else if ((base >> 24) != 0)
        dl->AddRectFilled({wp.x + x, wp.y}, {wp.x + x + w, wp.y + h}, base);
    return clicked;
}

bool IsIdent(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '$';
}

std::string ReadFileUtf8(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in)
        return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool WriteFileUtf8(const std::wstring& path, const std::string& text) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    if (!out)
        return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

bool SetClipboardUtf8(const std::string& text) {
    std::wstring wide = Utf8ToWide(text);
    if (!OpenClipboard(nullptr))
        return false;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
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
    memcpy(data, wide.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

std::string GetClipboardUtf8() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr))
        return {};
    std::string out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(h));
        if (text) {
            out = WideToUtf8(text);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

sptr_t Sci(HWND hwnd, unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) {
    return hwnd ? static_cast<sptr_t>(SendMessageW(hwnd, msg, wParam, lParam)) : 0;
}

COLORREF Rgb(int r, int g, int b) {
    return RGB(r, g, b);
}

int ChromeHitTest(HWND hwnd, POINT client) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (!IsZoomed(hwnd)) {
        const bool left = client.x >= 0 && client.x < kResizeBorder;
        const bool right = client.x < rc.right && client.x >= rc.right - kResizeBorder;
        const bool top = client.y >= 0 && client.y < kResizeBorder;
        const bool bottom = client.y < rc.bottom && client.y >= rc.bottom - kResizeBorder;

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    if (client.y >= 0 && client.y < kTitleBarHeight) {
        const int buttonsX = rc.right - kTitleButtonWidth * 3;
        if (client.x < buttonsX)
            return HTCAPTION;
    }
    return HTCLIENT;
}

HCURSOR CursorForChromeHit(int hit) {
    switch (hit) {
    case HTLEFT:
    case HTRIGHT:
        return LoadCursorW(nullptr, IDC_SIZEWE);
    case HTTOP:
    case HTBOTTOM:
        return LoadCursorW(nullptr, IDC_SIZENS);
    case HTTOPLEFT:
    case HTBOTTOMRIGHT:
        return LoadCursorW(nullptr, IDC_SIZENWSE);
    case HTTOPRIGHT:
    case HTBOTTOMLEFT:
        return LoadCursorW(nullptr, IDC_SIZENESW);
    default:
        return LoadCursorW(nullptr, IDC_ARROW);
    }
}

bool IsResizeHit(int hit) {
    return hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM ||
           hit == HTTOPLEFT || hit == HTTOPRIGHT || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT;
}

void ClearIdeInputState() {
    if (!ImGui::GetCurrentContext())
        return;
    ImGui::ClearActiveID();
    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); ++i)
        io.MouseDown[i] = false;
}

} // namespace

bool ClipboardIdeApp::Init(HINSTANCE hInstance, const IdeLaunchOptions& options) {
    m_hInstance = hInstance;
    m_launchOptions = options;

    if (!CreateAppWindow(hInstance))
        return false;
    if (!CreateD3D())
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImFontConfig cfg{};
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 14.5f, &cfg);
    else
        io.Fonts->AddFontDefault();
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\consola.ttf") != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 15.0f, &cfg);

    ApplyTheme();
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    CreateScintillaEditor();
    LoadLaunchDocument(options);
    UpdateTitle();
    return true;
}

int ClipboardIdeApp::Run() {
    MSG msg{};
    while (m_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                m_running = false;
        }
        if (!m_running)
            break;

        RenderFrame();
    }
    Shutdown();
    return static_cast<int>(msg.wParam);
}

void ClipboardIdeApp::Shutdown() {
    if (m_launchOptions.returnToClipboard)
        CopyDocumentToClipboard();
    DestroyScintillaEditor();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyD3D();
    if (m_hwnd && IsWindow(m_hwnd))
        DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    UnregisterClassW(kWndClass, m_hInstance);
}

bool ClipboardIdeApp::CreateAppWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        kWndClass,
        L"Clipboard++ IDE",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1240, 820,
        nullptr, nullptr, hInstance, nullptr);
    if (!m_hwnd)
        return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);
    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    return true;
}

bool ClipboardIdeApp::CreateD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &sd, &m_swapChain, &m_device, &level, &m_context);
    if (FAILED(hr))
        return false;
    CreateRenderTarget();
    return true;
}

void ClipboardIdeApp::DestroyD3D() {
    DestroyRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

void ClipboardIdeApp::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void ClipboardIdeApp::DestroyRenderTarget() {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
}

void ClipboardIdeApp::ResizeSwapChain(UINT width, UINT height) {
    if (!m_swapChain)
        return;
    width = std::max<UINT>(1, width);
    height = std::max<UINT>(1, height);
    if (m_context)
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    DestroyRenderTarget();
    if (SUCCEEDED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
        CreateRenderTarget();
}

void ClipboardIdeApp::RenderFrame() {
    if (m_renderingFrame || !m_swapChain || !m_context || !m_renderTarget)
        return;

    m_renderingFrame = true;
    if (m_swapChainOccluded) {
        HRESULT test = m_swapChain->Present(0, DXGI_PRESENT_TEST);
        if (test == DXGI_STATUS_OCCLUDED) {
            m_renderingFrame = false;
            Sleep(8);
            return;
        }
        m_swapChainOccluded = false;
    }
    if (m_resizeW && m_resizeH) {
        ResizeSwapChain(m_resizeW, m_resizeH);
        m_resizeW = m_resizeH = 0;
        if (!m_renderTarget) {
            m_renderingFrame = false;
            return;
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Render();
    ImGui::Render();

    const float clear[4] = {0.118f, 0.118f, 0.118f, 1.0f};
    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_context->ClearRenderTargetView(m_renderTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = m_swapChain->Present(1, 0);
    m_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    m_renderingFrame = false;
}

LRESULT CALLBACK ClipboardIdeApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<ClipboardIdeApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE)
            return 0;
        break;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = kMinWindowWidth;
        mmi->ptMinTrackSize.y = kMinWindowHeight;

        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(monitor, &mi)) {
            const RECT& mon = mi.rcMonitor;
            const RECT& work = mi.rcWork;
            mmi->ptMaxPosition.x = work.left - mon.left;
            mmi->ptMaxPosition.y = work.top - mon.top;
            mmi->ptMaxSize.x = work.right - work.left;
            mmi->ptMaxSize.y = work.bottom - work.top;
        }
        return 0;
    }
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SETCURSOR: {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (pt.x >= 0 && pt.x < rc.right && pt.y >= 0 && pt.y < rc.bottom) {
            const bool draggingChrome = app &&
                (app->m_chromeDragHit == HTCAPTION || IsResizeHit(app->m_chromeDragHit));
            const int hit = draggingChrome ? app->m_chromeDragHit : ChromeHitTest(hwnd, pt);
            if (hit != HTCLIENT || pt.y < kTitleBarHeight) {
                SetCursor(CursorForChromeHit(hit));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (!app)
            break;
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int hit = ChromeHitTest(hwnd, client);
        if (hit == HTCAPTION || IsResizeHit(hit)) {
            app->m_chromeDragHit = hit;
            GetCursorPos(&app->m_chromeDragStart);
            GetWindowRect(hwnd, &app->m_chromeDragRect);
            app->m_liveResize = IsResizeHit(hit);
            SetCapture(hwnd);
            if (app->m_liveResize && app->m_scintillaHwnd)
                ShowWindow(app->m_scintillaHwnd, SW_HIDE);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (app && (app->m_chromeDragHit == HTCAPTION || IsResizeHit(app->m_chromeDragHit))) {
            POINT pt{};
            GetCursorPos(&pt);
            const int dx = pt.x - app->m_chromeDragStart.x;
            const int dy = pt.y - app->m_chromeDragStart.y;
            RECT next = app->m_chromeDragRect;

            switch (app->m_chromeDragHit) {
            case HTCAPTION:
                OffsetRect(&next, dx, dy);
                break;
            case HTLEFT:
            case HTTOPLEFT:
            case HTBOTTOMLEFT:
                next.left += dx;
                break;
            case HTRIGHT:
            case HTTOPRIGHT:
            case HTBOTTOMRIGHT:
                next.right += dx;
                break;
            }
            switch (app->m_chromeDragHit) {
            case HTTOP:
            case HTTOPLEFT:
            case HTTOPRIGHT:
                next.top += dy;
                break;
            case HTBOTTOM:
            case HTBOTTOMLEFT:
            case HTBOTTOMRIGHT:
                next.bottom += dy;
                break;
            }

            if (IsResizeHit(app->m_chromeDragHit)) {
                if (next.right - next.left < kMinWindowWidth) {
                    if (app->m_chromeDragHit == HTLEFT || app->m_chromeDragHit == HTTOPLEFT || app->m_chromeDragHit == HTBOTTOMLEFT)
                        next.left = next.right - kMinWindowWidth;
                    else
                        next.right = next.left + kMinWindowWidth;
                }
                if (next.bottom - next.top < kMinWindowHeight) {
                    if (app->m_chromeDragHit == HTTOP || app->m_chromeDragHit == HTTOPLEFT || app->m_chromeDragHit == HTTOPRIGHT)
                        next.top = next.bottom - kMinWindowHeight;
                    else
                        next.bottom = next.top + kMinWindowHeight;
                }
            }

            SetWindowPos(hwnd, nullptr,
                         next.left, next.top,
                         next.right - next.left,
                         next.bottom - next.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SetCursor(CursorForChromeHit(ChromeHitTest(hwnd, client)));
        break;
    }
    case WM_LBUTTONUP:
        if (app && (app->m_chromeDragHit == HTCAPTION || IsResizeHit(app->m_chromeDragHit))) {
            ReleaseCapture();
            app->m_chromeDragHit = HTNOWHERE;
            app->m_liveResize = false;
            if (app->m_scintillaHwnd)
                ShowWindow(app->m_scintillaHwnd, SW_SHOW);
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            app->RenderFrame();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (app) {
            app->m_chromeDragHit = HTNOWHERE;
            app->m_liveResize = false;
            if (app->m_scintillaHwnd)
                ShowWindow(app->m_scintillaHwnd, SW_SHOW);
        }
        break;
    case WM_SIZE:
        if (app && wParam != SIZE_MINIMIZED) {
            const UINT width = std::max<UINT>(1, LOWORD(lParam));
            const UINT height = std::max<UINT>(1, HIWORD(lParam));
            if (app->m_swapChain) {
                app->ResizeSwapChain(width, height);
                app->m_resizeW = app->m_resizeH = 0;
            } else {
                app->m_resizeW = width;
                app->m_resizeH = height;
            }
        }
        return 0;
    case WM_TIMER:
        if (app && wParam == kResizeRenderTimerId) {
            app->RenderFrame();
            return 0;
        }
        break;
    case WM_DROPFILES:
        if (app)
            app->CheckDroppedFile(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_NOTIFY:
        if (app) {
            auto* scn = reinterpret_cast<SCNotification*>(lParam);
            if (scn && scn->nmhdr.hwndFrom == app->m_scintillaHwnd &&
                scn->nmhdr.code == SCN_MODIFIED &&
                (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
                app->m_doc.dirty = true;
                app->m_scintillaTextDirty = true;
                app->UpdateTitle();
            }
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return TRUE;

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ClipboardIdeApp::ScintillaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<ClipboardIdeApp*>(GetPropW(hwnd, kScintillaAppProp));
    if (app && msg == WM_NCHITTEST) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        POINT client = pt;
        ScreenToClient(app->m_hwnd, &client);
        RECT rc{};
        GetClientRect(app->m_hwnd, &rc);

        if (!IsZoomed(app->m_hwnd)) {
            const bool onL = client.x >= 0 && client.x < kResizeBorder;
            const bool onR = client.x < rc.right && client.x >= rc.right - kResizeBorder;
            const bool onT = client.y >= 0 && client.y < kResizeBorder;
            const bool onB = client.y < rc.bottom && client.y >= rc.bottom - kResizeBorder;
            if (onL || onR || onT || onB)
                return HTTRANSPARENT;
        }
    }

    WNDPROC previous = app ? app->m_scintillaWndProc : nullptr;
    return previous ? CallWindowProcW(previous, hwnd, msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClipboardIdeApp::ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 3.0f;
    s.PopupRounding = 4.0f;
    s.TabRounding = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding = 2.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.ScrollbarSize = 12.0f;
    s.FramePadding = {8.0f, 5.0f};
    s.ItemSpacing = {8.0f, 6.0f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = {0.118f, 0.118f, 0.118f, 1.0f};
    c[ImGuiCol_ChildBg] = {0.118f, 0.118f, 0.118f, 1.0f};
    c[ImGuiCol_PopupBg] = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_Border] = {0.240f, 0.240f, 0.260f, 0.7f};
    c[ImGuiCol_Text] = {0.824f, 0.824f, 0.824f, 1.0f};
    c[ImGuiCol_TextDisabled] = {0.520f, 0.520f, 0.540f, 1.0f};
    c[ImGuiCol_FrameBg] = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_FrameBgHovered] = {0.190f, 0.200f, 0.220f, 1.0f};
    c[ImGuiCol_FrameBgActive] = {0.149f, 0.475f, 1.0f, 0.35f};
    c[ImGuiCol_Button] = {0.184f, 0.196f, 0.220f, 1.0f};
    c[ImGuiCol_ButtonHovered] = {0.220f, 0.240f, 0.270f, 1.0f};
    c[ImGuiCol_ButtonActive] = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_Header] = {0.149f, 0.475f, 1.0f, 0.28f};
    c[ImGuiCol_HeaderHovered] = {0.149f, 0.475f, 1.0f, 0.45f};
    c[ImGuiCol_HeaderActive] = {0.149f, 0.475f, 1.0f, 0.62f};
    c[ImGuiCol_Tab] = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_TabHovered] = {0.190f, 0.200f, 0.220f, 1.0f};
    c[ImGuiCol_TabActive] = {0.118f, 0.118f, 0.118f, 1.0f};
    c[ImGuiCol_MenuBarBg] = {0.094f, 0.094f, 0.098f, 1.0f};
    c[ImGuiCol_ScrollbarBg] = {0.118f, 0.118f, 0.118f, 0.55f};
    c[ImGuiCol_ScrollbarGrab] = {0.330f, 0.350f, 0.390f, 1.0f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.430f, 0.460f, 0.520f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive] = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_Separator] = {0.240f, 0.240f, 0.260f, 0.7f};
    c[ImGuiCol_InputTextCursor] = {0.824f, 0.824f, 0.824f, 1.0f};
}

bool ClipboardIdeApp::CreateScintillaEditor() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path dllPath = std::filesystem::path(modulePath).parent_path() / L"Scintilla.dll";
    m_scintillaModule = LoadLibraryW(dllPath.c_str());
    if (!m_scintillaModule)
        m_scintillaModule = LoadLibraryW(L"Scintilla.dll");
    if (!m_scintillaModule) {
        SetStatus("Scintilla.dll not found");
        return false;
    }

    using RegisterFn = int (*)(void*);
    auto registerClasses = reinterpret_cast<RegisterFn>(
        GetProcAddress(m_scintillaModule, "Scintilla_RegisterClasses"));
    if (registerClasses)
        registerClasses(m_hInstance);

    m_scintillaHwnd = CreateWindowExW(
        0, L"Scintilla", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
        0, 0, 100, 100,
        m_hwnd, nullptr, m_hInstance, nullptr);
    if (!m_scintillaHwnd) {
        SetStatus("Could not create Scintilla editor");
        return false;
    }

    SetPropW(m_scintillaHwnd, kScintillaAppProp, this);
    m_scintillaWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(m_scintillaHwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&ClipboardIdeApp::ScintillaWndProc)));

    m_scintillaReady = true;
    ApplyScintillaTheme();
    return true;
}

void ClipboardIdeApp::DestroyScintillaEditor() {
    if (m_scintillaHwnd) {
        if (m_scintillaWndProc) {
            SetWindowLongPtrW(m_scintillaHwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(m_scintillaWndProc));
            m_scintillaWndProc = nullptr;
        }
        RemovePropW(m_scintillaHwnd, kScintillaAppProp);
        DestroyWindow(m_scintillaHwnd);
        m_scintillaHwnd = nullptr;
    }
    if (m_scintillaModule) {
        using ReleaseFn = int (*)();
        auto releaseResources = reinterpret_cast<ReleaseFn>(
            GetProcAddress(m_scintillaModule, "Scintilla_ReleaseResources"));
        if (releaseResources)
            releaseResources();
        FreeLibrary(m_scintillaModule);
        m_scintillaModule = nullptr;
    }
    m_scintillaReady = false;
}

void ClipboardIdeApp::ApplyScintillaTheme() {
    if (!m_scintillaHwnd)
        return;

    Sci(m_scintillaHwnd, SCI_SETCODEPAGE, SC_CP_UTF8);
    Sci(m_scintillaHwnd, SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    Sci(m_scintillaHwnd, SCI_SETUNDOCOLLECTION, 1);
    Sci(m_scintillaHwnd, SCI_SETTABWIDTH, static_cast<uptr_t>(m_tabSize));
    Sci(m_scintillaHwnd, SCI_SETINDENT, static_cast<uptr_t>(m_tabSize));
    Sci(m_scintillaHwnd, SCI_SETUSETABS, m_insertSpaces ? 0 : 1);
    Sci(m_scintillaHwnd, SCI_SETWRAPMODE, m_wordWrap ? SC_WRAP_WORD : 0);
    Sci(m_scintillaHwnd, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    Sci(m_scintillaHwnd, SCI_SETMARGINWIDTHN, 0, 52);
    Sci(m_scintillaHwnd, SCI_SETCARETFORE, Rgb(220, 220, 220));
    Sci(m_scintillaHwnd, SCI_SETCARETLINEVISIBLE, 1);
    Sci(m_scintillaHwnd, SCI_SETCARETLINEBACK, Rgb(36, 36, 40));
    Sci(m_scintillaHwnd, SCI_SETSELALPHA, 96);
    Sci(m_scintillaHwnd, SCI_SETSELBACK, 1, Rgb(38, 79, 120));

    const char* font = "Consolas";
    for (int style = 0; style <= 16; ++style) {
        Sci(m_scintillaHwnd, SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(font));
        Sci(m_scintillaHwnd, SCI_STYLESETSIZE, style, 11);
        Sci(m_scintillaHwnd, SCI_STYLESETBACK, style, Rgb(30, 30, 30));
        Sci(m_scintillaHwnd, SCI_STYLESETFORE, style, Rgb(212, 212, 212));
    }
    Sci(m_scintillaHwnd, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(font));
    Sci(m_scintillaHwnd, SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    Sci(m_scintillaHwnd, SCI_STYLESETBACK, STYLE_DEFAULT, Rgb(30, 30, 30));
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, STYLE_DEFAULT, Rgb(212, 212, 212));
    Sci(m_scintillaHwnd, SCI_STYLECLEARALL);
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, STYLE_LINENUMBER, Rgb(133, 133, 133));
    Sci(m_scintillaHwnd, SCI_STYLESETBACK, STYLE_LINENUMBER, Rgb(26, 26, 28));

    Sci(m_scintillaHwnd, SCI_STYLESETFORE, 1, Rgb(86, 156, 214));
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, 2, Rgb(206, 145, 120));
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, 3, Rgb(106, 153, 85));
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, 4, Rgb(181, 206, 168));
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, 5, Rgb(156, 220, 254));
    Sci(m_scintillaHwnd, SCI_STYLESETFORE, 6, Rgb(180, 180, 188));
    Sci(m_scintillaHwnd, SCI_STYLESETBOLD, 1, 1);
    Sci(m_scintillaHwnd, SCI_STYLESETBOLD, 5, 1);
}

void ClipboardIdeApp::SetScintillaText(const std::string& text) {
    if (!m_scintillaHwnd)
        return;
    Sci(m_scintillaHwnd, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.c_str()));
    Sci(m_scintillaHwnd, SCI_EMPTYUNDOBUFFER);
    m_scintillaTextDirty = false;
    StyleScintillaDocument();
}

std::string ClipboardIdeApp::GetScintillaText() const {
    if (!m_scintillaHwnd)
        return m_doc.text;
    const sptr_t len = Sci(m_scintillaHwnd, SCI_GETTEXTLENGTH);
    std::string out(static_cast<size_t>(len) + 1, '\0');
    Sci(m_scintillaHwnd, SCI_GETTEXT, static_cast<uptr_t>(out.size()), reinterpret_cast<sptr_t>(out.data()));
    if (!out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

void ClipboardIdeApp::StyleScintillaDocument() {
    if (!m_scintillaHwnd)
        return;
    const std::string text = GetScintillaText();
    std::vector<char> styles(text.size(), 0);
    auto mark = [&](size_t begin, size_t end, char style) {
        end = std::min(end, styles.size());
        for (size_t i = begin; i < end; ++i)
            styles[i] = style;
    };
    auto styleLine = [&](size_t lineStart, size_t lineEnd) {
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        bool markdownHeading = m_doc.language == LanguageMode::Markdown && !line.empty() && line[0] == '#';
        if (markdownHeading) {
            mark(lineStart, lineEnd, 1);
            return;
        }
        for (size_t i = 0; i < line.size();) {
            const size_t absolute = lineStart + i;
            const std::string lowTail = Lower(line.substr(i, std::min<size_t>(4, line.size() - i)));
            if ((m_doc.language == LanguageMode::Cpp && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') ||
                (m_doc.language == LanguageMode::PowerShell && line[i] == '#') ||
                (m_doc.language == LanguageMode::Batch && lowTail.rfind("rem", 0) == 0)) {
                mark(absolute, lineEnd, 3);
                return;
            }
            if (line[i] == '"' || line[i] == '\'') {
                const char quote = line[i];
                size_t j = i + 1;
                while (j < line.size()) {
                    if (line[j] == '\\') {
                        j += 2;
                        continue;
                    }
                    if (line[j++] == quote)
                        break;
                }
                bool jsonKey = m_doc.language == LanguageMode::Json;
                if (jsonKey) {
                    size_t k = j;
                    while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
                    jsonKey = k < line.size() && line[k] == ':';
                }
                mark(absolute, lineStart + j, jsonKey ? 5 : 2);
                i = j;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(line[i]))) {
                size_t j = i + 1;
                while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.')) ++j;
                mark(absolute, lineStart + j, 4);
                i = j;
                continue;
            }
            if (IsIdent(line[i])) {
                size_t j = i + 1;
                while (j < line.size() && IsIdent(line[j])) ++j;
                std::string word = Lower(line.substr(i, j - i));
                bool keyword = false;
                if (m_doc.language == LanguageMode::Cpp) {
                    static const std::unordered_set<std::string> words = {
                        "auto","bool","break","case","class","const","continue","default","delete","do","double",
                        "else","enum","false","float","for","if","int","namespace","new","nullptr","private",
                        "protected","public","return","static","struct","switch","true","using","void","while"
                    };
                    keyword = words.count(word) != 0;
                } else if (m_doc.language == LanguageMode::PowerShell) {
                    keyword = !word.empty() && word[0] == '$';
                } else if (m_doc.language == LanguageMode::Json) {
                    keyword = word == "true" || word == "false" || word == "null";
                } else if (m_doc.language == LanguageMode::Batch) {
                    keyword = word == "echo" || word == "set" || word == "if" || word == "for" || word == "goto" || word == "call";
                }
                if (keyword)
                    mark(absolute, lineStart + j, 1);
                i = j;
                continue;
            }
            if (std::ispunct(static_cast<unsigned char>(line[i])))
                styles[absolute] = 6;
            ++i;
        }
    };

    size_t lineStart = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            styleLine(lineStart, i);
            lineStart = i + 1;
        }
    }

    if (!styles.empty()) {
        Sci(m_scintillaHwnd, SCI_STARTSTYLING, 0);
        Sci(m_scintillaHwnd, SCI_SETSTYLINGEX, static_cast<uptr_t>(styles.size()),
            reinterpret_cast<sptr_t>(styles.data()));
    }
    m_scintillaStyledMode = m_doc.language;
}

void ClipboardIdeApp::UpdateScintillaLanguage() {
    if (m_scintillaStyledMode != m_doc.language)
        StyleScintillaDocument();
}

void ClipboardIdeApp::PositionScintilla(ImVec2 screenPos, ImVec2 size) {
    if (!m_scintillaHwnd)
        return;
    if (m_liveResize) {
        SetWindowPos(m_scintillaHwnd, nullptr, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    POINT pt{static_cast<LONG>(screenPos.x), static_cast<LONG>(screenPos.y)};
    ScreenToClient(m_hwnd, &pt);
    SetWindowPos(m_scintillaHwnd, HWND_TOP,
                 pt.x, pt.y,
                 std::max(1, static_cast<int>(size.x)),
                 std::max(1, static_cast<int>(size.y)),
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void ClipboardIdeApp::Render() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin("##ide_root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) SaveFile();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) SaveFileAs();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) {
        std::wstring path = OpenFileDialog();
        if (!path.empty()) OpenFile(path);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N)) NewDocument();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P))
        m_showCommandPalette = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F))
        m_showFind = true;

    DrawTitleBar();

    const float statusH = 24.0f;
    const float tabH = 36.0f;
    const float activityW = 48.0f;
    const float sidebarW = m_showExplorer ? 258.0f : 0.0f;
    const float availH = ImGui::GetContentRegionAvail().y - statusH;

    ImGui::BeginGroup();
    DrawActivityBar(availH);
    ImGui::SameLine(0.0f, 0.0f);
    if (m_showExplorer) {
        DrawSidebar(sidebarW, availH);
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::BeginGroup();
    DrawTabs(tabH);
    DrawEditor(ImGui::GetContentRegionAvail().x, availH - tabH);
    ImGui::EndGroup();
    ImGui::EndGroup();
    DrawStatusBar(statusH);

    DrawCommandBar();
    DrawFindPanel();
    DrawSettingsPanel();
    ImGui::End();
}

void ClipboardIdeApp::DrawTitleBar() {
    const float w = ImGui::GetWindowSize().x;
    const float h = static_cast<float>(kTitleBarHeight);
    const float bw = static_cast<float>(kTitleButtonWidth);
    ImVec2 wp = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x >= wp.x && mouse.x < wp.x + w &&
        mouse.y >= wp.y + static_cast<float>(kResizeBorder) && mouse.y < wp.y + h) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    const ImU32 bg = Color(0.075f, 0.075f, 0.080f);
    const ImU32 border = Color(0.220f, 0.220f, 0.240f, 0.85f);
    const ImU32 text = Color(0.860f, 0.860f, 0.860f);
    const ImU32 muted = Color(0.560f, 0.580f, 0.620f);
    const ImU32 hover = Color(0.180f, 0.195f, 0.225f);
    const ImU32 closeHover = Color(0.860f, 0.150f, 0.150f);
    const ImU32 glyph = Color(0.880f, 0.890f, 0.910f);

    dl->AddRectFilled(wp, {wp.x + w, wp.y + h}, bg);
    dl->AddLine({wp.x, wp.y + h}, {wp.x + w, wp.y + h}, border);

    const float iconX = 10.0f;
    const float iconY = 7.0f;
    const ImVec2 iconMin{wp.x + iconX, wp.y + iconY};
    const ImVec2 iconMax{iconMin.x + 18.0f, iconMin.y + 18.0f};
    dl->AddRectFilled(iconMin, iconMax, Color(0.149f, 0.475f, 1.0f), 3.0f);
    dl->AddRect(iconMin, iconMax, Color(0.040f, 0.040f, 0.050f), 3.0f, 0, 1.5f);
    dl->AddText({iconMin.x + 4.0f, iconMin.y + 1.0f}, Color(1.0f, 1.0f, 1.0f), "++");

    std::string title = "Clipboard++ IDE";
    if (!m_doc.name.empty())
        title += " - " + m_doc.name + (m_doc.dirty ? " *" : "");
    ImGui::SetCursorPos({36.0f, (h - ImGui::GetTextLineHeight()) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();

    const char* mode = ModeName(m_doc.language);
    const float modeW = ImGui::CalcTextSize(mode).x;
    ImGui::SetCursorPos({std::max(240.0f, w * 0.5f - modeW * 0.5f), (h - ImGui::GetTextLineHeight()) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    ImGui::TextUnformatted(mode);
    ImGui::PopStyleColor();

    const bool isMax = IsZoomed(m_hwnd) != 0;
    float x = std::max(0.0f, w - bw * 3.0f);
    if (TitleButton("##ide_min", x, bw, h, 0, hover))
        PostMessageW(m_hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    dl->AddLine({wp.x + x + bw * 0.5f - 5.0f, wp.y + h * 0.5f + 1.0f},
                {wp.x + x + bw * 0.5f + 5.0f, wp.y + h * 0.5f + 1.0f}, glyph, 1.5f);
    x += bw;

    if (TitleButton("##ide_max", x, bw, h, 0, hover))
        PostMessageW(m_hwnd, WM_SYSCOMMAND, isMax ? SC_RESTORE : SC_MAXIMIZE, 0);
    ImVec2 c{wp.x + x + bw * 0.5f, wp.y + h * 0.5f};
    if (isMax) {
        dl->AddRect({c.x - 2.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 2.0f}, glyph, 0.0f, 0, 1.2f);
        dl->AddRectFilled({c.x - 5.0f, c.y - 2.0f}, {c.x + 1.0f, c.y + 5.0f}, bg);
        dl->AddRect({c.x - 5.0f, c.y - 2.0f}, {c.x + 2.0f, c.y + 5.0f}, glyph, 0.0f, 0, 1.2f);
    } else {
        dl->AddRect({c.x - 5.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 5.0f}, glyph, 0.0f, 0, 1.2f);
    }
    x += bw;

    if (TitleButton("##ide_close", x, bw, h, 0, closeHover))
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    c = {wp.x + x + bw * 0.5f, wp.y + h * 0.5f};
    dl->AddLine({c.x - 5.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 5.0f}, glyph, 1.5f);
    dl->AddLine({c.x + 5.0f, c.y - 5.0f}, {c.x - 5.0f, c.y + 5.0f}, glyph, 1.5f);

    ImGui::SetCursorPos({0.0f, h});
}

void ClipboardIdeApp::DrawActivityBar(float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.090f, 0.090f, 0.094f, 1.0f});
    ImGui::BeginChild("##activity", {48.0f, height}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(8.0f);
    if (ImGui::Button("F", {36.0f, 34.0f})) m_showExplorer = !m_showExplorer;
    if (ImGui::Button("S", {36.0f, 34.0f})) m_showFind = true;
    if (ImGui::Button("P", {36.0f, 34.0f})) m_showCommandPalette = true;
    if (ImGui::Button("G", {36.0f, 34.0f})) m_showSettings = true;
    ImGui::SetCursorPosY(height - 42.0f);
    ImGui::TextDisabled("++");
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ClipboardIdeApp::DrawSidebar(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.145f, 0.145f, 0.149f, 1.0f});
    ImGui::BeginChild("##sidebar", {width, height}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled("EXPLORER");
    ImGui::Separator();
    if (ImGui::Button("New Scratch", {-1.0f, 0.0f})) NewDocument();
    if (ImGui::Button("Open File", {-1.0f, 0.0f})) {
        std::wstring path = OpenFileDialog();
        if (!path.empty()) OpenFile(path);
    }
    if (ImGui::Button("Save", {-1.0f, 0.0f})) SaveFile();
    ImGui::Spacing();
    ImGui::TextDisabled("OPEN EDITOR");
    ImGui::Selectable(m_doc.name.c_str(), true);
    ImGui::Spacing();
    ImGui::TextDisabled("OUTLINE");
    ImGui::BulletText("%s", ModeName(m_doc.language));
    ImGui::BulletText("%zu bytes", m_doc.text.size());
    ImGui::BulletText("%s", m_doc.dirty ? "Unsaved changes" : "Saved");
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ClipboardIdeApp::DrawTabs(float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.110f, 0.110f, 0.114f, 1.0f});
    ImGui::BeginChild("##tabs", {0.0f, height}, false, ImGuiWindowFlags_NoScrollbar);
    std::string tab = m_doc.name + (m_doc.dirty ? " *" : "");
    ImGui::PushStyleColor(ImGuiCol_Button, {0.118f, 0.118f, 0.118f, 1.0f});
    ImGui::Button(tab.c_str(), {220.0f, height - 4.0f});
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_doc.path.empty() ? "scratch buffer" : WideToUtf8(m_doc.path).c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ClipboardIdeApp::DrawEditor(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.118f, 0.118f, 0.118f, 1.0f});
    ImGui::BeginChild("##editor_host", {width, height}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (m_showFind)
        ImGui::SetCursorPosY(34.0f);

    const float minimapW = m_showMinimap ? 92.0f : 0.0f;
    const float editorW = ImGui::GetContentRegionAvail().x - minimapW;
    DrawScintillaEditor(editorW, ImGui::GetContentRegionAvail().y);
    if (m_showMinimap) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.100f, 0.100f, 0.104f, 1.0f});
        ImGui::BeginChild("##minimap", {minimapW, 0.0f}, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        std::vector<std::string> lines = SplitLines();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float y = p.y;
        for (size_t i = 0; i < lines.size() && i < 220; ++i) {
            float w = std::min(minimapW - 12.0f, 6.0f + lines[i].size() * 0.55f);
            dl->AddRectFilled({p.x + 6.0f, y}, {p.x + 6.0f + w, y + 2.0f},
                              Color(0.42f, 0.45f, 0.50f, 0.75f));
            y += 3.0f;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ClipboardIdeApp::DrawScintillaEditor(float width, float height) {
    ImGui::BeginChild("##scintilla_slot", {width, height}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    PositionScintilla(pos, size);
    if (m_scintillaTextDirty) {
        m_doc.text = GetScintillaText();
        StyleScintillaDocument();
        m_scintillaTextDirty = false;
    }
    UpdateScintillaLanguage();
    ImGui::EndChild();
}

void ClipboardIdeApp::DrawEditorSurface(float width, float height) {
    std::vector<std::string> lines = SplitLines();
    ImGuiIO& io = ImGui::GetIO();
    ImFont* mono = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : ImGui::GetFont();
    ImGui::PushFont(mono);
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const float charW = ImGui::CalcTextSize("M").x;
    const float gutterW = std::max(52.0f, ImGui::CalcTextSize(std::to_string(lines.size()).c_str()).x + 24.0f);

    ImGui::BeginChild("##editor", {width, height}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::InvisibleButton("##editor_input", {std::max(width, 900.0f), std::max(height, lines.size() * lineH + 32.0f)});
    if (ImGui::IsItemClicked()) {
        m_editorFocused = true;
        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 origin = ImGui::GetItemRectMin();
        int line = std::clamp(static_cast<int>((mouse.y - origin.y - 8.0f) / lineH), 0, static_cast<int>(lines.size()) - 1);
        int col = std::max(0, static_cast<int>((mouse.x - origin.x - gutterW - 8.0f) / charW));
        m_doc.cursor = OffsetForLineColumn(lines, line, col);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_showFind = false;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    dl->AddRectFilled(origin, max, Color(0.118f, 0.118f, 0.118f));
    dl->AddRectFilled(origin, {origin.x + gutterW, max.y}, Color(0.104f, 0.104f, 0.108f));

    const ImVec2 clipMin = ImGui::GetWindowPos();
    const ImVec2 clipMax = {ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                            ImGui::GetWindowPos().y + ImGui::GetWindowSize().y};
    dl->PushClipRect(clipMin, clipMax, true);
    const float scrollY = ImGui::GetScrollY();
    const int firstLine = std::max(0, static_cast<int>(scrollY / lineH) - 1);
    const int lastLine = std::min(static_cast<int>(lines.size()), firstLine + static_cast<int>(height / lineH) + 4);
    for (int i = firstLine; i < lastLine; ++i) {
        const float y = origin.y + 8.0f + i * lineH;
        char num[32];
        snprintf(num, sizeof(num), "%d", i + 1);
        dl->AddText({origin.x + gutterW - ImGui::CalcTextSize(num).x - 12.0f, y},
                    Color(0.48f, 0.50f, 0.54f), num);

        if (!m_doc.search.empty()) {
            std::string lowerLine = Lower(lines[static_cast<size_t>(i)]);
            std::string needle = Lower(m_doc.search);
            size_t pos = lowerLine.find(needle);
            while (pos != std::string::npos) {
                float x = origin.x + gutterW + 8.0f + static_cast<float>(pos) * charW;
                dl->AddRectFilled({x, y}, {x + needle.size() * charW, y + lineH},
                                  Color(0.78f, 0.58f, 0.16f, 0.34f));
                pos = lowerLine.find(needle, pos + needle.size());
            }
        }

        DrawHighlightedLine(dl, {origin.x + gutterW + 8.0f, y},
                            lines[static_cast<size_t>(i)], m_doc.language, clipMax.x);
    }
    DrawCursor(dl, {origin.x + gutterW + 8.0f, origin.y + 8.0f}, lines, charW, lineH);
    dl->PopClipRect();

    if (m_editorFocused)
        HandleEditorInput(lines);
    ImGui::EndChild();
    ImGui::PopFont();
}

void ClipboardIdeApp::HandleEditorInput(const std::vector<std::string>& lines) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_editorFocused = false;
        return;
    }

    int line = 0, col = 0;
    LineColumnForOffset(lines, m_doc.cursor, line, col);
    auto setCursor = [&](int nextLine, int nextCol) {
        m_doc.cursor = OffsetForLineColumn(lines, nextLine, nextCol);
    };

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        if (m_doc.cursor > 0) --m_doc.cursor;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        if (m_doc.cursor < m_doc.text.size()) ++m_doc.cursor;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) setCursor(line - 1, col);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) setCursor(line + 1, col);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) setCursor(line, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) setCursor(line, static_cast<int>(lines[static_cast<size_t>(line)].size()));
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && m_doc.cursor > 0) {
        m_doc.text.erase(m_doc.cursor - 1, 1);
        --m_doc.cursor;
        m_doc.dirty = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_doc.cursor < m_doc.text.size()) {
        m_doc.text.erase(m_doc.cursor, 1);
        m_doc.dirty = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        m_doc.text.insert(m_doc.cursor, "\n");
        ++m_doc.cursor;
        m_doc.dirty = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        std::string tab = m_insertSpaces ? std::string(static_cast<size_t>(m_tabSize), ' ') : "\t";
        m_doc.text.insert(m_doc.cursor, tab);
        m_doc.cursor += tab.size();
        m_doc.dirty = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        std::string clip = GetClipboardUtf8();
        m_doc.text.insert(m_doc.cursor, clip);
        m_doc.cursor += clip.size();
        m_doc.dirty = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
        SetClipboardUtf8(m_doc.text);

    if (!io.KeyCtrl && !io.KeyAlt) {
        for (ImWchar ch : io.InputQueueCharacters) {
            if (ch < 32 || ch == 127)
                continue;
            char buf[8]{};
            if (ch < 0x80) {
                buf[0] = static_cast<char>(ch);
                m_doc.text.insert(m_doc.cursor, buf);
                ++m_doc.cursor;
                m_doc.dirty = true;
            }
        }
        io.InputQueueCharacters.resize(0);
    }
    m_doc.cursor = std::min(m_doc.cursor, m_doc.text.size());
    UpdateTitle();
}

void ClipboardIdeApp::DrawHighlightedLine(ImDrawList* drawList, ImVec2 pos, const std::string& line,
                                          LanguageMode mode, float clipRight) {
    static const std::unordered_set<std::string> cppKeywords = {
        "auto","bool","break","case","class","const","continue","default","delete","do","double",
        "else","enum","false","float","for","if","int","namespace","new","nullptr","private",
        "protected","public","return","static","struct","switch","true","using","void","while"
    };
    static const std::unordered_set<std::string> psKeywords = {
        "function","param","if","else","elseif","foreach","for","while","switch","return","true","false","null"
    };
    static const std::unordered_set<std::string> batchKeywords = {
        "echo","set","if","else","for","in","do","goto","call","exit","rem","pause"
    };

    auto emit = [&](const std::string& text, ImU32 color) {
        if (text.empty() || pos.x > clipRight)
            return;
        drawList->AddText(pos, color, text.c_str());
        pos.x += ImGui::CalcTextSize(text.c_str()).x;
    };

    const ImU32 normal = Color(0.824f, 0.824f, 0.824f);
    const ImU32 keyword = Color(0.337f, 0.612f, 0.839f);
    const ImU32 string = Color(0.808f, 0.569f, 0.471f);
    const ImU32 comment = Color(0.420f, 0.620f, 0.380f);
    const ImU32 number = Color(0.710f, 0.820f, 0.560f);
    const ImU32 keyColor = Color(0.612f, 0.800f, 1.000f);
    const ImU32 punctuation = Color(0.620f, 0.640f, 0.680f);
    const ImU32 heading = Color(0.337f, 0.612f, 0.839f);

    if (mode == LanguageMode::Markdown && !line.empty() && line[0] == '#') {
        emit(line, heading);
        return;
    }

    for (size_t i = 0; i < line.size();) {
        if ((mode == LanguageMode::Cpp && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') ||
            (mode == LanguageMode::PowerShell && line[i] == '#') ||
            (mode == LanguageMode::Batch && Lower(line.substr(i, 3)) == "rem")) {
            emit(line.substr(i), comment);
            return;
        }
        if (line[i] == '"' || line[i] == '\'') {
            const char quote = line[i];
            size_t j = i + 1;
            while (j < line.size()) {
                if (line[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (line[j++] == quote)
                    break;
            }
            bool jsonKey = mode == LanguageMode::Json;
            if (jsonKey) {
                size_t k = j;
                while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
                jsonKey = k < line.size() && line[k] == ':';
            }
            emit(line.substr(i, j - i), jsonKey ? keyColor : string);
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[i]))) {
            size_t j = i + 1;
            while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.')) ++j;
            emit(line.substr(i, j - i), number);
            i = j;
            continue;
        }
        if (IsIdent(line[i])) {
            size_t j = i + 1;
            while (j < line.size() && IsIdent(line[j])) ++j;
            std::string word = line.substr(i, j - i);
            std::string low = Lower(word);
            bool isKeyword = false;
            if (mode == LanguageMode::Cpp) isKeyword = cppKeywords.count(low) != 0;
            else if (mode == LanguageMode::PowerShell) isKeyword = psKeywords.count(low) != 0 || (!word.empty() && word[0] == '$');
            else if (mode == LanguageMode::Batch) isKeyword = batchKeywords.count(low) != 0;
            else if (mode == LanguageMode::Json) isKeyword = low == "true" || low == "false" || low == "null";
            emit(word, isKeyword ? keyword : normal);
            i = j;
            continue;
        }
        emit(line.substr(i, 1), std::ispunct(static_cast<unsigned char>(line[i])) ? punctuation : normal);
        ++i;
    }
}

void ClipboardIdeApp::DrawCursor(ImDrawList* drawList, ImVec2 origin,
                                 const std::vector<std::string>& lines,
                                 float charW, float lineH) {
    if (!m_editorFocused)
        return;
    const double t = ImGui::GetTime();
    if (fmod(t, 1.0) > 0.55)
        return;
    int line = 0, col = 0;
    LineColumnForOffset(lines, m_doc.cursor, line, col);
    const float x = origin.x + col * charW;
    const float y = origin.y + line * lineH;
    drawList->AddLine({x, y}, {x, y + lineH - 2.0f}, Color(0.850f, 0.850f, 0.850f), 1.4f);
}

void ClipboardIdeApp::DrawStatusBar(float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.000f, 0.478f, 0.800f, 1.0f});
    ImGui::BeginChild("##status", {0.0f, height}, false, ImGuiWindowFlags_NoScrollbar);
    int line = 0;
    int col = 0;
    if (m_scintillaHwnd) {
        const sptr_t pos = Sci(m_scintillaHwnd, SCI_GETCURRENTPOS);
        line = static_cast<int>(Sci(m_scintillaHwnd, SCI_LINEFROMPOSITION, static_cast<uptr_t>(pos)));
        col = static_cast<int>(Sci(m_scintillaHwnd, SCI_GETCOLUMN, static_cast<uptr_t>(pos)));
    } else {
        LineColumnForOffset(SplitLines(), m_doc.cursor, line, col);
    }
    ImGui::Text("  %s", ModeName(m_doc.language));
    ImGui::SameLine();
    ImGui::Text("| Ln %d, Col %d", line + 1, col + 1);
    ImGui::SameLine();
    ImGui::Text("| Spaces: %d", m_tabSize);
    ImGui::SameLine();
    ImGui::Text("| UTF-8");
    ImGui::SameLine();
    ImGui::Text("| %s", m_status.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ClipboardIdeApp::DrawCommandBar() {
    if (!m_showCommandPalette)
        return;
    ImGui::SetNextWindowSize({560.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.15f});
    ImGui::OpenPopup("Command Palette");
    if (ImGui::BeginPopupModal("Command Palette", &m_showCommandPalette,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##command", m_commandBuf, sizeof(m_commandBuf));
        if (ImGui::Selectable("File: New Scratch")) { NewDocument(); m_showCommandPalette = false; }
        if (ImGui::Selectable("File: Open")) { std::wstring path = OpenFileDialog(); if (!path.empty()) OpenFile(path); m_showCommandPalette = false; }
        if (ImGui::Selectable("File: Save")) { SaveFile(); m_showCommandPalette = false; }
        if (ImGui::Selectable("Editor: Toggle Minimap")) { m_showMinimap = !m_showMinimap; m_showCommandPalette = false; }
        if (ImGui::Selectable("Editor: Toggle Explorer")) { m_showExplorer = !m_showExplorer; m_showCommandPalette = false; }
        if (ImGui::Selectable("Clipboard: Copy Buffer")) { CopyDocumentToClipboard(); m_showCommandPalette = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            m_showCommandPalette = false;
        ImGui::EndPopup();
    }
}

void ClipboardIdeApp::DrawFindPanel() {
    if (!m_showFind)
        return;
    ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x - 396.0f,
                             ImGui::GetMainViewport()->WorkPos.y + 46.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({360.0f, 86.0f}, ImGuiCond_Always);
    ImGui::Begin("Find", &m_showFind, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##find", m_findBuf, sizeof(m_findBuf)))
        m_doc.search = m_findBuf;
    ImGui::TextDisabled("Matches are highlighted in the editor.");
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_showFind = false;
    ImGui::End();
}

void ClipboardIdeApp::DrawSettingsPanel() {
    if (!m_showSettings)
        return;
    ImGui::SetNextWindowSize({340.0f, 260.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor Settings", &m_showSettings);
    ImGui::Checkbox("Show explorer", &m_showExplorer);
    ImGui::Checkbox("Show minimap", &m_showMinimap);
    bool changedEditorBehavior = false;
    changedEditorBehavior |= ImGui::Checkbox("Word wrap", &m_wordWrap);
    changedEditorBehavior |= ImGui::Checkbox("Insert spaces", &m_insertSpaces);
    changedEditorBehavior |= ImGui::SliderInt("Tab size", &m_tabSize, 2, 8);
    ImGui::SliderFloat("UI scale", &m_fontScale, 0.85f, 1.35f);
    const char* modes[] = {"Plain Text", "PowerShell", "Batch", "JSON", "Markdown", "C++"};
    int mode = static_cast<int>(m_doc.language);
    if (ImGui::Combo("Language", &mode, modes, IM_ARRAYSIZE(modes))) {
        m_doc.language = static_cast<LanguageMode>(mode);
        StyleScintillaDocument();
    }
    if (changedEditorBehavior)
        ApplyScintillaTheme();
    ImGui::End();
}

bool ClipboardIdeApp::OpenFile(const std::wstring& path) {
    m_doc.text = ReadFileUtf8(path);
    m_doc.path = path;
    m_doc.name = FilenameUtf8(path);
    m_doc.language = ModeFromPath(path);
    m_doc.cursor = 0;
    m_doc.dirty = false;
    SetScintillaText(m_doc.text);
    m_doc.dirty = false;
    SetStatus("Opened " + m_doc.name);
    UpdateTitle();
    return true;
}

bool ClipboardIdeApp::SaveFile() {
    if (m_doc.path.empty())
        return SaveFileAs();
    m_doc.text = GetScintillaText();
    if (!WriteFileUtf8(m_doc.path, m_doc.text)) {
        SetStatus("Save failed");
        return false;
    }
    m_doc.dirty = false;
    SetStatus("Saved");
    UpdateTitle();
    return true;
}

bool ClipboardIdeApp::SaveFileAs() {
    std::wstring path = SaveFileDialog();
    if (path.empty())
        return false;
    m_doc.path = path;
    m_doc.name = FilenameUtf8(path);
    m_doc.language = ModeFromPath(path);
    return SaveFile();
}

void ClipboardIdeApp::NewDocument() {
    m_doc = DocumentState{};
    m_doc.name = "Untitled";
    SetScintillaText(m_doc.text);
    m_doc.dirty = false;
    SetStatus("New scratch buffer");
    UpdateTitle();
}

void ClipboardIdeApp::LoadLaunchDocument(const IdeLaunchOptions& options) {
    if (!options.filePath.empty() && std::filesystem::exists(options.filePath)) {
        OpenFile(options.filePath);
    } else {
        m_doc.text = GetClipboardUtf8();
        m_doc.name = "Clipboard Scratch";
        m_doc.dirty = !m_doc.text.empty();
        SetScintillaText(m_doc.text);
    }
    if (!options.mode.empty()) {
        m_doc.language = ModeFromText(options.mode);
        StyleScintillaDocument();
    }
}

void ClipboardIdeApp::UpdateTitle() {
    std::wstring title = L"Clipboard++ IDE - " + Utf8ToWide(m_doc.name);
    if (m_doc.dirty)
        title += L" *";
    SetWindowTextW(m_hwnd, title.c_str());
}

void ClipboardIdeApp::SetStatus(std::string status) {
    m_status = std::move(status);
}

void ClipboardIdeApp::CopyDocumentToClipboard() {
    m_doc.text = GetScintillaText();
    SetClipboardUtf8(m_doc.text);
    SetStatus("Copied buffer to clipboard");
}

void ClipboardIdeApp::CheckDroppedFile(HDROP drop) {
    wchar_t path[MAX_PATH]{};
    if (DragQueryFileW(drop, 0, path, MAX_PATH))
        OpenFile(path);
    DragFinish(drop);
}

std::wstring ClipboardIdeApp::OpenFileDialog() {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Code and text files\0*.txt;*.ps1;*.cmd;*.bat;*.json;*.md;*.cpp;*.h;*.hpp\0All files\0*.*\0";
    ofn.lpstrTitle = L"Open file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : std::wstring{};
}

std::wstring ClipboardIdeApp::SaveFileDialog() {
    wchar_t file[MAX_PATH]{};
    if (!m_doc.path.empty())
        wcsncpy_s(file, m_doc.path.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Code and text files\0*.txt;*.ps1;*.cmd;*.bat;*.json;*.md;*.cpp;*.h;*.hpp\0All files\0*.*\0";
    ofn.lpstrTitle = L"Save file";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? std::wstring(file) : std::wstring{};
}

std::vector<std::string> ClipboardIdeApp::SplitLines() const {
    const std::string text = m_scintillaHwnd ? GetScintillaText() : m_doc.text;
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty() || (!text.empty() && text.back() == '\n'))
        lines.push_back({});
    return lines;
}

size_t ClipboardIdeApp::OffsetForLineColumn(const std::vector<std::string>& lines, int line, int column) const {
    if (lines.empty())
        return 0;
    line = std::clamp(line, 0, static_cast<int>(lines.size()) - 1);
    size_t offset = 0;
    for (int i = 0; i < line; ++i)
        offset += lines[static_cast<size_t>(i)].size() + 1;
    column = std::clamp(column, 0, static_cast<int>(lines[static_cast<size_t>(line)].size()));
    return std::min(offset + static_cast<size_t>(column), m_doc.text.size());
}

void ClipboardIdeApp::LineColumnForOffset(const std::vector<std::string>& lines, size_t offset,
                                          int& line, int& column) const {
    line = 0;
    column = 0;
    offset = std::min(offset, m_doc.text.size());
    size_t at = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const size_t len = lines[i].size();
        if (offset <= at + len) {
            line = static_cast<int>(i);
            column = static_cast<int>(offset - at);
            return;
        }
        at += len + 1;
    }
    line = static_cast<int>(std::max<size_t>(1, lines.size()) - 1);
    column = lines.empty() ? 0 : static_cast<int>(lines.back().size());
}
