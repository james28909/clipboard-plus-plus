#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static constexpr wchar_t kPopupClass[] = L"CPPPopupWnd";

// ── Create / Destroy ──────────────────────────────────────────────────────────

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
    wc.hbrBackground = nullptr;  // D3D owns the background — prevents white flash on resize
    wc.lpszClassName = kPopupClass;
    RegisterClassExW(&wc);

    // WS_EX_TOPMOST  — always above other windows
    // WS_EX_LAYERED  — needed for SetLayeredWindowAttributes (opacity)
    // WS_EX_NOACTIVATE — don't steal focus from the app being pasted into.
    //                    Keyboard input for the search bar will be handled
    //                    via WH_KEYBOARD_LL in Milestone 4.
    // WS_POPUP + WS_THICKFRAME — borderless but resizable
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

    // ImGui context — independent from the main window's context
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();

    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io  = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style      = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding    = {8.0f, 8.0f};

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    // Restore the main context so we don't interfere with Application init
    ImGui::SetCurrentContext(prevCtx);
    return true;
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

// ── Show / Hide ───────────────────────────────────────────────────────────────

void PopupWindow::Show() {
    m_prevForeground = GetForegroundWindow();
    PositionAtCursor();
    ApplyOpacity();
    ShowWindow(m_hwnd, SW_SHOWNA); // NA = no-activate
    m_visible    = true;
    m_justOpened = true;
    m_queueMode  = false;
    m_queue.clear();
    std::memset(m_searchBuf, 0, sizeof(m_searchBuf));
}

void PopupWindow::Hide() {
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible   = false;
    m_queueMode = false;
    m_queue.clear();
}

// ── Render ────────────────────────────────────────────────────────────────────

void PopupWindow::Render() {
    if (!m_visible || !m_imguiCtx) return;

    // Switch to popup ImGui context for this frame
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Full-window ImGui overlay — no title bar, fills the entire HWND
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)(rc.right), (float)(rc.bottom)});
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

    // Escape closes
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::End(); ImGui::Render();
        ImGui::SetCurrentContext(prevCtx);
        Hide();
        return;
    }

    // ── Search bar ────────────────────────────────────────────────────────────
    float winW = ImGui::GetWindowWidth();
    ImGui::SetNextItemWidth(winW - 16.0f);
    if (m_justOpened) {
        // Note: keyboard focus won't work while WS_EX_NOACTIVATE is set.
        // Milestone 4 (keyboard hook) will handle search-bar typing.
        ImGui::SetKeyboardFocusHere();
        m_justOpened = false;
    }
    ImGui::InputTextWithHint("##search", "  Search...",
                              m_searchBuf, sizeof(m_searchBuf));

    ImGui::Spacing();
    DrawFilterStrip();
    ImGui::Separator();
    DrawItemList();

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

// ── Filter strip ──────────────────────────────────────────────────────────────

void PopupWindow::DrawFilterStrip() {
    struct Btn { const char* label; int mode; };
    static constexpr Btn kFilters[] = {
        {"All",0},{"Text",1},{"Image",2},{"URL",3}
    };
    for (const auto& f : kFilters) {
        bool active = (m_filterMode == f.mode);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(f.label)) m_filterMode = f.mode;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    bool qActive = m_queueMode;
    if (qActive)
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Queue")) { m_queueMode = !m_queueMode; m_queue.clear(); }
    if (qActive) ImGui::PopStyleColor();
    ImGui::SameLine();

    if (m_queueMode && !m_queue.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.f));
        if (ImGui::SmallButton("Paste All")) PasteQueue();
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    if (ImGui::SmallButton(" @ ")) {       // gear placeholder
        Application::Get()->ShowMainWindow();
        Hide();
    }
    ImGui::Spacing();
}

// ── Item list ─────────────────────────────────────────────────────────────────

bool PopupWindow::ItemPassesFilter(const ClipboardItem& item) const {
    switch (m_filterMode) {
    case 1: return item.IsText();
    case 2: return item.IsImage();
    case 3: return (item.tags & TAG_URL) != 0;
    default: return true;
    }
}

