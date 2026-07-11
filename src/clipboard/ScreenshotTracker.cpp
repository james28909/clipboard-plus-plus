#include "ScreenshotTracker.h"
#include <windows.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include <ole2.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <utility>

#pragma comment(lib, "gdiplus.lib")

namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

class GdiplusScope {
public:
    GdiplusScope() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&m_token, &input, nullptr) != Gdiplus::Ok)
            m_token = 0;
    }
    ~GdiplusScope() {
        if (m_token)
            Gdiplus::GdiplusShutdown(m_token);
    }
    bool ok() const { return m_token != 0; }

private:
    ULONG_PTR m_token{};
};

GdiplusScope& Gdi() {
    static GdiplusScope scope;
    return scope;
}

void HashByte(uint64_t& hash, uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void HashBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        HashByte(hash, bytes[i]);
}

std::filesystem::path EnvPath(const wchar_t* name) {
    wchar_t buf[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(name, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};
    return std::filesystem::path(buf);
}

std::filesystem::path KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw) != S_OK || !raw)
        return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

std::string LowerExt(std::filesystem::path path) {
    std::string ext = path.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::unique_ptr<Gdiplus::Bitmap> BitmapFromBytes(const std::vector<uint8_t>& bytes) {
    if (!Gdi().ok() || bytes.empty())
        return nullptr;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!mem)
        return nullptr;

    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        return nullptr;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    GlobalUnlock(mem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(mem, TRUE, &stream) != S_OK) {
        GlobalFree(mem);
        return nullptr;
    }

    auto bmp = std::make_unique<Gdiplus::Bitmap>(stream);
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok)
        return nullptr;
    return bmp;
}

std::unique_ptr<Gdiplus::Bitmap> BitmapFromDib(const std::vector<uint8_t>& dib) {
    if (!Gdi().ok() || dib.size() < sizeof(BITMAPINFOHEADER))
        return nullptr;

    const auto* bmi = reinterpret_cast<const BITMAPINFO*>(dib.data());
    const BITMAPINFOHEADER& hdr = bmi->bmiHeader;

    DWORD colorTableSize = 0;
    if (hdr.biBitCount <= 8)
        colorTableSize = (hdr.biClrUsed ? hdr.biClrUsed : (1u << hdr.biBitCount)) * sizeof(RGBQUAD);
    else if (hdr.biCompression == BI_BITFIELDS)
        colorTableSize = 3 * sizeof(DWORD);

    const size_t pixelOffset = hdr.biSize + colorTableSize;
    if (pixelOffset >= dib.size())
        return nullptr;

    auto bmp = std::make_unique<Gdiplus::Bitmap>(bmi, const_cast<uint8_t*>(dib.data() + pixelOffset));
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok)
        return nullptr;
    return bmp;
}

uint64_t PixelHashBitmap(Gdiplus::Bitmap& bmp) {
    const UINT w = bmp.GetWidth();
    const UINT h = bmp.GetHeight();
    if (w == 0 || h == 0)
        return 0;

    Gdiplus::Rect rect(0, 0, static_cast<INT>(w), static_cast<INT>(h));
    Gdiplus::BitmapData bits{};
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bits) != Gdiplus::Ok)
        return 0;

    uint64_t hash = kFnvOffset;
    HashBytes(hash, &w, sizeof(w));
    HashBytes(hash, &h, sizeof(h));
    const auto* scan0 = static_cast<const uint8_t*>(bits.Scan0);
    const int stride = bits.Stride;
    const size_t absStride = static_cast<size_t>(std::abs(stride));
    for (UINT y = 0; y < h; ++y) {
        const size_t rowIndex = stride < 0 ? static_cast<size_t>(h - 1 - y) : static_cast<size_t>(y);
        const auto* row = scan0 + rowIndex * absStride;
        for (UINT x = 0; x < w; ++x) {
            const auto* px = row + x * 4;
            HashByte(hash, px[0]); // B
            HashByte(hash, px[1]); // G
            HashByte(hash, px[2]); // R
        }
    }

    bmp.UnlockBits(&bits);
    return hash ? hash : 1;
}

} // namespace

