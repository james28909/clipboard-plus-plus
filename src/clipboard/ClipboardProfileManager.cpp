#include "ClipboardProfileManager.h"

#include "ClipboardDatabase.h"
#include "ClipboardHistoryStore.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
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
    ActiveHistoryCallback activeHistoryChanged,
    TimingCallback startupTiming,
    bool safeMode)
    : m_config(config),
      m_saveConfig(std::move(saveConfig)),
      m_addEvent(std::move(addEvent)),
      m_foregroundProcess(std::move(foregroundProcess)),
      m_activeHistoryChanged(std::move(activeHistoryChanged)),
      m_startupTiming(std::move(startupTiming)),
      m_safeMode(safeMode)
{}

void ClipboardProfileManager::EnterSafeMode(const std::string& reason) {
    StopHistorySaveWorker();
    m_database.reset();
    m_safeMode = true;
    const std::string message = reason.empty()
        ? "Safe mode is active; encrypted persistence and clipboard capture are disabled."
        : reason;
    if (std::find(m_persistenceErrors.begin(), m_persistenceErrors.end(), message) ==
        m_persistenceErrors.end())
        m_persistenceErrors.push_back(message);
    if (m_addEvent) m_addEvent(message);
}

bool ClipboardProfileManager::ConsumeBackgroundPersistenceFailure(
    std::string& message) {
    std::lock_guard<std::mutex> lock(m_failureMutex);
    if (m_backgroundPersistenceFailure.empty()) return false;
    message = std::move(m_backgroundPersistenceFailure);
    m_backgroundPersistenceFailure.clear();
    return true;
}

ClipboardProfileManager::~ClipboardProfileManager() {
    StopHistorySaveWorker();
}

void ClipboardProfileManager::Rebuild() {
    const auto rebuildStarted = std::chrono::steady_clock::now();
    StopHistorySaveWorker();
    InitializeProfileMetadata();
    LoadActiveHistory();
    if (m_startupTiming) {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - rebuildStarted).count();
        m_startupTiming("profile manager rebuild total", ms);
    }
}

bool ClipboardProfileManager::InitializeProfileMetadata() {
    m_persistenceErrors.clear();
    if (m_config.clipboards.empty()) {
        const std::string now = NowIsoLocal();
        m_config.clipboards.push_back({"default", "Default", now, now, ""});
        m_config.activeClipboardId = "default";
    }
    if (m_safeMode) {
        EnterSafeMode("Safe mode was requested; encrypted persistence and clipboard capture are disabled.");
        m_histories.clear();
        m_histories.resize(m_config.clipboards.size());
        return false;
    }

    const auto databaseStarted = std::chrono::steady_clock::now();
    const bool databaseReady = InitializeDatabase();
    if (m_startupTiming) {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - databaseStarted).count();
        m_startupTiming("encrypted clipboard DB + profile metadata", ms);
    }

    m_histories.clear();
    m_histories.resize(m_config.clipboards.size());
    return databaseReady;
}

ClipboardProfileMetadataLoadResult
ClipboardProfileManager::LoadProfileMetadataDetached(
    const std::filesystem::path& databasePath) {
    ClipboardProfileMetadataLoadResult result;
    const auto started = std::chrono::steady_clock::now();
    result.database = std::make_shared<ClipboardDatabase>();
    if (!result.database->Open(databasePath, &result.error) ||
        !result.database->LoadProfiles(result.profiles) ||
        result.profiles.empty() || !result.database->IntegrityCheck()) {
        if (result.error.empty())
            result.error = result.profiles.empty()
                ? "encrypted clipboard database has no readable profiles"
                : "encrypted clipboard database failed its integrity check";
        result.database.reset();
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }
    result.database->GetActiveProfileId(result.activeProfileId);
    const auto active = std::find_if(
        result.profiles.begin(), result.profiles.end(),
        [&](const ClipboardProfileConfig& profile) {
            return profile.id == result.activeProfileId;
        });
    if (active == result.profiles.end())
        result.activeProfileId = result.profiles.front().id;
    result.database->SetActiveProfileId(result.activeProfileId);
    result.ok = true;
    result.durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}

