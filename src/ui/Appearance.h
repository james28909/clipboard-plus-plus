#pragma once

#include <imgui.h>
#include <string>
#include <vector>

enum class ThemeId {
    DarkDefault = 0,
    Dracula,
    Nord,
    Monokai,
    OneDark,
    TokyoNight,
    SolarizedDark,
    GitHubDark,
    GitHubLight,
    SolarizedLight,
    VSLight,
    QuietLight,
    Count
};

struct SavedAppearanceTheme {
    std::string name{"Custom"};
    ImVec4 windowBg{0.118f, 0.118f, 0.118f, 1.0f};
    ImVec4 panelBg{0.145f, 0.145f, 0.149f, 1.0f};
    ImVec4 text{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 mutedText{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 accent{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 buttonOff{0.184f, 0.196f, 0.220f, 1.0f};
    ImVec4 buttonOn{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 opacityKnobFill{0.078f, 0.149f, 0.212f, 1.0f};
    ImVec4 opacityKnobRing{0.361f, 0.698f, 1.0f, 1.0f};
};

struct AppearanceSettings {
    ThemeId theme{ThemeId::DarkDefault};
    float popupOpacity{0.95f};
    int popupWidth{440};
    int popupHeight{540};
    std::string fontPath{"C:\\Windows\\Fonts\\segoeui.ttf"};
    float fontSize{15.0f};
    bool customColors{false};
    std::string customThemeName{"Custom"};
    ImVec4 windowBg{0.118f, 0.118f, 0.118f, 1.0f};
    ImVec4 panelBg{0.145f, 0.145f, 0.149f, 1.0f};
    ImVec4 text{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 mutedText{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 accent{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 buttonOff{0.184f, 0.196f, 0.220f, 1.0f};
    ImVec4 buttonOn{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 opacityKnobFill{0.078f, 0.149f, 0.212f, 1.0f};
    ImVec4 opacityKnobRing{0.361f, 0.698f, 1.0f, 1.0f};
    std::vector<SavedAppearanceTheme> savedThemes;
};

struct PopupToggleColors {
    ImVec4 off;
    ImVec4 offHovered;
    ImVec4 offActive;
    ImVec4 on;
    ImVec4 onHovered;
    ImVec4 onActive;
};

const char* ThemeName(ThemeId theme);
void ApplyThemeStyle(ThemeId theme, bool popupContext);
void ApplyThemeStyle(const AppearanceSettings& settings, bool popupContext);
AppearanceSettings ThemeDefaults(ThemeId theme);
SavedAppearanceTheme ToSavedTheme(const AppearanceSettings& settings, const std::string& name);
void ApplySavedTheme(AppearanceSettings& settings, const SavedAppearanceTheme& saved);
PopupToggleColors GetPopupToggleColors(ThemeId theme);
PopupToggleColors GetPopupToggleColors(const AppearanceSettings& settings);
bool RebuildFontAtlas(ImGuiIO& io, const AppearanceSettings& settings);
