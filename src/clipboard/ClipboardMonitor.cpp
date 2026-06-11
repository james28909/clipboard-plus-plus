#include "ClipboardMonitor.h"
#include "ContentDetector.h"
#include "ImageStore.h"
#include "../util/Win32Util.h"
#include <shellapi.h>   // DragQueryFileW
#include <chrono>
#include <vector>

static constexpr wchar_t kMonitorClass[] = L"CPPClipboardMonitor";
static constexpr int kOpenClipboardAttempts = 12;
static constexpr DWORD kOpenClipboardRetryMs = 8;

namespace {

std::string FileDropPathsUtf8(HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::string paths;
    for (UINT i = 0; i < count; ++i) {
        const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
        if (len == 0)
            continue;
        std::vector<wchar_t> buf(static_cast<size_t>(len) + 1);
        if (DragQueryFileW(hDrop, i, buf.data(), static_cast<UINT>(buf.size()))) {
            if (!paths.empty()) paths += '\n';
            paths += win32util::WideToUtf8(buf.data());
        }
    }
    return paths;
}

} // namespace

// -- Construction / destruction ------------------------------------------------

ClipboardMonitor::ClipboardMonitor() = default;

ClipboardMonitor::~ClipboardMonitor() {
    Stop();
}

// -- Public --------------------------------------------------------------------

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

// -- Private: Win32 message handler -------------------------------------------

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

// -- Private: clipboard reading ------------------------------------------------

