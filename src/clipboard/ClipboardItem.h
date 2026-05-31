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

// Bitmask — multiple tags can be set on one item
enum ContentTag : uint32_t {
    TAG_NONE    = 0,
    TAG_URL     = 1 << 0,
    TAG_EMAIL   = 1 << 1,
    TAG_CODE    = 1 << 2,
    TAG_JSON    = 1 << 3,
    TAG_XML     = 1 << 4,
    TAG_SQL     = 1 << 5,
    TAG_HEX     = 1 << 6,
    TAG_PATH    = 1 << 7,
    TAG_PHONE   = 1 << 8,
    TAG_SECRET  = 1 << 9,
};

struct ClipboardItem {
    uint64_t    id{};
    ContentType type{ContentType::Unknown};
    uint32_t    tags{TAG_NONE};

    std::string text;               // UTF-8; "[Image WxH]" for images
    std::vector<uint8_t> imageData; // raw DIB bytes
    int imageW{};
    int imageH{};

    std::string sourceProcess;      // e.g. "chrome.exe"  (populated in dev mode)
    std::chrono::system_clock::time_point timestamp;
    bool pinned{false};

    bool HasTag(ContentTag t) const { return (tags & t) != 0; }
    bool IsText()  const { return type == ContentType::Text
                               || type == ContentType::Html
                               || type == ContentType::RichText; }
    bool IsImage() const { return type == ContentType::Image; }
    bool IsEmpty() const { return type == ContentType::Unknown; }

    // First maxLen visible characters, newlines → spaces
    std::string Preview(size_t maxLen = 80) const;
};
