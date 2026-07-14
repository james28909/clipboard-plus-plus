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

// -- Sections: General, Popup, and Clipboard profiles ------------------------

void MainWindow::DrawGeneral() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("General", "Configure Windows startup and contextual interface help.");

    static bool startWithWindowsInitialized = false;
    static bool startWithWindows = false;

    if (!startWithWindowsInitialized) {
        startWithWindows = app->IsStartWithWindowsEnabled();
        startWithWindowsInitialized = true;
    }
    if (BeginSettingsCard("##application_startup", "Windows startup",
                          "Choose whether Clipboard++ launches when you sign in.")) {
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

    if (BeginSettingsCard("##application_shortcuts", "Related settings",
                          "Every behavior now has one owner.")) {
        SettingsLinkButton("Clipboard history", SettingsDestination::Clipboard, 0);
        ImGui::SameLine();
        SettingsLinkButton("Popup behavior", SettingsDestination::Popup);
        ImGui::SameLine();
        SettingsLinkButton("Hotkeys", SettingsDestination::Hotkeys);
    }
    EndSettingsCard();
}

void MainWindow::DrawProfiles() {
    Application* app = Application::Get();
    if (!app) return;

    enum class ProfileEditorMode { Closed, Create, Rename };
    static ProfileEditorMode editorMode = ProfileEditorMode::Closed;
    static char profileName[128]{};
    static std::string deleteName;

    const ClipboardProfileConfig* activeProfile = app->GetActiveClipboardProfile();
    if (BeginSettingsCard("##clipboard_profiles", "Clipboard profiles",
                          "Select a saved profile, or reveal the editor to create or rename one.")) {
        ImGui::TextUnformatted("Active profile");
        ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##active_clipboard_profile",
                              activeProfile ? activeProfile->name.c_str() : "(none)")) {
            for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
                const bool selected = activeProfile && profile.id == activeProfile->id;
                std::string label = profile.name;
                if (!profile.processName.empty())
                    label += "  (" + profile.processName + ")";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    app->SetActiveClipboardProfile(profile.id);
                    activeProfile = app->GetActiveClipboardProfile();
                    editorMode = ProfileEditorMode::Closed;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (activeProfile) {
            ImGui::TextDisabled("Bound app: %s",
                activeProfile->processName.empty() ? "(none)" : activeProfile->processName.c_str());
            ImGui::TextDisabled("Updated: %s", activeProfile->updatedAt.c_str());
        }

        if (BlueButton("New profile", 120.0f)) {
            editorMode = ProfileEditorMode::Create;
            profileName[0] = '\0';
        }
        ImGui::SameLine();
        if (!activeProfile) ImGui::BeginDisabled();
        if (PaddedButton("Rename", 100.0f)) {
            editorMode = ProfileEditorMode::Rename;
            strncpy_s(profileName, activeProfile->name.c_str(), _TRUNCATE);
        }
        if (!activeProfile) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!app->CanDeleteActiveClipboardProfile()) ImGui::BeginDisabled();
        if (DangerButton("Delete", 100.0f)) {
            deleteName = activeProfile ? activeProfile->name : "profile";
            MessageBeep(MB_ICONQUESTION);
            ImGui::OpenPopup("Delete clipboard profile");
        }
        if (!app->CanDeleteActiveClipboardProfile()) ImGui::EndDisabled();

        if (editorMode != ProfileEditorMode::Closed) {
            ImGui::Separator();
            ImGui::TextUnformatted(editorMode == ProfileEditorMode::Create
                ? "New profile name" : "New name");
            ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
            ImGui::InputText("##clipboard_profile_name", profileName, sizeof(profileName));
            const std::string candidate = TrimAscii(profileName);
            bool duplicate = false;
            for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
                const bool isCurrentRename = editorMode == ProfileEditorMode::Rename &&
                    activeProfile && profile.id == activeProfile->id;
                if (!isCurrentRename && EqualsIgnoreCase(profile.name, candidate)) {
                    duplicate = true;
                    break;
                }
            }
            if (candidate.empty())
                StatusMessage(SettingsStatus::Muted, "Enter a profile name to continue.");
            else if (duplicate)
                StatusMessage(SettingsStatus::Warning, "A profile with this name already exists.");

            if (candidate.empty() || duplicate) ImGui::BeginDisabled();
            if (BlueButton("Save", 90.0f)) {
                if (editorMode == ProfileEditorMode::Create)
                    app->CreateClipboardProfile(candidate);
                else
                    app->RenameActiveClipboardProfile(candidate);
                activeProfile = app->GetActiveClipboardProfile();
                editorMode = ProfileEditorMode::Closed;
            }
            if (candidate.empty() || duplicate) ImGui::EndDisabled();
            ImGui::SameLine();
            if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                editorMode = ProfileEditorMode::Closed;
        }

        if (ImGui::BeginPopupModal("Delete clipboard profile", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Delete the profile '%s' and its active history?", deleteName.c_str());
            ImGui::Spacing();
            if (DangerButton("Delete", 90.0f)) {
                app->DeleteActiveClipboardProfile();
                activeProfile = app->GetActiveClipboardProfile();
                editorMode = ProfileEditorMode::Closed;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
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

    if (BeginSettingsCard("##profile_shortcuts", "Profile shortcuts",
                          "Profile switching shortcuts are configured with the other global hotkeys.")) {
        SettingsLinkButton("Open profile hotkeys", SettingsDestination::Hotkeys);
    }
    EndSettingsCard();

}

void MainWindow::DrawPopupSettings() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("Popup", "Control how the quick-paste popup closes and what happens after a paste.");

    if (BeginSettingsCard("##popup_window_behavior", "Window behavior",
                          "Choose when the popup should dismiss itself.")) {
        bool hidePopupOnOutsideClick = app->GetHidePopupOnOutsideClick();
        if (ImGui::Checkbox("Close when clicking outside the popup", &hidePopupOnOutsideClick))
            app->SetHidePopupOnOutsideClick(hidePopupOnOutsideClick);
        StatusMessage(SettingsStatus::Muted,
                      "The popup remains non-activating and returns input to the calling application.");
    }
    EndSettingsCard();

    if (BeginSettingsCard("##popup_paste_behavior", "After paste",
                          "Apply these rules after Clipboard++ sends an item to the target application.")) {
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
            "Keep item in place", "Move item to top", "Move item to bottom"
        };
        ImGui::SetNextItemWidth(std::min(320.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::Combo("History position", &moveMode, modes, IM_ARRAYSIZE(modes))) {
            ClipboardHistory::MoveTarget target = ClipboardHistory::MoveTarget::None;
            if (moveMode == 1) target = ClipboardHistory::MoveTarget::Top;
            if (moveMode == 2) target = ClipboardHistory::MoveTarget::Bottom;
            app->SetPasteMoveTarget(target);
        }
    }
    EndSettingsCard();

    if (BeginSettingsCard("##popup_related", "Related settings",
                          "Visual effects and keyboard routes stay with their dedicated owners.")) {
        SettingsLinkButton("Popup appearance", SettingsDestination::Appearance);
        ImGui::SameLine();
        SettingsLinkButton("Popup hotkeys", SettingsDestination::Hotkeys);
        ImGui::SameLine();
        SettingsLinkButton("Capture rules", SettingsDestination::Clipboard, 2);
    }
    EndSettingsCard();
}
