#pragma once

#include <imgui.h>
#include <string>

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

struct AppearanceSettings {
    ThemeId theme{ThemeId::DarkDefault};
    float popupOpacity{0.95f};
    int popupWidth{440};
    int popupHeight{540};
    std::string fontPath{"C:\\Windows\\Fonts\\segoeui.ttf"};
    float fontSize{15.0f};
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
PopupToggleColors GetPopupToggleColors(ThemeId theme);
bool RebuildFontAtlas(ImGuiIO& io, const AppearanceSettings& settings);
