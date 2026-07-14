#include "ClipboardMonitor.h"
#include "ContentDetector.h"
#include "ImageStore.h"
#include "ScreenshotTracker.h"
#include "../util/Win32Util.h"
#include <shellapi.h>   // DragQueryFileW
#include <cctype>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

static constexpr wchar_t kMonitorClass[] = L"CPPClipboardMonitor";
static constexpr int kOpenClipboardAttempts = 12;
static constexpr DWORD kOpenClipboardRetryMs = 8;

namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint64_t kMaxPreservedFormatBytes = 16ull * 1024ull * 1024ull;
constexpr uint64_t kMaxPreservedBundleBytes = 32ull * 1024ull * 1024ull;

std::string ClipboardFormatName(UINT format) {
    switch (format) {
    case CF_TEXT: return "CF_TEXT";
    case CF_BITMAP: return "CF_BITMAP";
    case CF_METAFILEPICT: return "CF_METAFILEPICT";
    case CF_SYLK: return "CF_SYLK";
    case CF_DIF: return "CF_DIF";
    case CF_TIFF: return "CF_TIFF";
    case CF_OEMTEXT: return "CF_OEMTEXT";
    case CF_DIB: return "CF_DIB";
    case CF_PALETTE: return "CF_PALETTE";
    case CF_PENDATA: return "CF_PENDATA";
    case CF_RIFF: return "CF_RIFF";
    case CF_WAVE: return "CF_WAVE";
    case CF_UNICODETEXT: return "CF_UNICODETEXT";
    case CF_ENHMETAFILE: return "CF_ENHMETAFILE";
    case CF_HDROP: return "CF_HDROP";
    case CF_LOCALE: return "CF_LOCALE";
    case CF_DIBV5: return "CF_DIBV5";
    case CF_OWNERDISPLAY: return "CF_OWNERDISPLAY";
    case CF_DSPTEXT: return "CF_DSPTEXT";
    case CF_DSPBITMAP: return "CF_DSPBITMAP";
    case CF_DSPMETAFILEPICT: return "CF_DSPMETAFILEPICT";
    case CF_DSPENHMETAFILE: return "CF_DSPENHMETAFILE";
    default: break;
    }

    if (format >= 0xC000) {
        wchar_t name[256]{};
        const int length = GetClipboardFormatNameW(format, name, 256);
        if (length > 0)
            return win32util::WideToUtf8(name, length);
    }
    if (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST)
        return "CF_PRIVATE+" + std::to_string(format - CF_PRIVATEFIRST);
    if (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST)
        return "CF_GDIOBJ+" + std::to_string(format - CF_GDIOBJFIRST);
    return "Format " + std::to_string(format);
}

bool EqualsFormatName(const std::string& value, const char* expected) {
    if (value.size() != std::strlen(expected))
        return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(expected[i])))
            return false;
    }
    return true;
}

bool IsSafeHGlobalFormat(UINT format, const std::string& name) {
    if (format == CF_UNICODETEXT || format == CF_DIB || format == CF_DIBV5 ||
        format == CF_HDROP || format == CF_LOCALE)
        return true;
    return EqualsFormatName(name, "HTML Format") ||
           EqualsFormatName(name, "Rich Text Format") ||
           EqualsFormatName(name, "PNG") ||
           EqualsFormatName(name, "JFIF");
}

void CaptureFormatBundle(ClipboardItem& item) {
    uint64_t preservedBytes = 0;
    UINT format = 0;
    uint32_t order = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        ClipboardFormatRecord record;
        record.formatId = format;
        record.name = ClipboardFormatName(format);
        record.order = order++;
        record.replaySafe = IsSafeHGlobalFormat(format, record.name);
        record.status = ClipboardFormatStatus::MetadataOnly;

        if (record.replaySafe) {
            HANDLE handle = GetClipboardData(format);
            if (!handle) {
                record.status = ClipboardFormatStatus::ReadFailed;
            } else {
                const SIZE_T size = GlobalSize(handle);
                record.byteSize = static_cast<uint64_t>(size);
                if (size == 0) {
                    record.status = ClipboardFormatStatus::ReadFailed;
                } else if (record.byteSize > kMaxPreservedFormatBytes ||
                           preservedBytes > kMaxPreservedBundleBytes -
                                                std::min(record.byteSize, kMaxPreservedBundleBytes)) {
                    record.status = ClipboardFormatStatus::TooLarge;
                } else {
                    const auto* bytes = static_cast<const uint8_t*>(GlobalLock(handle));
                    if (!bytes) {
                        record.status = ClipboardFormatStatus::ReadFailed;
                    } else {
                        record.data.assign(bytes, bytes + size);
                        GlobalUnlock(handle);
                        record.status = ClipboardFormatStatus::Preserved;
                        preservedBytes += record.byteSize;
                    }
                }
            }
        }

        item.formats.push_back(std::move(record));
    }
}