bool ClipboardProfileManager::InstallDetachedProfileMetadata(
    ClipboardProfileMetadataLoadResult result) {
    m_persistenceErrors.clear();
    m_histories.clear();
    if (!result.ok || !result.database || result.profiles.empty()) {
        const std::string message = "encrypted clipboard database unavailable" +
            (result.error.empty() ? std::string{} : ": " + result.error);
        m_persistenceErrors.push_back(message);
        if (m_addEvent) m_addEvent(message);
        m_histories.resize(m_config.clipboards.size());
        EnterSafeMode("Safe mode: " + message +
                      ". Clipboard capture and storage writes are disabled until recovery.");
        return false;
    }
    m_config.clipboards = std::move(result.profiles);
    m_config.activeClipboardId = std::move(result.activeProfileId);
    m_database = std::move(result.database);
    m_histories.resize(m_config.clipboards.size());
    StartHistorySaveWorker(ConfigStore::Directory() / "clipboard.db");
    return true;
}

bool ClipboardProfileManager::LoadActiveHistory() {
    const auto started = std::chrono::steady_clock::now();
    ClipboardHistory* history = EnsureHistoryLoaded(m_config.activeClipboardId);
    if (m_startupTiming) {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        m_startupTiming("active history deserialization", ms);
    }
    NotifyActiveHistory();
    return history != nullptr;
}

ClipboardHistoryLoadResult ClipboardProfileManager::LoadHistoryDetached(
    const std::filesystem::path& databasePath,
    const std::string& profileId) {
    ClipboardHistoryLoadResult result;
    const auto started = std::chrono::steady_clock::now();
    result.profileId = profileId;
    ClipboardDatabase database;
    if (!database.Open(databasePath, &result.error)) {
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }
    ClipboardHistory history(kMaxClipboardHistoryItems);
    if (!database.LoadHistory(profileId, history, result.found)) {
        result.error = "could not deserialize encrypted history";
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }
    result.items = history.Snapshot();
    result.nextId = history.NextId();
    result.ok = true;
    result.durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}

bool ClipboardProfileManager::InstallDetachedHistory(
    ClipboardHistoryLoadResult result) {
    if (!result.ok) {
        const std::string message = "Encrypted clipboard database could not load profile " +
            result.profileId + (result.error.empty() ? std::string{} : ": " + result.error);
        m_persistenceErrors.push_back(message);
        if (m_addEvent) m_addEvent(message);
        return false;
    }
    for (size_t i = 0; i < m_config.clipboards.size(); ++i) {
        if (m_config.clipboards[i].id != result.profileId)
            continue;
        auto history = std::make_unique<ClipboardHistory>(m_config.activeHistoryLimit);
        history->SetNewItemsAtTop(m_config.newItemsAtTop);
        history->SetDeduplicationEnabled(m_config.deduplicateHistory);
        history->LoadSnapshot(std::move(result.items), result.nextId);
        ConfigureOverflow(result.profileId, *history);
        history->SetChangedCallback(
            [this, profileId = result.profileId]() { SaveHistory(profileId); });
        m_histories[i] = std::move(history);
        NotifyActiveHistory();
        return true;
    }
    return false;
}

bool ClipboardProfileManager::IsHistoryLoaded(const std::string& profileId) const {
    return LoadedHistoryForProfile(profileId) != nullptr;
}

void ClipboardProfileManager::SetNewItemsAtTop(bool value) {
    for (auto& history : m_histories) {
        if (history)
            history->SetNewItemsAtTop(value);
    }
}

void ClipboardProfileManager::SetDeduplicationEnabled(bool enabled) {
    m_config.deduplicateHistory = enabled;
    for (auto& history : m_histories)
        if (history) history->SetDeduplicationEnabled(enabled);
}

void ClipboardProfileManager::SetHistoryLimit(int value) {
    value = std::clamp(value, 1, kMaxClipboardHistoryItems);
    m_config.activeHistoryLimit = value;
    const bool transaction = m_database && m_database->Begin();
    for (auto& history : m_histories)
        if (history) history->SetMaxItems(value);
    if (transaction && !m_database->Commit())
        m_database->Rollback();
}

