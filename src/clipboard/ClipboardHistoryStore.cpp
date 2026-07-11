#include "ClipboardHistoryStore.h"
#include "../app/ConfigStore.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const uint32_t b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out += table[(triple >> 18) & 0x3f];
        out += table[(triple >> 12) & 0x3f];
        out += (i + 1 < bytes.size()) ? table[(triple >> 6) & 0x3f] : '=';
        out += (i + 2 < bytes.size()) ? table[triple & 0x3f] : '=';
    }

    return out;
}

int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<uint8_t> Base64Decode(const std::string& text) {
    std::vector<uint8_t> out;
    int value = 0;
    int bits = -8;

    for (char c : text) {
        if (c == '=') break;
        int v = Base64Value(c);
        if (v < 0) continue;
        value = (value << 6) | v;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
            bits -= 8;
        }
    }

    return out;
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

} // namespace

namespace ClipboardHistoryStore {

std::filesystem::path Directory() {
    return ConfigStore::Directory() / "history";
}

std::filesystem::path PathForProfile(const std::string& profileId) {
    return Directory() / (SafeProfileId(profileId) + ".json");
}

bool Load(const std::string& profileId, ClipboardHistory& history) {
    const std::filesystem::path path = PathForProfile(profileId);
    std::ifstream in(path);
    if (!in)
        return false;

    try {
        json root = json::parse(in, nullptr, true, true);
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

bool Save(const std::string& profileId, const ClipboardHistory& history) {
    std::error_code ec;
    std::filesystem::create_directories(Directory(), ec);
    if (ec)
        return false;

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

    json root = {
        {"version", 1},
        {"profileId", profileId},
        {"nextId", nextId},
        {"pinned-history", pinnedHistory},
        {"regular-history", regularHistory},
    };

    std::ofstream out(PathForProfile(profileId));
    if (!out)
        return false;

    out << root.dump(2);
    return true;
}

} // namespace ClipboardHistoryStore
