#include "ImageStore.h"
#include "../app/ConfigStore.h"
#include "../../third_party/sqlite/sqlite3.h"

#include <windows.h>
#include <d3d11.h>
#include <objidl.h>
#include <gdiplus.h>
#include <ole2.h>
#include <combaseapi.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

// ---------------------------------------------------------------------------
// GDI+ lifetime — reference-counted so multiple ImageStore instances are safe
// ---------------------------------------------------------------------------
namespace {

ULONG_PTR g_gdiplusToken    = 0;
int       g_gdiplusRefCount = 0;

void GdiplusAddRef() {
    if (++g_gdiplusRefCount == 1) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);
    }
}

void GdiplusRelease() {
    if (--g_gdiplusRefCount == 0 && g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

// ---------------------------------------------------------------------------
// Encoder helpers
// ---------------------------------------------------------------------------

bool GetEncoderClsid(const wchar_t* mimeType, CLSID& clsid) {
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (!size) return false;

    std::vector<uint8_t> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(count, size, codecs);

    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(codecs[i].MimeType, mimeType) == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// GDI+ Bitmap from in-memory image bytes (PNG, JPEG, or any GDI+-supported format)
// ---------------------------------------------------------------------------

std::unique_ptr<Gdiplus::Bitmap> BitmapFromBytes(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;

    HGLOBAL hIn = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hIn) return nullptr;
    void* ptr = GlobalLock(hIn);
    if (!ptr) { GlobalFree(hIn); return nullptr; }
    std::memcpy(ptr, bytes.data(), bytes.size());
    GlobalUnlock(hIn);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hIn, TRUE, &stream) != S_OK) {
        GlobalFree(hIn);
        return nullptr;
    }

    auto bmp = std::make_unique<Gdiplus::Bitmap>(stream);
    stream->Release();

    if (bmp->GetLastStatus() != Gdiplus::Ok) return nullptr;
    return bmp;
}

// ---------------------------------------------------------------------------
// GDI+ Bitmap from raw DIB bytes
// ---------------------------------------------------------------------------

std::unique_ptr<Gdiplus::Bitmap> BitmapFromDib(const std::vector<uint8_t>& dib) {
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return nullptr;

    const auto* bmi = reinterpret_cast<const BITMAPINFO*>(dib.data());
    const BITMAPINFOHEADER& hdr = bmi->bmiHeader;

    DWORD colorTableSize = 0;
    if (hdr.biBitCount <= 8)
        colorTableSize = (hdr.biClrUsed ? hdr.biClrUsed : (1u << hdr.biBitCount)) * sizeof(RGBQUAD);
    else if (hdr.biCompression == BI_BITFIELDS)
        colorTableSize = 3 * sizeof(DWORD);

    const size_t pixelOffset = hdr.biSize + colorTableSize;
    if (pixelOffset >= dib.size()) return nullptr;

    const void* pixelBits = dib.data() + pixelOffset;
    auto bmp = std::make_unique<Gdiplus::Bitmap>(bmi, const_cast<void*>(pixelBits));
    if (bmp->GetLastStatus() != Gdiplus::Ok) return nullptr;
    return bmp;
}

// ---------------------------------------------------------------------------
// Proportional scale — returns new unique_ptr if scaling needed, null if not.
// ---------------------------------------------------------------------------

std::unique_ptr<Gdiplus::Bitmap> ScaleIfNeeded(Gdiplus::Bitmap& src, bool doScale, int maxDim) {
    if (!doScale || maxDim <= 0) return nullptr;

    const UINT w = src.GetWidth();
    const UINT h = src.GetHeight();
    if (w == 0 || h == 0) return nullptr;
    if (static_cast<int>(w) <= maxDim && static_cast<int>(h) <= maxDim) return nullptr;

    const float scale = static_cast<float>(maxDim) / static_cast<float>(std::max(w, h));
    const UINT nw = std::max(1u, static_cast<UINT>(w * scale));
    const UINT nh = std::max(1u, static_cast<UINT>(h * scale));

    auto scaled = std::make_unique<Gdiplus::Bitmap>(nw, nh, PixelFormat32bppARGB);
    if (!scaled || scaled->GetLastStatus() != Gdiplus::Ok) return nullptr;

    Gdiplus::Graphics g(scaled.get());
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.DrawImage(&src, 0, 0, static_cast<INT>(nw), static_cast<INT>(nh));

    return scaled;
}

