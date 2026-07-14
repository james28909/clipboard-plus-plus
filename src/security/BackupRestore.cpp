#include "BackupRestore.h"

#include "DpapiProtection.h"
#include "EncryptedSqliteVfs.h"
#include "../../third_party/nlohmann/json.hpp"
#include "../../third_party/sqlite/sqlite3.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <windows.h>

using json = nlohmann::json;

namespace backup_restore {
namespace {

constexpr int kFormatVersion = 1;
constexpr const char* kManifestName = "backup-manifest.json";
constexpr const char* kPendingDirectory = "restore-pending";
constexpr const char* kPendingTemporary = "restore-pending.tmp";
constexpr const char* kRollbackDirectory = "restore-rollback";
constexpr const char* kLastResult = "restore-last-result.json";
constexpr const char* kProtectedConfigName = "config.json.dpapi";

std::string Timestamp() {
    const std::time_t value = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d-%H%M%S");
    return out.str();
}

std::filesystem::path UniqueDirectory(const std::filesystem::path& parent,
                                      const std::string& prefix) {
    std::error_code ec;
    std::filesystem::path candidate = parent / (prefix + Timestamp());
    for (int suffix = 2; std::filesystem::exists(candidate, ec); ++suffix)
        candidate = parent / (prefix + Timestamp() + "-" + std::to_string(suffix));
    return candidate;
}

bool WriteJson(const std::filesystem::path& path, const json& value,
               std::string* error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "Could not write " + path.filename().string();
        return false;
    }
    output << value.dump(2);
    if (!output.good()) {
        if (error) *error = "Could not finish writing " + path.filename().string();
        return false;
    }
    return true;
}

bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes,
               std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) *error = "Could not read " + path.filename().string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    if (input.bad()) {
        if (error) *error = "Could not finish reading " + path.filename().string();
        return false;
    }
    return true;
}

bool WriteBytes(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes, std::string* error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || (!bytes.empty() &&
        !output.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size())))) {
        if (error) *error = "Could not write " + path.filename().string();
        return false;
    }
    return true;
}

bool ProtectConfig(const std::filesystem::path& source,
                   const std::filesystem::path& destination,
                   std::string* error) {
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> protectedData;
    if (!ReadBytes(source, plaintext, error)) return false;
    try {
        const json parsed = json::parse(plaintext.begin(), plaintext.end());
        (void)parsed;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Configuration is invalid: ") + ex.what();
        return false;
    }
    uint32_t win32Error = 0;
    if (!DpapiProtection::Protect(plaintext, protectedData, &win32Error)) {
        if (error) *error = "Windows DPAPI could not protect the configuration (error " +
                            std::to_string(win32Error) + ").";
        return false;
    }
    SecureZeroMemory(plaintext.data(), plaintext.size());
    return WriteBytes(destination, protectedData, error);
}

bool UnprotectConfig(const std::filesystem::path& source,
                     std::vector<uint8_t>& plaintext, std::string* error) {
    std::vector<uint8_t> protectedData;
    if (!ReadBytes(source, protectedData, error)) return false;
    uint32_t win32Error = 0;
    if (!DpapiProtection::Unprotect(protectedData, plaintext, &win32Error)) {
        if (error) *error = "Windows DPAPI could not unlock the backup configuration (error " +
                            std::to_string(win32Error) + ").";
        return false;
    }
    try {
        const json parsed = json::parse(plaintext.begin(), plaintext.end());
        (void)parsed;
    } catch (const std::exception& ex) {
        SecureZeroMemory(plaintext.data(), plaintext.size());
        plaintext.clear();
        if (error) *error = std::string("Backup configuration is invalid: ") + ex.what();
        return false;
    }
    return true;
}

bool ReprotectConfig(const std::filesystem::path& source,
                     const std::filesystem::path& destination,
                     std::string* error) {
    std::vector<uint8_t> plaintext;
    if (!UnprotectConfig(source, plaintext, error)) return false;
    std::vector<uint8_t> protectedData;
    uint32_t win32Error = 0;
    const bool protectedOk = DpapiProtection::Protect(
        plaintext, protectedData, &win32Error);
    SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!protectedOk) {
        if (error) *error = "Windows DPAPI could not re-protect the configuration (error " +
                            std::to_string(win32Error) + ").";
        return false;
    }
    return WriteBytes(destination, protectedData, error);
}

