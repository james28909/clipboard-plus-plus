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

// -- Section: General ---------------------------------------------------------

void MainWindow::DrawGeneral() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("General", "Choose how Clipboard++ starts, captures, pastes, and presents everyday controls.");

    static bool startWithWindowsInitialized = false;
    static bool startWithWindows = false;

    if (!startWithWindowsInitialized) {
        startWithWindows = app->IsStartWithWindowsEnabled();
        startWithWindowsInitialized = true;
    }
    if (BeginSettingsCard("##general_startup", "Startup & capture",
                          "Core behavior for launching the app and placing newly captured items.")) {
        if (ImGui::Checkbox("Start with Windows", &startWithWindows)) {
            if (!app->SetStartWithWindowsEnabled(startWithWindows)) {
                startWithWindows = app->IsStartWithWindowsEnabled();
                MessageBoxW(app->GetHwnd(),
                            L"Clipboard++ could not update your Windows startup setting.",
                            L"Start with Windows", MB_OK | MB_ICONERROR);
            }
        }
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        HelpTooltip("Launch Clipboard++ automatically when you sign in to Windows.");

        bool newItemsAtTop = app->GetNewItemsAtTop();
        if (ImGui::Checkbox("Place new items at the top of history", &newItemsAtTop))
            app->SetNewItemsAtTop(newItemsAtTop);
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        HelpTooltip("When off, new items are added to the bottom.");

        bool hidePopupOnOutsideClick = app->GetHidePopupOnOutsideClick();
        if (ImGui::Checkbox("Close the popup when clicking elsewhere", &hidePopupOnOutsideClick))
            app->SetHidePopupOnOutsideClick(hidePopupOnOutsideClick);

        ImGui::TextDisabled(app->IsHistoryDeduplicationEnabled()
            ? "Duplicate clipboard content is currently consolidated (change in History)."
            : "Duplicate clipboard content is currently kept separately (change in History).");
    }
    EndSettingsCard();

    if (BeginSettingsCard("##general_paste", "Paste behavior",
                          "Control what happens after an item is pasted into the target application.")) {
        bool newline = app->GetAppendNewlineAfterPaste();
        if (ImGui::Checkbox("Append a newline after pasted text", &newline))
            app->SetAppendNewlineAfterPaste(newline);

        int moveMode = 0;
        switch (app->GetPasteMoveTarget()) {
        case ClipboardHistory::MoveTarget::Top:    moveMode = 1; break;
        case ClipboardHistory::MoveTarget::Bottom: moveMode = 2; break;
        default:                                   moveMode = 0; break;
        }
        const char* modes[] = {
            "Keep the pasted item in place",
            "Move the pasted item to the top",
            "Move the pasted item to the bottom"
        };
        ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::Combo("After paste", &moveMode, modes, IM_ARRAYSIZE(modes))) {
            ClipboardHistory::MoveTarget target = ClipboardHistory::MoveTarget::None;
            if (moveMode == 1) target = ClipboardHistory::MoveTarget::Top;
            if (moveMode == 2) target = ClipboardHistory::MoveTarget::Bottom;
            app->SetPasteMoveTarget(target);
        }
    }
    EndSettingsCard();

    if (BeginSettingsCard("##general_interface", "Interface help",
                          "Configure the contextual hints shown while learning the application.")) {
        UiSettings ui = app->GetUiSettings();
        bool uiChanged = ImGui::Checkbox("Show helper text", &ui.showHelperText);
        if (ui.showHelperText) {
            ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
            uiChanged |= SliderIntWheel("Helper delay (ms)", &ui.helperDelayMs, 0, 5000, "%d", 50);
            ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
            uiChanged |= SliderIntWheel("Helper duration (ms)", &ui.helperDurationMs, 500, 30000, "%d", 250);
        } else {
            ImGui::TextDisabled("Contextual helper popups are hidden.");
        }
        if (uiChanged) {
            ui.helperDelayMs = std::clamp(ui.helperDelayMs, 0, 5000);
            ui.helperDurationMs = std::clamp(ui.helperDurationMs, 500, 30000);
            app->SetUiSettings(ui);
        }
    }
    EndSettingsCard();

    const bool profileCardVisible = BeginSettingsCard(
        "##general_clipboards", "Clipboard profiles",
        "Select, create, rename, and manage independent clipboard histories.");

    const ClipboardProfileConfig* activeProfile = app->GetActiveClipboardProfile();
    static std::string lastProfileId;
    static char clipboardNameBuf[128]{};
    static bool clipboardDropdownOpen = false;
    enum class PendingClipboardAction { None, Rename, Create, Delete };
    static PendingClipboardAction pendingAction = PendingClipboardAction::None;
    static std::string pendingName;
    if (activeProfile && activeProfile->id != lastProfileId) {
        lastProfileId = activeProfile->id;
        strncpy_s(clipboardNameBuf, activeProfile->name.c_str(), _TRUNCATE);
    }

    auto clipboardNameExists = [&]() {
        const std::string name = TrimAscii(clipboardNameBuf);
        if (name.empty())
            return true;
        for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
            if (EqualsIgnoreCase(profile.name, name))
                return true;
        }
        return false;
    };

    const bool showClipboardSave = !clipboardNameExists();
    const float saveW = ButtonWidthForText("Save", 72.0f);
    const float saveReserve = showClipboardSave ? saveW + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - saveReserve));
    ImGui::InputText("##clipboard_profile_name", clipboardNameBuf, sizeof(clipboardNameBuf));
    const bool clipboardInputHovered = ImGui::IsItemHovered();
    const bool clipboardInputClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 clipboardInputMin = ImGui::GetItemRectMin();
    const ImVec2 clipboardInputMax = ImGui::GetItemRectMax();
    if (clipboardInputClicked) {
        clipboardDropdownOpen = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (clipboardInputHovered && !ImGui::IsItemActive())
        HelpTooltip("Click to select a clipboard or type a name");
    if (showClipboardSave) {
        ImGui::SameLine();
        if (BlueButton("Save", saveW)) {
            std::string name = TrimAscii(clipboardNameBuf);
            if (!name.empty()) {
                app->CreateClipboardProfile(name);
                if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile()) {
                    strncpy_s(clipboardNameBuf, profile->name.c_str(), _TRUNCATE);
                    lastProfileId = profile->id;
                }
                clipboardDropdownOpen = false;
            }
        }
    }

    if (clipboardDropdownOpen) {
        const float dropW = clipboardInputMax.x - clipboardInputMin.x;
        ImGui::SetNextWindowPos({clipboardInputMin.x, clipboardInputMax.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({dropW, 0.0f}, {dropW, 300.0f});
        constexpr ImGuiWindowFlags dropFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::Begin("##clipboard_profile_picker", nullptr, dropFlags);
        const bool dropdownHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
            ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
            const bool selected = activeProfile && profile.id == activeProfile->id;
            std::string label = profile.name;
            if (!profile.processName.empty())
                label += " (" + profile.processName + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                app->SetActiveClipboardProfile(profile.id);
                strncpy_s(clipboardNameBuf, profile.name.c_str(), _TRUNCATE);
                lastProfileId = profile.id;
                clipboardDropdownOpen = false;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::End();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dropdownHovered && !clipboardInputHovered)
            clipboardDropdownOpen = false;
    }

    auto requestConfirmation = [&](PendingClipboardAction action, std::string name) {
        pendingAction = action;
        pendingName = std::move(name);
        MessageBeep(MB_ICONQUESTION);
        ImGui::OpenPopup("Confirm clipboard action");
    };

    std::string typedName = TrimAscii(clipboardNameBuf);
    ImGui::Spacing();
    const float renameW = ButtonWidthForText("Rename", 100.0f);
    const float createW = ButtonWidthForText("New clipboard", 130.0f);
    const float deleteW = ButtonWidthForText("Delete active", 130.0f);
    if (PaddedButton("Rename", renameW)) {
        if (typedName.empty() && activeProfile)
            typedName = activeProfile->name;
        requestConfirmation(PendingClipboardAction::Rename, typedName);
    }
    SameLineIfFits(createW);
    if (PaddedButton("New clipboard", createW)) {
        std::string name = typedName;
        if (name.empty())
            name = "Clipboard " + std::to_string(app->GetClipboardProfiles().size() + 1);
        requestConfirmation(PendingClipboardAction::Create, name);
    }
    SameLineIfFits(deleteW);
    if (!app->CanDeleteActiveClipboardProfile())
        ImGui::BeginDisabled();
    if (DangerButton("Delete active", 130.0f)) {
        requestConfirmation(PendingClipboardAction::Delete,
                            activeProfile ? activeProfile->name : "Clipboard");
    }
    if (!app->CanDeleteActiveClipboardProfile())
        ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Confirm clipboard action", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* actionText = "continue";
        if (pendingAction == PendingClipboardAction::Rename)
            actionText = "rename the active clipboard";
        else if (pendingAction == PendingClipboardAction::Create)
            actionText = "create a new clipboard";
        else if (pendingAction == PendingClipboardAction::Delete)
            actionText = "delete the active clipboard";

        ImGui::TextWrapped("Confirm that you want to %s.", actionText);
        if (!pendingName.empty())
            ImGui::TextDisabled("%s", pendingName.c_str());
        ImGui::Spacing();

        if (PaddedButton("Confirm", 110.0f)) {
            if (pendingAction == PendingClipboardAction::Rename) {
                app->RenameActiveClipboardProfile(pendingName);
                strncpy_s(clipboardNameBuf, pendingName.c_str(), _TRUNCATE);
            } else if (pendingAction == PendingClipboardAction::Create) {
                app->CreateClipboardProfile(pendingName);
                strncpy_s(clipboardNameBuf, pendingName.c_str(), _TRUNCATE);
            } else if (pendingAction == PendingClipboardAction::Delete) {
                app->DeleteActiveClipboardProfile();
                if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile())
                    strncpy_s(clipboardNameBuf, profile->name.c_str(), _TRUNCATE);
            }
            pendingAction = PendingClipboardAction::None;
            pendingName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingAction = PendingClipboardAction::None;
            pendingName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (activeProfile) {
        ImGui::Separator();
        ImGui::TextDisabled("ID: %s", activeProfile->id.c_str());
        ImGui::TextDisabled("Created: %s", activeProfile->createdAt.c_str());
        ImGui::TextDisabled("Updated: %s", activeProfile->updatedAt.c_str());
        ImGui::TextDisabled("Bound app: %s",
                            activeProfile->processName.empty()
                                ? "(none)"
                                : activeProfile->processName.c_str());
    }

    (void)profileCardVisible;
    EndSettingsCard();

    if (BeginSettingsCard("##general_profile_automation", "Profile automation",
                          "Optionally bind and switch clipboard profiles using the application you were working in.")) {
        if (PaddedButton("Bind active profile to the last focused app", 280.0f))
            app->BindActiveClipboardToForegroundProcess();

        bool autoSwitch = app->GetAutoSwitchClipboardByProcess();
        if (ImGui::Checkbox("Automatically switch profiles for bound apps", &autoSwitch))
            app->SetAutoSwitchClipboardByProcess(autoSwitch);

        bool autoCreate = app->GetAutoCreateClipboardByProcess();
        if (ImGui::Checkbox("Create a profile when an unrecognized app is focused", &autoCreate))
            app->SetAutoCreateClipboardByProcess(autoCreate);
        ImGui::TextDisabled("Automatic creation can produce many profiles; leave it off unless that workflow is intentional.");
    }
    EndSettingsCard();

}