// ---------------------------------------------------------------------------
// Encode a GDI+ Bitmap to PNG bytes
// ---------------------------------------------------------------------------

std::vector<uint8_t> EncodePng(Gdiplus::Bitmap& bmp, bool scaleDown, int maxDim) {
    Gdiplus::Bitmap* target = &bmp;
    auto scaledOwner = ScaleIfNeeded(bmp, scaleDown, maxDim);
    if (scaledOwner) target = scaledOwner.get();

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return {};

    CLSID clsid{};
    if (!GetEncoderClsid(L"image/png", clsid)) { stream->Release(); return {}; }

    if (target->Save(stream, &clsid, nullptr) != Gdiplus::Ok) { stream->Release(); return {}; }

    HGLOBAL hg = nullptr;
    GetHGlobalFromStream(stream, &hg);
    const SIZE_T sz = GlobalSize(hg);
    void* ptr = GlobalLock(hg);

    std::vector<uint8_t> result;
    if (ptr && sz > 0) {
        result.resize(sz);
        std::memcpy(result.data(), ptr, sz);
    }
    GlobalUnlock(hg);
    stream->Release();
    return result;
}

// ---------------------------------------------------------------------------
// Encode a GDI+ Bitmap to JPEG bytes
// ---------------------------------------------------------------------------

std::vector<uint8_t> EncodeJpeg(Gdiplus::Bitmap& bmp, int quality, bool scaleDown, int maxDim) {
    Gdiplus::Bitmap* target = &bmp;
    auto scaledOwner = ScaleIfNeeded(bmp, scaleDown, maxDim);
    if (scaledOwner) target = scaledOwner.get();

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return {};

    CLSID clsid{};
    if (!GetEncoderClsid(L"image/jpeg", clsid)) { stream->Release(); return {}; }

    ULONG q = static_cast<ULONG>(std::clamp(quality, 1, 100));
    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid           = Gdiplus::EncoderQuality;
    params.Parameter[0].Type           = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value          = &q;

    if (target->Save(stream, &clsid, &params) != Gdiplus::Ok) { stream->Release(); return {}; }

    HGLOBAL hg = nullptr;
    GetHGlobalFromStream(stream, &hg);
    const SIZE_T sz = GlobalSize(hg);
    void* ptr = GlobalLock(hg);

    std::vector<uint8_t> result;
    if (ptr && sz > 0) {
        result.resize(sz);
        std::memcpy(result.data(), ptr, sz);
    }
    GlobalUnlock(hg);
    stream->Release();
    return result;
}

// ---------------------------------------------------------------------------
// Build CF_DIB HGLOBAL from a GDI+ Bitmap
// ---------------------------------------------------------------------------

