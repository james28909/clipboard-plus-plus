#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

enum class ContentType : uint8_t {
    Text,       // CF_UNICODETEXT
    Html,       // CF_HTML
    RichText,   // CF_RTF
    Image,      // CF_DIB
    FilePaths,  // CF_HDROP
    Unknown,
};

const char* ContentTypeName(ContentType type);

// Bitmask - multiple tags can be set on one item
enum ContentTag : uint32_t {
    TAG_NONE       = 0,
    TAG_URL        = 1 << 0,
    TAG_EMAIL      = 1 << 1,
    TAG_CODE       = 1 << 2,
    TAG_JSON       = 1 << 3,
    TAG_XML        = 1 << 4,
    TAG_SQL        = 1 << 5,
    TAG_HEX        = 1 << 6,
    TAG_PATH       = 1 << 7,
    TAG_PHONE      = 1 << 8,
    TAG_SECRET     = 1 << 9,
    TAG_FILE       = 1 << 10,
    TAG_FOLDER     = 1 << 11,
    TAG_IMAGE_FILE = 1 << 12,
    TAG_DOCUMENT   = 1 << 13,
    TAG_ARCHIVE    = 1 << 14,
    TAG_EXECUTABLE = 1 << 15,
    TAG_SCRIPT     = 1 << 16,
    TAG_CONFIG     = 1 << 17,
    TAG_DATA       = 1 << 18,
    TAG_AUDIO      = 1 << 19,
    TAG_VIDEO      = 1 << 20,
    TAG_MARKDOWN   = 1 << 21,
    TAG_CSV        = 1 << 22,
    TAG_HTML       = 1 << 23,
    TAG_UUID       = 1 << 24,
    TAG_IP         = 1 << 25,
    TAG_DATE       = 1 << 26,
    TAG_BASE64     = 1 << 27,
    TAG_COMMAND    = 1 << 28,
    TAG_LOG        = 1 << 29,
};

struct ClipboardItem {
    uint64_t    id{};
    uint64_t    contentHash{};
    ContentType type{ContentType::Unknown};
    uint32_t    tags{TAG_NONE};

    std::string text;               // UTF-8; "[Image WxH]" for images
    std::string imageStoreId;       // UUID in ImageStore DB (images only)
    std::string sourceFilePath;      // optional original/screenshot file path
    std::string sourceKind;          // optional hint, e.g. "screenshot"
    uint64_t sourcePixelHash{};      // transient normalized image hash for late screenshot file matching
    int imageW{};
    int imageH{};

    std::string sourceProcess;      // e.g. "chrome.exe"  (populated in dev mode)
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::chrono::system_clock::time_point lastUsedAt;
    bool pinned{false};

    bool IsText()  const { return type == ContentType::Text
                               || type == ContentType::Html
                               || type == ContentType::RichText; }
    bool IsImage() const { return type == ContentType::Image; }
    bool IsEmpty() const { return type == ContentType::Unknown; }

    // First maxLen visible characters, newlines -> spaces
    std::string Preview(size_t maxLen = 80) const;
    uint64_t ComputeContentHash() const;
    void EnsureContentHash();
};
