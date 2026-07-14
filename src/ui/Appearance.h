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
    ImVec4 hover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 selectedTab{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 buttonOff{0.184f, 0.196f, 0.220f, 1.0f};
    ImVec4 buttonOn{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 popupFilterBorder{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 popupActionBorder{0.180f, 0.720f, 0.420f, 1.0f};
    ImVec4 popupDestinationBorder{0.620f, 0.360f, 0.900f, 1.0f};
    bool customCommandBarColors{false};
    ImVec4 closeButton{0.184f, 0.196f, 0.220f, 1.0f};
    ImVec4 closeButtonHover{0.769f, 0.169f, 0.110f, 1.0f};
    ImVec4 closeButtonText{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 titleBarBg{0.118f, 0.118f, 0.118f, 1.0f};
    ImVec4 titleBarBorder{0.204f, 0.204f, 0.204f, 1.0f};
    ImVec4 titleBarText{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 titleMinBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleMaxBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleCloseBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleMinHover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 titleMaxHover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 titleCloseHover{0.769f, 0.169f, 0.110f, 1.0f};
    ImVec4 titleExitBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleExitHover{0.800f, 0.350f, 0.0f, 1.0f};
    ImVec4 titleMinGlyph{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 titleMaxGlyph{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 titleExitGlyph{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 titleCloseGlyph{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 iconBoardTop{0.302f, 0.659f, 1.0f,   1.0f};
    ImVec4 iconBoardBottom{0.051f, 0.239f, 0.710f, 1.0f};
    ImVec4 iconPaper{1.0f, 1.0f, 1.0f, 1.0f};
    ImVec4 iconMarginLine{1.0f, 0.0f, 0.133f, 1.0f};
    ImVec4 iconRuledLines{0.118f, 0.310f, 0.549f, 1.0f};
    ImVec4 opacityKnobFill{0.078f, 0.149f, 0.212f, 1.0f};
    ImVec4 opacityKnobRing{0.361f, 0.698f, 1.0f, 1.0f};
    ImVec4 scrollbarBg{0.145f, 0.145f, 0.149f, 0.45f};
    ImVec4 scrollbarGrab{0.310f, 0.360f, 0.460f, 1.0f};
    ImVec4 scrollbarGrabHover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 scrollbarGrabActive{0.149f, 0.475f, 1.0f, 1.0f};
    bool showScrollbars{true};
    float scrollbarSize{10.0f};
    float scrollbarRounding{7.0f};
    float scrollbarPadding{2.0f};
    float popupRounding{6.0f};
    float popupButtonRowPadding{0.0f};
    float popupButtonColumnPadding{5.0f};
    float popupCommandBarRounding{3.0f};
    float controlRounding{3.0f};
};

struct AppearanceSettings {
    ThemeId theme{ThemeId::DarkDefault};
    float popupOpacity{0.95f};
    float popupOutlineStrength{0.65f};
    int popupOutlineEffect{0}; // 0=solid 1=rainbow 2=pulse 3=comet
    bool popupOutlineAnimated{false};
    float popupOutlineAnimationSpeed{1.0f};
    float popupOutlineColorSharpness{0.55f};
    float popupOutlineColorSpread{1.0f};
    float popupOutlineSaturation{0.72f};
    float popupOutlineBrightness{1.0f};
    bool popupOutlineReverse{false};
    int popupWidth{440};
    int popupHeight{540};
    int mainWindowWidth{1200};
    int mainWindowHeight{750};
    std::string fontPath{"C:\\Windows\\Fonts\\segoeui.ttf"};
    float fontSize{15.0f};
    std::string exeIconPath{}; // path to .ico; patched into the exe on shutdown
    std::string exeIconThemeHash{}; // theme color hash of the last theme-icon patch ("" = custom or none)
    float uiScale{1.0f};
    float dpiScale{1.0f};
    bool customColors{false};
    std::string customThemeName{"Custom"};
    ImVec4 windowBg{0.118f, 0.118f, 0.118f, 1.0f};
    ImVec4 panelBg{0.145f, 0.145f, 0.149f, 1.0f};
    ImVec4 text{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 mutedText{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 accent{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 hover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 selectedTab{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 buttonOff{0.184f, 0.196f, 0.220f, 1.0f};
    ImVec4 buttonOn{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 popupFilterBorder{0.149f, 0.475f, 1.0f, 1.0f};
    ImVec4 popupActionBorder{0.180f, 0.720f, 0.420f, 1.0f};
    ImVec4 popupDestinationBorder{0.620f, 0.360f, 0.900f, 1.0f};
    bool customCommandBarColors{false};
    ImVec4 closeButton{0.184f, 0.196f, 0.220f, 1.0f};
    ImVec4 closeButtonHover{0.769f, 0.169f, 0.110f, 1.0f};
    ImVec4 closeButtonText{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 titleBarBg{0.118f, 0.118f, 0.118f, 1.0f};
    ImVec4 titleBarBorder{0.204f, 0.204f, 0.204f, 1.0f};
    ImVec4 titleBarText{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 titleMinBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleMaxBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleCloseBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleMinHover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 titleMaxHover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 titleCloseHover{0.769f, 0.169f, 0.110f, 1.0f};
    ImVec4 titleExitBase{0.0f, 0.0f, 0.0f, 0.0f};
    ImVec4 titleExitHover{0.800f, 0.350f, 0.0f, 1.0f};
    ImVec4 titleMinGlyph{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 titleMaxGlyph{0.588f, 0.588f, 0.588f, 1.0f};
    ImVec4 titleExitGlyph{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 titleCloseGlyph{0.863f, 0.863f, 0.863f, 1.0f};
    ImVec4 iconBoardTop{0.302f, 0.659f, 1.0f,   1.0f};
    ImVec4 iconBoardBottom{0.051f, 0.239f, 0.710f, 1.0f};
    ImVec4 iconPaper{1.0f, 1.0f, 1.0f, 1.0f};
    ImVec4 iconMarginLine{1.0f, 0.0f, 0.133f, 1.0f};
    ImVec4 iconRuledLines{0.118f, 0.310f, 0.549f, 1.0f};
    ImVec4 opacityKnobFill{0.078f, 0.149f, 0.212f, 1.0f};
    ImVec4 opacityKnobRing{0.361f, 0.698f, 1.0f, 1.0f};
    ImVec4 scrollbarBg{0.145f, 0.145f, 0.149f, 0.45f};
    ImVec4 scrollbarGrab{0.310f, 0.360f, 0.460f, 1.0f};
    ImVec4 scrollbarGrabHover{0.294f, 0.561f, 1.0f, 1.0f};
    ImVec4 scrollbarGrabActive{0.149f, 0.475f, 1.0f, 1.0f};
    bool showScrollbars{true};
    float scrollbarSize{10.0f};
    float scrollbarRounding{7.0f};
    float scrollbarPadding{2.0f};
    float popupRounding{6.0f};
    float popupButtonRowPadding{0.0f};
    float popupButtonColumnPadding{5.0f};
    float popupCommandBarRounding{3.0f};
    float controlRounding{3.0f};
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
float EffectiveUiScale(const AppearanceSettings& settings);
bool RebuildFontAtlas(ImGuiIO& io, const AppearanceSettings& settings);
