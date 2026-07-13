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

    static int  activeLimit    = kMaxClipboardHistoryItems;
    static bool persistHistory = true;
    static bool sessionOnly    = false;
    static bool vaultUnlimited = true;
    static int  vaultLimitMB   = 0;

    ImGui::Text("Active history size (items)");
    ImGui::SetNextItemWidth(120.0f);
    SliderIntWheel("##active", &activeLimit, 1, kMaxClipboardHistoryItems, "%d", 1);

    ImGui::Spacing();
    ImGui::Checkbox("Persist history across sessions", &persistHistory);
    if (persistHistory) {
        ImGui::Indent();
        ImGui::Checkbox("Session only (clear on exit)", &sessionOnly);
        ImGui::Unindent();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Vault (overflow archive)");
    ImGui::Checkbox("Unlimited vault size", &vaultUnlimited);
    if (!vaultUnlimited) {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Max vault size (MB)", &vaultLimitMB);
        if (vaultLimitMB < 1) vaultLimitMB = 1;
    }
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
