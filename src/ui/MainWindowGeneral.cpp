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

    ImGui::TextDisabled("General");
    ImGui::Separator();
    ImGui::Spacing();

    static bool startWithWindowsInitialized = false;
    static bool startWithWindows = false;
    static bool deduplication = true;

    if (!startWithWindowsInitialized) {
        startWithWindows = app->IsStartWithWindowsEnabled();
        startWithWindowsInitialized = true;
    }
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
    ImGui::Spacing();
    bool newItemsAtTop = app->GetNewItemsAtTop();
    if (ImGui::Checkbox("New items added to top of list", &newItemsAtTop)) {
        app->SetNewItemsAtTop(newItemsAtTop);
    }
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("When off, new items are added to the bottom.");
    ImGui::Spacing();
    bool hidePopupOnOutsideClick = app->GetHidePopupOnOutsideClick();
    if (ImGui::Checkbox("Hide popup when clicking outside it", &hidePopupOnOutsideClick))
        app->SetHidePopupOnOutsideClick(hidePopupOnOutsideClick);
    ImGui::Spacing();
    ImGui::Checkbox("Deduplicate - move existing copy to configured position", &deduplication);

    SectionHeader("Interface");
    UiSettings ui = app->GetUiSettings();
    bool uiChanged = false;
    uiChanged |= ImGui::Checkbox("Show helper text", &ui.showHelperText);
    if (ui.showHelperText) {
        ImGui::SetNextItemWidth(180.0f);
        uiChanged |= SliderIntWheel("Helper delay (ms)", &ui.helperDelayMs, 0, 5000, "%d", 50);
        ImGui::SetNextItemWidth(180.0f);
        uiChanged |= SliderIntWheel("Helper duration (ms)", &ui.helperDurationMs, 500, 30000, "%d", 250);
    } else {
        ImGui::TextDisabled("Inline helper popups are hidden.");
    }
    if (uiChanged) {
        ui.helperDelayMs = std::clamp(ui.helperDelayMs, 0, 5000);
        ui.helperDurationMs = std::clamp(ui.helperDurationMs, 500, 30000);
        app->SetUiSettings(ui);
    }

    SectionHeader("Clipboards");

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
    if (PaddedButton("Set name", 100.0f)) {
        if (typedName.empty() && activeProfile)
            typedName = activeProfile->name;
        requestConfirmation(PendingClipboardAction::Rename, typedName);
    }
    ImGui::SameLine();
    if (PaddedButton("New clipboard", 130.0f)) {
        std::string name = typedName;
        if (name.empty())
            name = "Clipboard " + std::to_string(app->GetClipboardProfiles().size() + 1);
        requestConfirmation(PendingClipboardAction::Create, name);
    }
    ImGui::SameLine();
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
        ImGui::TextDisabled("ID: %s", activeProfile->id.c_str());
        ImGui::TextDisabled("Created: %s", activeProfile->createdAt.c_str());
        ImGui::TextDisabled("Updated: %s", activeProfile->updatedAt.c_str());
        ImGui::TextDisabled("Bound app: %s",
                            activeProfile->processName.empty()
                                ? "(none)"
                                : activeProfile->processName.c_str());
    }

}