bool ClipboardProfileManager::SearchVault(
    const std::string& query, std::vector<ClipboardVaultEntry>& entries) const {
    const auto started = std::chrono::steady_clock::now();
    const bool ok = m_database && m_database->SearchVault(
        m_config.activeClipboardId, query, entries);
    m_lastDatabaseQueryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool ClipboardProfileManager::PromoteVaultItem(int64_t archiveId) {
    if (!m_database) return false;
    ClipboardVaultEntry entry;
    if (!m_database->GetVaultItem(m_config.activeClipboardId, archiveId, entry))
        return false;
    entry.item.id = 0;
    entry.item.pinned = false;
    ClipboardHistory* history = ActiveHistory();
    if (!history) return false;
    history->Push(std::move(entry.item));
    const bool deleted = m_database->DeleteVaultItem(m_config.activeClipboardId, archiveId);
    if (deleted) m_vaultCountCached = false;
    return deleted;
}

bool ClipboardProfileManager::DeleteVaultItem(int64_t archiveId) {
    const bool deleted = m_database &&
        m_database->DeleteVaultItem(m_config.activeClipboardId, archiveId);
    if (deleted) m_vaultCountCached = false;
    return deleted;
}

size_t ClipboardProfileManager::VaultCount() const {
    if (m_vaultCountCached && m_vaultCountProfileId == m_config.activeClipboardId)
        return m_vaultCountCache;
    size_t count = 0;
    const auto started = std::chrono::steady_clock::now();
    if (m_database && m_database->VaultCount(m_config.activeClipboardId, count)) {
        m_vaultCountProfileId = m_config.activeClipboardId;
        m_vaultCountCache = count;
        m_vaultCountCached = true;
    }
    m_lastDatabaseQueryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return count;
}

void ClipboardProfileManager::ApplyVaultLimit() {
    if (!m_database || m_config.vaultUnlimited) return;
    const int64_t bytes = static_cast<int64_t>(m_config.vaultLimitMB) * 1024 * 1024;
    for (const auto& profile : m_config.clipboards)
        m_database->PruneVault(profile.id, bytes);
    m_vaultCountCached = false;
}

std::unordered_set<std::string> ClipboardProfileManager::ReferencedImageIds() const {
    std::unordered_set<std::string> ids;
    if (m_database)
        m_database->ReferencedImageIds(ids);
    return ids;
}

bool ClipboardProfileManager::LoadNamedSlots(
    std::vector<NamedClipboardSlot>& slots) const {
    const auto started = std::chrono::steady_clock::now();
    const bool ok = m_database && m_database->LoadNamedSlots(slots);
    m_lastDatabaseQueryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool ClipboardProfileManager::SaveNamedSlot(NamedClipboardSlot& slot) {
    return m_database && m_database->SaveNamedSlot(slot);
}

bool ClipboardProfileManager::DeleteNamedSlot(int64_t slotId) {
    return m_database && m_database->DeleteNamedSlot(slotId);
}

bool ClipboardProfileManager::LoadRegexTransforms(
    std::vector<RegexTransformDefinition>& transforms) const {
    const auto started = std::chrono::steady_clock::now();
    const bool ok = m_database && m_database->LoadRegexTransforms(transforms);
    m_lastDatabaseQueryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool ClipboardProfileManager::SaveRegexTransform(
    RegexTransformDefinition& transform) {
    return m_database && m_database->SaveRegexTransform(transform);
}

bool ClipboardProfileManager::DeleteRegexTransform(int64_t transformId) {
    return m_database && m_database->DeleteRegexTransform(transformId);
}

bool ClipboardProfileManager::LoadPasteTemplates(
    std::vector<PasteTemplateDefinition>& templates) const {
    const auto started = std::chrono::steady_clock::now();
    const bool ok = m_database && m_database->LoadPasteTemplates(templates);
    m_lastDatabaseQueryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool ClipboardProfileManager::SavePasteTemplate(PasteTemplateDefinition& value) {
    return m_database && m_database->SavePasteTemplate(value);
}

bool ClipboardProfileManager::DeletePasteTemplate(int64_t templateId) {
    return m_database && m_database->DeletePasteTemplate(templateId);
}

bool ClipboardProfileManager::LoadCustomActions(
    std::vector<CustomActionDefinition>& actions) const {
    const auto started = std::chrono::steady_clock::now();
    const bool ok = m_database && m_database->LoadCustomActions(actions);
    m_lastDatabaseQueryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool ClipboardProfileManager::SaveCustomAction(CustomActionDefinition& action) {
    return m_database && m_database->SaveCustomAction(action);
}

bool ClipboardProfileManager::DeleteCustomAction(int64_t actionId,
                                                 std::string* error) {
    if (!m_database) {
        if (error) *error = "The encrypted clipboard database is unavailable.";
        return false;
    }
    return m_database->DeleteCustomAction(actionId, error);
}

ClipboardHistory* ClipboardProfileManager::ActiveHistory() const {
    for (size_t i = 0; i < m_config.clipboards.size() && i < m_histories.size(); ++i) {
        if (m_config.clipboards[i].id == m_config.activeClipboardId)
            return m_histories[i].get();
    }
    return m_histories.empty() ? nullptr : m_histories.front().get();
}

ClipboardHistory* ClipboardProfileManager::HistoryForProfile(
    const std::string& profileId) {
    return EnsureHistoryLoaded(profileId);
}

ClipboardHistory* ClipboardProfileManager::LoadedHistoryForProfile(
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

    if (!EnsureHistoryLoaded(id))
        return;
    m_config.activeClipboardId = id;
    if (m_database)
        m_database->SetActiveProfileId(id);
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
    if (m_database)
        m_database->UpsertProfile(m_config.clipboards.back(),
                                  static_cast<int>(m_config.clipboards.size() - 1));
    auto history = std::make_unique<ClipboardHistory>(m_config.activeHistoryLimit);
    history->SetNewItemsAtTop(m_config.newItemsAtTop);
    history->SetDeduplicationEnabled(m_config.deduplicateHistory);
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

bool ClipboardProfileManager::UpdateProfileDefinition(
    const std::string& id, const std::string& name,
    const std::string& processName) {
    const auto it = std::find_if(m_config.clipboards.begin(),
        m_config.clipboards.end(), [&](const ClipboardProfileConfig& value) {
            return value.id == id;
        });
    if (it == m_config.clipboards.end() || name.empty()) return false;
    it->name = name;
    it->processName = processName;
    it->updatedAt = NowIsoLocal();
    if (!SaveProfile(*it)) return false;
    if (m_saveConfig) m_saveConfig();
    return true;
}

void ClipboardProfileManager::RenameActiveProfile(const std::string& name) {
    if (name.empty())
        return;
    for (ClipboardProfileConfig& profile : m_config.clipboards) {
        if (profile.id == m_config.activeClipboardId) {
            profile.name = name;
            profile.updatedAt = NowIsoLocal();
            SaveProfile(profile);
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
    StopHistorySaveWorker();

    const size_t index = static_cast<size_t>(std::distance(m_config.clipboards.begin(), it));
    m_config.clipboards.erase(it);
    if (index < m_histories.size())
        m_histories.erase(m_histories.begin() + static_cast<std::ptrdiff_t>(index));

    if (m_database)
        m_database->DeleteProfile(deletedId);
    else {
        std::error_code error;
        std::filesystem::remove(ClipboardHistoryStore::PathForProfile(deletedId), error);
        error.clear();
        std::filesystem::remove(ClipboardHistoryStore::LegacyPathForProfile(deletedId), error);
    }

    if (m_database) {
        for (size_t profileIndex = 0; profileIndex < m_config.clipboards.size(); ++profileIndex)
            m_database->UpsertProfile(m_config.clipboards[profileIndex],
                                      static_cast<int>(profileIndex));
    }

    const size_t nextIndex = std::min(index, m_config.clipboards.size() - 1);
    m_config.activeClipboardId = m_config.clipboards[nextIndex].id;
    EnsureHistoryLoaded(m_config.activeClipboardId);
    if (m_database)
        m_database->SetActiveProfileId(m_config.activeClipboardId);
    if (m_database)
        StartHistorySaveWorker(ConfigStore::Directory() / "clipboard.db");
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
            SaveProfile(profile);
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

    if (!EnsureHistoryLoaded(profile->id))
        return;
    m_config.activeClipboardId = profile->id;
    if (m_database)
        m_database->SetActiveProfileId(profile->id);
    NotifyActiveHistory();
    if (m_saveConfig)
        m_saveConfig();
}

void ClipboardProfileManager::SaveHistory(const std::string& profileId) {
    if (m_safeMode) return;
    if (ClipboardHistory* history = LoadedHistoryForProfile(profileId)) {
        if (m_database)
            ScheduleHistorySave(profileId, history);
        else
            ClipboardHistoryStore::Save(profileId, *history);
    }
}

void ClipboardProfileManager::SaveActiveHistory() {
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
    if (m_safeMode) return;
    if (m_database) {
        ConfigureOverflow(profileId, history);
        bool found = false;
        if (!m_database->LoadHistory(profileId, history, found)) {
            const std::string message =
                "Encrypted clipboard database could not load profile " + profileId;
            m_persistenceErrors.push_back(message);
            if (m_addEvent) m_addEvent(message);
            return;
        }
        history.SetChangedCallback([this, profileId]() { SaveHistory(profileId); });
        return;
    }

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

ClipboardHistory* ClipboardProfileManager::EnsureHistoryLoaded(
    const std::string& profileId) {
    for (size_t i = 0; i < m_config.clipboards.size(); ++i) {
        if (m_config.clipboards[i].id != profileId)
            continue;
        if (!m_histories[i]) {
            auto history = std::make_unique<ClipboardHistory>(m_config.activeHistoryLimit);
            history->SetNewItemsAtTop(m_config.newItemsAtTop);
            history->SetDeduplicationEnabled(m_config.deduplicateHistory);
            LoadAndConfigureHistory(profileId, *history);
            m_histories[i] = std::move(history);
        }
        return m_histories[i].get();
    }
    return nullptr;
}

void ClipboardProfileManager::ConfigureOverflow(
    const std::string& profileId, ClipboardHistory& history) {
    if (!m_database) return;
    history.SetOverflowCallback([this, profileId](ClipboardItem item) {
        if (!m_database->ArchiveItem(profileId, item)) {
            const std::string message =
                "could not archive overflow item for profile: " + profileId;
            if (m_addEvent) m_addEvent(message);
            return;
        }
        m_vaultCountCached = false;
        if (!m_config.vaultUnlimited) {
            const int64_t bytes = static_cast<int64_t>(m_config.vaultLimitMB) *
                                  1024 * 1024;
            m_database->PruneVault(profileId, bytes);
        }
    });
}

bool ClipboardProfileManager::InitializeDatabase() {
    m_database.reset();
    auto database = std::make_shared<ClipboardDatabase>();
    std::string error;
    if (!database->Open(ConfigStore::Directory() / "clipboard.db", &error)) {
        if (m_addEvent)
            m_addEvent("encrypted clipboard database unavailable: " + error);
        return false;
    }

    std::vector<ClipboardProfileConfig> storedProfiles;
    if (!database->LoadProfiles(storedProfiles)) {
        if (m_addEvent) m_addEvent("could not read encrypted clipboard profiles");
        return false;
    }

    bool migratedLegacyProfiles = false;
    if (storedProfiles.empty()) {
        if (m_config.profilesStoredInDatabase) {
            if (m_addEvent)
                m_addEvent("encrypted clipboard database has no profiles; refusing legacy fallback");
            return false;
        }
        if (!database->Begin()) return false;
        bool ok = true;
        for (size_t i = 0; i < m_config.clipboards.size() && ok; ++i) {
            ClipboardHistory legacy(kMaxClipboardHistoryItems);
            legacy.SetNewItemsAtTop(m_config.newItemsAtTop);
            const auto result = ClipboardHistoryStore::Load(
                m_config.clipboards[i].id, legacy);
            ok = ClipboardHistoryStore::AllowsPersistence(result) &&
                 database->UpsertProfile(m_config.clipboards[i],
                                         static_cast<int>(i)) &&
                 database->SaveHistory(m_config.clipboards[i].id, legacy);
        }
        ok = ok && database->SetActiveProfileId(m_config.activeClipboardId) &&
             database->IntegrityCheck();
        if (!ok || !database->Commit()) {
            database->Rollback();
            if (m_addEvent)
                m_addEvent("clipboard database migration failed; legacy history retained");
            return false;
        }
        if (!database->LoadProfiles(storedProfiles) || storedProfiles.empty())
            return false;
        migratedLegacyProfiles = true;
        if (m_addEvent)
            m_addEvent("migrated clipboard profiles and history to encrypted clipboard.db");
    }

    // Only a legacy migration needs an immediate full parse-back verification.
    // Existing encrypted databases are loaded profile-by-profile on demand.
    if (migratedLegacyProfiles) {
        for (const ClipboardProfileConfig& profile : storedProfiles) {
            ClipboardHistory verified(kMaxClipboardHistoryItems);
            bool found = false;
            if (!database->LoadHistory(profile.id, verified, found) || !found) {
                if (m_addEvent)
                    m_addEvent("clipboard database migration verification failed for profile: " +
                               profile.id);
                return false;
            }
        }
    }

    m_config.clipboards = std::move(storedProfiles);
    std::string activeId;
    database->GetActiveProfileId(activeId);
    const auto active = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& profile) { return profile.id == activeId; });
    m_config.activeClipboardId = active != m_config.clipboards.end()
        ? activeId : m_config.clipboards.front().id;
    database->SetActiveProfileId(m_config.activeClipboardId);
    m_database = std::move(database);
    StartHistorySaveWorker(ConfigStore::Directory() / "clipboard.db");
    if (!m_config.profilesStoredInDatabase) {
        m_config.profilesStoredInDatabase = true;
        if (m_saveConfig) m_saveConfig();
    }
    return true;
}

void ClipboardProfileManager::ScheduleHistorySave(
    const std::string& profileId, ClipboardHistory* history) {
    if (!history)
        return;
    {
        std::lock_guard<std::mutex> lock(m_saveMutex);
        if (m_stopSaveWorker || !m_saveThread.joinable())
            return;
        m_pendingSaves[profileId] = history;
        ++m_saveGeneration;
    }
    m_saveCv.notify_all();
}

void ClipboardProfileManager::StartHistorySaveWorker(
    const std::filesystem::path& databasePath) {
    StopHistorySaveWorker();
    {
        std::lock_guard<std::mutex> lock(m_saveMutex);
        m_stopSaveWorker = false;
        m_saveGeneration = 0;
    }
    m_saveThread = std::thread(
        [this, databasePath]() { HistorySaveWorkerMain(databasePath); });
}

void ClipboardProfileManager::StopHistorySaveWorker() {
    {
        std::lock_guard<std::mutex> lock(m_saveMutex);
        if (!m_saveThread.joinable()) {
            m_stopSaveWorker = false;
            return;
        }
        m_stopSaveWorker = true;
        ++m_saveGeneration;
    }
    m_saveCv.notify_all();
    m_saveThread.join();
    std::lock_guard<std::mutex> lock(m_saveMutex);
    m_pendingSaves.clear();
    m_saveInFlight = false;
    m_stopSaveWorker = false;
}

void ClipboardProfileManager::HistorySaveWorkerMain(
    std::filesystem::path databasePath) {
    ClipboardDatabase database;
    std::string error;
    const bool databaseReady = database.Open(databasePath, &error);
    if (!databaseReady) {
        const std::string message =
            "Clipboard++ background history persistence unavailable: " + error + "\n";
        OutputDebugStringA(message.c_str());
        std::lock_guard<std::mutex> failureLock(m_failureMutex);
        m_backgroundPersistenceFailure = message;
    }

    for (;;) {
        std::unordered_map<std::string, ClipboardHistory*> saves;
        {
            std::unique_lock<std::mutex> lock(m_saveMutex);
            m_saveCv.wait(lock, [this]() {
                return m_stopSaveWorker || !m_pendingSaves.empty();
            });
            if (!m_stopSaveWorker) {
                const uint64_t observedGeneration = m_saveGeneration;
                m_saveCv.wait_for(lock, std::chrono::milliseconds(200),
                    [this, observedGeneration]() {
                        return m_stopSaveWorker ||
                               m_saveGeneration != observedGeneration;
                    });
                if (!m_stopSaveWorker && m_saveGeneration != observedGeneration)
                    continue;
            }
            if (m_pendingSaves.empty()) {
                if (m_stopSaveWorker)
                    break;
                continue;
            }
            saves.swap(m_pendingSaves);
            m_saveInFlight = true;
        }

        if (databaseReady) {
            for (const auto& [profileId, history] : saves) {
                if (history && !database.SaveHistory(profileId, *history)) {
                    std::lock_guard<std::mutex> failureLock(m_failureMutex);
                    m_backgroundPersistenceFailure =
                        "Background history save failed for profile " + profileId +
                        ". Safe mode will disable capture and further storage writes.";
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_saveMutex);
            m_saveInFlight = false;
        }
        m_saveCv.notify_all();
    }
    m_saveCv.notify_all();
}

bool ClipboardProfileManager::SaveProfile(const ClipboardProfileConfig& profile) {
    if (!m_database) return false;
    const auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& candidate) { return candidate.id == profile.id; });
    if (it == m_config.clipboards.end()) return false;
    return m_database->UpsertProfile(
        profile, static_cast<int>(std::distance(m_config.clipboards.begin(), it)));
}

void ClipboardProfileManager::NotifyActiveHistory() {
    if (m_activeHistoryChanged)
        m_activeHistoryChanged(ActiveHistory());
}
