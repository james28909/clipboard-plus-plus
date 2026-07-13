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

// -- Section: Developer -------------------------------------------------------

#ifndef NDEBUG
void MainWindow::DrawDeveloper() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Developer Mode");
    ImGui::Separator();
    ImGui::Spacing();

    DeveloperSettings dev = app->GetDeveloperSettings();
    bool changed = false;

    changed |= ImGui::Checkbox("Enable Developer Mode", &dev.enabled);
    if (changed)
        app->SetDeveloperSettings(dev);

    if (!dev.enabled) {
        ImGui::Spacing();
        ImGui::TextDisabled("Developer tools are hidden until Developer Mode is enabled.");
        return;
    }

    ImGui::Spacing();
    changed |= ImGui::Checkbox("Enable CLI interface", &dev.cliEnabled);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("When off, runtime CLI commands are blocked. Help, status, config, and --set still work.");
    changed |= ImGui::Checkbox("Show source process metadata", &dev.showSourceProcess);
    changed |= ImGui::Checkbox("Enable developer event log", &dev.eventLogEnabled);

    if (changed)
        app->SetDeveloperSettings(dev);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Advanced Clipboard Routing");

    if (ImGui::SmallButton("Bind active clipboard to focused app"))
        app->BindActiveClipboardToForegroundProcess();

    bool autoSwitch = app->GetAutoSwitchClipboardByProcess();
    if (ImGui::Checkbox("Auto-switch clipboard by focused app", &autoSwitch))
        app->SetAutoSwitchClipboardByProcess(autoSwitch);

    bool autoCreate = app->GetAutoCreateClipboardByProcess();
    if (ImGui::Checkbox("Auto-create clipboard for focused app", &autoCreate))
        app->SetAutoCreateClipboardByProcess(autoCreate);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Runtime Diagnostics");
    const ClipboardProfileConfig* active = app->GetActiveClipboardProfile();
    ClipboardHistory* hist = app->GetHistory();
    const SIZE livePopupSize = app->PopupCurrentSize();
    ImGui::Text("Process ID: %lu", static_cast<unsigned long>(app->ProcessId()));
    ImGui::TextWrapped("Executable: %s    Popup: %ldx%ld",
                       app->ExecutablePath().c_str(),
                       static_cast<long>(livePopupSize.cx),
                       static_cast<long>(livePopupSize.cy));
    ImGui::TextWrapped("Working directory: %s", app->WorkingDirectory().c_str());
    ImGui::Text("Foreground process: %s", app->ForegroundProcessName().c_str());
    ImGui::Text("Active clipboard: %s", active ? active->name.c_str() : "(none)");
    ImGui::Text("Clipboard ID: %s", active ? active->id.c_str() : "(none)");
    ImGui::Text("Bound process: %s",
                active && !active->processName.empty() ? active->processName.c_str() : "(none)");
    ImGui::Text("History items: %zu", hist ? hist->Size() : 0);
    ImGui::Text("Pinned items: %zu", hist ? hist->PinnedSize() : 0);
    ImGui::Text("Config: %s", ConfigStore::Path().string().c_str());
    ImGui::Text("Fonts: %s", ConfigStore::FontsDirectory().string().c_str());
    if (ImGui::SmallButton("Toggle debug output"))
        app->ToggleDebugWindow();
    ImGui::SameLine();
    static std::filesystem::path lastIconDumpPath;
    if (ImGui::SmallButton("Dump current icons")) {
        lastIconDumpPath = DumpCurrentIcons();
        if (!lastIconDumpPath.empty())
            ShellExecuteW(nullptr, L"open", lastIconDumpPath.wstring().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (!lastIconDumpPath.empty()) {
        ImGui::TextWrapped("Icon dump: %s", lastIconDumpPath.string().c_str());
    }

    if (dev.enabled && hist && hist->Size() > 0) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Clipboard Item Inspector");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##dev_item_inspector", {-1.0f, 210.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
            const size_t limit = std::min<size_t>(hist->Size(), 25);
            for (size_t i = 0; i < limit; ++i) {
                const ClipboardItem* item = hist->Get(i);
                if (!item) continue;
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::to_string(i + 1) + "  " + item->Preview(64);
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::Text("ID: %llu", static_cast<unsigned long long>(item->id));
                    ImGui::Text("Hash: %llu", static_cast<unsigned long long>(item->contentHash));
                    ImGui::Text("Type: %s", ContentTypeName(item->type));
                    ImGui::TextWrapped("Tags: %s", TagList(item->tags).c_str());
                    ImGui::Text("Pinned: %s", item->pinned ? "yes" : "no");
                    ImGui::Text("Source: %s",
                                item->sourceProcess.empty() ? "(unknown)" : item->sourceProcess.c_str());
                    ImGui::Text("Captured: %s", TimeLabel(item->timestamp).c_str());
                    ImGui::Text("Created: %s", TimeLabel(item->createdAt).c_str());
                    ImGui::Text("Updated: %s", TimeLabel(item->updatedAt).c_str());
                    ImGui::Text("Last used: %s", TimeLabel(item->lastUsedAt).c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            SmoothScrollCurrentWindow("dev_item_inspector", 72.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    if (dev.enabled) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Developer Event Log");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy log")) {
            std::string text;
            for (const std::string& line : app->GetDeveloperEvents()) {
                text += line;
                text += "\n";
            }
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear log"))
            app->ClearDeveloperEvents();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##dev_event_log", {-1.0f, 180.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
            const auto& events = app->GetDeveloperEvents();
            if (!dev.eventLogEnabled) {
                ImGui::TextDisabled("  Enable developer event log to collect new events.");
            } else if (events.empty()) {
                ImGui::TextDisabled("  No developer events yet.");
            } else {
                for (const std::string& line : events)
                    ImGui::Selectable(line.c_str(), false);
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            SmoothScrollCurrentWindow("dev_event_log", 72.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::TextDisabled("Still planned: regex transforms, named slots, raw bytes viewer, pretty-print, exports.");
    }
}
#endif