HGLOBAL BitmapToDibGlobal(Gdiplus::Bitmap& bmp) {
    const UINT w = bmp.GetWidth();
    const UINT h = bmp.GetHeight();
    if (!w || !h) return nullptr;

    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = static_cast<LONG>(w);
    bi.biHeight      = -static_cast<LONG>(h); // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;
    bi.biCompression = BI_RGB;
    const DWORD rowBytes = ((w * 3 + 3) & ~3u);
    bi.biSizeImage   = rowBytes * h;

    const SIZE_T totalSize = sizeof(BITMAPINFOHEADER) + bi.biSizeImage;
    HGLOBAL hOut = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hOut) return nullptr;

    auto* buf = static_cast<uint8_t*>(GlobalLock(hOut));
    if (!buf) { GlobalFree(hOut); return nullptr; }
    std::memcpy(buf, &bi, sizeof(bi));

    HDC hdcScreen = GetDC(nullptr);
    HBITMAP hbmp = nullptr;
    bmp.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hbmp);

    if (hbmp) {
        BITMAPINFO bmiGet{};
        bmiGet.bmiHeader       = bi;
        bmiGet.bmiHeader.biHeight = static_cast<LONG>(h); // bottom-up for GetDIBits
        GetDIBits(hdcScreen, hbmp, 0, h, buf + sizeof(bi), &bmiGet, DIB_RGB_COLORS);
        DeleteObject(hbmp);
        reinterpret_cast<BITMAPINFOHEADER*>(buf)->biHeight = -static_cast<LONG>(h);
    } else {
        GlobalUnlock(hOut);
        GlobalFree(hOut);
        ReleaseDC(nullptr, hdcScreen);
        return nullptr;
    }

    ReleaseDC(nullptr, hdcScreen);
    GlobalUnlock(hOut);
    return hOut;
}

} // namespace

// ---------------------------------------------------------------------------
// ImageStore
// ---------------------------------------------------------------------------

ImageStore::ImageStore() {
    GdiplusAddRef();
}

ImageStore::~ImageStore() {
    Close();
    GdiplusRelease();
}

bool ImageStore::Open(const std::filesystem::path& dbPath) {
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);

    const std::string pathUtf8 = dbPath.u8string();
    if (sqlite3_open_v2(pathUtf8.c_str(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                        SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        m_db = nullptr;
        return false;
    }

    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;",      nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;",    nullptr, nullptr, nullptr);

    return CreateSchema();
}

