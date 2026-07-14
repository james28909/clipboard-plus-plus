#include "ClipboardDatabase.h"

#include "ClipboardHistoryStore.h"
#include "../security/EncryptedSqliteVfs.h"
#include "../../third_party/sqlite/sqlite3.h"

#include <cstring>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace {

void BindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::string ColumnText(sqlite3_stmt* statement, int column) {
    const auto* value = sqlite3_column_text(statement, column);
    const int size = sqlite3_column_bytes(statement, column);
    return value && size > 0
        ? std::string(reinterpret_cast<const char*>(value), static_cast<size_t>(size))
        : std::string{};
}

std::string SerializeVaultItem(const std::string& profileId,
                               const ClipboardItem& item) {
    ClipboardHistory single(1);
    single.LoadSnapshot({item}, std::max<uint64_t>(item.id + 1, 1));
    return ClipboardHistoryStore::Serialize(profileId, single);
}

bool DeserializeVaultItem(const std::string& profileId,
                          const std::string& payload, ClipboardItem& item) {
    ClipboardHistory single(1);
    if (!ClipboardHistoryStore::Deserialize(profileId, payload, single))
        return false;
    const auto items = single.Snapshot();
    if (items.size() != 1) return false;
    item = items.front();
    return true;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

ClipboardDatabase::~ClipboardDatabase() {
    Close();
}

bool ClipboardDatabase::Open(const std::filesystem::path& path,
                             std::string* error) {
    Close();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create clipboard database directory";
        return false;
    }
    if (!EncryptedSqliteVfs::MigratePlaintextDatabase(path, error) ||
        EncryptedSqliteVfs::Open(
            path, &m_db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            error) != SQLITE_OK) {
        Close();
        return false;
    }
    if (!Exec("PRAGMA page_size=4096;") ||
        !Exec("PRAGMA journal_mode=WAL;") ||
        !Exec("PRAGMA synchronous=NORMAL;") ||
        !Exec("PRAGMA foreign_keys=ON;") || !CreateSchema()) {
        if (error) *error = m_db ? sqlite3_errmsg(m_db) : "Could not initialize clipboard database";
        Close();
        return false;
    }
    return true;
}

