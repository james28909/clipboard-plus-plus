#include "ClipboardItem.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashByte(uint64_t& hash, uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void HashBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        HashByte(hash, bytes[i]);
}

void HashString(uint64_t& hash, const std::string& value, bool lowercase) {
    for (char c : value) {
        if (c == '\r')
            continue;
        unsigned char out = static_cast<unsigned char>(c == '\n' ? '\n' : c);
        if (lowercase)
            out = static_cast<unsigned char>(std::tolower(out));
        HashByte(hash, out);
    }
}

std::string TrimPath(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsImageExtension(const std::string& ext) {
    const std::string e = Lower(ext);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".gif" ||
           e == ".bmp" || e == ".webp" || e == ".ico" || e == ".tif" ||
           e == ".tiff" || e == ".svg";
}

std::string FileDisplayName(const std::string& path) {
    std::error_code ec;
    std::filesystem::path fs = std::filesystem::u8path(path);
    std::string name = fs.filename().u8string();
    if (name.empty())
        name = path;

    const bool isDir = std::filesystem::is_directory(fs, ec);
    if (isDir)
        return "[DIR] " + name;
    if (IsImageExtension(fs.extension().u8string()))
        return "[IMG] " + name;
    return "[FILE] " + name;
}

std::string FileListPreview(const std::string& text, size_t maxLen) {
    std::vector<std::string> names;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = TrimPath(line);
        if (!line.empty())
            names.push_back(FileDisplayName(line));
    }
    if (names.empty())
        return "[Files]";

    std::string out;
    if (names.size() > 1)
        out = "[Items " + std::to_string(names.size()) + "] ";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += ", ";
        out += names[i];
        if (out.size() >= maxLen) {
            out.resize(maxLen);
            out += "...";
            break;
        }
    }
    return out;
}

} // namespace

const char* ContentTypeName(ContentType type) {
    switch (type) {
    case ContentType::Text:      return "text";
    case ContentType::Html:      return "html";
    case ContentType::RichText:  return "rich-text";
    case ContentType::Image:     return "image";
    case ContentType::FilePaths: return "file-paths";
    default:                     return "unknown";
    }
}

std::string ClipboardItem::Preview(size_t maxLen) const {
    if (type == ContentType::Image)
        return text; // already "[Image WxH]"
    if (type == ContentType::FilePaths)
        return FileListPreview(text, maxLen);

    std::string out;
    out.reserve(maxLen + 4);

    for (char c : text) {
        if (out.size() >= maxLen) { out += "..."; break; }
        out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    return out;
}

uint64_t ClipboardItem::ComputeContentHash() const {
    uint64_t hash = kFnvOffset;
    const uint8_t typeByte = static_cast<uint8_t>(type);
    HashByte(hash, typeByte);

    if (type == ContentType::Image) {
        HashBytes(hash, &imageW, sizeof(imageW));
        HashBytes(hash, &imageH, sizeof(imageH));
        if (!imageData.empty())
            HashBytes(hash, imageData.data(), imageData.size());
    } else {
        const bool pathLike = type == ContentType::FilePaths || (tags & TAG_PATH) != 0;
        HashString(hash, text, pathLike);
    }

    return hash ? hash : 1;
}

void ClipboardItem::EnsureContentHash() {
    if (contentHash == 0)
        contentHash = ComputeContentHash();
}
