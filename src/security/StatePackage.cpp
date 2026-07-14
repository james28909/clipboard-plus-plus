#include "StatePackage.h"

#include "DpapiProtection.h"
#include "../../third_party/nlohmann/json.hpp"

#include <fstream>
#include <windows.h>

using json = nlohmann::json;

namespace state_package {
namespace {

constexpr const char* kFormat = "clipboardpp-state-package";
constexpr int kVersion = 1;
constexpr char kEncryptedHeader[] = "CPPSTATE-DPAPI-1\n";
constexpr const char* kPendingConfig = "config-import-pending.dpapi";

json ProfileJson(const ClipboardProfileConfig& value) {
    return {{"id", value.id}, {"name", value.name},
            {"createdAt", value.createdAt}, {"updatedAt", value.updatedAt},
            {"processName", value.processName}};
}

json SlotJson(const NamedClipboardSlot& value) {
    return {{"id", value.slotId}, {"name", value.name}, {"text", value.text},
            {"createdAt", value.createdAtMs}, {"updatedAt", value.updatedAtMs}};
}

json TransformJson(const RegexTransformDefinition& value) {
    return {{"id", value.transformId}, {"name", value.name},
            {"pattern", value.pattern}, {"replacement", value.replacement},
            {"caseSensitive", value.caseSensitive}, {"multiline", value.multiline},
            {"dotMatchesNewline", value.dotMatchesNewline},
            {"replaceAll", value.replaceAll}, {"createdAt", value.createdAtMs},
            {"updatedAt", value.updatedAtMs}};
}

json TemplateJson(const PasteTemplateDefinition& value) {
    return {{"id", value.templateId}, {"name", value.name}, {"body", value.body},
            {"createdAt", value.createdAtMs}, {"updatedAt", value.updatedAtMs}};
}

bool ParsePayload(const std::vector<uint8_t>& bytes, Data& data,
                  std::string& error) {
    try {
        const json root = json::parse(bytes.begin(), bytes.end());
        if (root.value("format", std::string{}) != kFormat ||
            root.value("version", 0) != kVersion) {
            error = "This is not a supported Clipboard++ state package.";
            return false;
        }
        data = {};
        if (root.contains("configuration"))
            data.configurationJson = root["configuration"].dump(2);
        for (const auto& item : root.value("profiles", json::array())) {
            ClipboardProfileConfig value;
            value.id = item.value("id", ""); value.name = item.value("name", "");
            value.createdAt = item.value("createdAt", "");
            value.updatedAt = item.value("updatedAt", "");
            value.processName = item.value("processName", "");
            if (!value.name.empty()) data.profiles.push_back(std::move(value));
        }
        for (const auto& item : root.value("namedSlots", json::array())) {
            NamedClipboardSlot value;
            value.slotId = item.value("id", int64_t{}); value.name = item.value("name", "");
            value.text = item.value("text", ""); value.createdAtMs = item.value("createdAt", int64_t{});
            value.updatedAtMs = item.value("updatedAt", int64_t{});
            if (!value.name.empty()) data.namedSlots.push_back(std::move(value));
        }
        for (const auto& item : root.value("transforms", json::array())) {
            RegexTransformDefinition value;
            value.transformId = item.value("id", int64_t{}); value.name = item.value("name", "");
            value.pattern = item.value("pattern", ""); value.replacement = item.value("replacement", "");
            value.caseSensitive = item.value("caseSensitive", true); value.multiline = item.value("multiline", false);
            value.dotMatchesNewline = item.value("dotMatchesNewline", false); value.replaceAll = item.value("replaceAll", true);
            value.createdAtMs = item.value("createdAt", int64_t{}); value.updatedAtMs = item.value("updatedAt", int64_t{});
            if (!value.name.empty()) data.transforms.push_back(std::move(value));
        }
        for (const auto& item : root.value("templates", json::array())) {
            PasteTemplateDefinition value;
            value.templateId = item.value("id", int64_t{}); value.name = item.value("name", "");
            value.body = item.value("body", ""); value.createdAtMs = item.value("createdAt", int64_t{});
            value.updatedAtMs = item.value("updatedAt", int64_t{});
            if (!value.name.empty()) data.templates.push_back(std::move(value));
        }
        for (const auto& item : root.value("actions", json::array())) {
            CustomActionDefinition value;
            std::string actionError;
            if (!DeserializeCustomAction(item.dump(), value, &actionError)) {
                error = "A workflow action is invalid: " + actionError;
                return false;
            }
            data.actions.push_back(std::move(value));
        }
        return true;
    } catch (const std::exception& ex) {
        error = std::string("State package is invalid: ") + ex.what();
        return false;
    }
}

bool WriteBytes(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || (!bytes.empty() && !output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))) {
        error = "Could not write the state package.";
        return false;
    }
    return true;
}

bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes,
               std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "Could not open the state package."; return false; }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) { error = "Could not finish reading the state package."; return false; }
    return true;
}

} // namespace

