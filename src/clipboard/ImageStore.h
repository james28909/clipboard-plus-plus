#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_set>

struct sqlite3;
struct ImageSettings;

// Tag stored in the DB so GetPng/GetDibForPaste know how to decode each row.
enum class StoredFormat : int {
    RawDib = 0,  // exact DIB bytes from CF_DIB / CF_DIBV5 / CF_BITMAP
    Png    = 1,  // PNG-encoded (lossless)
    Jpeg   = 2   // JPEG-encoded (lossy)
};

struct ImageRecord {
    std::string  id;
    std::string  profileId;
    int          width{};
    int          height{};
    std::string  sourceProc;
    int64_t      capturedAt{};
    int64_t      byteSize{};
    StoredFormat storedFormat{StoredFormat::Png};
};

class ImageStore {
public:
    using ProtectedImageIdsProvider =
        std::function<std::unordered_set<std::string>()>;
    ImageStore();
    ~ImageStore();

    bool Open(const std::filesystem::path& dbPath);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    // Apply user settings. Call at startup and whenever settings change.
    void SetSettings(const ImageSettings& settings);
    void SetProtectedImageIdsProvider(ProtectedImageIdsProvider provider);

    // Unified capture entry point - applies current settings (format, scale-down,
    // min-size filter, max-count enforcement).
    // rawBytes: raw DIB bytes OR raw PNG bytes depending on isPng.
    // Returns assigned UUID, or empty string if capture is disabled / filtered out.
    std::string StoreImage(const std::vector<uint8_t>& rawBytes, bool isPng,
                           const std::string& profileId,
                           int width, int height,
                           const std::string& sourceProc,
                           int64_t capturedAtMs);

    // Retrieve PNG bytes for display (converts from stored format transparently).
    std::vector<uint8_t> GetPng(const std::string& id) const;
    bool GetStoredBytes(const std::string& id, std::vector<uint8_t>& bytes,
                        StoredFormat& format) const;

    // Build a CF_DIB HGLOBAL for SetClipboardData - handles all stored formats.
    // Caller owns the returned HGLOBAL; returns NULL on failure.
    HGLOBAL GetDibForPaste(const std::string& id) const;

    // Metadata only - no pixel data loaded.
    bool GetRecord(const std::string& id, ImageRecord& out) const;

    // All records for a profile, newest first.
    std::vector<ImageRecord> ListByProfile(const std::string& profileId) const;

    // All records across all profiles, newest first.
    std::vector<ImageRecord> ListAll() const;

    bool Delete(const std::string& id);
    void DeleteByProfile(const std::string& profileId);
    void DeleteAll();

    // Create a D3D11 thumbnail SRV for use with ImGui::Image().
    // maxSize: maximum pixel dimension (longest side is scaled to fit).
    // outW / outH: actual texture dimensions.
    // Returns nullptr on failure; caller owns and must Release() the SRV.
    ID3D11ShaderResourceView* CreateThumbnailSRV(const std::string& id,
                                                  ID3D11Device* device,
                                                  int maxSize,
                                                  int& outW, int& outH) const;

    // Static helper: PNG (or JPEG) bytes → CF_DIB HGLOBAL via GDI+.
    static HGLOBAL PngToDibGlobal(const std::vector<uint8_t>& imageBytes);

private:
    bool CreateSchema();
    bool MigrateImageProtection();
    bool ReadProtectedBlob(const std::string& id, std::vector<uint8_t>& data,
                           int* storedFormat = nullptr) const;
    void EnforceMaxImages(const std::string& newlyStoredId);
    std::string InsertBlob(const std::vector<uint8_t>& data, StoredFormat fmt,
                           const std::string& profileId,
                           int width, int height,
                           const std::string& sourceProc,
                           int64_t capturedAtMs);
    static std::string NewUuid();

    sqlite3* m_db{};

    // Cached settings (avoid pulling ConfigStore.h into this header)
    bool  m_captureImages{true};
    int   m_storedFormat{0};   // ImageFormat as int: 0=PNG, 1=JPEG, 2=Raw
    int   m_jpegQuality{85};
    bool  m_scaleDown{false};
    int   m_maxDimension{1920};
    bool  m_skipSmallImages{true};
    int   m_minWidth{32};
    int   m_minHeight{32};
    int   m_maxImages{0};
    ProtectedImageIdsProvider m_protectedImageIdsProvider;
};
