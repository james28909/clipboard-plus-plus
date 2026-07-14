#include "Appearance.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <filesystem>
#endif
#include <algorithm>

static ImVec4 Color(int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

static ImVec4 Mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

static ImVec4 Brighten(ImVec4 color, float amount) {
    return Mix(color, ImVec4(1.0f, 1.0f, 1.0f, color.w), amount);
}

static ImVec4 Darken(ImVec4 color, float amount) {
    return Mix(color, ImVec4(0.0f, 0.0f, 0.0f, color.w), amount);
}

static bool IsLightTheme(ThemeId theme) {
    return theme == ThemeId::GitHubLight ||
           theme == ThemeId::SolarizedLight ||
           theme == ThemeId::VSLight ||
           theme == ThemeId::QuietLight;
}

const char* ThemeName(ThemeId theme) {
    switch (theme) {
    case ThemeId::Dracula:        return "Dracula";
    case ThemeId::Nord:           return "Nord";
    case ThemeId::Monokai:        return "Monokai";
    case ThemeId::OneDark:        return "One Dark Pro";
    case ThemeId::TokyoNight:     return "Tokyo Night";
    case ThemeId::SolarizedDark:  return "Solarized Dark";
    case ThemeId::GitHubDark:     return "GitHub Dark";
    case ThemeId::GitHubLight:    return "GitHub Light";
    case ThemeId::SolarizedLight: return "Solarized Light";
    case ThemeId::VSLight:        return "VS Light";
    case ThemeId::QuietLight:     return "Quiet Light";
    default:                      return "Dark Default";
    }
}

AppearanceSettings ThemeDefaults(ThemeId theme) {
    AppearanceSettings settings;
    settings.theme = theme;
    settings.customColors = false;

    const bool light = IsLightTheme(theme);
    settings.windowBg = light ? Color(246, 248, 250) : Color(30, 30, 30);
    settings.panelBg = light ? Color(255, 255, 255) : Color(37, 37, 38);
    settings.text = light ? Color(36, 41, 47) : Color(220, 220, 220);
    settings.mutedText = light ? Color(87, 96, 106) : Color(150, 150, 150);
    settings.accent = Color(38, 121, 255);
    settings.hover = Brighten(settings.accent, 0.18f);
    settings.selectedTab = settings.accent;
    settings.buttonOff = light ? Color(224, 229, 235) : Color(47, 50, 56);
    settings.buttonOn = settings.accent;
    settings.closeButton = settings.buttonOff;
    settings.closeButtonHover = light ? Color(220, 53, 69) : Color(196, 43, 28);
    settings.closeButtonText = light ? Color(255, 255, 255) : settings.text;
    settings.titleBarBg = settings.windowBg;
    settings.titleBarBorder = light ? Darken(settings.panelBg, 0.16f) : Brighten(settings.panelBg, 0.16f);
    settings.titleBarText = settings.text;
    settings.titleMinBase    = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
    settings.titleMaxBase    = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
    settings.titleCloseBase  = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
    settings.titleExitBase   = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
    settings.titleMinHover   = settings.hover;
    settings.titleMaxHover   = settings.hover;
    settings.titleCloseHover = settings.closeButtonHover;
    settings.titleExitHover  = ImVec4{0.800f, 0.350f, 0.0f, 1.0f};
    settings.titleMinGlyph   = settings.mutedText;
    settings.titleMaxGlyph   = settings.mutedText;
    settings.titleExitGlyph  = settings.text;
    settings.titleCloseGlyph = settings.text;
    ImVec4 knobFill = Color(20, 38, 54);
    ImVec4 knobRing = Color(92, 178, 255);

    switch (theme) {
    case ThemeId::Dracula:
        settings.windowBg = Color(40, 42, 54); settings.panelBg = Color(68, 71, 90);
        settings.text = Color(248, 248, 242); settings.mutedText = Color(189, 147, 249);
        settings.accent = Color(80, 250, 123); settings.hover = Color(112, 255, 150); settings.selectedTab = Color(80, 250, 123); settings.buttonOff = Color(68, 71, 90);
        settings.buttonOn = Color(80, 250, 123); knobFill = Color(50, 40, 64); knobRing = Color(255, 121, 198); break;
    case ThemeId::Nord:
        settings.windowBg = Color(46, 52, 64); settings.panelBg = Color(59, 66, 82);
        settings.text = Color(236, 239, 244); settings.mutedText = Color(216, 222, 233);
        settings.accent = Color(136, 192, 208); settings.hover = Color(160, 210, 222); settings.selectedTab = Color(136, 192, 208); settings.buttonOff = Color(59, 66, 82);
        settings.buttonOn = Color(136, 192, 208); knobFill = Color(49, 62, 76); knobRing = Color(180, 142, 173); break;
    case ThemeId::Monokai:
        settings.windowBg = Color(39, 40, 34); settings.panelBg = Color(55, 56, 48);
        settings.text = Color(248, 248, 242); settings.mutedText = Color(166, 226, 46);
        settings.accent = Color(102, 217, 239); settings.hover = Color(145, 230, 246); settings.selectedTab = Color(166, 226, 46); settings.buttonOff = Color(55, 56, 48);
        settings.buttonOn = Color(166, 226, 46); knobFill = Color(45, 45, 36); knobRing = Color(249, 38, 114); break;
    case ThemeId::OneDark:
        settings.windowBg = Color(33, 37, 43); settings.panelBg = Color(40, 44, 52);
        settings.text = Color(171, 178, 191); settings.mutedText = Color(130, 137, 151);
        settings.accent = Color(97, 175, 239); settings.hover = Color(128, 194, 245); settings.selectedTab = Color(97, 175, 239); settings.buttonOff = Color(40, 44, 52);
        settings.buttonOn = Color(97, 175, 239); knobFill = Color(35, 45, 54); knobRing = Color(198, 120, 221); break;
    case ThemeId::TokyoNight:
        settings.windowBg = Color(26, 27, 38); settings.panelBg = Color(36, 40, 59);
        settings.text = Color(192, 202, 245); settings.mutedText = Color(122, 162, 247);
        settings.accent = Color(187, 154, 247); settings.hover = Color(206, 178, 250); settings.selectedTab = Color(187, 154, 247); settings.buttonOff = Color(36, 40, 59);
        settings.buttonOn = Color(187, 154, 247); knobFill = Color(36, 40, 59); knobRing = Color(247, 118, 142); break;
    case ThemeId::SolarizedDark:
        settings.windowBg = Color(0, 43, 54); settings.panelBg = Color(7, 54, 66);
        settings.text = Color(131, 148, 150); settings.mutedText = Color(88, 110, 117);
        settings.accent = Color(38, 139, 210); settings.hover = Color(72, 160, 220); settings.selectedTab = Color(38, 139, 210); settings.buttonOff = Color(7, 54, 66);
        settings.buttonOn = Color(38, 139, 210); knobFill = Color(0, 55, 68); knobRing = Color(181, 137, 0); break;
    case ThemeId::GitHubDark:
        settings.windowBg = Color(13, 17, 23); settings.panelBg = Color(22, 27, 34);
        settings.text = Color(201, 209, 217); settings.mutedText = Color(139, 148, 158);
        settings.accent = Color(88, 166, 255); settings.hover = Color(118, 184, 255); settings.selectedTab = Color(88, 166, 255); settings.buttonOff = Color(22, 27, 34);
        settings.buttonOn = Color(88, 166, 255); knobFill = Color(25, 36, 48); knobRing = Color(210, 153, 34); break;
    case ThemeId::SolarizedLight:
        settings.windowBg = Color(253, 246, 227); settings.panelBg = Color(238, 232, 213);
        settings.text = Color(101, 123, 131); settings.mutedText = Color(147, 161, 161);
        settings.accent = Color(38, 139, 210); settings.hover = Color(78, 162, 222); settings.selectedTab = Color(38, 139, 210); settings.buttonOff = Color(238, 232, 213);
        settings.buttonOn = Color(38, 139, 210); knobFill = Color(245, 236, 205); knobRing = Color(203, 75, 22); break;
    case ThemeId::VSLight:
        settings.windowBg = Color(245, 245, 245); settings.panelBg = Color(255, 255, 255);
        settings.text = Color(30, 30, 30); settings.mutedText = Color(104, 104, 104);
        settings.accent = Color(0, 122, 204); settings.hover = Color(51, 148, 220); settings.selectedTab = Color(0, 122, 204); settings.buttonOff = Color(230, 230, 230);
        settings.buttonOn = Color(0, 122, 204); knobFill = Color(230, 240, 248); knobRing = Color(208, 89, 36); break;
    case ThemeId::QuietLight:
        settings.windowBg = Color(250, 250, 250); settings.panelBg = Color(243, 243, 243);
        settings.text = Color(51, 51, 51); settings.mutedText = Color(110, 110, 110);
        settings.accent = Color(64, 120, 242); settings.hover = Color(98, 146, 246); settings.selectedTab = Color(64, 120, 242); settings.buttonOff = Color(235, 235, 235);
        settings.buttonOn = Color(64, 120, 242); knobFill = Color(235, 239, 247); knobRing = Color(202, 82, 101); break;
    default:
        break;
    }

    settings.titleBarBg = settings.windowBg;
    settings.titleBarBorder = light ? Darken(settings.panelBg, 0.16f) : Brighten(settings.panelBg, 0.16f);
    settings.titleBarText = settings.text;
    settings.titleMinGlyph = settings.mutedText;
    settings.titleMaxGlyph = settings.mutedText;
    settings.titleExitGlyph = settings.text;
    settings.titleCloseGlyph = settings.text;
    settings.opacityKnobFill = knobFill;
    settings.opacityKnobRing = knobRing;
    settings.scrollbarBg = ImVec4(settings.panelBg.x, settings.panelBg.y, settings.panelBg.z, light ? 0.55f : 0.30f);
    settings.scrollbarGrab = Mix(settings.panelBg, settings.accent, light ? 0.24f : 0.34f);
    settings.scrollbarGrabHover = settings.hover;
    settings.scrollbarGrabActive = Brighten(settings.accent, light ? 0.06f : 0.16f);
    settings.iconBoardTop    = settings.hover;
    settings.iconBoardBottom = Mix(settings.accent, Color(8, 12, 30), 0.55f);
    settings.iconPaper       = Color(255, 255, 255);
    settings.iconMarginLine  = settings.closeButtonHover;
    settings.iconRuledLines  = settings.accent;
    settings.popupFilterBorder = settings.accent;
    settings.popupActionBorder = Mix(settings.accent, Color(46, 204, 113), 0.65f);
    settings.popupDestinationBorder = Mix(settings.accent, Color(180, 100, 235), 0.65f);
    settings.customCommandBarColors = false;
    return settings;
}

static AppearanceSettings EffectiveSettings(const AppearanceSettings& settings) {
    if (settings.customColors)
        return settings;

    // Start from settings to preserve all user preferences, then overlay theme colors.
    // New user-setting fields flow through automatically without touching this function.
    AppearanceSettings effective = settings;
    const AppearanceSettings theme = ThemeDefaults(settings.theme);
    effective.windowBg        = theme.windowBg;
    effective.panelBg         = theme.panelBg;
    effective.text            = theme.text;
    effective.mutedText       = theme.mutedText;
    effective.accent          = theme.accent;
    effective.hover           = theme.hover;
    effective.selectedTab     = theme.selectedTab;
    effective.buttonOff       = theme.buttonOff;
    effective.buttonOn        = theme.buttonOn;
    if (!settings.customCommandBarColors) {
        effective.popupFilterBorder = theme.popupFilterBorder;
        effective.popupActionBorder = theme.popupActionBorder;
        effective.popupDestinationBorder = theme.popupDestinationBorder;
        effective.customCommandBarColors = false;
    }
    effective.closeButton     = theme.closeButton;
    effective.closeButtonHover = theme.closeButtonHover;
    effective.closeButtonText = theme.closeButtonText;
    effective.titleBarBg      = theme.titleBarBg;
    effective.titleBarBorder  = theme.titleBarBorder;
    effective.titleBarText    = theme.titleBarText;
    effective.titleMinBase    = theme.titleMinBase;
    effective.titleMaxBase    = theme.titleMaxBase;
    effective.titleCloseBase  = theme.titleCloseBase;
    effective.titleExitBase   = theme.titleExitBase;
    effective.titleMinHover   = theme.titleMinHover;
    effective.titleMaxHover   = theme.titleMaxHover;
    effective.titleCloseHover = theme.titleCloseHover;
    effective.titleExitHover  = theme.titleExitHover;
    effective.titleMinGlyph   = theme.titleMinGlyph;
    effective.titleMaxGlyph   = theme.titleMaxGlyph;
    effective.titleExitGlyph  = theme.titleExitGlyph;
    effective.titleCloseGlyph = theme.titleCloseGlyph;
    effective.iconBoardTop    = theme.iconBoardTop;
    effective.iconBoardBottom = theme.iconBoardBottom;
    effective.iconPaper       = theme.iconPaper;
    effective.iconMarginLine  = theme.iconMarginLine;
    effective.iconRuledLines  = theme.iconRuledLines;
    effective.opacityKnobFill = theme.opacityKnobFill;
    effective.opacityKnobRing = theme.opacityKnobRing;
    effective.scrollbarBg          = theme.scrollbarBg;
    effective.scrollbarGrab        = theme.scrollbarGrab;
    effective.scrollbarGrabHover   = theme.scrollbarGrabHover;
    effective.scrollbarGrabActive  = theme.scrollbarGrabActive;
    effective.uiScale = 1.0f;
    effective.customColors = false;
    return effective;
}

SavedAppearanceTheme ToSavedTheme(const AppearanceSettings& settings, const std::string& name) {
    const AppearanceSettings effective = EffectiveSettings(settings);
    SavedAppearanceTheme saved;
    saved.name = name.empty() ? "Custom" : name;
    saved.windowBg = effective.windowBg;
    saved.panelBg = effective.panelBg;
    saved.text = effective.text;
    saved.mutedText = effective.mutedText;
    saved.accent = effective.accent;
    saved.hover = effective.hover;
    saved.selectedTab = effective.selectedTab;
    saved.buttonOff = effective.buttonOff;
    saved.buttonOn = effective.buttonOn;
    saved.popupFilterBorder = effective.popupFilterBorder;
    saved.popupActionBorder = effective.popupActionBorder;
    saved.popupDestinationBorder = effective.popupDestinationBorder;
    saved.customCommandBarColors = settings.customCommandBarColors;
    saved.closeButton = effective.closeButton;
    saved.closeButtonHover = effective.closeButtonHover;
    saved.closeButtonText = effective.closeButtonText;
    saved.titleBarBg      = effective.titleBarBg;
    saved.titleBarBorder  = effective.titleBarBorder;
    saved.titleBarText    = effective.titleBarText;
    saved.titleMinBase    = effective.titleMinBase;
    saved.titleMaxBase    = effective.titleMaxBase;
    saved.titleCloseBase  = effective.titleCloseBase;
    saved.titleExitBase   = effective.titleExitBase;
    saved.titleMinHover   = effective.titleMinHover;
    saved.titleMaxHover   = effective.titleMaxHover;
    saved.titleCloseHover = effective.titleCloseHover;
    saved.titleExitHover  = effective.titleExitHover;
    saved.titleMinGlyph   = effective.titleMinGlyph;
    saved.titleMaxGlyph   = effective.titleMaxGlyph;
    saved.titleExitGlyph  = effective.titleExitGlyph;
    saved.titleCloseGlyph = effective.titleCloseGlyph;
    saved.iconBoardTop    = effective.iconBoardTop;
    saved.iconBoardBottom = effective.iconBoardBottom;
    saved.iconPaper       = effective.iconPaper;
    saved.iconMarginLine  = effective.iconMarginLine;
    saved.iconRuledLines  = effective.iconRuledLines;
    saved.opacityKnobFill = effective.opacityKnobFill;
    saved.opacityKnobRing = effective.opacityKnobRing;
    saved.scrollbarBg = effective.scrollbarBg;
    saved.scrollbarGrab = effective.scrollbarGrab;
    saved.scrollbarGrabHover = effective.scrollbarGrabHover;
    saved.scrollbarGrabActive = effective.scrollbarGrabActive;
    saved.showScrollbars = effective.showScrollbars;
    saved.scrollbarSize = effective.scrollbarSize;
    saved.scrollbarRounding = effective.scrollbarRounding;
    saved.scrollbarPadding = effective.scrollbarPadding;
    saved.popupRounding = effective.popupRounding;
    saved.popupButtonRowPadding = effective.popupButtonRowPadding;
    saved.popupButtonColumnPadding = effective.popupButtonColumnPadding;
    saved.popupCommandBarRounding = effective.popupCommandBarRounding;
    saved.controlRounding = effective.controlRounding;
    return saved;
}

void ApplySavedTheme(AppearanceSettings& settings, const SavedAppearanceTheme& saved) {
    settings.customColors = true;
    settings.customThemeName = saved.name.empty() ? "Custom" : saved.name;
    settings.windowBg = saved.windowBg;
    settings.panelBg = saved.panelBg;
    settings.text = saved.text;
    settings.mutedText = saved.mutedText;
    settings.accent = saved.accent;
    settings.hover = saved.hover;
    settings.selectedTab = saved.selectedTab;
    settings.buttonOff = saved.buttonOff;
    settings.buttonOn = saved.buttonOn;
    settings.popupFilterBorder = saved.popupFilterBorder;
    settings.popupActionBorder = saved.popupActionBorder;
    settings.popupDestinationBorder = saved.popupDestinationBorder;
    settings.customCommandBarColors = saved.customCommandBarColors;
    settings.closeButton = saved.closeButton;
    settings.closeButtonHover = saved.closeButtonHover;
    settings.closeButtonText = saved.closeButtonText;
    settings.titleBarBg      = saved.titleBarBg;
    settings.titleBarBorder  = saved.titleBarBorder;
    settings.titleBarText    = saved.titleBarText;
    settings.titleMinBase    = saved.titleMinBase;
    settings.titleMaxBase    = saved.titleMaxBase;
    settings.titleCloseBase  = saved.titleCloseBase;
    settings.titleExitBase   = saved.titleExitBase;
    settings.titleMinHover   = saved.titleMinHover;
    settings.titleMaxHover   = saved.titleMaxHover;
    settings.titleCloseHover = saved.titleCloseHover;
    settings.titleExitHover  = saved.titleExitHover;
    settings.titleMinGlyph   = saved.titleMinGlyph;
    settings.titleMaxGlyph   = saved.titleMaxGlyph;
    settings.titleExitGlyph  = saved.titleExitGlyph;
    settings.titleCloseGlyph = saved.titleCloseGlyph;
    settings.iconBoardTop    = saved.iconBoardTop;
    settings.iconBoardBottom = saved.iconBoardBottom;
    settings.iconPaper       = saved.iconPaper;
    settings.iconMarginLine  = saved.iconMarginLine;
    settings.iconRuledLines  = saved.iconRuledLines;
    settings.opacityKnobFill = saved.opacityKnobFill;
    settings.opacityKnobRing = saved.opacityKnobRing;
    settings.scrollbarBg = saved.scrollbarBg;
    settings.scrollbarGrab = saved.scrollbarGrab;
    settings.scrollbarGrabHover = saved.scrollbarGrabHover;
    settings.scrollbarGrabActive = saved.scrollbarGrabActive;
    settings.showScrollbars = saved.showScrollbars;
    settings.scrollbarSize = saved.scrollbarSize;
    settings.scrollbarRounding = saved.scrollbarRounding;
    settings.scrollbarPadding = saved.scrollbarPadding;
    settings.popupRounding = saved.popupRounding;
    settings.popupButtonRowPadding = saved.popupButtonRowPadding;
    settings.popupButtonColumnPadding = saved.popupButtonColumnPadding;
    settings.popupCommandBarRounding = saved.popupCommandBarRounding;
    settings.controlRounding = saved.controlRounding;
}

PopupToggleColors GetPopupToggleColors(ThemeId theme) {
    return GetPopupToggleColors(ThemeDefaults(theme));
}

PopupToggleColors GetPopupToggleColors(const AppearanceSettings& settings) {
    const AppearanceSettings effective = EffectiveSettings(settings);
    const bool light = IsLightTheme(effective.theme);
    const ImVec4 off = effective.buttonOff;
    const ImVec4 on = effective.buttonOn;
    const ImVec4 hover = effective.hover;

    return { off, hover, Brighten(hover, light ? 0.06f : 0.10f),
             on, hover, Darken(on, 0.12f) };
}

float EffectiveUiScale(const AppearanceSettings& settings) {
    return std::clamp(settings.dpiScale, 0.5f, 4.0f);
}

void ApplyThemeStyle(ThemeId theme, bool popupContext) {
    ApplyThemeStyle(ThemeDefaults(theme), popupContext);
}

void ApplyThemeStyle(const AppearanceSettings& settings, bool popupContext) {
    const AppearanceSettings effective = EffectiveSettings(settings);
    const bool light = IsLightTheme(effective.theme);
    const float scale = EffectiveUiScale(effective);

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.FontSizeBase = std::clamp(effective.fontSize, 9.0f, 32.0f);

    if (light)
        ImGui::StyleColorsLight();
    else
        ImGui::StyleColorsDark();

    style.WindowRounding = popupContext ? std::clamp(effective.popupRounding, 0.0f, 18.0f) : 0.0f;
    style.FrameRounding = std::clamp(effective.controlRounding, 0.0f, 12.0f);
    style.ScrollbarRounding = std::clamp(effective.scrollbarRounding, 0.0f, 16.0f);
    style.GrabRounding = std::clamp(effective.controlRounding, 0.0f, 12.0f);
    style.WindowBorderSize = popupContext ? 1.0f : 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = popupContext ? ImVec2(8.0f, 8.0f) : ImVec2(0.0f, 0.0f);
    style.ScrollbarSize = std::clamp(effective.scrollbarSize, 0.0f, 24.0f);
    style.ScrollbarPadding = std::clamp(effective.scrollbarPadding, 0.0f, 8.0f);

    ImVec4* c = style.Colors;
    ImVec4 bg = effective.windowBg;
    ImVec4 panel = effective.panelBg;
    ImVec4 text = effective.text;
    ImVec4 muted = effective.mutedText;
    ImVec4 accent = effective.accent;
    ImVec4 hover = effective.hover;

    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = popupContext ? bg : panel;
    c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_Border] = light ? Darken(panel, 0.16f) : Brighten(panel, 0.16f);
    c[ImGuiCol_FrameBg] = light ? Darken(panel, 0.04f) : Brighten(panel, 0.08f);
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = Brighten(c[ImGuiCol_FrameBg], light ? 0.14f : 0.22f);
    PopupToggleColors toggles = GetPopupToggleColors(effective);
    c[ImGuiCol_Button] = toggles.off;
    c[ImGuiCol_ButtonHovered] = toggles.offHovered;
    c[ImGuiCol_ButtonActive] = toggles.offActive;
    c[ImGuiCol_Header] = c[ImGuiCol_Button];
    c[ImGuiCol_HeaderHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_HeaderActive] = c[ImGuiCol_ButtonActive];
    c[ImGuiCol_Tab] = Darken(panel, light ? 0.04f : 0.02f);
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = Mix(c[ImGuiCol_Tab], accent, light ? 0.18f : 0.24f);
    c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(Brighten(accent, 0.12f).x, Brighten(accent, 0.12f).y, Brighten(accent, 0.12f).z, 0.75f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(accent.x, accent.y, accent.z, 0.95f);
    c[ImGuiCol_CheckMark] = light ? Color(255, 255, 255) : text;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = Brighten(accent, 0.18f);
    c[ImGuiCol_ScrollbarBg] = effective.scrollbarBg;
    c[ImGuiCol_ScrollbarGrab] = effective.scrollbarGrab;
    c[ImGuiCol_ScrollbarGrabHovered] = effective.scrollbarGrabHover;
    c[ImGuiCol_ScrollbarGrabActive] = effective.scrollbarGrabActive;
    c[ImGuiCol_TableHeaderBg] = light ? Darken(panel, 0.06f) : Brighten(panel, 0.10f);
    c[ImGuiCol_TableRowBgAlt] = light ? Darken(panel, 0.025f) : Brighten(panel, 0.035f);

    if (scale != 1.0f)
        style.ScaleAllSizes(scale);
}

bool RebuildFontAtlas(ImGuiIO& io, const AppearanceSettings& settings) {
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;

    io.Fonts->Clear();
    const float baseSize = std::clamp(settings.fontSize, 9.0f, 32.0f);
    const float size = baseSize * EffectiveUiScale(settings);
    ImFont* font = nullptr;
    auto fontExists = [](const std::string& path) {
#ifdef _WIN32
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
        std::error_code ec;
        return std::filesystem::exists(path, ec);
#endif
    };

    if (!settings.fontPath.empty() && fontExists(settings.fontPath)) {
        font = io.Fonts->AddFontFromFileTTF(settings.fontPath.c_str(), size, &fontCfg);
    } else {
#ifdef _WIN32
        const char* fallback = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (fontExists(fallback))
            font = io.Fonts->AddFontFromFileTTF(fallback, size, &fontCfg);
        else
#endif
            font = io.Fonts->AddFontDefault();
    }
    io.FontDefault = font;
    io.FontGlobalScale = 1.0f;
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = size;
    style.FontScaleMain = 1.0f;
    return io.Fonts->Build();
}
