#include "Appearance.h"

#include <windows.h>
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

PopupToggleColors GetPopupToggleColors(ThemeId theme) {
    const bool light = theme == ThemeId::GitHubLight ||
                       theme == ThemeId::SolarizedLight ||
                       theme == ThemeId::VSLight ||
                       theme == ThemeId::QuietLight;

    ImVec4 off = light ? Color(224, 229, 235) : Color(47, 50, 56);
    ImVec4 offHovered = light ? Color(211, 219, 228) : Color(58, 62, 70);
    ImVec4 offActive = light ? Color(197, 207, 219) : Color(68, 74, 84);
    ImVec4 on = Color(38, 121, 255);

    switch (theme) {
    case ThemeId::Dracula:        off = Color(68, 71, 90); offHovered = Color(78, 82, 104); offActive = Color(88, 92, 116); on = Color(80, 250, 123); break;
    case ThemeId::Nord:           off = Color(59, 66, 82); offHovered = Color(67, 76, 94); offActive = Color(76, 86, 106); on = Color(136, 192, 208); break;
    case ThemeId::Monokai:        off = Color(55, 56, 48); offHovered = Color(66, 68, 57); offActive = Color(75, 77, 65); on = Color(166, 226, 46); break;
    case ThemeId::OneDark:        off = Color(40, 44, 52); offHovered = Color(50, 56, 66); offActive = Color(60, 66, 78); on = Color(97, 175, 239); break;
    case ThemeId::TokyoNight:     off = Color(36, 40, 59); offHovered = Color(45, 50, 72); offActive = Color(54, 60, 86); on = Color(187, 154, 247); break;
    case ThemeId::SolarizedDark:  off = Color(7, 54, 66); offHovered = Color(18, 68, 82); offActive = Color(28, 82, 96); on = Color(38, 139, 210); break;
    case ThemeId::GitHubDark:     off = Color(22, 27, 34); offHovered = Color(33, 38, 45); offActive = Color(44, 51, 61); on = Color(88, 166, 255); break;
    case ThemeId::SolarizedLight: off = Color(238, 232, 213); offHovered = Color(228, 222, 203); offActive = Color(218, 211, 193); on = Color(38, 139, 210); break;
    case ThemeId::VSLight:        off = Color(230, 230, 230); offHovered = Color(218, 228, 238); offActive = Color(205, 219, 235); on = Color(0, 122, 204); break;
    case ThemeId::QuietLight:     off = Color(235, 235, 235); offHovered = Color(224, 229, 238); offActive = Color(210, 220, 235); on = Color(64, 120, 242); break;
    default:                      break;
    }

    return { off, Brighten(off, light ? 0.10f : 0.16f), Brighten(off, light ? 0.16f : 0.24f),
             on, Brighten(on, 0.16f), Darken(on, 0.12f) };
}

