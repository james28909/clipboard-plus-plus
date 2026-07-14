#include "../src/clipboard/ImageStore.h"
#include "../src/app/ConfigStore.h"
#include "../src/security/EncryptedSqliteVfs.h"
#include "../third_party/sqlite/sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool value, const char* name) {
    if (value)
        return true;
    std::cerr << "FAILED: " << name << '\n';
    return false;
}

bool Exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool CreateLegacyDatabase(const std::filesystem::path& path,
                          const std::vector<uint8_t>& bytes,
                          bool addInvalidRow = false) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.u8string().c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        return false;
    bool ok = Exec(db,
        "CREATE TABLE images("
        "id TEXT PRIMARY KEY,profile_id TEXT NOT NULL,width INTEGER NOT NULL,"
        "height INTEGER NOT NULL,source_proc TEXT NOT NULL DEFAULT '',"
        "captured_at INTEGER NOT NULL,byte_size INTEGER NOT NULL,"
        "stored_format INTEGER NOT NULL DEFAULT 1,data BLOB NOT NULL);"
        "CREATE INDEX idx_images_profile ON images(profile_id);"
        "CREATE INDEX idx_images_captured ON images(captured_at DESC);");

    sqlite3_stmt* insert = nullptr;
    if (ok && sqlite3_prepare_v2(db,
            "INSERT INTO images VALUES(?,?,?,?,?,?,?,?,?);",
            -1, &insert, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(insert, 1, "legacy-image", -1, SQLITE_STATIC);
        sqlite3_bind_text(insert, 2, "legacy-profile", -1, SQLITE_STATIC);
        sqlite3_bind_int(insert, 3, 64);
        sqlite3_bind_int(insert, 4, 64);
        sqlite3_bind_text(insert, 5, "legacy.exe", -1, SQLITE_STATIC);
        sqlite3_bind_int64(insert, 6, 1234);
        sqlite3_bind_int64(insert, 7, static_cast<sqlite3_int64>(bytes.size()));
        sqlite3_bind_int(insert, 8, static_cast<int>(StoredFormat::Png));
        sqlite3_bind_blob(insert, 9, bytes.data(), static_cast<int>(bytes.size()), SQLITE_STATIC);
        ok = sqlite3_step(insert) == SQLITE_DONE;
    } else {
        ok = false;
    }
    sqlite3_finalize(insert);

    if (ok && addInvalidRow)
        ok = Exec(db,
            "INSERT INTO images VALUES('empty-image','legacy-profile',64,64,'',"
            "1235,0,1,X'');");
    sqlite3_close(db);
    return ok;
}