Result Write(const std::filesystem::path& path, const Data& data,
             bool encrypted) {
    Result result; result.encrypted = encrypted;
    json root = {{"format", kFormat}, {"version", kVersion}};
    if (!data.configurationJson.empty()) {
        try { root["configuration"] = json::parse(data.configurationJson); }
        catch (...) { result.message = "The current configuration is invalid."; return result; }
    }
    root["profiles"] = json::array();
    for (const auto& value : data.profiles) root["profiles"].push_back(ProfileJson(value));
    root["namedSlots"] = json::array();
    for (const auto& value : data.namedSlots) root["namedSlots"].push_back(SlotJson(value));
    root["transforms"] = json::array();
    for (const auto& value : data.transforms) root["transforms"].push_back(TransformJson(value));
    root["templates"] = json::array();
    for (const auto& value : data.templates) root["templates"].push_back(TemplateJson(value));
    root["actions"] = json::array();
    for (const auto& value : data.actions)
        root["actions"].push_back(json::parse(SerializeCustomAction(value)));
    const std::string payload = root.dump(encrypted ? -1 : 2);
    std::vector<uint8_t> bytes(payload.begin(), payload.end());
    std::string error;
    if (encrypted) {
        std::vector<uint8_t> protectedData;
        uint32_t win32Error = 0;
        if (!DpapiProtection::Protect(bytes, protectedData, &win32Error)) {
            result.message = "Windows DPAPI could not encrypt the package (error " +
                             std::to_string(win32Error) + ").";
            return result;
        }
        SecureZeroMemory(bytes.data(), bytes.size());
        bytes.assign(std::begin(kEncryptedHeader), std::end(kEncryptedHeader) - 1);
        bytes.insert(bytes.end(), protectedData.begin(), protectedData.end());
    }
    result.ok = WriteBytes(path, bytes, error);
    result.message = result.ok ? (encrypted ? "Encrypted state package exported."
                                           : "Plaintext state package exported.") : error;
    return result;
}

Result Read(const std::filesystem::path& path, Data& data) {
    Result result;
    std::vector<uint8_t> bytes;
    if (!ReadBytes(path, bytes, result.message)) return result;
    const size_t headerSize = sizeof(kEncryptedHeader) - 1;
    if (bytes.size() >= headerSize && std::equal(
            bytes.begin(), bytes.begin() + headerSize, kEncryptedHeader)) {
        result.encrypted = true;
        std::vector<uint8_t> protectedData(bytes.begin() + headerSize, bytes.end());
        uint32_t win32Error = 0;
        if (!DpapiProtection::Unprotect(protectedData, bytes, &win32Error)) {
            result.message = "Windows DPAPI could not decrypt the package (error " +
                             std::to_string(win32Error) + ").";
            return result;
        }
    }
    result.ok = ParsePayload(bytes, data, result.message);
    SecureZeroMemory(bytes.data(), bytes.size());
    if (result.ok) result.message = result.encrypted
        ? "Encrypted state package loaded." : "Plaintext state package loaded.";
    return result;
}

bool StageConfigurationImport(const std::filesystem::path& dataDirectory,
                              const std::string& configurationJson,
                              std::string* error) {
    try { const json parsed = json::parse(configurationJson); (void)parsed; }
    catch (const std::exception& ex) { if (error) *error = ex.what(); return false; }
    std::vector<uint8_t> plaintext(configurationJson.begin(), configurationJson.end());
    std::vector<uint8_t> protectedData;
    uint32_t win32Error = 0;
    if (!DpapiProtection::Protect(plaintext, protectedData, &win32Error)) {
        if (error) *error = "DPAPI error " + std::to_string(win32Error);
        return false;
    }
    SecureZeroMemory(plaintext.data(), plaintext.size());
    std::error_code ec; std::filesystem::create_directories(dataDirectory, ec);
    std::string writeError;
    const bool ok = WriteBytes(dataDirectory / kPendingConfig, protectedData, writeError);
    if (!ok && error) *error = writeError;
    return ok;
}

bool ApplyPendingConfigurationImport(const std::filesystem::path& dataDirectory,
                                     std::string* error) {
    const auto pending = dataDirectory / kPendingConfig;
    if (!std::filesystem::exists(pending)) return true;
    std::vector<uint8_t> protectedData, plaintext;
    std::string readError;
    if (!ReadBytes(pending, protectedData, readError)) { if (error) *error = readError; return false; }
    uint32_t win32Error = 0;
    if (!DpapiProtection::Unprotect(protectedData, plaintext, &win32Error)) {
        if (error) *error = "DPAPI error " + std::to_string(win32Error); return false;
    }
    try { const json parsed = json::parse(plaintext.begin(), plaintext.end()); (void)parsed; }
    catch (const std::exception& ex) { if (error) *error = ex.what(); return false; }
    const auto temporary = dataDirectory / "config-import-applying.tmp";
    std::string writeError;
    if (!WriteBytes(temporary, plaintext, writeError)) {
        SecureZeroMemory(plaintext.data(), plaintext.size());
        if (error) *error = writeError; return false;
    }
    SecureZeroMemory(plaintext.data(), plaintext.size());
    const auto destination = dataDirectory / "config.json";
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = "Could not atomically install imported settings (error " +
                            std::to_string(GetLastError()) + ").";
        std::error_code ec; std::filesystem::remove(temporary, ec);
        return false;
    }
    std::error_code ec; std::filesystem::remove(pending, ec);
    return true;
}

bool HasPendingConfigurationImport(const std::filesystem::path& dataDirectory) {
    std::error_code ec;
    return std::filesystem::exists(dataDirectory / kPendingConfig, ec);
}

} // namespace state_package