void ApplyThemeStyle(ThemeId theme, bool popupContext) {
    const bool light = theme == ThemeId::GitHubLight ||
                       theme == ThemeId::SolarizedLight ||
                       theme == ThemeId::VSLight ||
                       theme == ThemeId::QuietLight;

    if (light)
        ImGui::StyleColorsLight();
    else
        ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = popupContext ? 6.0f : 0.0f;
    style.FrameRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowBorderSize = popupContext ? 1.0f : 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = popupContext ? ImVec2(8.0f, 8.0f) : ImVec2(0.0f, 0.0f);

    ImVec4* c = style.Colors;
    ImVec4 bg = light ? Color(246, 248, 250) : Color(30, 30, 30);
    ImVec4 panel = light ? Color(255, 255, 255) : Color(37, 37, 38);
    ImVec4 text = light ? Color(36, 41, 47) : Color(220, 220, 220);
    ImVec4 muted = light ? Color(87, 96, 106) : Color(150, 150, 150);
    ImVec4 accent = Color(38, 121, 255);

    switch (theme) {
    case ThemeId::Dracula:
        bg = Color(40, 42, 54); panel = Color(68, 71, 90); text = Color(248, 248, 242); muted = Color(189, 147, 249); accent = Color(80, 250, 123); break;
    case ThemeId::Nord:
        bg = Color(46, 52, 64); panel = Color(59, 66, 82); text = Color(236, 239, 244); muted = Color(216, 222, 233); accent = Color(136, 192, 208); break;
    case ThemeId::Monokai:
        bg = Color(39, 40, 34); panel = Color(55, 56, 48); text = Color(248, 248, 242); muted = Color(166, 226, 46); accent = Color(102, 217, 239); break;
    case ThemeId::OneDark:
        bg = Color(33, 37, 43); panel = Color(40, 44, 52); text = Color(171, 178, 191); muted = Color(130, 137, 151); accent = Color(97, 175, 239); break;
    case ThemeId::TokyoNight:
        bg = Color(26, 27, 38); panel = Color(36, 40, 59); text = Color(192, 202, 245); muted = Color(122, 162, 247); accent = Color(187, 154, 247); break;
    case ThemeId::SolarizedDark:
        bg = Color(0, 43, 54); panel = Color(7, 54, 66); text = Color(131, 148, 150); muted = Color(88, 110, 117); accent = Color(38, 139, 210); break;
    case ThemeId::GitHubDark:
        bg = Color(13, 17, 23); panel = Color(22, 27, 34); text = Color(201, 209, 217); muted = Color(139, 148, 158); accent = Color(88, 166, 255); break;
    case ThemeId::SolarizedLight:
        bg = Color(253, 246, 227); panel = Color(238, 232, 213); text = Color(101, 123, 131); muted = Color(147, 161, 161); accent = Color(38, 139, 210); break;
    case ThemeId::VSLight:
        bg = Color(245, 245, 245); panel = Color(255, 255, 255); text = Color(30, 30, 30); muted = Color(104, 104, 104); accent = Color(0, 122, 204); break;
    case ThemeId::QuietLight:
        bg = Color(250, 250, 250); panel = Color(243, 243, 243); text = Color(51, 51, 51); muted = Color(110, 110, 110); accent = Color(64, 120, 242); break;
    default:
        break;
    }

    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = popupContext ? bg : panel;
    c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_Border] = light ? Darken(panel, 0.16f) : Brighten(panel, 0.16f);
    c[ImGuiCol_FrameBg] = light ? Darken(panel, 0.04f) : Brighten(panel, 0.08f);
    c[ImGuiCol_FrameBgHovered] = Brighten(c[ImGuiCol_FrameBg], light ? 0.08f : 0.14f);
    c[ImGuiCol_FrameBgActive] = Brighten(c[ImGuiCol_FrameBg], light ? 0.14f : 0.22f);
    PopupToggleColors toggles = GetPopupToggleColors(theme);
    c[ImGuiCol_Button] = toggles.off;
    c[ImGuiCol_ButtonHovered] = toggles.offHovered;
    c[ImGuiCol_ButtonActive] = toggles.offActive;
    c[ImGuiCol_Header] = c[ImGuiCol_Button];
    c[ImGuiCol_HeaderHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_HeaderActive] = c[ImGuiCol_ButtonActive];
    c[ImGuiCol_Tab] = Darken(panel, light ? 0.04f : 0.02f);
    c[ImGuiCol_TabHovered] = Brighten(c[ImGuiCol_Tab], light ? 0.10f : 0.18f);
    c[ImGuiCol_TabSelected] = Mix(c[ImGuiCol_Tab], accent, light ? 0.18f : 0.24f);
    c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(Brighten(accent, 0.12f).x, Brighten(accent, 0.12f).y, Brighten(accent, 0.12f).z, 0.75f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(accent.x, accent.y, accent.z, 0.95f);
    c[ImGuiCol_CheckMark] = light ? Color(255, 255, 255) : text;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = Brighten(accent, 0.18f);
    c[ImGuiCol_TableHeaderBg] = light ? Darken(panel, 0.06f) : Brighten(panel, 0.10f);
    c[ImGuiCol_TableRowBgAlt] = light ? Darken(panel, 0.025f) : Brighten(panel, 0.035f);
}

bool RebuildFontAtlas(ImGuiIO& io, const AppearanceSettings& settings) {
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;

    io.Fonts->Clear();
    const float size = std::clamp(settings.fontSize, 9.0f, 32.0f);
    ImFont* font = nullptr;
    if (!settings.fontPath.empty() &&
        GetFileAttributesA(settings.fontPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        font = io.Fonts->AddFontFromFileTTF(settings.fontPath.c_str(), size, &fontCfg);
    } else {
        const char* fallback = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (GetFileAttributesA(fallback) != INVALID_FILE_ATTRIBUTES)
            font = io.Fonts->AddFontFromFileTTF(fallback, size, &fontCfg);
        else
            font = io.Fonts->AddFontDefault();
    }
    io.FontDefault = font;
    io.FontGlobalScale = size / 15.0f;
    return io.Fonts->Build();
}