void PopupWindow::DrawItemList() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    const std::string query(m_searchBuf);
    std::string lquery = query;
    std::transform(lquery.begin(), lquery.end(), lquery.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });

    ImGui::BeginChild("##items", {0.f, 0.f}, ImGuiChildFlags_None);

    size_t slot     = 0;
    bool   anyShown = false;

    for (size_t i = 0; i < hist->Size() && slot < 35; ++i) {
        const ClipboardItem* item = hist->Get(i);
        if (!item || !ItemPassesFilter(*item)) continue;

        if (!lquery.empty()) {
            std::string lt = item->text;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lt.find(lquery) == std::string::npos) continue;
        }

        anyShown = true;

        char key[2]{};
        key[0] = slot < 9 ? (char)('1' + (int)slot)
                           : (char)('a' + (int)(slot - 9));
        ++slot;

        int qpos = -1;
        for (size_t q = 0; q < m_queue.size(); ++q)
            if (m_queue[q] == i) { qpos = (int)q + 1; break; }

        const bool isSecret = (item->tags & TAG_SECRET) != 0;
        if (isSecret)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.42f, 0.42f, 1.f));

        char label[320]{};
        if (qpos >= 0)
            std::snprintf(label, sizeof(label), " %s [%d]  %s##r%zu",
                          key, qpos, item->Preview(60).c_str(), i);
        else
            std::snprintf(label, sizeof(label), " %s   %s##r%zu",
                          key, item->Preview(60).c_str(), i);

        if (ImGui::Selectable(label, qpos >= 0,
                               ImGuiSelectableFlags_SpanAllColumns)) {
            // Check physical Ctrl state — ImGui's KeyCtrl is unreliable since
            // the popup has WS_EX_NOACTIVATE and never receives raw key events.
            const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

            if (ctrlHeld) {
                // Ctrl+click: paste immediately and KEEP popup open so the
                // user can keep clicking items one after another.
                // The background window already has focus (WS_EX_NOACTIVATE),
                // so we just write to clipboard and send V — Ctrl is already
                // physically held, so the background app sees Ctrl+V.
                if (isSecret) ImGui::PopStyleColor();
                WriteToClipboard(*item);
                INPUT in[2]{};
                in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = 'V';
                in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, in, sizeof(INPUT));
                ImGui::EndChild();
                return;
            } else if (m_queueMode) {
                if (qpos >= 0)
                    m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), i),
                                   m_queue.end());
                else
                    m_queue.push_back(i);
            } else {
                if (isSecret) ImGui::PopStyleColor();
                PasteItem(*item);
                ImGui::EndChild();
                return;
            }
        }
        if (isSecret) ImGui::PopStyleColor();
    }

    if (!anyShown)
        ImGui::TextDisabled("  No items match.");

    ImGui::EndChild();
}

// ── Paste ─────────────────────────────────────────────────────────────────────

void PopupWindow::PasteDirect(const ClipboardItem& item, HWND targetWindow) {
    m_prevForeground = targetWindow;
    WriteToClipboard(item);
    RestoreFocusAndPaste();
}

void PopupWindow::PasteItem(const ClipboardItem& item) {
    WriteToClipboard(item);
    Hide();
    RestoreFocusAndPaste();
}

void PopupWindow::PasteQueue() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    std::vector<size_t> indices = m_queue;
    Hide();

    for (size_t qi = 0; qi < indices.size(); ++qi) {
        const ClipboardItem* item = hist->Get(indices[qi]);
        if (!item) continue;
        WriteToClipboard(*item);
        RestoreFocusAndPaste();
        if (qi + 1 < indices.size() && m_queueDelayMs > 0)
            Sleep(static_cast<DWORD>(m_queueDelayMs));
    }
}

void PopupWindow::WriteToClipboard(const ClipboardItem& item) {
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
                                        item.text.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE,
                                      static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
            if (hm) {
                MultiByteToWideChar(CP_UTF8, 0, item.text.c_str(), -1,
                                    static_cast<wchar_t*>(GlobalLock(hm)), wlen);
                GlobalUnlock(hm);
                SetClipboardData(CF_UNICODETEXT, hm);
            }
        }
    }
    CloseClipboard();
}

void PopupWindow::RestoreFocusAndPaste() {
    if (m_prevForeground && IsWindow(m_prevForeground)) {
        SetForegroundWindow(m_prevForeground);
        Sleep(25);
    }

    // If Shift is physically held (e.g. the user triggered this via Ctrl+Shift+<slot>),
    // our injected Ctrl+V would look like Ctrl+Shift+V to our own LL hook and
    // re-open the popup. Release Shift first so the injected V is plain Ctrl+V.
    const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    INPUT in[5]{};
    int   n = 0;

    if (shiftHeld) {
        in[n].type         = INPUT_KEYBOARD;
        in[n].ki.wVk       = VK_SHIFT;
        in[n].ki.dwFlags   = KEYEVENTF_KEYUP;
        ++n;
    }

    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_CONTROL;             ++n;
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = 'V';                    ++n;
    in[n]      = in[n-1]; in[n].ki.dwFlags    = KEYEVENTF_KEYUP;        ++n;
    in[n]      = in[0];   in[n].ki.dwFlags    = KEYEVENTF_KEYUP;        ++n;

    SendInput(static_cast<UINT>(n), in, sizeof(INPUT));
}

// ── D3D11 swap chain ──────────────────────────────────────────────────────────

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

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = static_cast<UINT>(m_width);
    sd.BufferDesc.Height                  = static_cast<UINT>(m_height);
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

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// ── Win32 message handler ─────────────────────────────────────────────────────

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
    case WM_ERASEBKGND:
        return TRUE; // tell Windows we handled it — prevents white fill on resize

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize = {280, 180};
        return 0;
    }

    case WM_SIZE:
        if (pw && pw->m_swapChain && wParam != SIZE_MINIMIZED) {
            pw->DestroyRenderTarget();
            pw->m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                            DXGI_FORMAT_UNKNOWN, 0);
            pw->CreateRenderTarget();
        }
        return 0;

    case WM_KILLFOCUS:
        if (pw) pw->Hide();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
