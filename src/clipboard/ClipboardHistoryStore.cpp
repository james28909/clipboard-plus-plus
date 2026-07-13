#include "ClipboardHistoryStore.h"
#include "../app/ConfigStore.h"
#include "../security/DpapiProtection.h"

#include <json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

int64_t TimeToMs(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point MsToTime(int64_t ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

std::string SafeProfileId(const std::string& id) {
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '-' || c == '_';
        out += ok ? c : '_';
    }
    return out.empty() ? "default" : out;
}

ContentType TypeFromJson(const json& j) {
    if (j.is_number_integer())
        return static_cast<ContentType>(j.get<int>());
    if (!j.is_string())
        return ContentType::Unknown;

    const std::string type = j.get<std::string>();
    if (type == "text") return ContentType::Text;
    if (type == "html") return ContentType::Html;
    if (type == "rich-text") return ContentType::RichText;
    if (type == "image") return ContentType::Image;
    if (type == "file-paths") return ContentType::FilePaths;
    return ContentType::Unknown;
}

json TimestampsToJson(const ClipboardItem& item) {
    json out = json::object();
    if (item.timestamp.time_since_epoch().count() != 0)
        out["captured"] = TimeToMs(item.timestamp);
    if (item.createdAt.time_since_epoch().count() != 0)
        out["created"] = TimeToMs(item.createdAt);
    if (item.updatedAt.time_since_epoch().count() != 0)
        out["updated"] = TimeToMs(item.updatedAt);
    if (item.lastUsedAt.time_since_epoch().count() != 0)
        out["lastUsed"] = TimeToMs(item.lastUsedAt);
    return out;
}

void LoadTimestamps(const json& j, ClipboardItem& item) {
    if (j.contains("timestamps") && j["timestamps"].is_object()) {
        const json& t = j["timestamps"];
        item.timestamp = MsToTime(t.value("captured", int64_t{}));
        item.createdAt = MsToTime(t.value("created", int64_t{}));
        item.updatedAt = MsToTime(t.value("updated", int64_t{}));
        item.lastUsedAt = MsToTime(t.value("lastUsed", int64_t{}));
        return;
    }

    item.timestamp = MsToTime(j.value("timestamp", int64_t{}));
    item.createdAt = MsToTime(j.value("createdAt", int64_t{}));
    item.updatedAt = MsToTime(j.value("updatedAt", int64_t{}));
    item.lastUsedAt = MsToTime(j.value("lastUsedAt", int64_t{}));
}

json ItemToJson(const ClipboardItem& item) {
    json out = {
        {"id", item.id},
        {"type", ContentTypeName(item.type)},
    };

    if (item.contentHash != 0)
        out["contentHash"] = item.contentHash;
    if (item.tags != TAG_NONE)
        out["tags"] = item.tags;
    if (!item.sourceProcess.empty())
        out["sourceProcess"] = item.sourceProcess;
    if (!item.sourceFilePath.empty())
        out["sourceFilePath"] = item.sourceFilePath;
    if (!item.sourceKind.empty())
        out["sourceKind"] = item.sourceKind;

    json timestamps = TimestampsToJson(item);
    if (!timestamps.empty())
        out["timestamps"] = std::move(timestamps);

    switch (item.type) {
    case ContentType::Image:
        out["width"]  = item.imageW;
        out["height"] = item.imageH;
        if (!item.imageStoreId.empty())
            out["imageStoreId"] = item.imageStoreId;
        if (!item.text.empty())
            out["description"] = item.text;
        break;
    case ContentType::FilePaths:
        out["pathsText"] = item.text;
        break;
    case ContentType::Text:
    case ContentType::Html:
    case ContentType::RichText:
        out["text"] = item.text;
        break;
    default:
        if (!item.text.empty())
            out["text"] = item.text;
        break;
    }

    return out;
}

ClipboardItem ItemFromJson(const json& j, bool pinnedSection) {
    ClipboardItem item;
    item.id = j.value("id", uint64_t{});
    item.contentHash = j.value("contentHash", uint64_t{});
    item.type = j.contains("type") ? TypeFromJson(j["type"]) : ContentType::Unknown;
    item.tags = j.value("tags", uint32_t{TAG_NONE});
    item.sourceProcess = j.value("sourceProcess", std::string{});
    item.sourceFilePath = j.value("sourceFilePath", std::string{});
    item.sourceKind = j.value("sourceKind", std::string{});
    LoadTimestamps(j, item);

    switch (item.type) {
    case ContentType::Image:
        item.imageW       = j.value("width",  j.value("imageW", 0));
        item.imageH       = j.value("height", j.value("imageH", 0));
        item.imageStoreId = j.value("imageStoreId", std::string{});
        item.text         = j.value("description", j.value("text", std::string{}));
        // Legacy: dibBase64 field is ignored — old inline images are dropped on first save
        break;
    case ContentType::FilePaths:
        item.text = j.value("pathsText", j.value("text", std::string{}));
        break;
    default:
        item.text = j.value("text", std::string{});
        break;
    }

    item.pinned = pinnedSection || j.value("pinned", false);
    item.EnsureContentHash();
    return item;
}

constexpr std::array<uint8_t, 8> kEncryptedMagic = {
    'C', 'P', 'P', 'H', 'I', 'S', 'T', '1'
};
constexpr uint32_t kEncryptedVersion = 1;

json HistoryToJson(const std::string& profileId,
                   const ClipboardHistory& history) {
    const std::vector<ClipboardItem> items = history.Snapshot();
    json pinnedHistory = json::array();
    json regularHistory = json::array();
    uint64_t nextId = history.NextId();
    for (const ClipboardItem& item : items) {
        nextId = std::max(nextId, item.id + 1);
        if (item.pinned)
            pinnedHistory.push_back(ItemToJson(item));
        else
            regularHistory.push_back(ItemToJson(item));
    }

    return {
        {"format", "clipboardpp-history"},
        {"version", 1},
        {"profileId", profileId},
        {"nextId", nextId},
        {"pinned-history", pinnedHistory},
        {"regular-history", regularHistory},
    };
}

bool JsonToHistory(const std::string& text, ClipboardHistory& history,
                   bool requireFormatMarker,
                   const std::string& expectedProfileId = {}) {
    try {
        json root = json::parse(text, nullptr, true, true);
        if (!root.is_object())
            return false;
        if (requireFormatMarker &&
            root.value("format", std::string{}) != "clipboardpp-history") {
            return false;
        }
        if (requireFormatMarker &&
            root.value("profileId", std::string{}) != expectedProfileId) {
            return false;
        }

        std::vector<ClipboardItem> items;
        uint64_t nextId = root.value("nextId", uint64_t{1});

        if (root.contains("pinned-history") && root["pinned-history"].is_array()) {
            for (const json& itemJson : root["pinned-history"]) {
                ClipboardItem item = ItemFromJson(itemJson, true);
                nextId = std::max(nextId, item.id + 1);
                items.push_back(std::move(item));
            }
        }

        if (root.contains("regular-history") && root["regular-history"].is_array()) {
            for (const json& itemJson : root["regular-history"]) {
                ClipboardItem item = ItemFromJson(itemJson, false);
                nextId = std::max(nextId, item.id + 1);
                items.push_back(std::move(item));
            }
        }

        if (items.empty() && root.contains("items") && root["items"].is_array()) {
            for (const json& itemJson : root["items"]) {
                ClipboardItem item = ItemFromJson(itemJson, itemJson.value("pinned", false));
                nextId = std::max(nextId, item.id + 1);
                items.push_back(std::move(item));
            }
        }

        history.LoadSnapshot(std::move(items), nextId);
        return true;
    } catch (...) {
        return false;
    }
}

void AppendU32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value));
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value >> 16));
    output.push_back(static_cast<uint8_t>(value >> 24));
}

