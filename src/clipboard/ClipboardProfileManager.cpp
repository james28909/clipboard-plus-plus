#include "ClipboardProfileManager.h"

#include "ClipboardDatabase.h"
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

ClipboardProfileManager::~ClipboardProfileManager() = default;

void ClipboardProfileManager::Rebuild() {
    m_persistenceErrors.clear();
    if (m_config.clipboards.empty()) {
        const std::string now = NowIsoLocal();
        m_config.clipboards.push_back({"default", "Default", now, now, ""});
        m_config.activeClipboardId = "default";
    }

    InitializeDatabase();

    m_histories.clear();
    m_histories.reserve(m_config.clipboards.size());
    for (const ClipboardProfileConfig& profile : m_config.clipboards) {
        auto history = std::make_unique<ClipboardHistory>(m_config.activeHistoryLimit);
        history->SetNewItemsAtTop(m_config.newItemsAtTop);
        history->SetDeduplicationEnabled(m_config.deduplicateHistory);
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
    return m_database && m_database->SearchVault(
        m_config.activeClipboardId, query, entries);
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
    return m_database->DeleteVaultItem(m_config.activeClipboardId, archiveId);
}

bool ClipboardProfileManager::DeleteVaultItem(int64_t archiveId) {
    return m_database &&
        m_database->DeleteVaultItem(m_config.activeClipboardId, archiveId);
}

size_t ClipboardProfileManager::VaultCount() const {
    size_t count = 0;
    if (m_database)
        m_database->VaultCount(m_config.activeClipboardId, count);
    return count;
}

void ClipboardProfileManager::ApplyVaultLimit() {
    if (!m_database || m_config.vaultUnlimited) return;
    const int64_t bytes = static_cast<int64_t>(m_config.vaultLimitMB) * 1024 * 1024;
    for (const auto& profile : m_config.clipboards)
        m_database->PruneVault(profile.id, bytes);
}

std::unordered_set<std::string> ClipboardProfileManager::ReferencedImageIds() const {
    std::unordered_set<std::string> ids;
    if (m_database)
        m_database->ReferencedImageIds(ids);
    return ids;
}

bool ClipboardProfileManager::LoadNamedSlots(
    std::vector<NamedClipboardSlot>& slots) const {
    return m_database && m_database->LoadNamedSlots(slots);
}

bool ClipboardProfileManager::SaveNamedSlot(NamedClipboardSlot& slot) {
    return m_database && m_database->SaveNamedSlot(slot);
}

bool ClipboardProfileManager::DeleteNamedSlot(int64_t slotId) {
    return m_database && m_database->DeleteNamedSlot(slotId);
}

bool ClipboardProfileManager::LoadRegexTransforms(
    std::vector<RegexTransformDefinition>& transforms) const {
    return m_database && m_database->LoadRegexTransforms(transforms);
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
    return m_database && m_database->LoadPasteTemplates(templates);
}

bool ClipboardProfileManager::SavePasteTemplate(PasteTemplateDefinition& value) {
    return m_database && m_database->SavePasteTemplate(value);
}

bool ClipboardProfileManager::DeletePasteTemplate(int64_t templateId) {
    return m_database && m_database->DeletePasteTemplate(templateId);
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
    if (m_database)
        m_database->SetActiveProfileId(m_config.activeClipboardId);
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

    m_config.activeClipboardId = profile->id;
    if (m_database)
        m_database->SetActiveProfileId(profile->id);
    NotifyActiveHistory();
    if (m_saveConfig)
        m_saveConfig();
}

void ClipboardProfileManager::SaveHistory(const std::string& profileId) const {
    if (ClipboardHistory* history = HistoryForProfile(profileId)) {
        if (m_database)
            m_database->SaveHistory(profileId, *history);
        else
            ClipboardHistoryStore::Save(profileId, *history);
    }
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
        m_database->SaveHistory(profileId, history);
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
        if (!m_config.vaultUnlimited) {
            const int64_t bytes = static_cast<int64_t>(m_config.vaultLimitMB) *
                                  1024 * 1024;
            m_database->PruneVault(profileId, bytes);
        }
    });
}

bool ClipboardProfileManager::InitializeDatabase() {
    auto database = std::make_unique<ClipboardDatabase>();
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
        if (m_addEvent)
            m_addEvent("migrated clipboard profiles and history to encrypted clipboard.db");
    }

    // Do not make the database authoritative until every stored history payload
    // can be parsed back into the in-memory model. The legacy files remain
    // untouched, so a failed verification can still fall back safely.
    for (const ClipboardProfileConfig& profile : storedProfiles) {
        ClipboardHistory verified(kMaxClipboardHistoryItems);
        bool found = false;
        if (!database->LoadHistory(profile.id, verified, found) || !found) {
            if (m_addEvent)
                m_addEvent("clipboard database verification failed for profile: " +
                           profile.id);
            return false;
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
    if (!m_config.profilesStoredInDatabase) {
        m_config.profilesStoredInDatabase = true;
        if (m_saveConfig) m_saveConfig();
    }
    return true;
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