bool ReadManifest(const std::filesystem::path& directory, json& manifest,
                  std::string* error) {
    try {
        std::ifstream input(directory / kManifestName, std::ios::binary);
        if (!input) {
            if (error) *error = "The selected folder has no Clipboard++ backup manifest.";
            return false;
        }
        input >> manifest;
        if (manifest.value("format", std::string{}) != "clipboardpp-encrypted-backup" ||
            manifest.value("version", 0) != kFormatVersion ||
            !manifest.value("clipboardDatabase", false)) {
            if (error) *error = "The selected folder is not a supported Clipboard++ encrypted backup.";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Backup manifest is invalid: ") + ex.what();
        return false;
    }
}

bool HasTable(sqlite3* database, const char* table) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(statement, 1, table, -1, SQLITE_STATIC);
    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

bool ValidateDatabase(const std::filesystem::path& path, const char* table,
                      std::string* error) {
    sqlite3* database = nullptr;
    if (EncryptedSqliteVfs::Open(path, &database,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, error) != SQLITE_OK)
        return false;
    sqlite3_stmt* statement = nullptr;
    bool integrity = sqlite3_prepare_v2(database, "PRAGMA integrity_check;", -1,
                                        &statement, nullptr) == SQLITE_OK &&
                     sqlite3_step(statement) == SQLITE_ROW &&
                     std::string(reinterpret_cast<const char*>(
                         sqlite3_column_text(statement, 0))) == "ok";
    sqlite3_finalize(statement);
    const bool schema = integrity && HasTable(database, table);
    sqlite3_close(database);
    if (!schema && error)
        *error = integrity ? "Backup database has the wrong schema."
                           : "Backup database failed its integrity check.";
    return schema;
}

bool ValidateBackupDirectory(const std::filesystem::path& directory,
                             bool& imagesIncluded, bool& configIncluded,
                             std::string* error) {
    json manifest;
    if (!ReadManifest(directory, manifest, error)) return false;
    imagesIncluded = manifest.value("imageDatabase", false);
    configIncluded = manifest.value("configuration", false);
    if (!ValidateDatabase(directory / "clipboard.db", "profiles", error))
        return false;
    if (imagesIncluded &&
        !ValidateDatabase(directory / "images.db", "images", error))
        return false;
    if (configIncluded) {
        std::vector<uint8_t> plaintext;
        if (!UnprotectConfig(directory / kProtectedConfigName, plaintext, error))
            return false;
        SecureZeroMemory(plaintext.data(), plaintext.size());
    }
    return true;
}

json Manifest(bool imagesIncluded, bool configIncluded) {
    return {
        {"format", "clipboardpp-encrypted-backup"},
        {"version", kFormatVersion},
        {"createdLocal", Timestamp()},
        {"clipboardDatabase", true},
        {"imageDatabase", imagesIncluded},
        {"configuration", configIncluded},
        {"encrypted", true},
        {"keyProtection", "Windows DPAPI current user"},
        {"containsDecryptedClipboardContent", false},
    };
}

std::vector<std::filesystem::path> Components(const std::string& name) {
    return {name, name + ".key", name + "-wal", name + "-shm",
            name + "-journal"};
}

bool MoveExisting(const std::filesystem::path& from,
                  const std::filesystem::path& to, std::string* error) {
    std::error_code ec;
    if (!std::filesystem::exists(from, ec)) return true;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::filesystem::rename(from, to, ec);
    if (ec) {
        if (error) *error = "Could not move " + from.filename().string() +
                            ": " + ec.message();
        return false;
    }
    return true;
}

void WriteLastResult(const std::filesystem::path& dataDirectory,
                     const Result& result) {
    std::string ignored;
    WriteJson(dataDirectory / kLastResult, {
        {"ok", result.ok}, {"message", result.message},
        {"rollbackPath", result.rollbackPath.u8string()},
        {"recordedLocal", Timestamp()},
    }, &ignored);
}

} // namespace