bool ReadStoredBlob(const std::filesystem::path& path, const std::string& id,
                    std::vector<uint8_t>& bytes, int& protectionVersion) {
    sqlite3* db = nullptr;
    if (EncryptedSqliteVfs::Open(path, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        return false;
    sqlite3_stmt* stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db,
        "SELECT data,protection_version FROM images WHERE id=?;",
        -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        ok = sqlite3_step(stmt) == SQLITE_ROW;
    }
    if (ok) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        const int size = sqlite3_column_bytes(stmt, 0);
        protectionVersion = sqlite3_column_int(stmt, 1);
        bytes.resize(static_cast<size_t>(size));
        if (blob && size > 0)
            std::memcpy(bytes.data(), blob, bytes.size());
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool CorruptBlob(const std::filesystem::path& path, const std::string& id) {
    sqlite3* db = nullptr;
    if (EncryptedSqliteVfs::Open(path, &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
        return false;
    sqlite3_stmt* stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db,
        "UPDATE images SET data=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
    const uint8_t corruption[] = {0x01, 0x02, 0x03, 0x04};
    if (ok) {
        sqlite3_bind_blob(stmt, 1, corruption, sizeof(corruption), SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

} // namespace

int main() {
    const std::filesystem::path testDirectory =
        std::filesystem::temp_directory_path() /
        ("clipboardpp-image-encryption-" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(testDirectory, error);
    std::filesystem::create_directories(testDirectory, error);

    const std::vector<uint8_t> secret = {
        'u','n','i','q','u','e','-','i','m','a','g','e','-','b','y','t','e','s'
    };
    bool ok = true;

    const auto migrationDb = testDirectory / "migration.db";
    ok &= Expect(CreateLegacyDatabase(migrationDb, secret), "legacy database is created");
    {
        ImageStore store;
        ok &= Expect(store.Open(migrationDb), "legacy image database migrates");
        ok &= Expect(store.GetPng("legacy-image") == secret,
                     "migrated image decrypts to its original bytes");
    }

    std::vector<uint8_t> stored;
    int protectionVersion = 0;
    ok &= Expect(ReadStoredBlob(migrationDb, "legacy-image", stored, protectionVersion),
                 "migrated database row can be inspected");
    ok &= Expect(protectionVersion == 1, "migrated row is marked protected");
    ok &= Expect(stored != secret, "migrated image BLOB is not plaintext");
    ok &= Expect(std::search(stored.begin(), stored.end(), secret.begin(), secret.end()) == stored.end(),
                 "encrypted BLOB does not contain the original image bytes");

    {
        ImageStore store;
        ok &= Expect(store.Open(migrationDb), "encrypted image database reopens");
        ok &= Expect(store.GetPng("legacy-image") == secret,
                     "encrypted image reload preserves bytes");
    }

    const auto newDb = testDirectory / "new-images.db";
    std::string newId;
    {
        ImageStore store;
        ok &= Expect(store.Open(newDb), "new image database opens");
        newId = store.StoreImage(secret, true, "new-profile", 64, 64, "test.exe", 5678);
        ok &= Expect(!newId.empty(), "new image is stored");
        ok &= Expect(store.GetPng(newId) == secret, "new image decrypts on read");
        std::vector<uint8_t> exactBytes;
        StoredFormat exactFormat = StoredFormat::RawDib;
        ok &= Expect(store.GetStoredBytes(newId, exactBytes, exactFormat) &&
                     exactBytes == secret && exactFormat == StoredFormat::Png,
                     "exact stored image representation is available for vault export");
    }
    stored.clear();
    protectionVersion = 0;
    ok &= Expect(ReadStoredBlob(newDb, newId, stored, protectionVersion),
                 "new database row can be inspected");
    ok &= Expect(protectionVersion == 1 && stored != secret,
                 "new image is stored encrypted");
    ok &= Expect(CorruptBlob(newDb, newId), "encrypted image can be corrupted for testing");
    {
        ImageStore store;
        ok &= Expect(store.Open(newDb), "database with corrupt protected row still opens");
        ok &= Expect(store.GetPng(newId).empty(), "corrupt encrypted image is rejected");
    }

    const auto largeDb = testDirectory / "large-images.db";
    {
        std::vector<uint8_t> largeImage(8 * 1024 * 1024);
        for (size_t i = 0; i < largeImage.size(); ++i)
            largeImage[i] = static_cast<uint8_t>((i * 131u) & 0xffu);
        ImageStore store;
        ok &= Expect(store.Open(largeDb), "large-image stress database opens");
        const std::string largeId = store.StoreImage(
            largeImage, true, "stress-profile", 4096, 2160, "stress.exe", 9999);
        ok &= Expect(!largeId.empty() && store.GetPng(largeId) == largeImage,
                     "multi-megabyte encrypted image round-trips byte for byte");
    }

    const auto rollbackDb = testDirectory / "rollback.db";
    ok &= Expect(CreateLegacyDatabase(rollbackDb, secret, true),
                 "rollback test database is created");
    {
        ImageStore store;
        ok &= Expect(!store.Open(rollbackDb), "failed migration does not open the store");
    }
    stored.clear();
    protectionVersion = -1;
    ok &= Expect(ReadStoredBlob(rollbackDb, "legacy-image", stored, protectionVersion),
                 "rolled-back legacy row can be inspected");
    ok &= Expect(protectionVersion == 0 && stored == secret,
                 "failed migration rolls back every image row");

    const auto retentionDb = testDirectory / "retention.db";
    {
        ImageStore store;
        ok &= Expect(store.Open(retentionDb), "retention database opens");
        ImageSettings settings;
        settings.maxImages = 1;
        settings.skipSmallImages = false;
        store.SetSettings(settings);
        const std::string protectedId = store.StoreImage(
            {'p','r','o','t','e','c','t','e','d'}, true,
            "profile", 64, 64, "test.exe", 1);
        store.SetProtectedImageIdsProvider([protectedId] {
            return std::unordered_set<std::string>{protectedId};
        });
        const std::string disposableId = store.StoreImage(
            {'d','i','s','p','o','s','a','b','l','e'}, true,
            "profile", 64, 64, "test.exe", 2);
        const std::string newestId = store.StoreImage(
            {'n','e','w','e','s','t'}, true,
            "profile", 64, 64, "test.exe", 3);
        ImageRecord record;
        ok &= Expect(store.GetRecord(protectedId, record),
                     "vault-referenced image survives max-image cleanup");
        ok &= Expect(!store.GetRecord(disposableId, record),
                     "unreferenced image is eligible for cleanup");
        ok &= Expect(store.GetRecord(newestId, record),
                     "new image survives cleanup before history references it");
    }

    std::filesystem::remove_all(testDirectory, error);
    if (!ok)
        return 1;
    std::cout << "image encryption tests passed\n";
    return 0;
}