void ClipboardDatabase::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool ClipboardDatabase::Exec(const char* sql) const {
    return m_db && sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool ClipboardDatabase::CreateSchema() {
    return Exec(
        "CREATE TABLE IF NOT EXISTS profiles("
        "id TEXT PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL,"
        "process_name TEXT NOT NULL DEFAULT '',"
        "sort_order INTEGER NOT NULL);"
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_profiles_sort ON profiles(sort_order);"
        "CREATE TABLE IF NOT EXISTS histories("
        "profile_id TEXT PRIMARY KEY REFERENCES profiles(id) ON DELETE CASCADE,"
        "payload TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS metadata("
        "key TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS vault_items("
        "archive_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "profile_id TEXT NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
        "content_hash INTEGER NOT NULL DEFAULT 0,"
        "archived_at INTEGER NOT NULL,"
        "payload TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_vault_profile_time "
        "ON vault_items(profile_id,archived_at DESC);"
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_vault_profile_hash "
        "ON vault_items(profile_id,content_hash) WHERE content_hash<>0;"
        "CREATE TABLE IF NOT EXISTS named_slots("
        "slot_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
        "text TEXT NOT NULL,"
        "created_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_named_slots_name "
        "ON named_slots(name COLLATE NOCASE);"
        "CREATE TABLE IF NOT EXISTS regex_transforms("
        "transform_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
        "pattern TEXT NOT NULL,"
        "replacement TEXT NOT NULL,"
        "case_sensitive INTEGER NOT NULL DEFAULT 1,"
        "multiline INTEGER NOT NULL DEFAULT 0,"
        "dot_matches_newline INTEGER NOT NULL DEFAULT 0,"
        "replace_all INTEGER NOT NULL DEFAULT 1,"
        "created_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_regex_transforms_name "
        "ON regex_transforms(name COLLATE NOCASE);"
        "CREATE TABLE IF NOT EXISTS paste_templates("
        "template_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
        "body TEXT NOT NULL,"
        "created_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_paste_templates_name "
        "ON paste_templates(name COLLATE NOCASE);"
        "CREATE TABLE IF NOT EXISTS custom_actions("
        "action_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "label TEXT NOT NULL COLLATE NOCASE UNIQUE,"
        "toolbar_order INTEGER NOT NULL DEFAULT 0,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "placement INTEGER NOT NULL DEFAULT 0,"
        "payload TEXT NOT NULL,"
        "created_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_custom_actions_order "
        "ON custom_actions(placement,toolbar_order,action_id);"
        "PRAGMA user_version=6;");
}

bool ClipboardDatabase::Begin() { return Exec("BEGIN IMMEDIATE;"); }
bool ClipboardDatabase::Commit() { return Exec("COMMIT;"); }
void ClipboardDatabase::Rollback() { Exec("ROLLBACK;"); }

bool ClipboardDatabase::LoadProfiles(
    std::vector<ClipboardProfileConfig>& profiles) const {
    profiles.clear();
    if (!m_db) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT id,name,created_at,updated_at,process_name "
            "FROM profiles ORDER BY sort_order;", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        ClipboardProfileConfig profile;
        profile.id = ColumnText(statement, 0);
        profile.name = ColumnText(statement, 1);
        profile.createdAt = ColumnText(statement, 2);
        profile.updatedAt = ColumnText(statement, 3);
        profile.processName = ColumnText(statement, 4);
        if (profile.id.empty()) { ok = false; break; }
        profiles.push_back(std::move(profile));
    }
    if (sqlite3_errcode(m_db) != SQLITE_OK && sqlite3_errcode(m_db) != SQLITE_DONE)
        ok = false;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::UpsertProfile(const ClipboardProfileConfig& profile,
                                      int sortOrder) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO profiles(id,name,created_at,updated_at,process_name,sort_order) "
        "VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "name=excluded.name,created_at=excluded.created_at,"
        "updated_at=excluded.updated_at,process_name=excluded.process_name,"
        "sort_order=excluded.sort_order;";
    if (!m_db || sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profile.id);
    BindText(statement, 2, profile.name);
    BindText(statement, 3, profile.createdAt);
    BindText(statement, 4, profile.updatedAt);
    BindText(statement, 5, profile.processName);
    sqlite3_bind_int(statement, 6, sortOrder);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::DeleteProfile(const std::string& profileId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db, "DELETE FROM profiles WHERE id=?;",
                                     -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::LoadHistory(const std::string& profileId,
                                    ClipboardHistory& history,
                                    bool& found) const {
    found = false;
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT payload FROM histories WHERE profile_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    const int step = sqlite3_step(statement);
    bool ok = step == SQLITE_DONE;
    if (step == SQLITE_ROW) {
        found = true;
        ok = ClipboardHistoryStore::Deserialize(
            profileId, ColumnText(statement, 0), history);
    }
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::SaveHistory(const std::string& profileId,
                                    const ClipboardHistory& history) {
    const std::string payload = ClipboardHistoryStore::Serialize(profileId, history);
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "INSERT INTO histories(profile_id,payload) VALUES(?,?) "
            "ON CONFLICT(profile_id) DO UPDATE SET payload=excluded.payload;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    BindText(statement, 2, payload);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::ArchiveItem(const std::string& profileId,
                                    const ClipboardItem& item) {
    const std::string payload = SerializeVaultItem(profileId, item);
    const int64_t archivedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO vault_items(profile_id,content_hash,archived_at,payload) "
        "VALUES(?,?,?,?) ON CONFLICT(profile_id,content_hash) WHERE content_hash<>0 "
        "DO UPDATE SET archived_at=excluded.archived_at,payload=excluded.payload;";
    if (!m_db || sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(item.contentHash));
    sqlite3_bind_int64(statement, 3, archivedAt);
    BindText(statement, 4, payload);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::SearchVault(
    const std::string& profileId, const std::string& query,
    std::vector<ClipboardVaultEntry>& entries, size_t limit) const {
    entries.clear();
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT archive_id,archived_at,payload FROM vault_items "
            "WHERE profile_id=? ORDER BY archived_at DESC;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    const std::string needle = Lower(query);
    int step = SQLITE_ROW;
    bool ok = true;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        ClipboardVaultEntry entry;
        entry.archiveId = sqlite3_column_int64(statement, 0);
        entry.archivedAtMs = sqlite3_column_int64(statement, 1);
        if (!DeserializeVaultItem(profileId, ColumnText(statement, 2), entry.item)) {
            ok = false;
            break;
        }
        if (!needle.empty()) {
            const std::string haystack = Lower(entry.item.text + "\n" +
                entry.item.sourceProcess + "\n" + entry.item.sourceFilePath +
                "\n" + entry.item.sourceKind);
            if (haystack.find(needle) == std::string::npos)
                continue;
        }
        entries.push_back(std::move(entry));
        if (entries.size() >= limit) break;
    }
    if (step != SQLITE_DONE && step != SQLITE_ROW) ok = false;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::DeleteVaultItem(const std::string& profileId,
                                        int64_t archiveId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "DELETE FROM vault_items WHERE profile_id=? AND archive_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    sqlite3_bind_int64(statement, 2, archiveId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    sqlite3_changes(m_db) == 1;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::GetVaultItem(const std::string& profileId,
                                     int64_t archiveId,
                                     ClipboardVaultEntry& entry) const {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT archived_at,payload FROM vault_items "
            "WHERE profile_id=? AND archive_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    sqlite3_bind_int64(statement, 2, archiveId);
    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    bool ok = found;
    if (found) {
        entry.archiveId = archiveId;
        entry.archivedAtMs = sqlite3_column_int64(statement, 0);
        ok = DeserializeVaultItem(profileId, ColumnText(statement, 1), entry.item);
    }
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::VaultCount(const std::string& profileId,
                                   size_t& count) const {
    count = 0;
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT COUNT(*) FROM vault_items WHERE profile_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    const bool ok = sqlite3_step(statement) == SQLITE_ROW;
    if (ok) count = static_cast<size_t>(sqlite3_column_int64(statement, 0));
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::PruneVault(const std::string& profileId,
                                   int64_t maximumBytes) {
    if (!m_db || maximumBytes <= 0) return true;
    sqlite3_stmt* statement = nullptr;
    // Payloads vary greatly in size, so prune one oldest row at a time until the
    // stored payload total is within the configured byte budget.
    if (sqlite3_prepare_v2(m_db,
            "SELECT COALESCE(SUM(length(payload)),0) FROM vault_items WHERE profile_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    int64_t total = 0;
    if (sqlite3_step(statement) == SQLITE_ROW)
        total = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    while (total > maximumBytes) {
        if (sqlite3_prepare_v2(m_db,
                "SELECT archive_id,length(payload) FROM vault_items WHERE profile_id=? "
                "ORDER BY archived_at ASC LIMIT 1;", -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, profileId);
        if (sqlite3_step(statement) != SQLITE_ROW) {
            sqlite3_finalize(statement);
            break;
        }
        const int64_t id = sqlite3_column_int64(statement, 0);
        const int64_t bytes = sqlite3_column_int64(statement, 1);
        sqlite3_finalize(statement);
        if (!DeleteVaultItem(profileId, id)) return false;
        total -= bytes;
    }
    return true;
}

bool ClipboardDatabase::ReferencedImageIds(
    std::unordered_set<std::string>& ids) const {
    ids.clear();
    if (!m_db) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT profile_id,payload FROM histories "
            "UNION ALL SELECT profile_id,payload FROM vault_items;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = true;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        const std::string profileId = ColumnText(statement, 0);
        ClipboardHistory contents(kMaxClipboardHistoryItems);
        if (!ClipboardHistoryStore::Deserialize(
                profileId, ColumnText(statement, 1), contents)) {
            ok = false;
            break;
        }
        for (const ClipboardItem& item : contents.Snapshot())
            if (item.IsImage() && !item.imageStoreId.empty())
                ids.insert(item.imageStoreId);
    }
    if (step != SQLITE_DONE) ok = false;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::LoadNamedSlots(
    std::vector<NamedClipboardSlot>& slots) const {
    slots.clear();
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT slot_id,name,text,created_at,updated_at FROM named_slots "
            "ORDER BY name COLLATE NOCASE,slot_id;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = true;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        NamedClipboardSlot slot;
        slot.slotId = sqlite3_column_int64(statement, 0);
        slot.name = ColumnText(statement, 1);
        slot.text = ColumnText(statement, 2);
        slot.createdAtMs = sqlite3_column_int64(statement, 3);
        slot.updatedAtMs = sqlite3_column_int64(statement, 4);
        slots.push_back(std::move(slot));
    }
    if (step != SQLITE_DONE) ok = false;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::SaveNamedSlot(NamedClipboardSlot& slot) {
    if (!m_db || slot.name.empty()) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_stmt* statement = nullptr;
    if (slot.slotId == 0) {
        if (sqlite3_prepare_v2(m_db,
                "INSERT INTO named_slots(name,text,created_at,updated_at) "
                "VALUES(?,?,?,?);", -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, slot.name);
        BindText(statement, 2, slot.text);
        sqlite3_bind_int64(statement, 3, now);
        sqlite3_bind_int64(statement, 4, now);
    } else {
        if (sqlite3_prepare_v2(m_db,
                "UPDATE named_slots SET name=?,text=?,updated_at=? WHERE slot_id=?;",
                -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, slot.name);
        BindText(statement, 2, slot.text);
        sqlite3_bind_int64(statement, 3, now);
        sqlite3_bind_int64(statement, 4, slot.slotId);
    }
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    (slot.slotId == 0 || sqlite3_changes(m_db) == 1);
    sqlite3_finalize(statement);
    if (!ok) return false;
    if (slot.slotId == 0) {
        slot.slotId = sqlite3_last_insert_rowid(m_db);
        slot.createdAtMs = now;
    }
    slot.updatedAtMs = now;
    return true;
}

bool ClipboardDatabase::DeleteNamedSlot(int64_t slotId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || slotId <= 0 || sqlite3_prepare_v2(m_db,
            "DELETE FROM named_slots WHERE slot_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(statement, 1, slotId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    sqlite3_changes(m_db) == 1;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::LoadRegexTransforms(
    std::vector<RegexTransformDefinition>& transforms) const {
    transforms.clear();
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT transform_id,name,pattern,replacement,case_sensitive,"
            "multiline,dot_matches_newline,replace_all,created_at,updated_at "
            "FROM regex_transforms ORDER BY name COLLATE NOCASE,transform_id;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = true;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        RegexTransformDefinition transform;
        transform.transformId = sqlite3_column_int64(statement, 0);
        transform.name = ColumnText(statement, 1);
        transform.pattern = ColumnText(statement, 2);
        transform.replacement = ColumnText(statement, 3);
        transform.caseSensitive = sqlite3_column_int(statement, 4) != 0;
        transform.multiline = sqlite3_column_int(statement, 5) != 0;
        transform.dotMatchesNewline = sqlite3_column_int(statement, 6) != 0;
        transform.replaceAll = sqlite3_column_int(statement, 7) != 0;
        transform.createdAtMs = sqlite3_column_int64(statement, 8);
        transform.updatedAtMs = sqlite3_column_int64(statement, 9);
        transforms.push_back(std::move(transform));
    }
    if (step != SQLITE_DONE) ok = false;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::SaveRegexTransform(RegexTransformDefinition& transform) {
    if (!m_db || transform.name.empty() || transform.pattern.empty()) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_stmt* statement = nullptr;
    if (transform.transformId == 0) {
        if (sqlite3_prepare_v2(m_db,
                "INSERT INTO regex_transforms(name,pattern,replacement,case_sensitive,"
                "multiline,dot_matches_newline,replace_all,created_at,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?,?);", -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, transform.name);
        BindText(statement, 2, transform.pattern);
        BindText(statement, 3, transform.replacement);
        sqlite3_bind_int(statement, 4, transform.caseSensitive ? 1 : 0);
        sqlite3_bind_int(statement, 5, transform.multiline ? 1 : 0);
        sqlite3_bind_int(statement, 6, transform.dotMatchesNewline ? 1 : 0);
        sqlite3_bind_int(statement, 7, transform.replaceAll ? 1 : 0);
        sqlite3_bind_int64(statement, 8, now);
        sqlite3_bind_int64(statement, 9, now);
    } else {
        if (sqlite3_prepare_v2(m_db,
                "UPDATE regex_transforms SET name=?,pattern=?,replacement=?,"
                "case_sensitive=?,multiline=?,dot_matches_newline=?,replace_all=?,"
                "updated_at=? WHERE transform_id=?;",
                -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, transform.name);
        BindText(statement, 2, transform.pattern);
        BindText(statement, 3, transform.replacement);
        sqlite3_bind_int(statement, 4, transform.caseSensitive ? 1 : 0);
        sqlite3_bind_int(statement, 5, transform.multiline ? 1 : 0);
        sqlite3_bind_int(statement, 6, transform.dotMatchesNewline ? 1 : 0);
        sqlite3_bind_int(statement, 7, transform.replaceAll ? 1 : 0);
        sqlite3_bind_int64(statement, 8, now);
        sqlite3_bind_int64(statement, 9, transform.transformId);
    }
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    (transform.transformId == 0 || sqlite3_changes(m_db) == 1);
    sqlite3_finalize(statement);
    if (!ok) return false;
    if (transform.transformId == 0) {
        transform.transformId = sqlite3_last_insert_rowid(m_db);
        transform.createdAtMs = now;
    }
    transform.updatedAtMs = now;
    return true;
}

bool ClipboardDatabase::DeleteRegexTransform(int64_t transformId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || transformId <= 0 || sqlite3_prepare_v2(m_db,
            "DELETE FROM regex_transforms WHERE transform_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(statement, 1, transformId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    sqlite3_changes(m_db) == 1;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::LoadPasteTemplates(
    std::vector<PasteTemplateDefinition>& templates) const {
    templates.clear();
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT template_id,name,body,created_at,updated_at "
            "FROM paste_templates ORDER BY name COLLATE NOCASE,template_id;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = true;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        PasteTemplateDefinition value;
        value.templateId = sqlite3_column_int64(statement, 0);
        value.name = ColumnText(statement, 1);
        value.body = ColumnText(statement, 2);
        value.createdAtMs = sqlite3_column_int64(statement, 3);
        value.updatedAtMs = sqlite3_column_int64(statement, 4);
        templates.push_back(std::move(value));
    }
    if (step != SQLITE_DONE) ok = false;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::SavePasteTemplate(PasteTemplateDefinition& value) {
    if (!m_db || value.name.empty() || value.body.empty()) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_stmt* statement = nullptr;
    if (value.templateId == 0) {
        if (sqlite3_prepare_v2(m_db,
                "INSERT INTO paste_templates(name,body,created_at,updated_at) "
                "VALUES(?,?,?,?);", -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, value.name);
        BindText(statement, 2, value.body);
        sqlite3_bind_int64(statement, 3, now);
        sqlite3_bind_int64(statement, 4, now);
    } else {
        if (sqlite3_prepare_v2(m_db,
                "UPDATE paste_templates SET name=?,body=?,updated_at=? "
                "WHERE template_id=?;", -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, value.name);
        BindText(statement, 2, value.body);
        sqlite3_bind_int64(statement, 3, now);
        sqlite3_bind_int64(statement, 4, value.templateId);
    }
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    (value.templateId == 0 || sqlite3_changes(m_db) == 1);
    sqlite3_finalize(statement);
    if (!ok) return false;
    if (value.templateId == 0) {
        value.templateId = sqlite3_last_insert_rowid(m_db);
        value.createdAtMs = now;
    }
    value.updatedAtMs = now;
    return true;
}

bool ClipboardDatabase::DeletePasteTemplate(int64_t templateId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || templateId <= 0 || sqlite3_prepare_v2(m_db,
            "DELETE FROM paste_templates WHERE template_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(statement, 1, templateId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    sqlite3_changes(m_db) == 1;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::LoadCustomActions(
    std::vector<CustomActionDefinition>& actions) const {
    actions.clear();
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT action_id,label,toolbar_order,enabled,placement,payload,"
            "created_at,updated_at FROM custom_actions "
            "ORDER BY placement,toolbar_order,action_id;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = true;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        CustomActionDefinition action;
        std::string error;
        if (!DeserializeCustomAction(ColumnText(statement, 5), action, &error)) {
            ok = false;
            break;
        }
        action.actionId = sqlite3_column_int64(statement, 0);
        action.label = ColumnText(statement, 1);
        action.toolbarOrder = sqlite3_column_int(statement, 2);
        action.enabled = sqlite3_column_int(statement, 3) != 0;
        action.placement = static_cast<CustomActionPlacement>(
            std::clamp(sqlite3_column_int(statement, 4), 0, 1));
        action.createdAtMs = sqlite3_column_int64(statement, 6);
        action.updatedAtMs = sqlite3_column_int64(statement, 7);
        actions.push_back(std::move(action));
    }
    if (step != SQLITE_DONE) ok = false;
    sqlite3_finalize(statement);
    if (!ok) actions.clear();
    return ok;
}

bool ClipboardDatabase::SaveCustomAction(CustomActionDefinition& action) {
    if (!m_db || !ValidateCustomAction(action).empty()) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string payload = SerializeCustomAction(action);
    sqlite3_stmt* statement = nullptr;
    if (action.actionId == 0) {
        if (sqlite3_prepare_v2(m_db,
                "INSERT INTO custom_actions(label,toolbar_order,enabled,placement,"
                "payload,created_at,updated_at) VALUES(?,?,?,?,?,?,?);",
                -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, action.label);
        sqlite3_bind_int(statement, 2, action.toolbarOrder);
        sqlite3_bind_int(statement, 3, action.enabled ? 1 : 0);
        sqlite3_bind_int(statement, 4, static_cast<int>(action.placement));
        BindText(statement, 5, payload);
        sqlite3_bind_int64(statement, 6, now);
        sqlite3_bind_int64(statement, 7, now);
    } else {
        if (sqlite3_prepare_v2(m_db,
                "UPDATE custom_actions SET label=?,toolbar_order=?,enabled=?,"
                "placement=?,payload=?,updated_at=? WHERE action_id=?;",
                -1, &statement, nullptr) != SQLITE_OK)
            return false;
        BindText(statement, 1, action.label);
        sqlite3_bind_int(statement, 2, action.toolbarOrder);
        sqlite3_bind_int(statement, 3, action.enabled ? 1 : 0);
        sqlite3_bind_int(statement, 4, static_cast<int>(action.placement));
        BindText(statement, 5, payload);
        sqlite3_bind_int64(statement, 6, now);
        sqlite3_bind_int64(statement, 7, action.actionId);
    }
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    (action.actionId == 0 || sqlite3_changes(m_db) == 1);
    sqlite3_finalize(statement);
    if (!ok) return false;
    if (action.actionId == 0) {
        action.actionId = sqlite3_last_insert_rowid(m_db);
        action.createdAtMs = now;
    }
    action.updatedAtMs = now;
    return true;
}

bool ClipboardDatabase::DeleteCustomAction(int64_t actionId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || actionId <= 0 || sqlite3_prepare_v2(m_db,
            "DELETE FROM custom_actions WHERE action_id=?;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(statement, 1, actionId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE &&
                    sqlite3_changes(m_db) == 1;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::GetActiveProfileId(std::string& profileId) const {
    profileId.clear();
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "SELECT value FROM metadata WHERE key='active_profile_id';",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    const int step = sqlite3_step(statement);
    if (step == SQLITE_ROW)
        profileId = ColumnText(statement, 0);
    const bool ok = step == SQLITE_ROW || step == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::SetActiveProfileId(const std::string& profileId) {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db,
            "INSERT INTO metadata(key,value) VALUES('active_profile_id',?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
            -1, &statement, nullptr) != SQLITE_OK)
        return false;
    BindText(statement, 1, profileId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ClipboardDatabase::IntegrityCheck() const {
    sqlite3_stmt* statement = nullptr;
    if (!m_db || sqlite3_prepare_v2(m_db, "PRAGMA integrity_check;", -1,
                                     &statement, nullptr) != SQLITE_OK)
        return false;
    const bool ok = sqlite3_step(statement) == SQLITE_ROW &&
        std::strcmp(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
                    "ok") == 0;
    sqlite3_finalize(statement);
    return ok;
}
