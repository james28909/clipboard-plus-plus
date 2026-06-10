#include "ConfigStore.h"

#include <json.hpp>
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {

std::filesystem::path ResolveConfigPath() {
#ifdef _WIN32
    PWSTR roaming = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        base = roaming;
        CoTaskMemFree(roaming);
    } else {
        char buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
        base = n > 0 ? std::filesystem::path(buf) : std::filesystem::current_path();
    }
    return base / "Clipboard++" / "config.json";
#else
    const char* configHome = std::getenv("XDG_CONFIG_HOME");
    if (configHome && configHome[0] != '\0')
        return std::filesystem::path(configHome) / "clipboardpp" / "config.json";

    const char* home = std::getenv("HOME");
    std::filesystem::path base = home && home[0] != '\0'
        ? std::filesystem::path(home) / ".config"
        : std::filesystem::current_path();
    return base / "clipboardpp" / "config.json";
#endif
}

int ThemeToInt(ThemeId theme) {
    return std::clamp(static_cast<int>(theme), 0, static_cast<int>(ThemeId::Count) - 1);
}

ThemeId ThemeFromInt(int value) {
    return static_cast<ThemeId>(std::clamp(value, 0, static_cast<int>(ThemeId::Count) - 1));
}

json ColorToJson(const ImVec4& color) {
    return json::array({color.x, color.y, color.z, color.w});
}

ImVec4 ColorFromJson(const json& j, const ImVec4& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    auto at = [&](size_t index, float value) {
        return j.size() > index && j[index].is_number()
            ? std::clamp(j[index].get<float>(), 0.0f, 1.0f)
            : value;
    };
    return ImVec4(
        at(0, fallback.x),
        at(1, fallback.y),
        at(2, fallback.z),
        at(3, fallback.w));
}

SavedAppearanceTheme SavedThemeFromJson(const json& item) {
    SavedAppearanceTheme saved;
    saved.name = item.value("name", saved.name);
    saved.windowBg = ColorFromJson(item.value("windowBg", json::array()), saved.windowBg);
    saved.panelBg = ColorFromJson(item.value("panelBg", json::array()), saved.panelBg);
    saved.text = ColorFromJson(item.value("text", json::array()), saved.text);
    saved.mutedText = ColorFromJson(item.value("mutedText", json::array()), saved.mutedText);
    saved.accent = ColorFromJson(item.value("accent", json::array()), saved.accent);
    saved.hover = ColorFromJson(item.value("hover", json::array()), saved.hover);
    saved.selectedTab = ColorFromJson(item.value("selectedTab", json::array()), saved.selectedTab);
    saved.buttonOff = ColorFromJson(item.value("buttonOff", json::array()), saved.buttonOff);
    saved.buttonOn = ColorFromJson(item.value("buttonOn", json::array()), saved.buttonOn);
    saved.closeButton = ColorFromJson(item.value("closeButton", json::array()), saved.closeButton);
    saved.closeButtonHover = ColorFromJson(item.value("closeButtonHover", json::array()), saved.closeButtonHover);
    saved.closeButtonText = ColorFromJson(item.value("closeButtonText", json::array()), saved.closeButtonText);
    saved.opacityKnobFill = ColorFromJson(item.value("opacityKnobFill", json::array()), saved.opacityKnobFill);
    saved.opacityKnobRing = ColorFromJson(item.value("opacityKnobRing", json::array()), saved.opacityKnobRing);
    saved.scrollbarBg = ColorFromJson(item.value("scrollbarBg", json::array()), saved.scrollbarBg);
    saved.scrollbarGrab = ColorFromJson(item.value("scrollbarGrab", json::array()), saved.scrollbarGrab);
    saved.scrollbarGrabHover = ColorFromJson(item.value("scrollbarGrabHover", json::array()), saved.scrollbarGrabHover);
    saved.scrollbarGrabActive = ColorFromJson(item.value("scrollbarGrabActive", json::array()), saved.scrollbarGrabActive);
    saved.showScrollbars = item.value("showScrollbars", saved.showScrollbars);
    saved.scrollbarSize = std::clamp(item.value("scrollbarSize", saved.scrollbarSize), 0.0f, 24.0f);
    saved.scrollbarRounding = std::clamp(item.value("scrollbarRounding", saved.scrollbarRounding), 0.0f, 16.0f);
    saved.scrollbarPadding = std::clamp(item.value("scrollbarPadding", saved.scrollbarPadding), 0.0f, 8.0f);
    saved.popupRounding = std::clamp(item.value("popupRounding", saved.popupRounding), 0.0f, 18.0f);
    saved.controlRounding = std::clamp(item.value("controlRounding", saved.controlRounding), 0.0f, 12.0f);
    return saved;
}

