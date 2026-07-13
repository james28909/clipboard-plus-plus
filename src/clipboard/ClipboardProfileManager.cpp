#include "ClipboardProfileManager.h"

#include "ClipboardHistoryStore.h"

#include <windows.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

std::string NowIsoLocal() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string MakeClipboardId() {
    static unsigned int sequence = 0;
    std::ostringstream out;
    out << "cb-" << std::hex << GetTickCount64() << "-" << ++sequence;
    return out.str();
}

} // namespace

ClipboardProfileManager::ClipboardProfileManager(
    AppConfig& config,
    SaveConfigCallback saveConfig,
    EventCallback addEvent,
    ForegroundProcessCallback foregroundProcess,
    ActiveHistoryCallback activeHistoryChanged)
    : m_config(config),
      m_saveConfig(std::move(saveConfig)),
      m_addEvent(std::move(addEvent)),
      m_foregroundProcess(std::move(foregroundProcess)),
      m_activeHistoryChanged(std::move(activeHistoryChanged))
{}

void ClipboardProfileManager::Rebuild() {
    m_persistenceErrors.clear();
    if (m_config.clipboards.empty()) {
        const std::string now = NowIsoLocal();
        m_config.clipboards.push_back({"default", "Default", now, now, ""});
        m_config.activeClipboardId = "default";
    }

    m_histories.clear();
    m_histories.reserve(m_config.clipboards.size());
    for (const ClipboardProfileConfig& profile : m_config.clipboards) {
        auto history = std::make_unique<ClipboardHistory>(kMaxClipboardHistoryItems);
        history->SetNewItemsAtTop(m_config.newItemsAtTop);
        LoadAndConfigureHistory(profile.id, *history);
        m_histories.push_back(std::move(history));
    }

    NotifyActiveHistory();
}

void ClipboardProfileManager::SetNewItemsAtTop(bool value) {
    for (auto& history : m_histories) {
        if (history)
            history->SetNewItemsAtTop(value);
    }
}

ClipboardHistory* ClipboardProfileManager::ActiveHistory() const {
    for (size_t i = 0; i < m_config.clipboards.size() && i < m_histories.size(); ++i) {
        if (m_config.clipboards[i].id == m_config.activeClipboardId)
            return m_histories[i].get();
    }
    return m_histories.empty() ? nullptr : m_histories.front().get();
}

ClipboardHistory* ClipboardProfileManager::HistoryForProfile(
    const std::string& profileId) const {
    for (size_t i = 0; i < m_config.clipboards.size() && i < m_histories.size(); ++i) {
        if (m_config.clipboards[i].id == profileId)
            return m_histories[i].get();
    }
    return nullptr;
}

const ClipboardProfileConfig* ClipboardProfileManager::ActiveProfile() const {
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& profile) {
            return profile.id == m_config.activeClipboardId;
        });
    return it == m_config.clipboards.end() ? nullptr : &(*it);
}

const std::vector<ClipboardProfileConfig>& ClipboardProfileManager::Profiles() const {
    return m_config.clipboards;
}

bool ClipboardProfileManager::CanDeleteActiveProfile() const {
    return m_config.clipboards.size() > 1;
}

void ClipboardProfileManager::SetActiveProfile(const std::string& id) {
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& profile) { return profile.id == id; });
    if (it == m_config.clipboards.end())
        return;

    m_config.activeClipboardId = id;
    NotifyActiveHistory();
    m_manualProcessOverride = m_foregroundProcess ? m_foregroundProcess() : std::string{};
    if (m_saveConfig)
        m_saveConfig();
    if (m_addEvent)
        m_addEvent("selected clipboard profile: " + it->name + " (" + it->id + ")");
}

void ClipboardProfileManager::SelectProfileSlot(int slot) {
    if (slot < 0)
        return;
    const size_t index = static_cast<size_t>(slot);
    if (index < m_config.clipboards.size())
        SetActiveProfile(m_config.clipboards[index].id);
}

void ClipboardProfileManager::CreateProfile(const std::string& name,
                                            const std::string& processName) {
    const std::string now = NowIsoLocal();
    ClipboardProfileConfig profile;
    profile.id = MakeClipboardId();
    profile.name = name.empty() ? "New Clipboard" : name;
    profile.createdAt = now;
    profile.updatedAt = now;
    profile.processName = processName;

    m_config.clipboards.push_back(std::move(profile));
    auto history = std::make_unique<ClipboardHistory>(kMaxClipboardHistoryItems);
    history->SetNewItemsAtTop(m_config.newItemsAtTop);
    const std::string savedId = m_config.clipboards.back().id;
    LoadAndConfigureHistory(savedId, *history);
    m_histories.push_back(std::move(history));

    m_config.activeClipboardId = savedId;
    NotifyActiveHistory();
    m_manualProcessOverride = m_foregroundProcess ? m_foregroundProcess() : std::string{};
    if (m_saveConfig)
        m_saveConfig();
    SaveActiveHistory();
    if (m_addEvent)
        m_addEvent("created clipboard profile: " + m_config.clipboards.back().name);
}

