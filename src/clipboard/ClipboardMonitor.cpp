#include "ClipboardMonitor.h"
#include "ContentDetector.h"
#include <shellapi.h>   // DragQueryFileW
#include <psapi.h>      // QueryFullProcessImageNameW

static constexpr wchar_t kMonitorClass[] = L"CPPClipboardMonitor";

// ── Construction / destruction ────────────────────────────────────────────────

ClipboardMonitor::ClipboardMonitor() = default;

ClipboardMonitor::~ClipboardMonitor() {
    Stop();
}

// ── Public ────────────────────────────────────────────────────────────────────

bool ClipboardMonitor::Start(HINSTANCE hInstance, ItemCallback onItem) {
    m_hInstance = hInstance;
    m_callback  = std::move(onItem);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kMonitorClass;
    RegisterClassExW(&wc);

    // HWND_MESSAGE = message-only window, no UI, no taskbar entry
    m_hwnd = CreateWindowExW(0, kMonitorClass, nullptr, 0,
                              0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, hInstance,
                              static_cast<LPVOID>(this));
    if (!m_hwnd) return false;

    if (!AddClipboardFormatListener(m_hwnd)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    m_lastSeq = GetClipboardSequenceNumber();
    return true;
}

void ClipboardMonitor::Stop() {
    if (m_hwnd) {
        RemoveClipboardFormatListener(m_hwnd);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(kMonitorClass, m_hInstance);
}

void ClipboardMonitor::SuppressNextUpdate() {
    m_ignoreUntilTick = GetTickCount64() + 250;
}

// ── Private: Win32 message handler ───────────────────────────────────────────

LRESULT CALLBACK ClipboardMonitor::WndProc(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam) {
    // Store 'this' from CreateWindowEx lpCreateParams on first message
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* mon = reinterpret_cast<ClipboardMonitor*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_CLIPBOARDUPDATE && mon)
        mon->OnClipboardUpdate();

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClipboardMonitor::OnClipboardUpdate() {
    // Guard against duplicate firings and self-generated updates
    DWORD seq = GetClipboardSequenceNumber();
    if (seq == m_lastSeq) return;
    m_lastSeq = seq;

    if (m_ignoreUntilTick && GetTickCount64() <= m_ignoreUntilTick) {
        return;
    }
    m_ignoreUntilTick = 0;

    ClipboardItem item = ReadClipboard();
    if (item.IsEmpty()) return;

    if (m_callback)
        m_callback(std::move(item));
}

// ── Private: clipboard reading ────────────────────────────────────────────────

ClipboardItem ClipboardMonitor::ReadClipboard() const {
    ClipboardItem item;

    if (!OpenClipboard(m_hwnd))
        return item;

    // ── Plain / unicode text (most common) ────────────────────────────────────
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE h = GetClipboardData(CF_HDROP);
        if (h) {
            auto* hDrop  = static_cast<HDROP>(h);
            UINT  count  = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::string paths;
            for (UINT i = 0; i < count; ++i) {
                wchar_t buf[MAX_PATH]{};
                if (DragQueryFileW(hDrop, i, buf, MAX_PATH)) {
                    if (!paths.empty()) paths += '\n';
                    paths += WideToUtf8(buf);
                }
            }
            item.text = paths;
        }
        item.type = ContentType::FilePaths;
    }
    else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            auto* pw = static_cast<wchar_t*>(GlobalLock(h));
            if (pw) {
                item.text = WideToUtf8(pw);
                GlobalUnlock(h);
            }
        }
        item.type = ContentType::Text;
    }
    // ── DIB image ─────────────────────────────────────────────────────────────
    else if (IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE h = GetClipboardData(CF_DIB);
        if (h) {
            auto* bmi = static_cast<BITMAPINFO*>(GlobalLock(h));
            if (bmi) {
                item.imageW = bmi->bmiHeader.biWidth;
                item.imageH = std::abs(bmi->bmiHeader.biHeight);
                SIZE_T sz   = GlobalSize(h);
                item.imageData.resize(sz);
                std::memcpy(item.imageData.data(), bmi, sz);
                GlobalUnlock(h);
            }
        }
        item.type = ContentType::Image;
        item.text = "[Image " + std::to_string(item.imageW)
                  + "x"      + std::to_string(item.imageH) + "]";
    }
    // ── File drop ─────────────────────────────────────────────────────────────
    else if (false && IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE h = GetClipboardData(CF_HDROP);
        if (h) {
            auto* hDrop  = static_cast<HDROP>(h);
            UINT  count  = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::string paths;
            for (UINT i = 0; i < count; ++i) {
                wchar_t buf[MAX_PATH]{};
                if (DragQueryFileW(hDrop, i, buf, MAX_PATH)) {
                    if (!paths.empty()) paths += '\n';
                    paths += WideToUtf8(buf);
                }
            }
            item.text = paths;
        }
        item.type = ContentType::FilePaths;
    }

    CloseClipboard();

    if (item.IsEmpty()) return item; // nothing readable

    // Source process (best-effort; empty string on failure)
    item.sourceProcess = GetForegroundProcessName();

    // Auto-tag text content
    if (!item.text.empty() && item.type != ContentType::Image)
        item.tags = ContentDetector::DetectTags(item.text);

    return item;
}

// ── Private: helpers ──────────────────────────────────────────────────────────

std::string ClipboardMonitor::WideToUtf8(const wchar_t* w, int len) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    // Remove null terminator if WideCharToMultiByte included it
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string ClipboardMonitor::GetForegroundProcessName() {
    HWND fg = GetForegroundWindow();
    if (!fg) return {};

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return {};

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};

    wchar_t buf[MAX_PATH]{};
    DWORD   sz = MAX_PATH;
    BOOL    ok = QueryFullProcessImageNameW(hProc, 0, buf, &sz);
    CloseHandle(hProc);
    if (!ok) return {};

    // Strip directory — keep only "app.exe"
    std::wstring path(buf, sz);
    auto pos = path.rfind(L'\\');
    std::wstring name = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
    return WideToUtf8(name.c_str());
}