Result CreateEncryptedBackup(const std::filesystem::path& dataDirectory,
                             const std::filesystem::path& destinationParent) {
    Result result;
    std::error_code ec;
    if (destinationParent.empty() ||
        !std::filesystem::is_directory(destinationParent, ec)) {
        result.message = "Choose an existing destination folder.";
        return result;
    }
    result.path = UniqueDirectory(destinationParent, "Clipboard++ Backup ");
    std::filesystem::create_directories(result.path, ec);
    if (ec) {
        result.message = "Could not create the backup folder: " + ec.message();
        return result;
    }
    std::string error;
    if (!EncryptedSqliteVfs::BackupEncryptedDatabase(
            dataDirectory / "clipboard.db", result.path / "clipboard.db", &error)) {
        result.message = "Clipboard database backup failed: " + error;
        std::filesystem::remove_all(result.path, ec);
        return result;
    }
    const auto images = dataDirectory / "images.db";
    if (std::filesystem::exists(images, ec) || EncryptedSqliteVfs::HasKey(images)) {
        if (!EncryptedSqliteVfs::BackupEncryptedDatabase(
                images, result.path / "images.db", &error)) {
            result.message = "Image database backup failed: " + error;
            std::filesystem::remove_all(result.path, ec);
            return result;
        }
        result.imagesIncluded = true;
    }
    const auto config = dataDirectory / "config.json";
    result.configIncluded = std::filesystem::exists(config, ec);
    if (result.configIncluded && !ProtectConfig(
            config, result.path / kProtectedConfigName, &error)) {
        result.message = "Configuration backup failed: " + error;
        std::filesystem::remove_all(result.path, ec);
        return result;
    }
    if (!WriteJson(result.path / kManifestName,
                   Manifest(result.imagesIncluded, result.configIncluded), &error)) {
        result.message = error;
        std::filesystem::remove_all(result.path, ec);
        return result;
    }
    result.ok = true;
    result.message = "Encrypted clipboard";
    if (result.imagesIncluded) result.message += ", image";
    if (result.configIncluded) result.message += ", and application-settings";
    result.message += " backup created.";
    if (!result.imagesIncluded) result.message += " No image database existed.";
    return result;
}

Result StageEncryptedRestore(const std::filesystem::path& dataDirectory,
                             const std::filesystem::path& backupDirectory) {
    Result result;
    std::string error;
    bool configIncluded = false;
    if (!ValidateBackupDirectory(backupDirectory, result.imagesIncluded,
                                 configIncluded, &error)) {
        result.message = error;
        return result;
    }
    result.configIncluded = configIncluded;
    const auto pending = dataDirectory / kPendingDirectory;
    const auto temporary = dataDirectory / kPendingTemporary;
    std::error_code ec;
    if (std::filesystem::exists(pending, ec)) {
        result.message = "A restore is already pending. Cancel it before staging another.";
        return result;
    }
    std::filesystem::remove_all(temporary, ec);
    std::filesystem::create_directories(temporary, ec);
    if (ec) {
        result.message = "Could not create the restore staging folder.";
        return result;
    }
    if (!EncryptedSqliteVfs::BackupEncryptedDatabase(
            backupDirectory / "clipboard.db", temporary / "clipboard.db", &error) ||
        (result.imagesIncluded && !EncryptedSqliteVfs::BackupEncryptedDatabase(
            backupDirectory / "images.db", temporary / "images.db", &error)) ||
        (configIncluded && !ReprotectConfig(
            backupDirectory / kProtectedConfigName,
            temporary / kProtectedConfigName, &error)) ||
        !WriteJson(temporary / kManifestName,
                   Manifest(result.imagesIncluded, configIncluded), &error)) {
        result.message = "Restore staging failed: " + error;
        std::filesystem::remove_all(temporary, ec);
        return result;
    }
    std::filesystem::rename(temporary, pending, ec);
    if (ec) {
        result.message = "Could not finalize restore staging: " + ec.message();
        std::filesystem::remove_all(temporary, ec);
        return result;
    }
    result.ok = true;
    result.path = pending;
    result.message = "Encrypted restore staged and verified. Restart Clipboard++ to apply it.";
    return result;
}