void ClipboardProfileManager::RenameActiveProfile(const std::string& name) {
    if (name.empty())
        return;
    for (ClipboardProfileConfig& profile : m_config.clipboards) {
        if (profile.id == m_config.activeClipboardId) {
            profile.name = name;
            profile.updatedAt = NowIsoLocal();
            if (m_saveConfig)
                m_saveConfig();
            if (m_addEvent)
                m_addEvent("renamed clipboard profile: " + profile.name);
            return;
        }
    }
}

bool ClipboardProfileManager::DeleteActiveProfile() {
    if (!CanDeleteActiveProfile())
        return false;

    const std::string deletedId = m_config.activeClipboardId;
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& profile) { return profile.id == deletedId; });
    if (it == m_config.clipboards.end())
        return false;

    const size_t index = static_cast<size_t>(std::distance(m_config.clipboards.begin(), it));
    m_config.clipboards.erase(it);
    if (index < m_histories.size())
        m_histories.erase(m_histories.begin() + static_cast<std::ptrdiff_t>(index));

    std::error_code error;
    std::filesystem::remove(ClipboardHistoryStore::PathForProfile(deletedId), error);
    error.clear();
    std::filesystem::remove(ClipboardHistoryStore::LegacyPathForProfile(deletedId), error);

    const size_t nextIndex = std::min(index, m_config.clipboards.size() - 1);
    m_config.activeClipboardId = m_config.clipboards[nextIndex].id;
    NotifyActiveHistory();
    if (m_saveConfig)
        m_saveConfig();
    if (m_addEvent)
        m_addEvent("deleted clipboard profile: " + deletedId);
    return true;
}

void ClipboardProfileManager::CreateFromForegroundProcess(const std::string& processName) {
    if (processName.empty())
        return;
    if (ClipboardProfileConfig* existing = FindForProcess(processName)) {
        SetActiveProfile(existing->id);
        return;
    }
    CreateProfile(processName, processName);
}

void ClipboardProfileManager::BindActiveToProcess(const std::string& processName) {
    if (processName.empty())
        return;
    for (ClipboardProfileConfig& profile : m_config.clipboards) {
        if (profile.id == m_config.activeClipboardId) {
            profile.processName = processName;
            profile.updatedAt = NowIsoLocal();
            if (m_saveConfig)
                m_saveConfig();
            if (m_addEvent)
                m_addEvent("bound active clipboard to process: " + processName);
            return;
        }
    }
}

void ClipboardProfileManager::SwitchForProcess(const std::string& processName) {
    if (!m_config.autoSwitchClipboardByProcess || processName.empty())
        return;

    if (!m_manualProcessOverride.empty()) {
        if (_stricmp(m_manualProcessOverride.c_str(), processName.c_str()) == 0)
            return;
        m_manualProcessOverride.clear();
    }

    ClipboardProfileConfig* profile = FindForProcess(processName);
    if (!profile && m_config.autoCreateClipboardByProcess) {
        CreateProfile(processName, processName);
        return;
    }
    if (!profile || profile->id == m_config.activeClipboardId)
        return;

    m_config.activeClipboardId = profile->id;
    NotifyActiveHistory();
    if (m_saveConfig)
        m_saveConfig();
}

void ClipboardProfileManager::SaveHistory(const std::string& profileId) const {
    if (ClipboardHistory* history = HistoryForProfile(profileId))
        ClipboardHistoryStore::Save(profileId, *history);
}

void ClipboardProfileManager::SaveActiveHistory() const {
    SaveHistory(m_config.activeClipboardId);
}

ClipboardProfileConfig* ClipboardProfileManager::FindForProcess(
    const std::string& processName) {
    if (processName.empty())
        return nullptr;
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& profile) {
            return !profile.processName.empty() &&
                   _stricmp(profile.processName.c_str(), processName.c_str()) == 0;
        });
    return it == m_config.clipboards.end() ? nullptr : &(*it);
}

void ClipboardProfileManager::LoadAndConfigureHistory(
    const std::string& profileId, ClipboardHistory& history) {
    const ClipboardHistoryStore::LoadResult result =
        ClipboardHistoryStore::Load(profileId, history);

    if (ClipboardHistoryStore::AllowsPersistence(result)) {
        history.SetChangedCallback([this, profileId]() { SaveHistory(profileId); });
    } else {
        const std::string message = "History persistence disabled for profile " +
            profileId + ": " + ClipboardHistoryStore::LoadResultName(result);
        m_persistenceErrors.push_back(message);
        if (m_addEvent)
            m_addEvent(message);
    }

    if (m_addEvent && result == ClipboardHistoryStore::LoadResult::Migrated)
        m_addEvent("encrypted history migrated for profile: " + profileId);
    else if (m_addEvent && result == ClipboardHistoryStore::LoadResult::LoadedLegacy)
        m_addEvent("history loaded but encryption migration is still pending: " + profileId);
}

void ClipboardProfileManager::NotifyActiveHistory() {
    if (m_activeHistoryChanged)
        m_activeHistoryChanged(ActiveHistory());
}
