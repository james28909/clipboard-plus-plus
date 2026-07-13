#pragma once

#include "ClipboardHistory.h"
#include "../app/ConfigStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

    void Rebuild();
    void SetNewItemsAtTop(bool value);

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

    AppConfig& m_config;
    SaveConfigCallback m_saveConfig;
    EventCallback m_addEvent;
    ForegroundProcessCallback m_foregroundProcess;
    ActiveHistoryCallback m_activeHistoryChanged;
    std::vector<std::unique_ptr<ClipboardHistory>> m_histories;
    std::vector<std::string> m_persistenceErrors;
    std::string m_manualProcessOverride;
};
