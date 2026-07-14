#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "ImGuiWidgets.h"
#include "../app/Application.h"
#include "../app/ConfigStore.h"
#include "../app/TrayIcon.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"
#include "../filters/CustomFilter.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>


using namespace MainWindowInternal;

// -- Section: History ---------------------------------------------------------

void MainWindow::DrawHistory() {
    Application* app = Application::Get();
    if (!app) return;

    static std::string recoveryStatus;
    static bool recoveryError = false;
    if (app->IsSafeMode()) {
        if (BeginSettingsCard("##history_safe_mode", "Storage safe mode",
                              "Clipboard capture and storage writes are disabled to protect unavailable or damaged data.")) {
            StatusMessage(SettingsStatus::Warning,
                "Clipboard++ is running without opening history or image storage. Existing files have not been deleted or replaced.");
            ImGui::TextWrapped("Restore a known-good encrypted backup from Privacy, inspect the storage folder, retry a normal restart, or retain the unavailable database in a recovery folder and start fresh.");
            if (PaddedButton("Open storage folder", 175.0f))
                ShellExecuteW(nullptr, L"open", ConfigStore::Directory().c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::SameLine();
            if (PaddedButton("Retry normal restart", 175.0f)) {
                recoveryError = !app->RestartForEncryptedRestore();
                if (recoveryError) recoveryStatus = "Could not restart Clipboard++.";
            }
            if (DangerButton("Quarantine storage and start fresh", 285.0f)) {
                const int answer = MessageBoxW(app->GetHwnd(),
                    L"Move the clipboard and image databases, keys, and sidecars into a timestamped recovery folder, then start with new empty encrypted storage?\n\nThe old files will be retained and can be inspected or recovered later.",
                    L"Start with fresh encrypted storage",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (answer == IDYES)
                    recoveryError = !app->QuarantineStorageAndRestart(recoveryStatus);
            }
            if (!recoveryStatus.empty())
                StatusMessage(recoveryError ? SettingsStatus::Error
                                             : SettingsStatus::Success,
                              recoveryStatus.c_str());
        }
        EndSettingsCard();
    }

    if (app) {
        for (const std::string& error : app->GetHistoryPersistenceErrors()) {
            StatusMessage(SettingsStatus::Error, error.c_str());
        }
        if (!app->GetHistoryPersistenceErrors().empty())
            ImGui::Spacing();
    }

    const bool storageDisabled = app->IsSafeMode();
    if (storageDisabled) ImGui::BeginDisabled();

    int activeLimit = app->GetActiveHistoryLimit();

    if (BeginSettingsCard("##history_behavior", "History behavior",
                          "Set ordering, capacity, duplicate handling, and retention.")) {
        ImGui::TextUnformatted("Active history limit");
        ImGui::SetNextItemWidth(160.0f);
        if (SliderIntWheel("##active", &activeLimit, 1,
                           kMaxClipboardHistoryItems, "%d items", 1))
            app->SetActiveHistoryLimit(activeLimit);

        bool newItemsAtTop = app->GetNewItemsAtTop();
        if (ImGui::Checkbox("Place new captures at the top", &newItemsAtTop))
            app->SetNewItemsAtTop(newItemsAtTop);

        bool deduplicateHistory = app->IsHistoryDeduplicationEnabled();
        if (ImGui::Checkbox("Consolidate duplicate items", &deduplicateHistory))
            app->SetHistoryDeduplicationEnabled(deduplicateHistory);
        StatusMessage(SettingsStatus::Muted, deduplicateHistory
            ? "Repeated content refreshes the existing item."
            : "Repeated content creates a separate history item.");

        bool persistHistory = true;
        ImGui::BeginDisabled();
        ImGui::Checkbox("Persist encrypted history across sessions", &persistHistory);
        ImGui::EndDisabled();
        StatusMessage(SettingsStatus::Muted,
                      "Encrypted persistence is currently always enabled. Session-only mode is not available yet.");
    }
    EndSettingsCard();

    if (BeginSettingsCard("##history_vault", "Overflow vault",
                          "Items beyond the active-history limit are archived here.")) {
    bool vaultUnlimited = app->IsVaultUnlimited();
    int vaultLimitMB = app->GetVaultLimitMB();
    if (ImGui::Checkbox("Unlimited vault size", &vaultUnlimited))
        app->SetVaultLimit(vaultUnlimited, vaultLimitMB);
    if (!vaultUnlimited) {
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Max vault size (MB)", &vaultLimitMB)) {
            vaultLimitMB = std::clamp(vaultLimitMB, 1, 102400);
            app->SetVaultLimit(false, vaultLimitMB);
        }
    }

    static char vaultSearch[256]{};
    static std::string cachedProfileId;
    static std::string cachedQuery;
    static size_t cachedVaultCount = static_cast<size_t>(-1);
    static std::vector<ClipboardVaultEntry> cachedVaultEntries;

    ImGui::SetNextItemWidth(-1.0f);
    const bool searchChanged = ImGui::InputTextWithHint(
        "##vaultsearch", "Search archived text, paths, or source applications...",
        vaultSearch, sizeof(vaultSearch));
    const ClipboardProfileConfig* activeProfile = app ? app->GetActiveClipboardProfile() : nullptr;
    const std::string profileId = activeProfile ? activeProfile->id : std::string{};
    const std::string query(vaultSearch);
    const size_t vaultCount = app ? app->GetVaultCount() : 0;
    if (searchChanged || cachedProfileId != profileId || cachedQuery != query ||
        cachedVaultCount != vaultCount) {
        cachedProfileId = profileId;
        cachedQuery = query;
        cachedVaultCount = vaultCount;
        cachedVaultEntries = app->SearchVault(query);
    }

    ImGui::TextDisabled("%zu archived item%s%s", vaultCount,
                        vaultCount == 1 ? "" : "s",
                        cachedVaultEntries.size() < vaultCount && !query.empty()
                            ? " (filtered)" : "");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 8.0f});
    if (ImGui::BeginChild("##vaultitems", {-1.0f, 190.0f},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        if (cachedVaultEntries.empty()) {
            EmptyState(query.empty()
                ? "The vault is empty. Overflow items will appear here."
                : "No archived items match this search.");
        }
        for (const ClipboardVaultEntry& entry : cachedVaultEntries) {
            ImGui::PushID(static_cast<int>(entry.archiveId));
            const float actionsWidth = 150.0f;
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                                   std::max(100.0f, ImGui::GetContentRegionAvail().x - actionsWidth));
            ImGui::TextUnformatted(entry.item.Preview(110).c_str());
            ImGui::PopTextWrapPos();
            if (!entry.item.sourceProcess.empty())
                ImGui::TextDisabled("%s", entry.item.sourceProcess.c_str());
            ImGui::EndGroup();
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - actionsWidth));
            if (ImGui::Button("Restore", {70.0f, 0.0f}) && app) {
                app->PromoteVaultItem(entry.archiveId);
                cachedVaultCount = static_cast<size_t>(-1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete", {70.0f, 0.0f}) && app) {
                app->DeleteVaultItem(entry.archiveId);
                cachedVaultCount = static_cast<size_t>(-1);
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    }
    EndSettingsCard();

    // -- Live history preview --------------------------------------------------
    if (BeginSettingsCard("##history_live_preview", "Live history",
                          "A compact preview of the active profile in stored order.")) {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    size_t count = hist ? hist->Size() : 0;
    ImGui::TextDisabled("%zu / %d items", count, activeLimit);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = Application::Get()->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##histlive", {-1.0f, 220.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
        if (count == 0) {
            EmptyState("Nothing captured yet. Copy something to populate this profile.");
        } else {
            for (size_t i = 0; i < count; ++i) {
                const ClipboardItem* item = hist->Get(i);
                if (!item) break;

                // Slot label: 1-9 then a-z; items beyond slot 'z' show no label
                char slot[4]{};
                if (i < 9)       slot[0] = (char)('1' + (int)i);
                else if (i < 35) slot[0] = (char)('a' + (int)(i - 9));
                ImGui::TextDisabled(" %s ", slot);
                ImGui::SameLine();

                // Inline tag badges
                for (ContentTag t : kDisplayTagOrder) {
                    if (!(item->tags & t)) continue;
                    ImVec4 col = (item->tags & TAG_SECRET)
                        ? ImVec4(1.f, 0.34f, 0.34f, 1.f)
                        : ImVec4(0.4f, 0.7f, 1.0f, 1.f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Text("[%s]", ContentDetector::TagName(t));
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                }

#ifndef NDEBUG
                const DeveloperSettings& dev = Application::Get()->GetDeveloperSettings();
                if (dev.enabled && dev.showSourceProcess && !item->sourceProcess.empty()) {
                    ImGui::TextDisabled("{%s}", item->sourceProcess.c_str());
                    ImGui::SameLine();
                }
#endif

                ImGui::TextUnformatted(item->Preview(72).c_str());
            }
        }
        SmoothScrollCurrentWindow("history_live", 72.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    }
    EndSettingsCard();
    if (storageDisabled) ImGui::EndDisabled();
}