ClipboardItem ClipboardMonitor::ReadClipboard() const {
    ClipboardItem item;

    bool opened = false;
    for (int attempt = 0; attempt < kOpenClipboardAttempts; ++attempt) {
        if (OpenClipboard(m_hwnd)) {
            opened = true;
            break;
        }
        Sleep(kOpenClipboardRetryMs);
    }

    if (!opened)
        return item;

    // -- Plain / unicode text (most common) ------------------------------------
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE h = GetClipboardData(CF_HDROP);
        if (h)
            item.text = FileDropPathsUtf8(static_cast<HDROP>(h));
        item.type = ContentType::FilePaths;
    }
    else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            auto* pw = static_cast<wchar_t*>(GlobalLock(h));
            if (pw) {
                item.text = win32util::WideToUtf8(pw);
                GlobalUnlock(h);
            }
        }
        item.type = ContentType::Text;
    }
    // -- Image: PNG clipboard format > CF_DIBV5 > CF_DIB > CF_BITMAP ----------
    else if (IsImageAvailable()) {
        ReadImageFormats(item);
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

// -- Private: helpers ----------------------------------------------------------

std::string ClipboardMonitor::GetForegroundProcessName() {
    return win32util::ProcessNameFromWindow(GetForegroundWindow());
}

bool ClipboardMonitor::IsImageAvailable() {
    // Registered PNG format used by Chrome, Firefox, Edge, Photoshop, etc.
    static const UINT CF_PNG = RegisterClipboardFormatW(L"PNG");
    return IsClipboardFormatAvailable(CF_PNG)
        || IsClipboardFormatAvailable(CF_DIBV5)
        || IsClipboardFormatAvailable(CF_DIB)
        || IsClipboardFormatAvailable(CF_BITMAP);
}

void ClipboardMonitor::ReadImageFormats(ClipboardItem& item) const {
    static const UINT CF_PNG  = RegisterClipboardFormatW(L"PNG");
    static const UINT CF_JFIF = RegisterClipboardFormatW(L"JFIF"); // JPEG

    std::vector<uint8_t> rawBytes; // DIB bytes (for GDI+ path) or PNG bytes directly
    bool isPngDirect = false;      // true when rawBytes already contains a PNG

    // Priority 1: PNG clipboard format — already perfect, no conversion needed
    if (IsClipboardFormatAvailable(CF_PNG)) {
        HANDLE h = GetClipboardData(CF_PNG);
        if (h) {
            SIZE_T sz = GlobalSize(h);
            auto* ptr = static_cast<const uint8_t*>(GlobalLock(h));
            if (ptr && sz > 0) {
                rawBytes.assign(ptr, ptr + sz);
                isPngDirect = true;
            }
            GlobalUnlock(h);
        }
    }

    // Priority 2: CF_DIBV5 — higher quality DIB with alpha + ICC profile
    if (rawBytes.empty() && IsClipboardFormatAvailable(CF_DIBV5)) {
        HANDLE h = GetClipboardData(CF_DIBV5);
        if (h) {
            SIZE_T sz = GlobalSize(h);
            auto* ptr = static_cast<const uint8_t*>(GlobalLock(h));
            if (ptr && sz >= sizeof(BITMAPV5HEADER)) {
                const auto* hdr = reinterpret_cast<const BITMAPV5HEADER*>(ptr);
                item.imageW = hdr->bV5Width;
                item.imageH = std::abs(hdr->bV5Height);
                rawBytes.assign(ptr, ptr + sz);
            }
            GlobalUnlock(h);
        }
    }

    // Priority 3: CF_DIB — standard DIB
    if (rawBytes.empty() && IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE h = GetClipboardData(CF_DIB);
        if (h) {
            SIZE_T sz = GlobalSize(h);
            auto* ptr = static_cast<const uint8_t*>(GlobalLock(h));
            if (ptr && sz >= sizeof(BITMAPINFOHEADER)) {
                const auto* hdr = reinterpret_cast<const BITMAPINFOHEADER*>(ptr);
                item.imageW = hdr->biWidth;
                item.imageH = std::abs(hdr->biHeight);
                rawBytes.assign(ptr, ptr + sz);
            }
            GlobalUnlock(h);
        }
    }

    // Priority 4: CF_BITMAP — legacy HBITMAP, convert to DIB
    if (rawBytes.empty() && IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP hbmp = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if (hbmp) {
            BITMAP bm{};
            GetObjectW(hbmp, sizeof(bm), &bm);
            item.imageW = bm.bmWidth;
            item.imageH = std::abs(bm.bmHeight);

            BITMAPINFOHEADER bi{};
            bi.biSize      = sizeof(bi);
            bi.biWidth     = bm.bmWidth;
            bi.biHeight    = bm.bmHeight;
            bi.biPlanes    = 1;
            bi.biBitCount  = 24;
            bi.biCompression = BI_RGB;
            const DWORD rowBytes = ((bm.bmWidth * 3 + 3) & ~3u);
            bi.biSizeImage = rowBytes * static_cast<DWORD>(std::abs(bm.bmHeight));

            rawBytes.resize(sizeof(bi) + bi.biSizeImage);
            std::memcpy(rawBytes.data(), &bi, sizeof(bi));

            HDC hdc = GetDC(nullptr);
            BITMAPINFO bmiGet{};
            bmiGet.bmiHeader = bi;
            GetDIBits(hdc, hbmp, 0,
                      static_cast<UINT>(std::abs(bm.bmHeight)),
                      rawBytes.data() + sizeof(bi),
                      &bmiGet, DIB_RGB_COLORS);
            ReleaseDC(nullptr, hdc);
        }
    }

    if (rawBytes.empty()) return;

    item.type = ContentType::Image;

    // If we got PNG dimensions from the PNG header, extract them
    if (isPngDirect && item.imageW == 0 && rawBytes.size() >= 24) {
        // PNG IHDR: bytes 16-19 = width, 20-23 = height (big-endian)
        item.imageW = (rawBytes[16] << 24) | (rawBytes[17] << 16)
                    | (rawBytes[18] << 8)  |  rawBytes[19];
        item.imageH = (rawBytes[20] << 24) | (rawBytes[21] << 16)
                    | (rawBytes[22] << 8)  |  rawBytes[23];
    }

    item.text = "[Image " + std::to_string(item.imageW)
              + "x"       + std::to_string(item.imageH) + "]";

    // Store into ImageStore if available
    if (m_imageStore) {
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        const std::string profileId = m_profileIdGetter ? m_profileIdGetter() : "default";
        const std::string proc = GetForegroundProcessName();

        item.imageStoreId = m_imageStore->StoreImage(rawBytes, isPngDirect,
                                                       profileId,
                                                       item.imageW, item.imageH,
                                                       proc, nowMs);
    }
}
