#pragma once

#include <filesystem>
#include <string>

namespace backup_restore {

struct Result {
    bool ok{false};
    bool imagesIncluded{false};
    bool configIncluded{false};
    bool safeToContinue{true};
    std::filesystem::path path;
    std::filesystem::path rollbackPath;
    std::string message;
};

// Creates a timestamped encrypted backup directory beneath destinationParent.
Result CreateEncryptedBackup(const std::filesystem::path& dataDirectory,
                             const std::filesystem::path& destinationParent);

// Validates an encrypted backup and creates a fresh encrypted restore snapshot
// under dataDirectory. The live databases are not changed until next startup.
Result StageEncryptedRestore(const std::filesystem::path& dataDirectory,
                             const std::filesystem::path& backupDirectory);

// Called before Clipboard++ opens either database. On success, the previous
// database files remain in an encrypted rollback directory.
Result ApplyPendingRestore(const std::filesystem::path& dataDirectory);

bool HasPendingRestore(const std::filesystem::path& dataDirectory);
bool CancelPendingRestore(const std::filesystem::path& dataDirectory,
                          std::string* error = nullptr);
std::string ReadLastRestoreStatus(const std::filesystem::path& dataDirectory);

} // namespace backup_restore