ScreenshotTracker& ScreenshotTracker::Instance() {
    static ScreenshotTracker tracker;
    return tracker;
}

ScreenshotTracker::ScreenshotTracker() {
    RefreshFolders();
}

void ScreenshotTracker::RefreshFolders() {
    m_folders.clear();

    const std::filesystem::path user = EnvPath(L"USERPROFILE");
    const std::filesystem::path pictures = KnownFolderPath(FOLDERID_Pictures);

    auto add = [&](std::filesystem::path path) {
        if (path.empty())
            return;
        std::error_code ec;
        path = std::filesystem::weakly_canonical(path, ec);
        if (ec || !std::filesystem::is_directory(path, ec))
            return;
        if (std::find(m_folders.begin(), m_folders.end(), path) == m_folders.end())
            m_folders.push_back(std::move(path));
    };
    auto addScreenshotFoldersUnder = [&](const std::filesystem::path& parent) {
        if (parent.empty())
            return;
        std::error_code ec;
        if (!std::filesystem::is_directory(parent, ec))
            return;

        for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
            if (ec || !entry.is_directory(ec))
                continue;
            std::string name = entry.path().filename().u8string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name.rfind("screenshots", 0) == 0)
                add(entry.path());
        }
    };

    add(user / "Pictures" / "Screenshots");
    add(user / "Pictures" / "Screenshots 1");
    add(pictures / "Screenshots");
    addScreenshotFoldersUnder(user / "Pictures");
    addScreenshotFoldersUnder(pictures);
    add(user / "Videos" / "Captures");
}

void ScreenshotTracker::NoteHotkey(const std::string& hint) {
    m_lastHint = hint;
    m_lastHotkeyTime = std::chrono::system_clock::now();
    RefreshFolders();
}

std::string ScreenshotTracker::LastHint() const {
    if (m_lastHotkeyTime.time_since_epoch().count() == 0)
        return {};
    if (std::chrono::system_clock::now() - m_lastHotkeyTime > std::chrono::seconds(10))
        return {};
    return m_lastHint;
}

bool ScreenshotTracker::IsCandidateImage(const std::filesystem::path& path) const {
    const std::string ext = LowerExt(path);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".webp";
}

uint64_t ScreenshotTracker::PixelHashFromImageBytes(const std::vector<uint8_t>& bytes,
                                                    bool isPngOrEncoded) {
    auto bmp = isPngOrEncoded ? BitmapFromBytes(bytes) : nullptr;
    if (!bmp)
        bmp = BitmapFromDib(bytes);
    return bmp ? PixelHashBitmap(*bmp) : 0;
}

std::filesystem::path ScreenshotTracker::FindRecentScreenshotFile(int imageW, int imageH,
                                                                  uint64_t pixelHash) {
    if (m_lastHotkeyTime.time_since_epoch().count() == 0)
        return {};
    if (pixelHash == 0)
        return {};

    if (std::chrono::system_clock::now() - m_lastHotkeyTime > std::chrono::seconds(10))
        return {};

    const auto now = std::chrono::system_clock::now();
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> candidates;

    for (const auto& folder : m_folders) {
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec))
            continue;

        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (ec || !entry.is_regular_file(ec) || !IsCandidateImage(entry.path()))
                continue;

            const auto write = entry.last_write_time(ec);
            if (ec)
                continue;

            const auto sysWrite = std::chrono::system_clock::now() +
                (write - std::filesystem::file_time_type::clock::now());
            if (sysWrite + std::chrono::seconds(2) < m_lastHotkeyTime ||
                now - sysWrite > std::chrono::seconds(12)) {
                continue;
            }

            candidates.emplace_back(write, entry.path());
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (candidates.size() > 6)
        candidates.resize(6);

    for (const auto& candidate : candidates) {
        Gdiplus::Bitmap probe(candidate.second.wstring().c_str());
        if (probe.GetLastStatus() != Gdiplus::Ok ||
            static_cast<int>(probe.GetWidth()) != imageW ||
            static_cast<int>(probe.GetHeight()) != imageH) {
            continue;
        }

        if (PixelHashBitmap(probe) == pixelHash)
            return candidate.second;
    }

    return {};
}
