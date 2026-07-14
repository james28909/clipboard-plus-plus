#pragma once

#include "ClipboardHistory.h"
#include "../app/ConfigStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

class ClipboardDatabase;
struct ClipboardVaultEntry;
struct NamedClipboardSlot;
struct RegexTransformDefinition;
struct PasteTemplateDefinition;

class ClipboardProfileManager {
public:
    using SaveConfigCallback = std::function<void()>;
    using EventCallback = std::function<void(const std::string&)>;
    using ForegroundProcessCallback = std::function<std::string()>;
    using ActiveHistoryCallback = std::function<void(ClipboardHistory*)>;

    ClipboardProfileManager(AppConfig& config,
                            SaveConfigCallback saveConfig,
                            EventCallback addEvent,
                            ForegroundProcessCallback foregroundProcess,
                            ActiveHistoryCallback activeHistoryChanged);
    ~ClipboardProfileManager();

    void Rebuild();
    void SetNewItemsAtTop(bool value);
    void SetDeduplicationEnabled(bool enabled);
    void SetHistoryLimit(int value);
    bool SearchVault(const std::string& query,
                     std::vector<ClipboardVaultEntry>& entries) const;
    bool PromoteVaultItem(int64_t archiveId);
    bool DeleteVaultItem(int64_t archiveId);
    size_t VaultCount() const;
    void ApplyVaultLimit();
    std::unordered_set<std::string> ReferencedImageIds() const;
    bool LoadNamedSlots(std::vector<NamedClipboardSlot>& slots) const;
    bool SaveNamedSlot(NamedClipboardSlot& slot);
    bool DeleteNamedSlot(int64_t slotId);
    bool LoadRegexTransforms(std::vector<RegexTransformDefinition>& transforms) const;
    bool SaveRegexTransform(RegexTransformDefinition& transform);
    bool DeleteRegexTransform(int64_t transformId);
    bool LoadPasteTemplates(std::vector<PasteTemplateDefinition>& templates) const;
    bool SavePasteTemplate(PasteTemplateDefinition& value);
    bool DeletePasteTemplate(int64_t templateId);

    ClipboardHistory* ActiveHistory() const;
    ClipboardHistory* HistoryForProfile(const std::string& profileId) const;
    const ClipboardProfileConfig* ActiveProfile() const;
    const std::vector<ClipboardProfileConfig>& Profiles() const;
    bool CanDeleteActiveProfile() const;
    const std::vector<std::string>& PersistenceErrors() const {
        return m_persistenceErrors;
    }

    void SetActiveProfile(const std::string& id);
    void SelectProfileSlot(int slot);
    void CreateProfile(const std::string& name,
                       const std::string& processName = {});
    void RenameActiveProfile(const std::string& name);
    bool DeleteActiveProfile();

    void CreateFromForegroundProcess(const std::string& processName);
    void BindActiveToProcess(const std::string& processName);
    void SwitchForProcess(const std::string& processName);

    void SaveHistory(const std::string& profileId) const;
    void SaveActiveHistory() const;

private:
    ClipboardProfileConfig* FindForProcess(const std::string& processName);
    void LoadAndConfigureHistory(const std::string& profileId,
                                 ClipboardHistory& history);
    void NotifyActiveHistory();
    bool InitializeDatabase();
    bool SaveProfile(const ClipboardProfileConfig& profile);
    void ConfigureOverflow(const std::string& profileId,
                           ClipboardHistory& history);

    AppConfig& m_config;
    SaveConfigCallback m_saveConfig;
    EventCallback m_addEvent;
    ForegroundProcessCallback m_foregroundProcess;
    ActiveHistoryCallback m_activeHistoryChanged;
    std::vector<std::unique_ptr<ClipboardHistory>> m_histories;
    std::unique_ptr<ClipboardDatabase> m_database;
    std::vector<std::string> m_persistenceErrors;
    std::string m_manualProcessOverride;
};