bool ReadU32(const std::vector<uint8_t>& input, size_t offset, uint32_t& value) {
    if (offset > input.size() || input.size() - offset < 4)
        return false;
    value = static_cast<uint32_t>(input[offset]) |
            (static_cast<uint32_t>(input[offset + 1]) << 8) |
            (static_cast<uint32_t>(input[offset + 2]) << 16) |
            (static_cast<uint32_t>(input[offset + 3]) << 24);
    return true;
}

std::vector<uint8_t> BuildEnvelope(const std::vector<uint8_t>& protectedData) {
    if (protectedData.size() > UINT32_MAX)
        return {};
    std::vector<uint8_t> output;
    output.reserve(kEncryptedMagic.size() + 8 + protectedData.size());
    output.insert(output.end(), kEncryptedMagic.begin(), kEncryptedMagic.end());
    AppendU32(output, kEncryptedVersion);
    AppendU32(output, static_cast<uint32_t>(protectedData.size()));
    output.insert(output.end(), protectedData.begin(), protectedData.end());
    return output;
}

bool ParseEnvelope(const std::vector<uint8_t>& input,
                   std::vector<uint8_t>& protectedData) {
    constexpr size_t headerSize = 16;
    if (input.size() < headerSize ||
        !std::equal(kEncryptedMagic.begin(), kEncryptedMagic.end(), input.begin())) {
        return false;
    }
    uint32_t version{};
    uint32_t payloadSize{};
    if (!ReadU32(input, 8, version) || !ReadU32(input, 12, payloadSize) ||
        version != kEncryptedVersion || payloadSize != input.size() - headerSize) {
        return false;
    }
    protectedData.assign(input.begin() + headerSize, input.end());
    return true;
}

bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool WriteBytesAtomically(const std::filesystem::path& path,
                          const std::vector<uint8_t>& bytes) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return false;
    }
#else
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return false;
    }
#endif
    return true;
}

ClipboardHistoryStore::LoadResult LoadEncryptedFile(
    const std::filesystem::path& path, const std::string& profileId,
    ClipboardHistory& history) {
    std::vector<uint8_t> envelope;
    if (!ReadBytes(path, envelope))
        return ClipboardHistoryStore::LoadResult::IoError;

    std::vector<uint8_t> protectedData;
    if (!ParseEnvelope(envelope, protectedData))
        return ClipboardHistoryStore::LoadResult::InvalidFormat;

    std::vector<uint8_t> plaintext;
    if (!DpapiProtection::Unprotect(protectedData, plaintext))
        return ClipboardHistoryStore::LoadResult::DecryptionFailed;

    const std::string jsonText(plaintext.begin(), plaintext.end());
#ifdef _WIN32
    if (!plaintext.empty())
        SecureZeroMemory(plaintext.data(), plaintext.size());
#endif
    return JsonToHistory(jsonText, history, true, profileId)
        ? ClipboardHistoryStore::LoadResult::Loaded
        : ClipboardHistoryStore::LoadResult::InvalidFormat;
}

} // namespace