uint64_t StableImageHash(const std::vector<uint8_t>& bytes, int width, int height, bool pngDirect) {
    uint64_t hash = kFnvOffset;
    auto hashByte = [&](uint8_t value) {
        hash ^= value;
        hash *= kFnvPrime;
    };
    auto hashBytes = [&](const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
            hashByte(p[i]);
    };

    const uint8_t type = pngDirect ? 1 : 2;
    hashByte(type);
    hashBytes(&width, sizeof(width));
    hashBytes(&height, sizeof(height));
    hashBytes(bytes.data(), bytes.size());
    return hash ? hash : 1;
}

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

bool IsImageExtension(const std::wstring& path) {
    auto dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext == L"png"  || ext == L"jpg"  || ext == L"jpeg" ||
           ext == L"bmp"  || ext == L"gif"  || ext == L"tiff" ||
           ext == L"tif"  || ext == L"webp";
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
    m_suppressNextBaseline.store(GetClipboardSequenceNumber());
    m_suppressNextArmed.store(true);
}

void ClipboardMonitor::BeginSelfWrite() {
    if (m_selfWriteDepth.fetch_add(1) == 0) {
        static std::atomic<uint64_t> nextToken{1};
        m_selfWriteStartSeq.store(GetClipboardSequenceNumber());
        m_selfWriteToken.store(nextToken.fetch_add(1));
    }
}

void ClipboardMonitor::MarkSelfWrite() {
    if (m_selfWriteDepth.load() <= 0)
        return;

    const UINT format = RegisterClipboardFormatW(L"Clipboard++ Self Write Token v1");
    if (!format)
        return;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(uint64_t));
    if (!memory)
        return;
    void* data = GlobalLock(memory);
    if (!data) {
        GlobalFree(memory);
        return;
    }
    *static_cast<uint64_t*>(data) = m_selfWriteToken.load();
    GlobalUnlock(memory);
    if (!SetClipboardData(format, memory))
        GlobalFree(memory);
}

void ClipboardMonitor::EndSelfWrite() {
    const int previousDepth = m_selfWriteDepth.fetch_sub(1);
    if (previousDepth <= 0) {
        m_selfWriteDepth.store(0);
        return;
    }
    if (previousDepth != 1)
        return;

    const DWORD finalSeq = GetClipboardSequenceNumber();
    if (finalSeq != m_selfWriteStartSeq.load())
        m_suppressedSelfSeq.store(finalSeq);
}

void ClipboardMonitor::SetCaptureEnabled(bool enabled) {
    m_captureEnabled = enabled;
    m_lastSeq = GetClipboardSequenceNumber();
    m_suppressNextArmed.store(false);
    m_suppressedSelfSeq.store(0);
    m_selfWriteDepth.store(0);
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

    if (!m_captureEnabled)
        return;

    if (m_selfWriteDepth.load() > 0)
        return;

    // A token survives additional sequence changes caused by delayed or
    // incrementally rendered formats, unlike a one-notification flag.
    if (ClipboardHasCurrentSelfWriteToken())
        return;

    if (seq == m_suppressedSelfSeq.exchange(0))
        return;

    if (m_suppressNextArmed.load() &&
        seq != m_suppressNextBaseline.load() &&
        m_suppressNextArmed.exchange(false)) {
        return;
    }

    ClipboardItem item = ReadClipboard();
    if (item.IsEmpty()) return;

    // Image-capture debounce: Windows can fire WM_CLIPBOARDUPDATE more than once
    // for one screenshot as delayed-render formats settle. Use a stable byte hash,
    // not dimensions alone, so same-size screenshots are still captured correctly.
    if (item.type == ContentType::Image) {
        const ULONGLONG now = GetTickCount64();
        if (item.contentHash != 0 &&
            item.contentHash == m_lastImgHash &&
            now - m_lastImgTickMs < 3000 &&
            item.imageW == m_lastImgW && item.imageH == m_lastImgH) {
            if (m_imageStore && !item.imageStoreId.empty())
                m_imageStore->Delete(item.imageStoreId);
            return;
        }
        m_lastImgHash   = item.contentHash;
        m_lastImgW      = item.imageW;
        m_lastImgH      = item.imageH;
        m_lastImgTickMs = now;
    }

    if (m_callback)
        m_callback(std::move(item));
}