json SavedThemeToJson(const SavedAppearanceTheme& saved) {
    return {
        {"name", saved.name},
        {"windowBg", ColorToJson(saved.windowBg)},
        {"panelBg", ColorToJson(saved.panelBg)},
        {"text", ColorToJson(saved.text)},
        {"mutedText", ColorToJson(saved.mutedText)},
        {"accent", ColorToJson(saved.accent)},
        {"hover", ColorToJson(saved.hover)},
        {"selectedTab", ColorToJson(saved.selectedTab)},
        {"buttonOff", ColorToJson(saved.buttonOff)},
        {"buttonOn", ColorToJson(saved.buttonOn)},
        {"closeButton", ColorToJson(saved.closeButton)},
        {"closeButtonHover", ColorToJson(saved.closeButtonHover)},
        {"closeButtonText", ColorToJson(saved.closeButtonText)},
        {"opacityKnobFill", ColorToJson(saved.opacityKnobFill)},
        {"opacityKnobRing", ColorToJson(saved.opacityKnobRing)},
        {"scrollbarBg", ColorToJson(saved.scrollbarBg)},
        {"scrollbarGrab", ColorToJson(saved.scrollbarGrab)},
        {"scrollbarGrabHover", ColorToJson(saved.scrollbarGrabHover)},
        {"scrollbarGrabActive", ColorToJson(saved.scrollbarGrabActive)},
        {"showScrollbars", saved.showScrollbars},
        {"scrollbarSize", saved.scrollbarSize},
        {"scrollbarRounding", saved.scrollbarRounding},
        {"scrollbarPadding", saved.scrollbarPadding},
        {"popupRounding", saved.popupRounding},
        {"controlRounding", saved.controlRounding},
    };
}

json BindingToJson(const KeyBinding& b) {
    return {
        {"ctrl", b.ctrl},
        {"shift", b.shift},
        {"alt", b.alt},
        {"vkey", b.vkey},
        {"action", static_cast<int>(b.action)},
        {"data", b.data},
    };
}

KeyBinding BindingFromJson(const json& j, const KeyBinding& fallback) {
    KeyBinding b = fallback;
    b.ctrl = j.value("ctrl", b.ctrl);
    b.shift = j.value("shift", b.shift);
    b.alt = j.value("alt", b.alt);
    b.vkey = j.value("vkey", b.vkey);
    b.action = static_cast<HotkeyAction>(j.value("action", static_cast<int>(b.action)));
    b.data = j.value("data", b.data);
    return b;
}

KeyBinding DefaultBindingForAction(HotkeyAction action) {
    for (const KeyBinding& b : HotkeyManager::DefaultBindings()) {
        if (b.action == action) return b;
    }
    return {};
}

bool IsFontPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".ttf" || ext == ".otf";
}

std::filesystem::path UniqueDestination(std::filesystem::path dir, std::filesystem::path filename) {
    std::filesystem::path dest = dir / filename.filename();
    if (!std::filesystem::exists(dest))
        return dest;

    const std::filesystem::path stem = filename.stem();
    const std::filesystem::path ext = filename.extension();
    for (int i = 1; i < 1000; ++i) {
        dest = dir / (stem.wstring() + L"-" + std::to_wstring(i) + ext.wstring());
        if (!std::filesystem::exists(dest))
            return dest;
    }
    return dir / filename.filename();
}