namespace ClipboardHistoryStore {

#ifdef CLIPBOARDPP_TESTING
namespace {
std::filesystem::path g_testDirectory;
}
#endif

std::filesystem::path Directory() {
#ifdef CLIPBOARDPP_TESTING
    return g_testDirectory;
#else
    return ConfigStore::Directory() / "history";
#endif
}

#ifdef CLIPBOARDPP_TESTING
void SetDirectoryForTesting(const std::filesystem::path& directory) {
    g_testDirectory = directory;
}
#endif

std::filesystem::path PathForProfile(const std::string& profileId) {
#ifdef _WIN32
    return Directory() / (SafeProfileId(profileId) + ".enc");
#else
    return LegacyPathForProfile(profileId);
#endif
}

std::filesystem::path LegacyPathForProfile(const std::string& profileId) {
    return Directory() / (SafeProfileId(profileId) + ".json");
}

LoadResult Load(const std::string& profileId, ClipboardHistory& history) {
#ifdef _WIN32
    const std::filesystem::path encryptedPath = PathForProfile(profileId);
    std::error_code existsError;
    const bool encryptedExists = std::filesystem::exists(encryptedPath, existsError);
    if (existsError)
        return LoadResult::IoError;
    if (encryptedExists) {
        const LoadResult result = LoadEncryptedFile(encryptedPath, profileId, history);
        if (result == LoadResult::Loaded) {
            std::error_code removeError;
            std::filesystem::remove(LegacyPathForProfile(profileId), removeError);
        }
        return result;
    }

    const std::filesystem::path legacyPath = LegacyPathForProfile(profileId);
    const bool legacyExists = std::filesystem::exists(legacyPath, existsError);
    if (existsError)
        return LoadResult::IoError;
    if (!legacyExists)
        return LoadResult::NotFound;

    std::vector<uint8_t> legacyBytes;
    if (!ReadBytes(legacyPath, legacyBytes))
        return LoadResult::IoError;
    const std::string legacyJson(legacyBytes.begin(), legacyBytes.end());
    if (!JsonToHistory(legacyJson, history, false))
        return LoadResult::InvalidFormat;

    if (!Save(profileId, history))
        return LoadResult::LoadedLegacy;

    ClipboardHistory verified;
    if (LoadEncryptedFile(encryptedPath, profileId, verified) != LoadResult::Loaded) {
        std::error_code removeError;
        std::filesystem::remove(encryptedPath, removeError);
        return LoadResult::LoadedLegacy;
    }

    std::error_code removeError;
    std::filesystem::remove(legacyPath, removeError);
    return LoadResult::Migrated;
#else
    const std::filesystem::path legacyPath = LegacyPathForProfile(profileId);
    std::vector<uint8_t> bytes;
    if (!ReadBytes(legacyPath, bytes))
        return std::filesystem::exists(legacyPath)
            ? LoadResult::IoError
            : LoadResult::NotFound;
    return JsonToHistory(std::string(bytes.begin(), bytes.end()), history, false)
        ? LoadResult::Loaded
        : LoadResult::InvalidFormat;
#endif
}

const char* LoadResultName(LoadResult result) {
    switch (result) {
    case LoadResult::Loaded:          return "loaded";
    case LoadResult::NotFound:        return "not found";
    case LoadResult::Migrated:        return "migrated";
    case LoadResult::LoadedLegacy:    return "loaded legacy";
    case LoadResult::DecryptionFailed:return "decryption failed";
    case LoadResult::InvalidFormat:   return "invalid format";
    case LoadResult::IoError:         return "I/O error";
    default:                          return "unknown";
    }
}

bool AllowsPersistence(LoadResult result) {
    return result == LoadResult::Loaded ||
           result == LoadResult::NotFound ||
           result == LoadResult::Migrated ||
           result == LoadResult::LoadedLegacy;
}

bool Save(const std::string& profileId, const ClipboardHistory& history) {
    std::error_code directoryError;
    std::filesystem::create_directories(Directory(), directoryError);
    if (directoryError)
        return false;

    const std::string jsonText = HistoryToJson(profileId, history).dump();
    std::vector<uint8_t> plaintext(jsonText.begin(), jsonText.end());

#ifdef _WIN32
    std::vector<uint8_t> protectedData;
    const bool protectedOk = DpapiProtection::Protect(plaintext, protectedData);
    if (!plaintext.empty())
        SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!protectedOk)
        return false;

    std::vector<uint8_t> envelope = BuildEnvelope(protectedData);
    if (envelope.empty())
        return false;
    return WriteBytesAtomically(PathForProfile(profileId), envelope);
#else
    return WriteBytesAtomically(LegacyPathForProfile(profileId), plaintext);
#endif
}
} // namespace ClipboardHistoryStore