Result ApplyPendingRestore(const std::filesystem::path& dataDirectory) {
    Result result;
    const auto pending = dataDirectory / kPendingDirectory;
    std::error_code ec;
    if (!std::filesystem::exists(pending, ec)) {
        result.ok = true;
        result.message = "No restore was pending.";
        return result;
    }
    std::string error;
    bool configIncluded = false;
    if (!ValidateBackupDirectory(pending, result.imagesIncluded,
                                 configIncluded, &error)) {
        result.message = "Pending restore was not applied: " + error;
        WriteLastResult(dataDirectory, result);
        return result;
    }
    result.configIncluded = configIncluded;

    result.rollbackPath = UniqueDirectory(
        dataDirectory / kRollbackDirectory, "Before restore ");
    std::filesystem::create_directories(result.rollbackPath, ec);
    if (ec) {
        result.message = "Could not create the restore rollback directory.";
        WriteLastResult(dataDirectory, result);
        return result;
    }

    const std::vector<std::string> databases = result.imagesIncluded
        ? std::vector<std::string>{"clipboard.db", "images.db"}
        : std::vector<std::string>{"clipboard.db"};
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> oldMoves;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> newMoves;
    bool moved = true;
    for (const std::string& database : databases) {
        for (const auto& component : Components(database)) {
            const auto current = dataDirectory / component;
            if (std::filesystem::exists(current, ec)) {
                const auto rollback = result.rollbackPath / component;
                if (!MoveExisting(current, rollback, &error)) { moved = false; break; }
                oldMoves.emplace_back(rollback, current);
            }
        }
        if (!moved) break;
        for (const auto& component : std::vector<std::filesystem::path>{
                 database, database + ".key"}) {
            const auto staged = pending / component;
            const auto current = dataDirectory / component;
            if (!MoveExisting(staged, current, &error)) { moved = false; break; }
            newMoves.emplace_back(current, staged);
        }
        if (!moved) break;
    }

    bool valid = moved && ValidateDatabase(
        dataDirectory / "clipboard.db", "profiles", &error);
    if (valid && result.imagesIncluded)
        valid = ValidateDatabase(dataDirectory / "images.db", "images", &error);
    std::vector<uint8_t> restoredConfig;
    if (valid && configIncluded)
        valid = UnprotectConfig(pending / kProtectedConfigName,
                                restoredConfig, &error);
    std::filesystem::path rollbackConfig;
    bool configInstalled = false;
    if (valid && configIncluded) {
        const auto currentConfig = dataDirectory / "config.json";
        rollbackConfig = result.rollbackPath / "config.json";
        if (!MoveExisting(currentConfig, rollbackConfig, &error)) {
            valid = false;
        } else if (!WriteBytes(currentConfig, restoredConfig, &error)) {
            std::filesystem::remove(currentConfig, ec);
            MoveExisting(rollbackConfig, currentConfig, nullptr);
            valid = false;
        } else {
            configInstalled = true;
        }
        SecureZeroMemory(restoredConfig.data(), restoredConfig.size());
    }
    if (!valid) {
        bool recovered = true;
        if (configInstalled) {
            std::filesystem::remove(dataDirectory / "config.json", ec);
            recovered &= MoveExisting(rollbackConfig,
                                      dataDirectory / "config.json", nullptr);
        }
        for (auto it = newMoves.rbegin(); it != newMoves.rend(); ++it)
            recovered &= MoveExisting(it->first, it->second, nullptr);
        for (auto it = oldMoves.rbegin(); it != oldMoves.rend(); ++it)
            recovered &= MoveExisting(it->first, it->second, nullptr);
        result.safeToContinue = recovered;
        result.message = "Restore failed and " + std::string(recovered
            ? "the previous encrypted databases were restored: "
            : "automatic rollback was incomplete; do not continue: ") + error;
        WriteLastResult(dataDirectory, result);
        return result;
    }

    std::filesystem::remove_all(pending, ec);
    result.ok = true;
    result.path = dataDirectory;
    result.message = "Encrypted backup restored successfully. Previous databases were retained in " +
                     result.rollbackPath.u8string();
    WriteLastResult(dataDirectory, result);
    return result;
}

bool HasPendingRestore(const std::filesystem::path& dataDirectory) {
    std::error_code ec;
    return std::filesystem::exists(dataDirectory / kPendingDirectory, ec);
}

bool CancelPendingRestore(const std::filesystem::path& dataDirectory,
                          std::string* error) {
    std::error_code ec;
    std::filesystem::remove_all(dataDirectory / kPendingDirectory, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

std::string ReadLastRestoreStatus(const std::filesystem::path& dataDirectory) {
    try {
        std::ifstream input(dataDirectory / kLastResult, std::ios::binary);
        if (!input) return {};
        json value; input >> value;
        return value.value("message", std::string{});
    } catch (...) {
        return "The previous restore result could not be read.";
    }
}

} // namespace backup_restore
