#pragma once

#include <filesystem>
#include <string>

struct sqlite3;

namespace EncryptedSqliteVfs {

constexpr int kPageSize = 4096;

const char* Name();
bool Register(std::string* error = nullptr);

std::filesystem::path KeyPath(const std::filesystem::path& databasePath);
bool HasKey(const std::filesystem::path& databasePath);
bool CreateKey(const std::filesystem::path& databasePath, std::string* error = nullptr);
bool RemoveKey(const std::filesystem::path& databasePath);

// Converts a plaintext SQLite database into an encrypted database using SQLite's
// backup API. The original is retained until the encrypted copy passes an
// integrity check and can be reopened at its final path.
bool MigratePlaintextDatabase(const std::filesystem::path& databasePath,
                              std::string* error = nullptr);
bool BackupEncryptedDatabase(const std::filesystem::path& sourcePath,
                             const std::filesystem::path& destinationPath,
                             std::string* error = nullptr);

int Open(const std::filesystem::path& databasePath, sqlite3** database,
         int flags, std::string* error = nullptr);

} // namespace EncryptedSqliteVfs
