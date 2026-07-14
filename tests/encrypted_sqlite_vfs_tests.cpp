#include "../src/security/EncryptedSqliteVfs.h"
#include "../third_party/sqlite/sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool value, const char* name) {
    if (value) return true;
    std::cerr << "FAILED: " << name << '\n';
    return false;
}

bool Exec(sqlite3* db, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        std::cerr << "SQLite error: " << (message ? message : "unknown") << '\n';
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool Contains(const std::filesystem::path& path, const std::string& needle) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

bool QueryValue(sqlite3* db, const std::string& id, std::string& value) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT value FROM secrets WHERE id=?;", -1,
                           &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_STATIC);
    const bool ok = sqlite3_step(statement) == SQLITE_ROW &&
                    sqlite3_column_text(statement, 0);
    if (ok) value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
    return ok;
}

bool CreatePlaintext(const std::filesystem::path& path,
                     const std::string& secret) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.u8string().c_str(), &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        return false;
    const bool ok = Exec(db, "PRAGMA page_size=4096;") &&
        Exec(db, "CREATE TABLE secrets(id TEXT PRIMARY KEY,value TEXT NOT NULL);") &&
        Exec(db, ("INSERT INTO secrets VALUES('legacy','" + secret + "');").c_str());
    sqlite3_close(db);
    return ok;
}

} // namespace

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("clipboardpp-vfs-tests-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);
    bool ok = true;
    std::string error;

    const std::string secret = "vfs-secret-payload-20260713";
    const auto databasePath = directory / "encrypted.db";
    ok &= Expect(EncryptedSqliteVfs::CreateKey(databasePath, &error),
                 "new database key is created");
    sqlite3* db = nullptr;
    ok &= Expect(EncryptedSqliteVfs::Open(databasePath, &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            &error) == SQLITE_OK, "new encrypted database opens");
    if (db) {
        ok &= Expect(Exec(db, "PRAGMA page_size=4096;"), "page size is configured");
        ok &= Expect(Exec(db, "PRAGMA journal_mode=WAL;"), "WAL mode is enabled");
        ok &= Expect(Exec(db, "PRAGMA wal_autocheckpoint=0;"), "automatic checkpoint is disabled");
        ok &= Expect(Exec(db, "CREATE TABLE secrets(id TEXT PRIMARY KEY,value TEXT NOT NULL);"),
                     "encrypted schema is created");
        ok &= Expect(Exec(db, ("INSERT INTO secrets VALUES('one','" + secret + "');").c_str()),
                     "encrypted WAL row is inserted");
        const std::filesystem::path walPath = databasePath.u8string() + "-wal";
        ok &= Expect(std::filesystem::exists(walPath), "encrypted WAL exists");
        ok &= Expect(!Contains(walPath, secret), "WAL does not contain plaintext row data");
        ok &= Expect(!Contains(walPath, "CREATE TABLE secrets"),
                     "WAL does not contain plaintext schema");
    }
    if (db) sqlite3_close(db);

    ok &= Expect(!Contains(databasePath, "SQLite format 3"),
                 "database header is encrypted");
    ok &= Expect(!Contains(databasePath, secret), "database does not contain plaintext row data");
    ok &= Expect(!Contains(databasePath, "secrets"), "database does not contain plaintext schema names");

    db = nullptr;
    ok &= Expect(EncryptedSqliteVfs::Open(databasePath, &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, &error) == SQLITE_OK,
                 "encrypted database reopens");
    std::string loaded;
    ok &= Expect(db && QueryValue(db, "one", loaded) && loaded == secret,
                 "encrypted row round-trips");
    if (db) {
        ok &= Expect(Exec(db, "PRAGMA journal_mode=DELETE;"),
                     "database switches to rollback journal mode");
        ok &= Expect(Exec(db, "BEGIN; UPDATE secrets SET value='rollback-mode-secret' WHERE id='one'; COMMIT;"),
                     "rollback-journal transaction succeeds");
        loaded.clear();
        ok &= Expect(QueryValue(db, "one", loaded) && loaded == "rollback-mode-secret",
                     "rollback-journal value round-trips");
        sqlite3_close(db);
    }

    sqlite3* plainAttempt = nullptr;
    const int plainOpen = sqlite3_open_v2(databasePath.u8string().c_str(), &plainAttempt,
                                          SQLITE_OPEN_READONLY, nullptr);
    sqlite3_stmt* plainStatement = nullptr;
    const bool plainRejected = plainOpen != SQLITE_OK ||
        sqlite3_prepare_v2(plainAttempt, "SELECT * FROM secrets;", -1,
                           &plainStatement, nullptr) != SQLITE_OK;
    ok &= Expect(plainRejected, "ordinary SQLite cannot read the encrypted database");
    sqlite3_finalize(plainStatement);
    if (plainAttempt) sqlite3_close(plainAttempt);

    const auto migrationPath = directory / "migration.db";
    const std::string legacySecret = "legacy-profile-history-secret";
    ok &= Expect(CreatePlaintext(migrationPath, legacySecret),
                 "plaintext migration source is created");
    ok &= Expect(Contains(migrationPath, "SQLite format 3"),
                 "migration source starts as plaintext SQLite");
    error.clear();
    ok &= Expect(EncryptedSqliteVfs::MigratePlaintextDatabase(migrationPath, &error),
                 "plaintext database migrates");
    ok &= Expect(EncryptedSqliteVfs::HasKey(migrationPath), "migrated key is installed");
    ok &= Expect(!Contains(migrationPath, "SQLite format 3"),
                 "migrated database header is encrypted");
    ok &= Expect(!Contains(migrationPath, legacySecret),
                 "migrated database hides legacy data");
    db = nullptr;
    ok &= Expect(EncryptedSqliteVfs::Open(migrationPath, &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, &error) == SQLITE_OK,
                 "migrated database reopens");
    loaded.clear();
    ok &= Expect(db && QueryValue(db, "legacy", loaded) && loaded == legacySecret,
                 "migrated data round-trips");
    if (db) sqlite3_close(db);

    const auto backupPath = directory / "encrypted-backup.db";
    error.clear();
    ok &= Expect(EncryptedSqliteVfs::BackupEncryptedDatabase(
                     migrationPath, backupPath, &error),
                 "encrypted online backup succeeds");
    ok &= Expect(EncryptedSqliteVfs::HasKey(backupPath),
                 "encrypted backup has its own DPAPI key");
    ok &= Expect(!Contains(backupPath, "SQLite format 3") &&
                 !Contains(backupPath, legacySecret),
                 "encrypted backup does not expose database content");
    db = nullptr;
    ok &= Expect(EncryptedSqliteVfs::Open(backupPath, &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, &error) == SQLITE_OK,
                 "encrypted backup reopens");
    loaded.clear();
    ok &= Expect(db && QueryValue(db, "legacy", loaded) && loaded == legacySecret,
                 "encrypted backup preserves source rows");
    if (db) sqlite3_close(db);

    std::filesystem::path keyPath = EncryptedSqliteVfs::KeyPath(migrationPath);
    std::fstream key(keyPath, std::ios::binary | std::ios::in | std::ios::out);
    if (key) {
        key.seekg(-1, std::ios::end);
        char byte = 0;
        key.read(&byte, 1);
        byte ^= 0x5a;
        key.seekp(-1, std::ios::end);
        key.write(&byte, 1);
    }
    key.close();
    db = nullptr;
    error.clear();
    ok &= Expect(EncryptedSqliteVfs::Open(migrationPath, &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, &error) != SQLITE_OK,
                 "damaged DPAPI key is rejected");
    if (db) sqlite3_close(db);

    std::filesystem::remove_all(directory, ec);
    if (!ok) {
        if (!error.empty()) std::cerr << "Last VFS error: " << error << '\n';
        return 1;
    }
    std::cout << "encrypted sqlite VFS tests passed\n";
    return 0;
}