void LoadAppearance(const json& root, AppConfig& config) {
    const json& a = root.value("appearance", json::object());
    config.appearance.theme = ThemeFromInt(a.value("theme", ThemeToInt(config.appearance.theme)));
    config.appearance.popupOpacity = std::clamp(a.value("popupOpacity", config.appearance.popupOpacity), 0.1f, 1.0f);
    config.appearance.popupOutlineStrength =
        std::clamp(a.value("popupOutlineStrength", config.appearance.popupOutlineStrength), 0.0f, 1.0f);
    config.appearance.popupOutlineAnimated =
        a.value("popupOutlineAnimated", config.appearance.popupOutlineAnimated);
    config.appearance.popupOutlineAnimationSpeed =
        std::clamp(a.value("popupOutlineAnimationSpeed", config.appearance.popupOutlineAnimationSpeed), 0.05f, 5.0f);
    config.appearance.popupOutlineColorSharpness =
        std::clamp(a.value("popupOutlineColorSharpness", config.appearance.popupOutlineColorSharpness), 0.0f, 1.0f);
    config.appearance.popupOutlineColorSpread =
        std::clamp(a.value("popupOutlineColorSpread", config.appearance.popupOutlineColorSpread), 0.0f, 2.0f);
    config.appearance.popupOutlineSaturation =
        std::clamp(a.value("popupOutlineSaturation", config.appearance.popupOutlineSaturation), 0.0f, 1.0f);
    config.appearance.popupOutlineBrightness =
        std::clamp(a.value("popupOutlineBrightness", config.appearance.popupOutlineBrightness), 0.20f, 1.0f);
    config.appearance.popupOutlineReverse =
        a.value("popupOutlineReverse", config.appearance.popupOutlineReverse);
    config.appearance.popupWidth = std::max(360, a.value("popupWidth", config.appearance.popupWidth));
    config.appearance.popupHeight = std::max(260, a.value("popupHeight", config.appearance.popupHeight));
    config.appearance.mainWindowWidth = std::max(800, a.value("mainWindowWidth", config.appearance.mainWindowWidth));
    config.appearance.mainWindowHeight = std::max(500, a.value("mainWindowHeight", config.appearance.mainWindowHeight));
    config.appearance.fontPath = a.value("fontPath", config.appearance.fontPath);
    config.appearance.fontSize = std::clamp(a.value("fontSize", config.appearance.fontSize), 9.0f, 32.0f);
    config.appearance.uiScale = 1.0f;
    config.appearance.customColors = a.value("customColors", config.appearance.customColors);
    config.appearance.customThemeName = a.value("customThemeName", config.appearance.customThemeName);
    config.appearance.windowBg = ColorFromJson(a.value("windowBg", json::array()), config.appearance.windowBg);
    config.appearance.panelBg = ColorFromJson(a.value("panelBg", json::array()), config.appearance.panelBg);
    config.appearance.text = ColorFromJson(a.value("text", json::array()), config.appearance.text);
    config.appearance.mutedText = ColorFromJson(a.value("mutedText", json::array()), config.appearance.mutedText);
    config.appearance.accent = ColorFromJson(a.value("accent", json::array()), config.appearance.accent);
    config.appearance.hover = ColorFromJson(a.value("hover", json::array()), config.appearance.hover);
    config.appearance.selectedTab = ColorFromJson(a.value("selectedTab", json::array()), config.appearance.selectedTab);
    config.appearance.buttonOff = ColorFromJson(a.value("buttonOff", json::array()), config.appearance.buttonOff);
    config.appearance.buttonOn = ColorFromJson(a.value("buttonOn", json::array()), config.appearance.buttonOn);
    config.appearance.closeButton = ColorFromJson(a.value("closeButton", json::array()), config.appearance.closeButton);
    config.appearance.closeButtonHover =
        ColorFromJson(a.value("closeButtonHover", json::array()), config.appearance.closeButtonHover);
    config.appearance.closeButtonText =
        ColorFromJson(a.value("closeButtonText", json::array()), config.appearance.closeButtonText);
    config.appearance.opacityKnobFill =
        ColorFromJson(a.value("opacityKnobFill", json::array()), config.appearance.opacityKnobFill);
    config.appearance.opacityKnobRing =
        ColorFromJson(a.value("opacityKnobRing", json::array()), config.appearance.opacityKnobRing);
    config.appearance.scrollbarBg =
        ColorFromJson(a.value("scrollbarBg", json::array()), config.appearance.scrollbarBg);
    config.appearance.scrollbarGrab =
        ColorFromJson(a.value("scrollbarGrab", json::array()), config.appearance.scrollbarGrab);
    config.appearance.scrollbarGrabHover =
        ColorFromJson(a.value("scrollbarGrabHover", json::array()), config.appearance.scrollbarGrabHover);
    config.appearance.scrollbarGrabActive =
        ColorFromJson(a.value("scrollbarGrabActive", json::array()), config.appearance.scrollbarGrabActive);
    config.appearance.showScrollbars =
        a.value("showScrollbars", config.appearance.showScrollbars);
    config.appearance.scrollbarSize =
        std::clamp(a.value("scrollbarSize", config.appearance.scrollbarSize), 0.0f, 24.0f);
    config.appearance.scrollbarRounding =
        std::clamp(a.value("scrollbarRounding", config.appearance.scrollbarRounding), 0.0f, 16.0f);
    config.appearance.scrollbarPadding =
        std::clamp(a.value("scrollbarPadding", config.appearance.scrollbarPadding), 0.0f, 8.0f);
    config.appearance.popupRounding =
        std::clamp(a.value("popupRounding", config.appearance.popupRounding), 0.0f, 18.0f);
    config.appearance.controlRounding =
        std::clamp(a.value("controlRounding", config.appearance.controlRounding), 0.0f, 12.0f);
    config.appearance.savedThemes.clear();
    if (a.contains("savedThemes") && a["savedThemes"].is_array()) {
        for (const json& item : a["savedThemes"]) {
            SavedAppearanceTheme saved = SavedThemeFromJson(item);
            if (!saved.name.empty())
                config.appearance.savedThemes.push_back(std::move(saved));
        }
    }
}

