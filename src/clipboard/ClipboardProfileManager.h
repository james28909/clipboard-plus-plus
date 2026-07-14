#pragma once

#include "ClipboardHistory.h"
#include "../app/ConfigStore.h"

#include <functional>
#include <filesystem>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <atomic>

class ClipboardDatabase;
struct ClipboardVaultEntry;
struct NamedClipboardSlot;
struct RegexTransformDefinition;
struct PasteTemplateDefinition;
struct CustomActionDefinition;

struct ClipboardHistoryLoadResult {
    std::string profileId;
    bool ok{false};
    bool found{false};
    std::vector<ClipboardItem> items;
    uint64_t nextId{1};
    double durationMs{0.0};
    std::string error;
};

struct ClipboardProfileMetadataLoadResult {
    bool ok{false};
    std::shared_ptr<ClipboardDatabase> database;
    std::vector<ClipboardProfileConfig> profiles;
    std::string activeProfileId;
    double durationMs{0.0};
    std::string error;
};

class ClipboardProfileManager {
public:
    using SaveConfigCallback = std::function<void()>;
    using EventCallback = std::function<void(const std::string&)>;
    using ForegroundProcessCallback = std::function<std::string()>;
    using ActiveHistoryCallback = std::function<void(ClipboardHistory*)>;
    using TimingCallback = std::function<void(const std::string&, double)>;

    ClipboardProfileManager(AppConfig& config,
                            SaveConfigCallback saveConfig,
                            EventCallback addEvent,
                            ForegroundProcessCallback foregroundProcess,
                            ActiveHistoryCallback activeHistoryChanged,
                            TimingCallback startupTiming = {},
                            bool safeMode = false);
    ~ClipboardProfileManager();

    void Rebuild();
    bool InitializeProfileMetadata();
    bool CanInitializeMetadataAsync() const {
        return !m_safeMode && m_config.profilesStoredInDatabase;
    }
    static ClipboardProfileMetadataLoadResult LoadProfileMetadataDetached(
        const std::filesystem::path& databasePath);
    bool InstallDetachedProfileMetadata(
        ClipboardProfileMetadataLoadResult result);
    bool LoadActiveHistory();
    bool CanLoadHistoryAsync() const { return m_database != nullptr; }
    static ClipboardHistoryLoadResult LoadHistoryDetached(
        const std::filesystem::path& databasePath,
        const std::string& profileId);
    bool InstallDetachedHistory(ClipboardHistoryLoadResult result);
    bool IsHistoryLoaded(const std::string& profileId) const;
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
    bool LoadCustomActions(std::vector<CustomActionDefinition>& actions) const;
    bool SaveCustomAction(CustomActionDefinition& action);
    bool DeleteCustomAction(int64_t actionId);

    ClipboardHistory* ActiveHistory() const;
    ClipboardHistory* HistoryForProfile(const std::string& profileId);
    const ClipboardProfileConfig* ActiveProfile() const;
    const std::vector<ClipboardProfileConfig>& Profiles() const;
    bool CanDeleteActiveProfile() const;
    const std::vector<std::string>& PersistenceErrors() const {
        return m_persistenceErrors;
    }
    bool IsSafeMode() const { return m_safeMode; }
    void EnterSafeMode(const std::string& reason);
    bool ConsumeBackgroundPersistenceFailure(std::string& message);
    double LastDatabaseQueryMs() const { return m_lastDatabaseQueryMs.load(); }

    void SetActiveProfile(const std::string& id);
    void SelectProfileSlot(int slot);
    void CreateProfile(const std::string& name,
                       const std::string& processName = {});
    bool UpdateProfileDefinition(const std::string& id,
                                 const std::string& name,
                                 const std::string& processName);
    void RenameActiveProfile(const std::string& name);
    bool DeleteActiveProfile();

    void CreateFromForegroundProcess(const std::string& processName);
    void BindActiveToProcess(const std::string& processName);
    void SwitchForProcess(const std::string& processName);

    void SaveHistory(const std::string& profileId);
    void SaveActiveHistory();

private:
    ClipboardProfileConfig* FindForProcess(const std::string& processName);
    void LoadAndConfigureHistory(const std::string& profileId,
                                 ClipboardHistory& history);
    ClipboardHistory* EnsureHistoryLoaded(const std::string& profileId);
    ClipboardHistory* LoadedHistoryForProfile(const std::string& profileId) const;
    void NotifyActiveHistory();
    bool InitializeDatabase();
    bool SaveProfile(const ClipboardProfileConfig& profile);
    void ConfigureOverflow(const std::string& profileId,
                           ClipboardHistory& history);
    void ScheduleHistorySave(const std::string& profileId,
                             ClipboardHistory* history);
    void StartHistorySaveWorker(const std::filesystem::path& databasePath);
    void StopHistorySaveWorker();
    void HistorySaveWorkerMain(std::filesystem::path databasePath);

    AppConfig& m_config;
    SaveConfigCallback m_saveConfig;
    EventCallback m_addEvent;
    ForegroundProcessCallback m_foregroundProcess;
    ActiveHistoryCallback m_activeHistoryChanged;
    TimingCallback m_startupTiming;
    std::vector<std::unique_ptr<ClipboardHistory>> m_histories;
    std::shared_ptr<ClipboardDatabase> m_database;
    std::vector<std::string> m_persistenceErrors;
    std::string m_manualProcessOverride;
    mutable bool m_vaultCountCached{false};
    mutable std::string m_vaultCountProfileId;
    mutable size_t m_vaultCountCache{0};
    std::mutex m_saveMutex;
    std::condition_variable m_saveCv;
    std::unordered_map<std::string, ClipboardHistory*> m_pendingSaves;
    std::thread m_saveThread;
    uint64_t m_saveGeneration{0};
    bool m_saveInFlight{false};
    bool m_stopSaveWorker{false};
    bool m_safeMode{false};
    std::mutex m_failureMutex;
    std::string m_backgroundPersistenceFailure;
    mutable std::atomic<double> m_lastDatabaseQueryMs{0.0};
};
