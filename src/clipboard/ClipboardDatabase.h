#pragma once

#include "ClipboardHistory.h"
#include "../app/ConfigStore.h"
#include "../transforms/RegexTransform.h"
#include "../templates/PasteTemplate.h"
#include "../actions/CustomAction.h"

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>

struct sqlite3;

struct ClipboardVaultEntry {
    int64_t archiveId{};
    int64_t archivedAtMs{};
    ClipboardItem item;
};

struct NamedClipboardSlot {
    int64_t slotId{};
    std::string name;
    std::string text;
    int64_t createdAtMs{};
    int64_t updatedAtMs{};
};

class ClipboardDatabase {
public:
    ClipboardDatabase() = default;
    ~ClipboardDatabase();

    ClipboardDatabase(const ClipboardDatabase&) = delete;
    ClipboardDatabase& operator=(const ClipboardDatabase&) = delete;

    bool Open(const std::filesystem::path& path, std::string* error = nullptr);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    bool Begin();
    bool Commit();
    void Rollback();

    bool LoadProfiles(std::vector<ClipboardProfileConfig>& profiles) const;
    bool UpsertProfile(const ClipboardProfileConfig& profile, int sortOrder);
    bool DeleteProfile(const std::string& profileId);

    bool LoadHistory(const std::string& profileId, ClipboardHistory& history,
                     bool& found) const;
    bool SaveHistory(const std::string& profileId,
                     const ClipboardHistory& history);

    bool ArchiveItem(const std::string& profileId, const ClipboardItem& item);
    bool SearchVault(const std::string& profileId, const std::string& query,
                     std::vector<ClipboardVaultEntry>& entries,
                     size_t limit = 250) const;
    bool GetVaultItem(const std::string& profileId, int64_t archiveId,
                      ClipboardVaultEntry& entry) const;
    bool DeleteVaultItem(const std::string& profileId, int64_t archiveId);
    bool VaultCount(const std::string& profileId, size_t& count) const;
    bool PruneVault(const std::string& profileId, int64_t maximumBytes);
    bool ReferencedImageIds(std::unordered_set<std::string>& ids) const;

    bool LoadNamedSlots(std::vector<NamedClipboardSlot>& slots) const;
    bool SaveNamedSlot(NamedClipboardSlot& slot);
    bool DeleteNamedSlot(int64_t slotId);

    bool LoadRegexTransforms(std::vector<RegexTransformDefinition>& transforms) const;
    bool SaveRegexTransform(RegexTransformDefinition& transform);
    bool DeleteRegexTransform(int64_t transformId);
    bool LoadPasteTemplates(std::vector<PasteTemplateDefinition>& templates) const;
    bool SavePasteTemplate(PasteTemplateDefinition& value);
    bool DeletePasteTemplate(int64_t templateId);
    bool LoadCustomActions(std::vector<CustomActionDefinition>& actions) const;
    bool SaveCustomAction(CustomActionDefinition& action);
    bool DeleteCustomAction(int64_t actionId);

    bool GetActiveProfileId(std::string& profileId) const;
    bool SetActiveProfileId(const std::string& profileId);
    bool IntegrityCheck() const;

private:
    bool CreateSchema();
    bool Exec(const char* sql) const;

    sqlite3* m_db{};
};