bool ClipboardMonitor::ClipboardHasCurrentSelfWriteToken() const {
    const uint64_t expected = m_selfWriteToken.load();
    if (expected == 0)
        return false;
    const UINT format = RegisterClipboardFormatW(L"Clipboard++ Self Write Token v1");
    if (!format || !IsClipboardFormatAvailable(format) || !OpenClipboard(m_hwnd))
        return false;

    bool matches = false;
    if (HANDLE handle = GetClipboardData(format)) {
        if (const void* data = GlobalLock(handle)) {
            matches = *static_cast<const uint64_t*>(data) == expected;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return matches;
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

    // -- File drop: single image file → treat as image; otherwise file paths ---
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE h = GetClipboardData(CF_HDROP);
        if (h) {
            HDROP hDrop = static_cast<HDROP>(h);
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            bool handledAsImage = false;
            if (count == 1 && m_imageStore) {
                const UINT len = DragQueryFileW(hDrop, 0, nullptr, 0);
                if (len > 0) {
                    std::vector<wchar_t> buf(static_cast<size_t>(len) + 1);
                    DragQueryFileW(hDrop, 0, buf.data(), static_cast<UINT>(buf.size()));
                    std::wstring wpath(buf.data());
                    if (IsImageExtension(wpath)) {
                        std::ifstream ifs(wpath, std::ios::binary);
                        if (ifs) {
                            std::vector<uint8_t> fileBytes(
                                (std::istreambuf_iterator<char>(ifs)), {});
                            if (!fileBytes.empty()) {
                                // Detect PNG by magic bytes
                                const bool isPng = fileBytes.size() >= 8 &&
                                    fileBytes[0] == 0x89 && fileBytes[1] == 0x50 &&
                                    fileBytes[2] == 0x4E && fileBytes[3] == 0x47;

                                const int64_t nowMs = std::chrono::duration_cast<
                                    std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                const std::string profileId =
                                    m_profileIdGetter ? m_profileIdGetter() : "default";
                                const std::string proc = GetForegroundProcessName();

                                item.imageStoreId = m_imageStore->StoreImage(
                                    fileBytes, isPng, profileId,
                                    0, 0, proc, nowMs);

                                if (!item.imageStoreId.empty()) {
                                    // Retrieve the dimensions stored
                                    ImageRecord rec;
                                    m_imageStore->GetRecord(item.imageStoreId, rec);
                                    item.imageW = rec.width;
                                    item.imageH = rec.height;
                                    item.contentHash = StableImageHash(fileBytes, item.imageW, item.imageH, isPng);
                                    item.sourceKind = "image-file";
                                    item.sourceFilePath = std::filesystem::path(wpath).u8string();
                                    std::string filename = std::filesystem::path(wpath).filename().u8string();
                                    if (filename.empty())
                                        filename = "Image";
                                    item.text   = "[IMG] " + filename + " "
                                                + std::to_string(item.imageW)
                                                + "x" + std::to_string(item.imageH);
                                    item.type   = ContentType::Image;
                                    handledAsImage = true;
                                }
                            }
                        }
                    }
                }
            }
            if (!handledAsImage) {
                item.text = FileDropPathsUtf8(hDrop);
                item.type = ContentType::FilePaths;
            }
        }
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
    CaptureFormatBundle(item);
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

    // Priority 1: PNG clipboard format - already perfect, no conversion needed
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

    // Priority 2: CF_DIBV5 - higher quality DIB with alpha + ICC profile
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

    // Priority 3: CF_DIB - standard DIB
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

    // Priority 4: CF_BITMAP - legacy HBITMAP, convert to DIB
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
    item.contentHash = StableImageHash(rawBytes, item.imageW, item.imageH, isPngDirect);
    item.sourceKind = "image";

    ScreenshotTracker& screenshots = ScreenshotTracker::Instance();
    const std::string screenshotHint = screenshots.LastHint();
    if (!screenshotHint.empty()) {
        item.sourceKind = "screenshot";
        const uint64_t pixelHash =
            ScreenshotTracker::PixelHashFromImageBytes(rawBytes, isPngDirect);
        item.sourcePixelHash = pixelHash;
        const std::filesystem::path screenshotPath =
            screenshots.FindRecentScreenshotFile(item.imageW, item.imageH, pixelHash);
        if (!screenshotPath.empty()) {
            item.sourceFilePath = screenshotPath.u8string();
            item.text = "[Screenshot] " + screenshotPath.filename().u8string() + " "
                      + std::to_string(item.imageW) + "x" + std::to_string(item.imageH)
                      + "\n" + item.sourceFilePath;
        } else {
            item.text = "[Screenshot] " + screenshotHint + " "
                      + std::to_string(item.imageW) + "x" + std::to_string(item.imageH);
        }
    }

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