void LoadHotkeys(const json& root, AppConfig& config) {
    const json& h = root.value("hotkeys", json::object());
    const HotkeySettings defaults = HotkeyManager::DefaultSettings();
    config.hotkeys.hiddenPasteCtrl = h.value("hiddenPasteCtrl", defaults.hiddenPasteCtrl);
    config.hotkeys.hiddenPasteShift = h.value("hiddenPasteShift", defaults.hiddenPasteShift);
    config.hotkeys.hiddenPasteAlt = h.value("hiddenPasteAlt", defaults.hiddenPasteAlt);
    config.hotkeys.hiddenPasteFunctionKeys = h.value("hiddenPasteFunctionKeys", defaults.hiddenPasteFunctionKeys);

    if (!h.contains("bindings") || !h["bindings"].is_array())
        return;

    for (const json& item : h["bindings"]) {
        HotkeyAction action = static_cast<HotkeyAction>(item.value("action", 0));
        auto it = std::find_if(config.hotkeys.bindings.begin(), config.hotkeys.bindings.end(),
            [&](const KeyBinding& b) { return b.action == action; });
        if (it != config.hotkeys.bindings.end())
            *it = BindingFromJson(item, *it);
        else if (action != HotkeyAction::None)
            config.hotkeys.bindings.push_back(BindingFromJson(item, DefaultBindingForAction(action)));
    }
}

void LoadDeveloper(const json& root, AppConfig& config) {
    const json& d = root.value("developer", json::object());
    config.developer.enabled = d.value("enabled", config.developer.enabled);
    config.developer.cliEnabled = d.value("cliEnabled", config.developer.cliEnabled);
    config.developer.showSourceProcess =
        d.value("showSourceProcess", config.developer.showSourceProcess);
    config.developer.eventLogEnabled =
        d.value("eventLogEnabled", config.developer.eventLogEnabled);
}

ClipboardProfileConfig DefaultClipboardProfile() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return {
        "default",
        "Default",
        ts.str(),
        ts.str(),
        "",
    };
}

void EnsureClipboardProfiles(AppConfig& config) {
    if (config.clipboards.empty())
        config.clipboards.push_back(DefaultClipboardProfile());

    auto active = std::find_if(config.clipboards.begin(), config.clipboards.end(),
        [&](const ClipboardProfileConfig& c) { return c.id == config.activeClipboardId; });
    if (active == config.clipboards.end())
        config.activeClipboardId = config.clipboards.front().id;
}

