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

// -- Section: Customize -------------------------------------------------------

static bool ColorControl(const char* label, ImVec4& color) {
    ImGui::SetNextItemWidth(220.0f);
    return ImGui::ColorEdit4(label, &color.x,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
}

static bool ColorControlWithReset(const char* label, ImVec4& color, const ImVec4& resetValue) {
    bool changed = ColorControl(label, color);
    ImGui::SameLine();
    std::string id = std::string("Reset##reset_") + label;
    if (ImGui::SmallButton(id.c_str())) {
        color = resetValue;
        changed = true;
    }
    return changed;
}

static void DrawPopupPreview(const AppearanceSettings& draft) {
    AppearanceSettings preview = draft.customColors ? draft : ThemeDefaults(draft.theme);
    const PopupToggleColors toggles = GetPopupToggleColors(draft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = {std::min(360.0f, ImGui::GetContentRegionAvail().x), 168.0f};
    ImVec2 end = {start.x + size.x, start.y + size.y};
    const float pad = 10.0f;
    const ImU32 text = ImGui::GetColorU32(preview.text);
    const ImU32 muted = ImGui::GetColorU32(preview.mutedText);

    dl->AddRectFilled(start, end, ImGui::GetColorU32(preview.windowBg), 6.0f);
    dl->AddRect(start, end, ImGui::GetColorU32(preview.accent), 6.0f, 0, 1.5f);

    ImVec2 closeA = {start.x + pad, start.y + pad};
    ImVec2 closeB = {closeA.x + 24.0f, closeA.y + 24.0f};
    dl->AddRectFilled(closeA, closeB, ImGui::GetColorU32(toggles.off), 3.0f);
    PreviewText(dl, {closeA.x + 8.0f, closeA.y + 4.0f}, {closeB.x - 4.0f, closeB.y}, text, "x");

    ImVec2 comboA = {closeB.x + 10.0f, closeA.y};
    ImVec2 comboB = {end.x - pad, closeB.y};
    dl->AddRectFilled(comboA, comboB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    PreviewText(dl, {comboA.x + 8.0f, comboA.y + 4.0f}, {comboB.x - 8.0f, comboB.y}, text, "Clipboard profile");

    ImVec2 searchA = {start.x + pad, closeB.y + 10.0f};
    ImVec2 searchB = {end.x - pad, searchA.y + 24.0f};
    dl->AddRectFilled(searchA, searchB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    PreviewText(dl, {searchA.x + 8.0f, searchA.y + 4.0f}, {searchB.x - 8.0f, searchB.y}, muted, "Search clipboard...");

    const char* labels[] = {"All", "Text", "Image", "URL"};
    float x = searchA.x;
    float y = searchB.y + 12.0f;
    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i) {
        const float labelW = std::min(58.0f, std::max(42.0f, ImGui::CalcTextSize(labels[i]).x + 16.0f));
        ImVec2 a = {x, y};
        ImVec2 b = {std::min(x + labelW, end.x - pad), y + 22.0f};
        if (b.x <= a.x + 20.0f)
            break;
        const ImU32 col = ImGui::GetColorU32(i == 0 ? toggles.on : toggles.off);
        dl->AddRectFilled(a, b, col, 3.0f);
        PreviewText(dl, {a.x + 8.0f, a.y + 3.0f}, {b.x - 6.0f, b.y}, text, labels[i]);
        x = b.x + 6.0f;
    }

    ImVec2 rowA = {start.x + pad, y + 34.0f};
    ImVec2 rowB = {end.x - pad - 10.0f, rowA.y + 28.0f};
    dl->AddRectFilled(rowA, rowB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    dl->AddCircleFilled({rowA.x + 10.0f, rowA.y + 14.0f}, 3.0f, IM_COL32(255, 196, 64, 255), 12);
    PreviewText(dl, {rowA.x + 20.0f, rowA.y + 6.0f}, {rowB.x - 8.0f, rowB.y}, text, "1  Pinned item preview");

    ImVec2 row2A = {rowA.x, rowB.y + 6.0f};
    ImVec2 row2B = {rowB.x, row2A.y + 28.0f};
    dl->AddRectFilled(row2A, row2B, ImGui::GetColorU32(preview.panelBg), 3.0f);
    PreviewText(dl, {row2A.x + 20.0f, row2A.y + 6.0f}, {row2B.x - 8.0f, row2B.y}, text, "2  History item preview");

    ImVec2 scrollA = {end.x - pad - 6.0f, searchB.y + 8.0f};
    ImVec2 scrollB = {end.x - pad, row2B.y};
    dl->AddRectFilled(scrollA, scrollB, ImGui::GetColorU32(preview.scrollbarBg), 3.0f);
    dl->AddRectFilled({scrollA.x, scrollA.y + 10.0f}, {scrollB.x, scrollA.y + 42.0f},
                      ImGui::GetColorU32(preview.scrollbarGrab), 3.0f);

    ImGui::Dummy(size);
}

static void DrawSettingsPreview(const AppearanceSettings& draft) {
    AppearanceSettings preview = draft.customColors ? draft : ThemeDefaults(draft.theme);
    const PopupToggleColors toggles = GetPopupToggleColors(draft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = {std::min(360.0f, ImGui::GetContentRegionAvail().x), 148.0f};
    ImVec2 end = {start.x + size.x, start.y + size.y};
    const ImU32 text = ImGui::GetColorU32(preview.text);
    const ImU32 muted = ImGui::GetColorU32(preview.mutedText);

    dl->AddRectFilled(start, end, ImGui::GetColorU32(preview.windowBg), 0.0f);
    dl->AddRectFilled(start, {end.x, start.y + 30.0f}, ImGui::GetColorU32(preview.panelBg), 0.0f);
    PreviewText(dl, {start.x + 12.0f, start.y + 7.0f}, {end.x - 12.0f, start.y + 28.0f}, text, "Clipboard++ Settings");

    ImVec2 sideA = {start.x + 10.0f, start.y + 38.0f};
    ImVec2 sideB = {std::min(sideA.x + 104.0f, end.x - 130.0f), end.y - 12.0f};
    dl->AddRectFilled(sideA, sideB, ImGui::GetColorU32(preview.panelBg), 4.0f);
    dl->AddRectFilled({sideA.x + 8.0f, sideA.y + 10.0f}, {sideB.x - 8.0f, sideA.y + 34.0f},
                      ImGui::GetColorU32(toggles.on), 3.0f);
    PreviewText(dl, {sideA.x + 14.0f, sideA.y + 14.0f}, {sideB.x - 10.0f, sideA.y + 32.0f}, text, "Customize");
    PreviewText(dl, {sideA.x + 14.0f, sideA.y + 44.0f}, {sideB.x - 10.0f, sideA.y + 62.0f}, muted, "General");

    ImVec2 paneA = {sideB.x + 14.0f, sideA.y};
    ImVec2 paneB = {end.x - 12.0f, sideB.y};
    dl->AddRectFilled(paneA, paneB, ImGui::GetColorU32(preview.panelBg), 4.0f);
    PreviewText(dl, {paneA.x + 12.0f, paneA.y + 12.0f}, {paneB.x - 12.0f, paneA.y + 32.0f}, text, "Theme selector");
    ImVec2 inputA = {paneA.x + 12.0f, paneA.y + 42.0f};
    ImVec2 inputB = {paneB.x - 12.0f, inputA.y + 24.0f};
    dl->AddRectFilled(inputA, inputB, ImGui::GetColorU32(preview.windowBg), 3.0f);
    PreviewText(dl, {inputA.x + 8.0f, inputA.y + 4.0f}, {inputB.x - 8.0f, inputB.y}, text, "Custom theme");
    ImVec2 buttonA = {paneA.x + 12.0f, inputB.y + 10.0f};
    ImVec2 buttonB = {std::min(buttonA.x + 96.0f, paneB.x - 12.0f), buttonA.y + 24.0f};
    dl->AddRectFilled(buttonA, buttonB, ImGui::GetColorU32(toggles.off), 3.0f);
    PreviewText(dl, {buttonA.x + 10.0f, buttonA.y + 4.0f}, {buttonB.x - 8.0f, buttonB.y}, text, "Save");
    ImGui::Dummy(size);
}

void MainWindow::DrawAppearance() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("Customize", "Personalize themes, popup effects, window layout, fonts, icons, and colors.");

    static AppearanceSettings draft = app->GetAppearance();
    static char fontPath[512]{};
    static char iconPathBuf[512]{};
    static char customName[96] = "Custom";
    static bool initialized = false;
    static bool themeDropdownOpen = false;
    if (!initialized) {
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        std::snprintf(iconPathBuf, sizeof(iconPathBuf), "%s", draft.exeIconPath.c_str());
        std::snprintf(customName, sizeof(customName), "%s",
                      draft.customColors ? draft.customThemeName.c_str() : ThemeName(draft.theme));
        initialized = true;
    }

    auto syncThemeName = [&]() {
        std::snprintf(customName, sizeof(customName), "%s",
                      draft.customColors ? draft.customThemeName.c_str() : ThemeName(draft.theme));
    };
    auto applyBuiltInTheme = [&](ThemeId theme) {
        AppearanceSettings next = ThemeDefaults(theme);
        const AppearanceSettings current = app->GetAppearance();
        next.fontPath = current.fontPath;
        next.fontSize = current.fontSize;
        next.uiScale = 1.0f;
        next.popupOpacity = current.popupOpacity;
        next.popupOutlineStrength = current.popupOutlineStrength;
        next.popupOutlineEffect = current.popupOutlineEffect;
        next.popupOutlineAnimated = current.popupOutlineAnimated;
        next.popupOutlineAnimationSpeed = current.popupOutlineAnimationSpeed;
        next.popupOutlineColorSharpness = current.popupOutlineColorSharpness;
        next.popupOutlineColorSpread = current.popupOutlineColorSpread;
        next.popupOutlineSaturation = current.popupOutlineSaturation;
        next.popupOutlineBrightness = current.popupOutlineBrightness;
        next.popupOutlineReverse = current.popupOutlineReverse;
        next.popupWidth = current.popupWidth;
        next.popupHeight = current.popupHeight;
        next.mainWindowWidth = current.mainWindowWidth;
        next.mainWindowHeight = current.mainWindowHeight;
        next.showScrollbars = current.showScrollbars;
        next.scrollbarSize = current.scrollbarSize;
        next.scrollbarRounding = current.scrollbarRounding;
        next.scrollbarPadding = current.scrollbarPadding;
        next.popupRounding = current.popupRounding;
        next.popupButtonRowPadding = current.popupButtonRowPadding;
        next.popupButtonColumnPadding = current.popupButtonColumnPadding;
        next.controlRounding = current.controlRounding;
        next.savedThemes  = current.savedThemes;
        next.exeIconPath  = current.exeIconPath;
        next.customColors = false;
        next.customThemeName = ThemeName(theme);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto applyUserTheme = [&](const SavedAppearanceTheme& saved) {
        AppearanceSettings next = app->GetAppearance();
        ApplySavedTheme(next, saved);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto saveUserTheme = [&](const std::string& requestedName) {
        AppearanceSettings next = draft;
        next.savedThemes = app->GetAppearance().savedThemes;
        next.fontPath = app->GetAppearance().fontPath;
        next.fontSize = app->GetAppearance().fontSize;
        next.uiScale = 1.0f;
        next.customColors = true;
        next.customThemeName = requestedName.empty() ? "Custom" : requestedName;
        SavedAppearanceTheme saved = ToSavedTheme(next, next.customThemeName);
        auto it = std::find_if(next.savedThemes.begin(), next.savedThemes.end(),
            [&](const SavedAppearanceTheme& existing) { return EqualsIgnoreCase(existing.name, saved.name); });
        if (it == next.savedThemes.end())
            next.savedThemes.push_back(saved);
        else
            *it = saved;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto findUserTheme = [&](const std::string& name) {
        const std::vector<SavedAppearanceTheme>& themes = app->GetAppearance().savedThemes;
        return std::find_if(themes.begin(), themes.end(),
            [&](const SavedAppearanceTheme& saved) { return EqualsIgnoreCase(saved.name, name); });
    };
    auto deleteUserTheme = [&](const std::string& name) {
        AppearanceSettings next = app->GetAppearance();
        auto it = std::remove_if(next.savedThemes.begin(), next.savedThemes.end(),
            [&](const SavedAppearanceTheme& saved) { return EqualsIgnoreCase(saved.name, name); });
        if (it == next.savedThemes.end())
            return;

        next.savedThemes.erase(it, next.savedThemes.end());
        if (next.customColors && EqualsIgnoreCase(next.customThemeName, name)) {
            AppearanceSettings fallback = ThemeDefaults(next.theme);
            fallback.fontPath = next.fontPath;
            fallback.fontSize = next.fontSize;
            fallback.uiScale = 1.0f;
            fallback.popupOpacity = next.popupOpacity;
            fallback.popupOutlineStrength = next.popupOutlineStrength;
            fallback.popupOutlineEffect = next.popupOutlineEffect;
            fallback.popupOutlineAnimated = next.popupOutlineAnimated;
            fallback.popupOutlineAnimationSpeed = next.popupOutlineAnimationSpeed;
            fallback.popupOutlineColorSharpness = next.popupOutlineColorSharpness;
            fallback.popupOutlineColorSpread = next.popupOutlineColorSpread;
            fallback.popupOutlineSaturation = next.popupOutlineSaturation;
            fallback.popupOutlineBrightness = next.popupOutlineBrightness;
            fallback.popupOutlineReverse = next.popupOutlineReverse;
            fallback.popupWidth = next.popupWidth;
            fallback.popupHeight = next.popupHeight;
            fallback.mainWindowWidth = next.mainWindowWidth;
            fallback.mainWindowHeight = next.mainWindowHeight;
            fallback.showScrollbars = next.showScrollbars;
            fallback.scrollbarSize = next.scrollbarSize;
            fallback.scrollbarRounding = next.scrollbarRounding;
            fallback.scrollbarPadding = next.scrollbarPadding;
            fallback.popupRounding = next.popupRounding;
            fallback.popupButtonRowPadding = next.popupButtonRowPadding;
            fallback.popupButtonColumnPadding = next.popupButtonColumnPadding;
            fallback.controlRounding = next.controlRounding;
            fallback.savedThemes = next.savedThemes;
            next = fallback;
        }
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };

    enum CustomizeTab {
        TAB_THEMES,
        TAB_POPUP,
        TAB_WINDOW,
        TAB_FONT_ICON,
        TAB_COLORS,
    };
    static int customizeTab = TAB_THEMES;
    if (ImGui::BeginTabBar("##customize_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Themes")) {
            customizeTab = TAB_THEMES;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Popup")) {
            customizeTab = TAB_POPUP;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Window & Layout")) {
            customizeTab = TAB_WINDOW;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Font & Icon")) {
            customizeTab = TAB_FONT_ICON;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Colors")) {
            customizeTab = TAB_COLORS;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::TextDisabled("Changes apply and save automatically unless an Apply button is shown.");

    if (customizeTab == TAB_THEMES) {
    SectionHeader("Theme");
    const std::string preThemeName = TrimAscii(customName);
    const std::string preSelectedThemeName = draft.customColors
        ? draft.customThemeName
        : ThemeName(draft.theme);
    const bool showThemeSave = !preThemeName.empty() &&
        !EqualsIgnoreCase(preThemeName, preSelectedThemeName);
    const float themeSaveW = ButtonWidthForText("Save", 72.0f);
    const float themeSaveReserve = showThemeSave ? themeSaveW + ImGui::GetStyle().ItemSpacing.x : 0.0f;

    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - themeSaveReserve));
    ImGui::InputText("##theme_name", customName, sizeof(customName));
    const bool themeInputHovered = ImGui::IsItemHovered();
    const bool themeInputClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 themeInputMin = ImGui::GetItemRectMin();
    const ImVec2 themeInputMax = ImGui::GetItemRectMax();
    if (themeInputClicked) {
        themeDropdownOpen = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (themeInputHovered && !ImGui::IsItemActive())
        HelpTooltip("Click to select a theme or type a custom theme name");
    if (showThemeSave) {
        ImGui::SameLine();
        if (BlueButton("Save", themeSaveW)) {
            const std::string name = TrimAscii(customName);
            if (IsBuiltInThemeName(name)) {
                MessageBeep(MB_ICONWARNING);
                ImGui::OpenPopup("Rename custom theme");
            } else {
                saveUserTheme(name);
            }
        }
    }

    if (themeDropdownOpen) {
        const float dropW = themeInputMax.x - themeInputMin.x;
        ImGui::SetNextWindowPos({themeInputMin.x, themeInputMax.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({dropW, 0.0f}, {dropW, 320.0f});
        constexpr ImGuiWindowFlags dropFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::Begin("##theme_picker", nullptr, dropFlags);
        const bool dropdownHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
            ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        ImGui::TextDisabled("Built-in themes");
        for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
            const ThemeId theme = static_cast<ThemeId>(i);
            const bool selected = !draft.customColors && draft.theme == theme;
            if (ImGui::Selectable(ThemeName(theme), selected)) {
                applyBuiltInTheme(theme);
                themeDropdownOpen = false;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();
        ImGui::TextDisabled("User configured");
        if (draft.savedThemes.empty()) {
            ImGui::TextDisabled("No saved themes");
        } else {
            for (const SavedAppearanceTheme& saved : draft.savedThemes) {
                const bool selected = draft.customColors && saved.name == draft.customThemeName;
                if (ImGui::Selectable(saved.name.c_str(), selected)) {
                    applyUserTheme(saved);
                    themeDropdownOpen = false;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::End();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dropdownHovered && !themeInputHovered)
            themeDropdownOpen = false;
    }

    const std::string themeNameForDelete = TrimAscii(customName);
    const bool canDeleteTheme = !themeNameForDelete.empty() &&
        findUserTheme(themeNameForDelete) != app->GetAppearance().savedThemes.end();
    ImGui::Spacing();
    if (!canDeleteTheme)
        ImGui::BeginDisabled();
    if (DangerButton("Delete theme", 122.0f)) {
        MessageBeep(MB_ICONWARNING);
        ImGui::OpenPopup("Delete custom theme");
    }
    if (!canDeleteTheme)
        ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Delete custom theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this user theme?");
        ImGui::TextDisabled("%s", themeNameForDelete.c_str());
        ImGui::Spacing();
        if (DangerButton("Delete", 100.0f)) {
            deleteUserTheme(themeNameForDelete);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Rename custom theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char renameThemeBuf[96]{};
        if (renameThemeBuf[0] == '\0') {
            std::string base = TrimAscii(customName);
            if (base.empty() || IsBuiltInThemeName(base))
                base = std::string(base.empty() ? "Custom" : base) + " Custom";
            std::snprintf(renameThemeBuf, sizeof(renameThemeBuf), "%s", base.c_str());
        }

        ImGui::TextWrapped("Built-in theme names are reserved. Choose a custom theme name.");
        {
            const float labelW = ImGui::CalcTextSize("Name").x + ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(-labelW);
        }
        ImGui::InputText("Name##rename_theme", renameThemeBuf, sizeof(renameThemeBuf));

        const std::string renameName = TrimAscii(renameThemeBuf);
        if (renameName.empty() || IsBuiltInThemeName(renameName))
            ImGui::BeginDisabled();
        if (PaddedButton("Save custom theme", 150.0f)) {
            saveUserTheme(renameName);
            renameThemeBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        if (renameName.empty() || IsBuiltInThemeName(renameName))
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renameThemeBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    }

    if (customizeTab == TAB_POPUP) {
    SectionHeader("Popup");
    ImGui::Text("Popup opacity");
    ImGui::SetNextItemWidth(240.0f);
    SliderFloatWheel("##opacity", &draft.popupOpacity, 0.1f, 1.0f, "%.2f", 0.05f);
    ImGui::Text("Popup outline");
    ImGui::SetNextItemWidth(240.0f);
    SliderFloatWheel("##outline_strength", &draft.popupOutlineStrength, 0.0f, 1.0f, "%.2f", 0.05f);
    const char* outlineEffects[] = {"Solid accent", "Rainbow flow", "Pulse glow", "Comet chase"};
    draft.popupOutlineEffect = std::clamp(draft.popupOutlineEffect, 0, 3);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::Combo("Outline effect", &draft.popupOutlineEffect,
                     outlineEffects, IM_ARRAYSIZE(outlineEffects))) {
        AppearanceSettings next = app->GetAppearance();
        next.popupOutlineEffect = draft.popupOutlineEffect;
        next.popupOutlineAnimated = draft.popupOutlineEffect != 0;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    if (draft.popupOutlineEffect != 0) {
        bool outlineAnimationChanged = false;
        auto applyOutlineAnimation = [&]() {
            AppearanceSettings next = app->GetAppearance();
            next.popupOutlineEffect = std::clamp(draft.popupOutlineEffect, 0, 3);
            next.popupOutlineAnimated = next.popupOutlineEffect != 0;
            next.popupOutlineAnimationSpeed = std::clamp(draft.popupOutlineAnimationSpeed, 0.05f, 5.0f);
            next.popupOutlineColorSharpness = std::clamp(draft.popupOutlineColorSharpness, 0.0f, 1.0f);
            next.popupOutlineColorSpread = std::clamp(draft.popupOutlineColorSpread, 0.0f, 2.0f);
            next.popupOutlineSaturation = std::clamp(draft.popupOutlineSaturation, 0.0f, 1.0f);
            next.popupOutlineBrightness = std::clamp(draft.popupOutlineBrightness, 0.20f, 1.0f);
            next.popupOutlineReverse = draft.popupOutlineReverse;
            app->RequestAppearance(next);
            draft = app->GetAppearance();
            std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        };
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Animation speed", &draft.popupOutlineAnimationSpeed,
                                                    0.05f, 5.0f, "%.2fx", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color spread", &draft.popupOutlineColorSpread,
                                                    0.0f, 2.0f, "%.2f", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color sharpness", &draft.popupOutlineColorSharpness,
                                                    0.0f, 1.0f, "%.2f", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color saturation", &draft.popupOutlineSaturation,
                                                    0.0f, 1.0f, "%.2f", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color brightness", &draft.popupOutlineBrightness,
                                                    0.20f, 1.0f, "%.2f", 0.05f);
        if (ImGui::Checkbox("Reverse direction", &draft.popupOutlineReverse))
            outlineAnimationChanged = true;
        if (outlineAnimationChanged)
            applyOutlineAnimation();
        if (PaddedButton("Reset outline animation", 180.0f)) {
            draft.popupOutlineEffect = 1;
            draft.popupOutlineAnimationSpeed = 1.0f;
            draft.popupOutlineColorSpread = 1.0f;
            draft.popupOutlineColorSharpness = 0.55f;
            draft.popupOutlineSaturation = 0.72f;
            draft.popupOutlineBrightness = 1.0f;
            draft.popupOutlineReverse = false;
            applyOutlineAnimation();
        }
    }

    SectionHeader("Default Popup Size");
    bool sizeChanged = false;
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    sizeChanged |= ImGui::InputInt("W##pw", &draft.popupWidth, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    sizeChanged |= ImGui::InputInt("H##ph", &draft.popupHeight, 10);
    if (sizeChanged) {
        draft.popupWidth = std::max(360, draft.popupWidth);
        draft.popupHeight = std::max(260, draft.popupHeight);
    }
    SIZE livePopupSize = app->PopupCurrentSize();
    ImGui::TextDisabled("Current popup: %ldx%ld",
                        static_cast<long>(livePopupSize.cx),
                        static_cast<long>(livePopupSize.cy));
    ImGui::Spacing();
    if (PaddedButton("Use current as default", 180.0f)) {
        app->UseCurrentPopupSizeAsDefault();
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    ImGui::SameLine();
    if (PaddedButton("Apply popup settings", 170.0f)) {
        AppearanceSettings next = app->GetAppearance();
        next.popupOpacity = std::clamp(draft.popupOpacity, 0.1f, 1.0f);
        next.popupOutlineStrength = std::clamp(draft.popupOutlineStrength, 0.0f, 1.0f);
        next.popupOutlineEffect = std::clamp(draft.popupOutlineEffect, 0, 3);
        next.popupOutlineAnimated = next.popupOutlineEffect != 0;
        next.popupOutlineAnimationSpeed = std::clamp(draft.popupOutlineAnimationSpeed, 0.05f, 5.0f);
        next.popupOutlineColorSharpness = std::clamp(draft.popupOutlineColorSharpness, 0.0f, 1.0f);
        next.popupOutlineColorSpread = std::clamp(draft.popupOutlineColorSpread, 0.0f, 2.0f);
        next.popupOutlineSaturation = std::clamp(draft.popupOutlineSaturation, 0.0f, 1.0f);
        next.popupOutlineBrightness = std::clamp(draft.popupOutlineBrightness, 0.20f, 1.0f);
        next.popupOutlineReverse = draft.popupOutlineReverse;
        next.popupWidth = std::max(360, draft.popupWidth);
        next.popupHeight = std::max(260, draft.popupHeight);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    }

    if (customizeTab == TAB_WINDOW) {
    SectionHeader("Default Settings Window Size");
    bool mainSizeChanged = false;
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    mainSizeChanged |= ImGui::InputInt("W##mw", &draft.mainWindowWidth, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    mainSizeChanged |= ImGui::InputInt("H##mh", &draft.mainWindowHeight, 10);
    if (mainSizeChanged) {
        draft.mainWindowWidth = std::max(800, draft.mainWindowWidth);
        draft.mainWindowHeight = std::max(500, draft.mainWindowHeight);
    }
    SIZE liveMainSize = app->MainWindowCurrentSize();
    ImGui::TextDisabled("Current settings: %ldx%ld",
                        static_cast<long>(liveMainSize.cx),
                        static_cast<long>(liveMainSize.cy));
    ImGui::Spacing();
    if (PaddedButton("Use current as default##main_size", 180.0f)) {
        app->UseCurrentMainWindowSizeAsDefault();
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    ImGui::SameLine();
    if (PaddedButton("Apply settings size", 160.0f)) {
        AppearanceSettings next = app->GetAppearance();
        next.mainWindowWidth = std::max(800, draft.mainWindowWidth);
        next.mainWindowHeight = std::max(500, draft.mainWindowHeight);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    }

    if (customizeTab == TAB_FONT_ICON) {
    SectionHeader("Font");
    auto applyFont = [&]() {
        AppearanceSettings next = app->GetAppearance();
        next.fontPath = fontPath;
        next.fontSize = std::clamp(std::round(draft.fontSize * 2.0f) * 0.5f, 9.0f, 32.0f);
        {
            std::ostringstream out;
            out << "MainWindow::ApplyFont: requested"
                << " path=\"" << next.fontPath << "\""
                << " size=" << next.fontSize
                << " draftSize=" << draft.fontSize;
            app->LogDebug(out.str());
        }
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    };

    ImGui::SetNextItemWidth(-FLT_MIN);
    bool fontPathEdited = ImGui::InputText("##fontPath", fontPath, sizeof(fontPath),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    bool fontPathDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Spacing();
    int fontSizeStep = static_cast<int>(std::round(draft.fontSize * 2.0f));
    if (PaddedButton("Browse", 100.0f)) {
        if (PickFontFile(fontPath, sizeof(fontPath)))
            applyFont();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    bool fontSizeChanged = SliderIntWheel("Font size##fontSizeSlider", &fontSizeStep,
                                          18, 64, "", 1);
    ImGui::SameLine();
    ImGui::Text("%.1f", static_cast<float>(fontSizeStep) * 0.5f);
    if (fontPathEdited)
        applyFont();
    else if (fontPathDeactivated) {
        draft.fontPath = fontPath;
    }

    if (fontSizeChanged) {
        fontSizeStep = std::clamp(fontSizeStep, 18, 64);
        const float snapped = static_cast<float>(fontSizeStep) * 0.5f;
        if (std::fabs(snapped - draft.fontSize) > 0.01f) {
            draft.fontSize = snapped;
            applyFont();
        }
    }

    SectionHeader("Exe Icon");
    ImGui::TextDisabled("Choose a .ico file to use as the Windows Explorer icon for this exe.");
    ImGui::TextColored({1.0f, 0.36f, 0.32f, 1.0f},
                       "Restart Clipboard++ after selecting a custom exe icon.");
    auto applyIconPath = [&]() {
        AppearanceSettings next = app->GetAppearance();
        next.exeIconPath = iconPathBuf;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(iconPathBuf, sizeof(iconPathBuf), "%s", draft.exeIconPath.c_str());
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    };
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool iconEdited      = ImGui::InputText("##exeIconPath", iconPathBuf, sizeof(iconPathBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    bool iconDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Spacing();
    if (PaddedButton("Browse##icon", 100.0f)) {
        if (PickIcoFile(iconPathBuf, sizeof(iconPathBuf)))
            applyIconPath();
    }
    ImGui::SameLine();
    if (PaddedButton("Clear##icon", 90.0f)) {
        iconPathBuf[0] = '\0';
        applyIconPath();
    }
    if (BlueButton("Restart Clipboard++", 160.0f)) {
        ShellExecuteA(nullptr, "open", app->ExecutablePath().c_str(), nullptr,
                      app->WorkingDirectory().c_str(), SW_SHOWNORMAL);
        DestroyWindow(app->GetHwnd());
    }
    if (iconEdited)
        applyIconPath();
    else if (iconDeactivated)
        draft.exeIconPath = iconPathBuf;

    }

    if (customizeTab == TAB_WINDOW) {
    SectionHeader("Scrollbars");
    bool scrollbarChanged = false;
    scrollbarChanged |= ImGui::Checkbox("Show scrollbars", &draft.showScrollbars);
    if (draft.showScrollbars) {
        ImGui::SetNextItemWidth(240.0f);
        scrollbarChanged |= SliderFloatWheel("Scrollbar width", &draft.scrollbarSize,
                                             0.0f, 24.0f, "%.1f", 1.0f);
        ImGui::SetNextItemWidth(240.0f);
        scrollbarChanged |= SliderFloatWheel("Scrollbar rounding", &draft.scrollbarRounding,
                                             0.0f, 16.0f, "%.1f", 1.0f);
        ImGui::SetNextItemWidth(240.0f);
        scrollbarChanged |= SliderFloatWheel("Scrollbar padding", &draft.scrollbarPadding,
                                             0.0f, 8.0f, "%.1f", 0.5f);
        if (draft.customColors) {
            const AppearanceSettings resetForScrollbars = ThemeDefaults(draft.theme);
            scrollbarChanged |= ColorControlWithReset("Scrollbar background", draft.scrollbarBg, resetForScrollbars.scrollbarBg);
            scrollbarChanged |= ColorControlWithReset("Scrollbar grab", draft.scrollbarGrab, resetForScrollbars.scrollbarGrab);
            scrollbarChanged |= ColorControlWithReset("Scrollbar hover", draft.scrollbarGrabHover, resetForScrollbars.scrollbarGrabHover);
            scrollbarChanged |= ColorControlWithReset("Scrollbar active", draft.scrollbarGrabActive, resetForScrollbars.scrollbarGrabActive);
        }
    } else {
        ImGui::TextDisabled("Scrollbar controls are hidden while scrollbars are disabled.");
    }
    if (scrollbarChanged) {
        draft.scrollbarSize = std::clamp(draft.scrollbarSize, 0.0f, 24.0f);
        draft.scrollbarRounding = std::clamp(draft.scrollbarRounding, 0.0f, 16.0f);
        draft.scrollbarPadding = std::clamp(draft.scrollbarPadding, 0.0f, 8.0f);
        AppearanceSettings next = app->GetAppearance();
        next.showScrollbars = draft.showScrollbars;
        next.scrollbarSize = draft.scrollbarSize;
        next.scrollbarRounding = draft.scrollbarRounding;
        next.scrollbarPadding = draft.scrollbarPadding;
        next.scrollbarBg = draft.scrollbarBg;
        next.scrollbarGrab = draft.scrollbarGrab;
        next.scrollbarGrabHover = draft.scrollbarGrabHover;
        next.scrollbarGrabActive = draft.scrollbarGrabActive;
        if (draft.customColors)
            next.customColors = true;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    }

    if (customizeTab == TAB_COLORS) {
    SectionHeader("Colors");
    const AppearanceSettings reset = ThemeDefaults(draft.theme);
    bool colorsChanged = ImGui::Checkbox("Enable custom colors", &draft.customColors);
    if (!draft.customColors) {
        ImGui::TextDisabled("Color controls are hidden while custom colors are disabled.");
    } else if (ImGui::BeginTable("##appearance_color_preview", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Colors", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        const ImGuiTreeNodeFlags groupFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ImGui::TreeNodeEx("Base surfaces and text", groupFlags)) {
            colorsChanged |= ColorControlWithReset("Window background", draft.windowBg, reset.windowBg);
            colorsChanged |= ColorControlWithReset("Panel background", draft.panelBg, reset.panelBg);
            colorsChanged |= ColorControlWithReset("Text", draft.text, reset.text);
            colorsChanged |= ColorControlWithReset("Muted text", draft.mutedText, reset.mutedText);
            colorsChanged |= ColorControlWithReset("Accent", draft.accent, reset.accent);
            colorsChanged |= ColorControlWithReset("Hover", draft.hover, reset.hover);
            colorsChanged |= ColorControlWithReset("Selected tab", draft.selectedTab, reset.selectedTab);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Popup controls", groupFlags)) {
            colorsChanged |= ColorControlWithReset("Button off", draft.buttonOff, reset.buttonOff);
            colorsChanged |= ColorControlWithReset("Button on", draft.buttonOn, reset.buttonOn);
            colorsChanged |= ColorControlWithReset("Popup close", draft.closeButton, reset.closeButton);
            colorsChanged |= ColorControlWithReset("Popup close hover", draft.closeButtonHover, reset.closeButtonHover);
            colorsChanged |= ColorControlWithReset("Popup close text", draft.closeButtonText, reset.closeButtonText);
            colorsChanged |= ColorControlWithReset("Opacity knob fill", draft.opacityKnobFill, reset.opacityKnobFill);
            colorsChanged |= ColorControlWithReset("Opacity knob ring", draft.opacityKnobRing, reset.opacityKnobRing);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Settings title bar", groupFlags)) {
            colorsChanged |= ColorControlWithReset("Title bar background", draft.titleBarBg, reset.titleBarBg);
            colorsChanged |= ColorControlWithReset("Title bar divider", draft.titleBarBorder, reset.titleBarBorder);
            colorsChanged |= ColorControlWithReset("Title text", draft.titleBarText, reset.titleBarText);
            colorsChanged |= ColorControlWithReset("Minimize base",  draft.titleMinBase,   reset.titleMinBase);
            colorsChanged |= ColorControlWithReset("Minimize hover", draft.titleMinHover,  reset.titleMinHover);
            colorsChanged |= ColorControlWithReset("Minimize glyph", draft.titleMinGlyph,  reset.titleMinGlyph);
            colorsChanged |= ColorControlWithReset("Restore base",   draft.titleMaxBase,   reset.titleMaxBase);
            colorsChanged |= ColorControlWithReset("Restore hover",  draft.titleMaxHover,  reset.titleMaxHover);
            colorsChanged |= ColorControlWithReset("Restore glyph",  draft.titleMaxGlyph,  reset.titleMaxGlyph);
            colorsChanged |= ColorControlWithReset("Close base",     draft.titleCloseBase,  reset.titleCloseBase);
            colorsChanged |= ColorControlWithReset("Close hover",    draft.titleCloseHover, reset.titleCloseHover);
            colorsChanged |= ColorControlWithReset("Close glyph",    draft.titleCloseGlyph, reset.titleCloseGlyph);
            colorsChanged |= ColorControlWithReset("Exit base",      draft.titleExitBase,   reset.titleExitBase);
            colorsChanged |= ColorControlWithReset("Exit hover",     draft.titleExitHover,  reset.titleExitHover);
            colorsChanged |= ColorControlWithReset("Exit glyph",     draft.titleExitGlyph,  reset.titleExitGlyph);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Clipboard icon", groupFlags)) {
            const float previewSz = 48.0f;
            ImGui::BeginGroup();
            colorsChanged |= ColorControlWithReset("Board top",    draft.iconBoardTop,    reset.iconBoardTop);
            colorsChanged |= ColorControlWithReset("Board bottom", draft.iconBoardBottom, reset.iconBoardBottom);
            colorsChanged |= ColorControlWithReset("Paper",        draft.iconPaper,       reset.iconPaper);
            colorsChanged |= ColorControlWithReset("Margin line",  draft.iconMarginLine,  reset.iconMarginLine);
            colorsChanged |= ColorControlWithReset("Ruled lines",  draft.iconRuledLines,  reset.iconRuledLines);
            ImGui::EndGroup();
            ImGui::SameLine(0, 16.0f);
            DrawClipboardIcon(previewSz, draft);
            ImGui::TreePop();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Preview");
        DrawPopupPreview(draft);
        ImGui::Spacing();
        DrawSettingsPreview(draft);
        ImGui::EndTable();
    }
    if (colorsChanged) {
        AppearanceSettings next = draft;
        next.fontPath = app->GetAppearance().fontPath;
        next.fontSize = app->GetAppearance().fontSize;
        next.uiScale = 1.0f;
        next.savedThemes = app->GetAppearance().savedThemes;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    }

    if (customizeTab == TAB_WINDOW) {
    ImGui::Spacing(); ImGui::SeparatorText("Advanced shape");
    ImGui::SetNextItemWidth(240.0f);
    bool shapeChanged = SliderFloatWheel("Popup corner rounding", &draft.popupRounding,
                                         0.0f, 18.0f, "%.1f", 0.5f);
    ImGui::SetNextItemWidth(240.0f);
    shapeChanged |= SliderFloatWheel("Control rounding", &draft.controlRounding,
                                     0.0f, 12.0f, "%.1f", 0.5f);
    if (shapeChanged) {
        draft.popupRounding = std::clamp(draft.popupRounding, 0.0f, 18.0f);
        draft.popupButtonRowPadding = std::clamp(draft.popupButtonRowPadding, 0.0f, 12.0f);
        draft.popupButtonColumnPadding = std::clamp(draft.popupButtonColumnPadding, 0.0f, 16.0f);
        draft.controlRounding = std::clamp(draft.controlRounding, 0.0f, 12.0f);
        AppearanceSettings next = app->GetAppearance();
        next.popupRounding = draft.popupRounding;
        next.popupButtonRowPadding = draft.popupButtonRowPadding;
        next.popupButtonColumnPadding = draft.popupButtonColumnPadding;
        next.controlRounding = draft.controlRounding;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    }
}
