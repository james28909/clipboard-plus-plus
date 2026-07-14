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
    ImGui::TextDisabled("History");
    ImGui::Separator();
    ImGui::Spacing();

    if (app) {
        for (const std::string& error : app->GetHistoryPersistenceErrors()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
        }
        if (!app->GetHistoryPersistenceErrors().empty())
            ImGui::Spacing();
    }

    int activeLimit = app ? app->GetActiveHistoryLimit() : kMaxClipboardHistoryItems;
    static bool persistHistory = true;
    static bool sessionOnly    = false;

    ImGui::Text("Active history size (items)");
    ImGui::SetNextItemWidth(120.0f);
    if (SliderIntWheel("##active", &activeLimit, 1,
                       kMaxClipboardHistoryItems, "%d", 1) && app)
        app->SetActiveHistoryLimit(activeLimit);

    bool deduplicateHistory = app ? app->IsHistoryDeduplicationEnabled() : true;
    if (ImGui::Checkbox("Consolidate duplicate clipboard items", &deduplicateHistory) && app)
        app->SetHistoryDeduplicationEnabled(deduplicateHistory);
    ImGui::TextDisabled(deduplicateHistory
        ? "Repeated content refreshes the existing item instead of adding another copy."
        : "Repeated content is kept as a separate history item each time.");

    ImGui::Spacing();
    ImGui::Checkbox("Persist history across sessions", &persistHistory);
    if (persistHistory) {
        ImGui::Indent();
        ImGui::Checkbox("Session only (clear on exit)", &sessionOnly);
        ImGui::Unindent();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Vault (overflow archive)");
    ImGui::TextDisabled("Items pushed beyond the active-history limit are archived here.");
    bool vaultUnlimited = app ? app->IsVaultUnlimited() : true;
    int vaultLimitMB = app ? app->GetVaultLimitMB() : 256;
    if (ImGui::Checkbox("Unlimited vault size", &vaultUnlimited) && app)
        app->SetVaultLimit(vaultUnlimited, vaultLimitMB);
    if (!vaultUnlimited) {
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Max vault size (MB)", &vaultLimitMB)) {
            vaultLimitMB = std::clamp(vaultLimitMB, 1, 102400);
            if (app) app->SetVaultLimit(false, vaultLimitMB);
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
        cachedVaultEntries = app ? app->SearchVault(query)
                                 : std::vector<ClipboardVaultEntry>{};
    }

    ImGui::TextDisabled("%zu archived item%s%s", vaultCount,
                        vaultCount == 1 ? "" : "s",
                        cachedVaultEntries.size() < vaultCount && !query.empty()
                            ? " (filtered)" : "");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 8.0f});
    if (ImGui::BeginChild("##vaultitems", {-1.0f, 190.0f},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        if (cachedVaultEntries.empty()) {
            ImGui::TextDisabled(query.empty()
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
    // -- Live history preview --------------------------------------------------
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ClipboardHistory* hist = Application::Get()->GetHistory();
    size_t count = hist ? hist->Size() : 0;
    ImGui::Text("Live history  (%zu / %d items)", count, activeLimit);
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = Application::Get()->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##histlive", {-1.0f, 220.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
        if (count == 0) {
            ImGui::TextDisabled("  Nothing captured yet - copy something!");
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