void LoadClipboards(const json& root, AppConfig& config) {
    config.activeClipboardId = root.value("activeClipboardId", config.activeClipboardId);
    config.autoSwitchClipboardByProcess =
        root.value("autoSwitchClipboardByProcess", config.autoSwitchClipboardByProcess);
    config.autoCreateClipboardByProcess =
        root.value("autoCreateClipboardByProcess", config.autoCreateClipboardByProcess);

    if (!root.contains("clipboards") || !root["clipboards"].is_array()) {
        EnsureClipboardProfiles(config);
        return;
    }

    config.clipboards.clear();
    for (const json& item : root["clipboards"]) {
        ClipboardProfileConfig profile;
        profile.id = item.value("id", "");
        profile.name = item.value("name", "");
        profile.createdAt = item.value("createdAt", "");
        profile.updatedAt = item.value("updatedAt", profile.createdAt);
        profile.processName = item.value("processName", "");
        if (profile.id.empty())
            continue;
        if (profile.name.empty())
            profile.name = profile.id;
        config.clipboards.push_back(std::move(profile));
    }

    EnsureClipboardProfiles(config);
}

} // namespace

namespace ConfigStore {

AppConfig Load() {
    AppConfig config;
    const std::filesystem::path path = ResolveConfigPath();
    std::ifstream in(path);
    if (!in) {
        EnsureClipboardProfiles(config);
        return config;
    }

    try {
        json root = json::parse(in, nullptr, true, true);
        LoadAppearance(root, config);
        LoadHotkeys(root, config);
        LoadDeveloper(root, config);
        config.newItemsAtTop = root.value("newItemsAtTop", config.newItemsAtTop);
        config.appendNewlineAfterPaste = root.value("appendNewlineAfterPaste", config.appendNewlineAfterPaste);
        config.pasteMoveTarget = std::clamp(root.value("pasteMoveTarget", config.pasteMoveTarget), 0, 2);
        LoadClipboards(root, config);
    } catch (...) {
        return AppConfig{};
    }

    EnsureClipboardProfiles(config);

    return config;
}

bool Save(const AppConfig& config) {
    const std::filesystem::path path = ResolveConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    json bindings = json::array();
    for (const KeyBinding& b : config.hotkeys.bindings)
        bindings.push_back(BindingToJson(b));

    json savedThemes = json::array();
    for (const SavedAppearanceTheme& saved : config.appearance.savedThemes)
        savedThemes.push_back(SavedThemeToJson(saved));

    json clipboards = json::array();
    for (const ClipboardProfileConfig& profile : config.clipboards) {
        clipboards.push_back({
            {"id", profile.id},
            {"name", profile.name},
            {"createdAt", profile.createdAt},
            {"updatedAt", profile.updatedAt},
            {"processName", profile.processName},
        });
    }

    json root = {
        {"version", 1},
        {"appearance", {
            {"theme", ThemeToInt(config.appearance.theme)},
            {"popupOpacity", config.appearance.popupOpacity},
            {"popupOutlineStrength", config.appearance.popupOutlineStrength},
            {"popupOutlineAnimated", config.appearance.popupOutlineAnimated},
            {"popupOutlineAnimationSpeed", config.appearance.popupOutlineAnimationSpeed},
            {"popupOutlineColorSharpness", config.appearance.popupOutlineColorSharpness},
            {"popupOutlineColorSpread", config.appearance.popupOutlineColorSpread},
            {"popupOutlineSaturation", config.appearance.popupOutlineSaturation},
            {"popupOutlineBrightness", config.appearance.popupOutlineBrightness},
            {"popupOutlineReverse", config.appearance.popupOutlineReverse},
            {"popupWidth", config.appearance.popupWidth},
            {"popupHeight", config.appearance.popupHeight},
            {"mainWindowWidth", config.appearance.mainWindowWidth},
            {"mainWindowHeight", config.appearance.mainWindowHeight},
            {"fontPath", config.appearance.fontPath},
            {"fontSize", config.appearance.fontSize},
            {"uiScale", 1.0f},
            {"customColors", config.appearance.customColors},
            {"customThemeName", config.appearance.customThemeName},
            {"windowBg", ColorToJson(config.appearance.windowBg)},
            {"panelBg", ColorToJson(config.appearance.panelBg)},
            {"text", ColorToJson(config.appearance.text)},
            {"mutedText", ColorToJson(config.appearance.mutedText)},
            {"accent", ColorToJson(config.appearance.accent)},
            {"hover", ColorToJson(config.appearance.hover)},
            {"selectedTab", ColorToJson(config.appearance.selectedTab)},
            {"buttonOff", ColorToJson(config.appearance.buttonOff)},
            {"buttonOn", ColorToJson(config.appearance.buttonOn)},
            {"closeButton", ColorToJson(config.appearance.closeButton)},
            {"closeButtonHover", ColorToJson(config.appearance.closeButtonHover)},
            {"closeButtonText", ColorToJson(config.appearance.closeButtonText)},
            {"opacityKnobFill", ColorToJson(config.appearance.opacityKnobFill)},
            {"opacityKnobRing", ColorToJson(config.appearance.opacityKnobRing)},
            {"scrollbarBg", ColorToJson(config.appearance.scrollbarBg)},
            {"scrollbarGrab", ColorToJson(config.appearance.scrollbarGrab)},
            {"scrollbarGrabHover", ColorToJson(config.appearance.scrollbarGrabHover)},
            {"scrollbarGrabActive", ColorToJson(config.appearance.scrollbarGrabActive)},
            {"showScrollbars", config.appearance.showScrollbars},
            {"scrollbarSize", config.appearance.scrollbarSize},
            {"scrollbarRounding", config.appearance.scrollbarRounding},
            {"scrollbarPadding", config.appearance.scrollbarPadding},
            {"popupRounding", config.appearance.popupRounding},
            {"controlRounding", config.appearance.controlRounding},
            {"savedThemes", savedThemes},
        }},
        {"hotkeys", {
            {"bindings", bindings},
            {"hiddenPasteCtrl", config.hotkeys.hiddenPasteCtrl},
            {"hiddenPasteShift", config.hotkeys.hiddenPasteShift},
            {"hiddenPasteAlt", config.hotkeys.hiddenPasteAlt},
            {"hiddenPasteFunctionKeys", config.hotkeys.hiddenPasteFunctionKeys},
        }},
        {"developer", {
            {"enabled", config.developer.enabled},
            {"cliEnabled", config.developer.cliEnabled},
            {"showSourceProcess", config.developer.showSourceProcess},
            {"eventLogEnabled", config.developer.eventLogEnabled},
        }},
        {"newItemsAtTop", config.newItemsAtTop},
        {"appendNewlineAfterPaste", config.appendNewlineAfterPaste},
        {"pasteMoveTarget", config.pasteMoveTarget},
        {"activeClipboardId", config.activeClipboardId},
        {"autoSwitchClipboardByProcess", config.autoSwitchClipboardByProcess},
        {"autoCreateClipboardByProcess", config.autoCreateClipboardByProcess},
        {"clipboards", clipboards},
    };

    std::ofstream out(path);
    if (!out) return false;
    out << root.dump(2);
    return true;
}

std::filesystem::path Path() {
    return ResolveConfigPath();
}

std::filesystem::path Directory() {
    return Path().parent_path();
}

std::filesystem::path FontsDirectory() {
    return Directory() / "fonts";
}

std::filesystem::path ImportFontFile(const std::filesystem::path& source) {
    std::error_code ec;
    if (source.empty() || !std::filesystem::exists(source, ec) || !IsFontPath(source))
        return {};

    const std::filesystem::path src = std::filesystem::absolute(source, ec);
    if (ec) return {};

    const std::filesystem::path fonts = FontsDirectory();
    std::filesystem::create_directories(fonts, ec);
    if (ec) return {};

    const std::filesystem::path destDir = std::filesystem::absolute(fonts, ec);
    if (ec) return {};

    std::filesystem::path rel;
    rel = std::filesystem::relative(src, destDir, ec);
    if (!ec && !rel.empty() && *rel.begin() != "..")
        return src;

    std::filesystem::path dest = UniqueDestination(destDir, src.filename());
    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::skip_existing, ec);
    if (ec) return {};
    return dest;
}

} // namespace ConfigStore