void ImageStore::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool ImageStore::CreateSchema() {
    const char* create =
        "CREATE TABLE IF NOT EXISTS images ("
        "  id           TEXT    PRIMARY KEY,"
        "  profile_id   TEXT    NOT NULL,"
        "  width        INTEGER NOT NULL,"
        "  height       INTEGER NOT NULL,"
        "  source_proc  TEXT    NOT NULL DEFAULT '',"
        "  captured_at  INTEGER NOT NULL,"
        "  byte_size    INTEGER NOT NULL,"
        "  stored_format INTEGER NOT NULL DEFAULT 1,"
        "  data         BLOB    NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_images_profile  ON images(profile_id);"
        "CREATE INDEX IF NOT EXISTS idx_images_captured ON images(captured_at DESC);";

    if (sqlite3_exec(m_db, create, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    // Migrate older DBs that lack the stored_format column.
    // SQLite does not support ALTER TABLE ADD COLUMN IF NOT EXISTS — just attempt the add
    // and ignore the error if the column already exists ("duplicate column name").
    sqlite3_exec(m_db,
        "ALTER TABLE images ADD COLUMN stored_format INTEGER NOT NULL DEFAULT 1;",
        nullptr, nullptr, nullptr);

    return true;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void ImageStore::SetSettings(const ImageSettings& s) {
    m_captureImages   = s.captureImages;
    m_storedFormat    = static_cast<int>(s.format);
    m_jpegQuality     = s.jpegQuality;
    m_scaleDown       = s.scaleDown;
    m_maxDimension    = s.maxDimension;
    m_skipSmallImages = s.skipSmallImages;
    m_minWidth        = s.minWidth;
    m_minHeight       = s.minHeight;
    m_maxImages       = s.maxImages;
}

// ---------------------------------------------------------------------------
// StoreImage — unified capture entry point
// ---------------------------------------------------------------------------

std::string ImageStore::StoreImage(const std::vector<uint8_t>& rawBytes, bool isPng,
                                    const std::string& profileId,
                                    int width, int height,
                                    const std::string& sourceProc,
                                    int64_t capturedAtMs) {
    if (!m_db || rawBytes.empty()) return {};
    if (!m_captureImages) return {};
    if (m_skipSmallImages && width > 0 && height > 0 &&
        (width < m_minWidth || height < m_minHeight)) return {};

    // Load a GDI+ Bitmap from either PNG/file-format bytes or raw DIB bytes.
    // Tries BitmapFromBytes first (handles PNG, JPEG, BMP file, GIF, TIFF, WebP),
    // then falls back to BitmapFromDib (raw clipboard DIB).
    auto loadBitmap = [&]() -> std::unique_ptr<Gdiplus::Bitmap> {
        if (isPng) return BitmapFromBytes(rawBytes);
        auto bmp = BitmapFromBytes(rawBytes);  // handles JPEG, BMP file, GIF, TIFF…
        if (bmp) return bmp;
        return BitmapFromDib(rawBytes);
    };

    // Fill in width/height from PNG header if not provided
    auto resolveDims = [&](int& w, int& h, Gdiplus::Bitmap* bmp) {
        if (w == 0 && h == 0) {
            if (isPng && rawBytes.size() >= 24) {
                w = (rawBytes[16] << 24) | (rawBytes[17] << 16) |
                    (rawBytes[18] <<  8) |  rawBytes[19];
                h = (rawBytes[20] << 24) | (rawBytes[21] << 16) |
                    (rawBytes[22] <<  8) |  rawBytes[23];
            } else if (bmp) {
                w = static_cast<int>(bmp->GetWidth());
                h = static_cast<int>(bmp->GetHeight());
            }
        }
    };

    int w = width, h = height;
    std::string id;

    switch (static_cast<ImageFormat>(m_storedFormat)) {

    case ImageFormat::Raw: {
        // Detect stored format from magic bytes for non-PNG inputs
        StoredFormat sf = StoredFormat::RawDib;
        if (isPng) {
            sf = StoredFormat::Png;
        } else if (rawBytes.size() >= 3 &&
                   rawBytes[0] == 0xFF && rawBytes[1] == 0xD8 && rawBytes[2] == 0xFF) {
            sf = StoredFormat::Jpeg;
        } else if (rawBytes.size() >= 8 &&
                   rawBytes[0] == 0x89 && rawBytes[1] == 0x50 &&
                   rawBytes[2] == 0x4E && rawBytes[3] == 0x47) {
            sf = StoredFormat::Png;
        }
        if (w == 0 || h == 0) {
            auto bmp = loadBitmap();
            if (bmp) { w = static_cast<int>(bmp->GetWidth()); h = static_cast<int>(bmp->GetHeight()); }
        }
        id = InsertBlob(rawBytes, sf, profileId, w, h, sourceProc, capturedAtMs);
        break;
    }

    case ImageFormat::PNG: {
        if (isPng && !m_scaleDown) {
            if (w == 0 && rawBytes.size() >= 24) {
                w = (rawBytes[16] << 24) | (rawBytes[17] << 16) | (rawBytes[18] << 8) | rawBytes[19];
                h = (rawBytes[20] << 24) | (rawBytes[21] << 16) | (rawBytes[22] << 8) | rawBytes[23];
            }
            id = InsertBlob(rawBytes, StoredFormat::Png, profileId, w, h, sourceProc, capturedAtMs);
        } else {
            auto bmp = loadBitmap();
            if (!bmp) break;
            resolveDims(w, h, bmp.get());
            const auto out = EncodePng(*bmp, m_scaleDown, m_maxDimension);
            id = InsertBlob(out.empty() ? rawBytes : out,
                            StoredFormat::Png, profileId, w, h, sourceProc, capturedAtMs);
        }
        break;
    }

    case ImageFormat::JPEG: {
        auto bmp = loadBitmap();
        if (!bmp) break;
        resolveDims(w, h, bmp.get());
        const auto out = EncodeJpeg(*bmp, m_jpegQuality, m_scaleDown, m_maxDimension);
        if (out.empty()) break;
        id = InsertBlob(out, StoredFormat::Jpeg, profileId, w, h, sourceProc, capturedAtMs);
        break;
    }
    }

    if (!id.empty())
        EnforceMaxImages();
    return id;
}

// ---------------------------------------------------------------------------
// Insert helper
// ---------------------------------------------------------------------------

std::string ImageStore::InsertBlob(const std::vector<uint8_t>& data, StoredFormat fmt,
                                    const std::string& profileId,
                                    int width, int height,
                                    const std::string& sourceProc,
                                    int64_t capturedAtMs) {
    if (!m_db || data.empty()) return {};

    const std::string id = NewUuid();
    const char* sql =
        "INSERT INTO images(id,profile_id,width,height,source_proc,captured_at,byte_size,stored_format,data)"
        " VALUES(?,?,?,?,?,?,?,?,?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

    sqlite3_bind_text (stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, profileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int  (stmt, 3, width);
    sqlite3_bind_int  (stmt, 4, height);
    sqlite3_bind_text (stmt, 5, sourceProc.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, capturedAtMs);
    sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(data.size()));
    sqlite3_bind_int  (stmt, 8, static_cast<int>(fmt));
    sqlite3_bind_blob (stmt, 9, data.data(), static_cast<int>(data.size()), SQLITE_STATIC);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok ? id : std::string{};
}

// ---------------------------------------------------------------------------
// Retrieval
// ---------------------------------------------------------------------------

std::vector<uint8_t> ImageStore::GetPng(const std::string& id) const {
    if (!m_db) return {};

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT data, stored_format FROM images WHERE id=?;",
            -1, &stmt, nullptr) != SQLITE_OK)
        return {};

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    std::vector<uint8_t> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int   sz   = sqlite3_column_bytes(stmt, 0);
        const int   fmt  = sqlite3_column_int(stmt, 1);

        if (blob && sz > 0) {
            std::vector<uint8_t> data(static_cast<size_t>(sz));
            std::memcpy(data.data(), blob, data.size());

            if (fmt == static_cast<int>(StoredFormat::Png)) {
                result = std::move(data);
            } else if (fmt == static_cast<int>(StoredFormat::RawDib)) {
                auto bmp = BitmapFromDib(data);
                if (bmp) result = EncodePng(*bmp, false, 0);
            } else { // JPEG or unknown
                auto bmp = BitmapFromBytes(data);
                if (bmp) result = EncodePng(*bmp, false, 0);
            }
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

HGLOBAL ImageStore::GetDibForPaste(const std::string& id) const {
    if (!m_db) return nullptr;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT data, stored_format FROM images WHERE id=?;",
            -1, &stmt, nullptr) != SQLITE_OK)
        return nullptr;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    HGLOBAL result = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int   sz   = sqlite3_column_bytes(stmt, 0);
        const int   fmt  = sqlite3_column_int(stmt, 1);

        if (blob && sz > 0) {
            std::vector<uint8_t> data(static_cast<size_t>(sz));
            std::memcpy(data.data(), blob, data.size());

            if (fmt == static_cast<int>(StoredFormat::RawDib)) {
                // Return raw DIB bytes directly as HGLOBAL — no conversion needed
                result = GlobalAlloc(GMEM_MOVEABLE, data.size());
                if (result) {
                    void* ptr = GlobalLock(result);
                    if (ptr) {
                        std::memcpy(ptr, data.data(), data.size());
                        GlobalUnlock(result);
                    }
                }
            } else {
                // PNG or JPEG — load via GDI+ and convert to DIB
                auto bmp = BitmapFromBytes(data);
                if (bmp) result = BitmapToDibGlobal(*bmp);
            }
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// Static helper — kept for backward compat with any callers
// ---------------------------------------------------------------------------

HGLOBAL ImageStore::PngToDibGlobal(const std::vector<uint8_t>& imageBytes) {
    auto bmp = BitmapFromBytes(imageBytes);
    if (!bmp) return nullptr;
    return BitmapToDibGlobal(*bmp);
}

// ---------------------------------------------------------------------------
// Metadata queries
// ---------------------------------------------------------------------------

bool ImageStore::GetRecord(const std::string& id, ImageRecord& out) const {
    if (!m_db) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT id,profile_id,width,height,source_proc,captured_at,byte_size,stored_format"
            " FROM images WHERE id=?;",
            -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto col = [&](int i) -> const char* {
            auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            return p ? p : "";
        };
        out.id           = col(0);
        out.profileId    = col(1);
        out.width        = sqlite3_column_int(stmt, 2);
        out.height       = sqlite3_column_int(stmt, 3);
        out.sourceProc   = col(4);
        out.capturedAt   = sqlite3_column_int64(stmt, 5);
        out.byteSize     = sqlite3_column_int64(stmt, 6);
        out.storedFormat = static_cast<StoredFormat>(sqlite3_column_int(stmt, 7));
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

static std::vector<ImageRecord> RunListQuery(sqlite3* db, const char* sql,
                                              const char* profileId = nullptr) {
    std::vector<ImageRecord> result;
    if (!db) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    if (profileId)
        sqlite3_bind_text(stmt, 1, profileId, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto col = [&](int i) -> const char* {
            auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            return p ? p : "";
        };
        ImageRecord r;
        r.id           = col(0);
        r.profileId    = col(1);
        r.width        = sqlite3_column_int(stmt, 2);
        r.height       = sqlite3_column_int(stmt, 3);
        r.sourceProc   = col(4);
        r.capturedAt   = sqlite3_column_int64(stmt, 5);
        r.byteSize     = sqlite3_column_int64(stmt, 6);
        r.storedFormat = static_cast<StoredFormat>(sqlite3_column_int(stmt, 7));
        result.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<ImageRecord> ImageStore::ListByProfile(const std::string& profileId) const {
    return RunListQuery(m_db,
        "SELECT id,profile_id,width,height,source_proc,captured_at,byte_size,stored_format"
        " FROM images WHERE profile_id=? ORDER BY captured_at DESC;",
        profileId.c_str());
}

std::vector<ImageRecord> ImageStore::ListAll() const {
    return RunListQuery(m_db,
        "SELECT id,profile_id,width,height,source_proc,captured_at,byte_size,stored_format"
        " FROM images ORDER BY captured_at DESC;");
}

// ---------------------------------------------------------------------------
// Deletion
// ---------------------------------------------------------------------------

bool ImageStore::Delete(const std::string& id) {
    if (!m_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM images WHERE id=?;",
                            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

void ImageStore::DeleteByProfile(const std::string& profileId) {
    if (!m_db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM images WHERE profile_id=?;",
                            -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, profileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void ImageStore::DeleteAll() {
    if (!m_db) return;
    sqlite3_exec(m_db, "DELETE FROM images;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "VACUUM;",            nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Max-images enforcement — purges oldest rows when limit is exceeded
// ---------------------------------------------------------------------------

void ImageStore::EnforceMaxImages() {
    if (!m_db || m_maxImages <= 0) return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM images;",
                            -1, &stmt, nullptr) != SQLITE_OK) return;

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    if (count <= m_maxImages) return;

    const int64_t toDelete = count - m_maxImages;
    const std::string sql =
        "DELETE FROM images WHERE id IN"
        " (SELECT id FROM images ORDER BY captured_at ASC LIMIT " +
        std::to_string(toDelete) + ");";
    sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Thumbnail SRV for ImGui rendering
// ---------------------------------------------------------------------------

ID3D11ShaderResourceView* ImageStore::CreateThumbnailSRV(const std::string& id,
                                                          ID3D11Device* device,
                                                          int maxSize,
                                                          int& outW, int& outH) const {
    outW = outH = 0;
    if (!device || !m_db) return nullptr;

    // Fetch stored data + format
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT data, stored_format FROM images WHERE id=?;",
            -1, &stmt, nullptr) != SQLITE_OK)
        return nullptr;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    std::vector<uint8_t> data;
    int storedFmt = static_cast<int>(StoredFormat::Png);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int   sz   = sqlite3_column_bytes(stmt, 0);
        storedFmt        = sqlite3_column_int(stmt, 1);
        if (blob && sz > 0) {
            data.resize(static_cast<size_t>(sz));
            std::memcpy(data.data(), blob, data.size());
        }
    }
    sqlite3_finalize(stmt);
    if (data.empty()) return nullptr;

    // Load into GDI+ Bitmap
    std::unique_ptr<Gdiplus::Bitmap> bmp;
    if (storedFmt == static_cast<int>(StoredFormat::RawDib))
        bmp = BitmapFromDib(data);
    else
        bmp = BitmapFromBytes(data);
    if (!bmp) return nullptr;

    // Scale to thumbnail size
    const UINT srcW = bmp->GetWidth();
    const UINT srcH = bmp->GetHeight();
    if (!srcW || !srcH) return nullptr;

    const float scale = static_cast<float>(maxSize) / static_cast<float>(std::max(srcW, srcH));
    const UINT tw = std::max(1u, static_cast<UINT>(srcW * std::min(scale, 1.0f)));
    const UINT th = std::max(1u, static_cast<UINT>(srcH * std::min(scale, 1.0f)));

    auto scaled = std::make_unique<Gdiplus::Bitmap>(tw, th, PixelFormat32bppARGB);
    if (!scaled || scaled->GetLastStatus() != Gdiplus::Ok) return nullptr;
    {
        Gdiplus::Graphics g(scaled.get());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(bmp.get(), 0, 0, static_cast<INT>(tw), static_cast<INT>(th));
    }

    // Lock pixel data (GDI+ PixelFormat32bppARGB is BGRA in memory)
    Gdiplus::BitmapData bmpData{};
    Gdiplus::Rect rect(0, 0, static_cast<INT>(tw), static_cast<INT>(th));
    if (scaled->LockBits(&rect, Gdiplus::ImageLockModeRead,
                          PixelFormat32bppARGB, &bmpData) != Gdiplus::Ok)
        return nullptr;

    // Create immutable D3D11 texture (BGRA matches PixelFormat32bppARGB)
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = tw;
    desc.Height           = th;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem          = bmpData.Scan0;
    init.SysMemPitch      = static_cast<UINT>(bmpData.Stride);

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &init, &tex);
    scaled->UnlockBits(&bmpData);

    if (FAILED(hr) || !tex) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();

    outW = static_cast<int>(tw);
    outH = static_cast<int>(th);
    return srv;
}

// ---------------------------------------------------------------------------
// UUID
// ---------------------------------------------------------------------------

std::string ImageStore::NewUuid() {
    GUID guid{};
    CoCreateGuid(&guid);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << guid.Data1 << '-'
       << std::setw(4) << guid.Data2 << '-'
       << std::setw(4) << guid.Data3 << '-'
       << std::setw(2) << static_cast<int>(guid.Data4[0])
       << std::setw(2) << static_cast<int>(guid.Data4[1]) << '-'
       << std::setw(2) << static_cast<int>(guid.Data4[2])
       << std::setw(2) << static_cast<int>(guid.Data4[3])
       << std::setw(2) << static_cast<int>(guid.Data4[4])
       << std::setw(2) << static_cast<int>(guid.Data4[5])
       << std::setw(2) << static_cast<int>(guid.Data4[6])
       << std::setw(2) << static_cast<int>(guid.Data4[7]);
    return ss.str();
}
