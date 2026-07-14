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
#include <vector>

using json = nlohmann::json;

namespace {

std::filesystem::path ResolveConfigPath() {
#ifdef _WIN32
#ifndef NDEBUG
    const DWORD testPathChars = GetEnvironmentVariableW(
        L"CLIPBOARDPP_TEST_DATA_DIR", nullptr, 0);
    if (testPathChars > 1) {
        std::vector<wchar_t> testPath(testPathChars);
        const DWORD written = GetEnvironmentVariableW(
            L"CLIPBOARDPP_TEST_DATA_DIR", testPath.data(), testPathChars);
        if (written > 0 && written < testPathChars)
            return std::filesystem::path(testPath.data()) / "config.json";
    }
#endif
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

// Load/save the shared color + shape fields that appear in both AppearanceSettings
// and SavedAppearanceTheme.  Adding a new color field means touching only these two
// helpers instead of every load/save site.
template<typename T>
void LoadColorFields(T& s, const json& j) {
    s.windowBg        = ColorFromJson(j.value("windowBg",         json::array()), s.windowBg);
    s.panelBg         = ColorFromJson(j.value("panelBg",          json::array()), s.panelBg);
    s.text            = ColorFromJson(j.value("text",             json::array()), s.text);
    s.mutedText       = ColorFromJson(j.value("mutedText",        json::array()), s.mutedText);
    s.accent          = ColorFromJson(j.value("accent",           json::array()), s.accent);
    s.hover           = ColorFromJson(j.value("hover",            json::array()), s.hover);
    s.selectedTab     = ColorFromJson(j.value("selectedTab",      json::array()), s.selectedTab);
    s.buttonOff       = ColorFromJson(j.value("buttonOff",        json::array()), s.buttonOff);
    s.buttonOn        = ColorFromJson(j.value("buttonOn",         json::array()), s.buttonOn);
    s.closeButton     = ColorFromJson(j.value("closeButton",      json::array()), s.closeButton);
    s.closeButtonHover= ColorFromJson(j.value("closeButtonHover", json::array()), s.closeButtonHover);
    s.closeButtonText = ColorFromJson(j.value("closeButtonText",  json::array()), s.closeButtonText);
    s.titleBarBg      = ColorFromJson(j.value("titleBarBg",       json::array()), s.titleBarBg);
    s.titleBarBorder  = ColorFromJson(j.value("titleBarBorder",   json::array()), s.titleBarBorder);
    s.titleBarText    = ColorFromJson(j.value("titleBarText",     json::array()), s.titleBarText);
    s.titleMinBase    = ColorFromJson(j.value("titleMinBase",     json::array()), s.titleMinBase);
    s.titleMaxBase    = ColorFromJson(j.value("titleMaxBase",     json::array()), s.titleMaxBase);
    s.titleCloseBase  = ColorFromJson(j.value("titleCloseBase",   json::array()), s.titleCloseBase);
    s.titleMinHover   = ColorFromJson(j.value("titleMinHover",    json::array()), s.titleMinHover);
    s.titleMaxHover   = ColorFromJson(j.value("titleMaxHover",    json::array()), s.titleMaxHover);
    s.titleCloseHover = ColorFromJson(j.value("titleCloseHover",  json::array()), s.titleCloseHover);
    s.titleExitBase   = ColorFromJson(j.value("titleExitBase",   json::array()), s.titleExitBase);
    s.titleExitHover  = ColorFromJson(j.value("titleExitHover",  json::array()), s.titleExitHover);
    s.titleMinGlyph   = ColorFromJson(j.value("titleMinGlyph",   json::array()), s.titleMinGlyph);
    s.titleMaxGlyph   = ColorFromJson(j.value("titleMaxGlyph",   json::array()), s.titleMaxGlyph);
    s.titleExitGlyph  = ColorFromJson(j.value("titleExitGlyph",  json::array()), s.titleExitGlyph);
    s.titleCloseGlyph = ColorFromJson(j.value("titleCloseGlyph", json::array()), s.titleCloseGlyph);
    s.iconBoardTop    = ColorFromJson(j.value("iconBoardTop",     json::array()), s.iconBoardTop);
    s.iconBoardBottom = ColorFromJson(j.value("iconBoardBottom",  json::array()), s.iconBoardBottom);
    s.iconPaper       = ColorFromJson(j.value("iconPaper",        json::array()), s.iconPaper);
    s.iconMarginLine  = ColorFromJson(j.value("iconMarginLine",   json::array()), s.iconMarginLine);
    s.iconRuledLines  = ColorFromJson(j.value("iconRuledLines",   json::array()), s.iconRuledLines);
    s.opacityKnobFill = ColorFromJson(j.value("opacityKnobFill",  json::array()), s.opacityKnobFill);
    s.opacityKnobRing = ColorFromJson(j.value("opacityKnobRing",  json::array()), s.opacityKnobRing);
    s.scrollbarBg         = ColorFromJson(j.value("scrollbarBg",         json::array()), s.scrollbarBg);
    s.scrollbarGrab       = ColorFromJson(j.value("scrollbarGrab",       json::array()), s.scrollbarGrab);
    s.scrollbarGrabHover  = ColorFromJson(j.value("scrollbarGrabHover",  json::array()), s.scrollbarGrabHover);
    s.scrollbarGrabActive = ColorFromJson(j.value("scrollbarGrabActive", json::array()), s.scrollbarGrabActive);
    s.showScrollbars   = j.value("showScrollbars",   s.showScrollbars);
    s.scrollbarSize    = std::clamp(j.value("scrollbarSize",    s.scrollbarSize),    0.0f, 24.0f);
    s.scrollbarRounding= std::clamp(j.value("scrollbarRounding",s.scrollbarRounding),0.0f, 16.0f);
    s.scrollbarPadding = std::clamp(j.value("scrollbarPadding", s.scrollbarPadding), 0.0f,  8.0f);
    s.popupRounding    = std::clamp(j.value("popupRounding",    s.popupRounding),    0.0f, 18.0f);
    s.popupButtonRowPadding = std::clamp(j.value("popupButtonRowPadding", s.popupButtonRowPadding), 0.0f, 12.0f);
    s.popupButtonColumnPadding = std::clamp(j.value("popupButtonColumnPadding", s.popupButtonColumnPadding), 0.0f, 16.0f);
    s.controlRounding  = std::clamp(j.value("controlRounding",  s.controlRounding),  0.0f, 12.0f);
}

template<typename T>
json SaveColorFields(const T& s) {
    return {
        {"windowBg",          ColorToJson(s.windowBg)},
        {"panelBg",           ColorToJson(s.panelBg)},
        {"text",              ColorToJson(s.text)},
        {"mutedText",         ColorToJson(s.mutedText)},
        {"accent",            ColorToJson(s.accent)},
        {"hover",             ColorToJson(s.hover)},
        {"selectedTab",       ColorToJson(s.selectedTab)},
        {"buttonOff",         ColorToJson(s.buttonOff)},
        {"buttonOn",          ColorToJson(s.buttonOn)},
        {"closeButton",       ColorToJson(s.closeButton)},
        {"closeButtonHover",  ColorToJson(s.closeButtonHover)},
        {"closeButtonText",   ColorToJson(s.closeButtonText)},
        {"titleBarBg",        ColorToJson(s.titleBarBg)},
        {"titleBarBorder",    ColorToJson(s.titleBarBorder)},
        {"titleBarText",      ColorToJson(s.titleBarText)},
        {"titleMinBase",      ColorToJson(s.titleMinBase)},
        {"titleMaxBase",      ColorToJson(s.titleMaxBase)},
        {"titleCloseBase",    ColorToJson(s.titleCloseBase)},
        {"titleMinHover",     ColorToJson(s.titleMinHover)},
        {"titleMaxHover",     ColorToJson(s.titleMaxHover)},
        {"titleCloseHover",   ColorToJson(s.titleCloseHover)},
        {"titleExitBase",     ColorToJson(s.titleExitBase)},
        {"titleExitHover",    ColorToJson(s.titleExitHover)},
        {"titleMinGlyph",     ColorToJson(s.titleMinGlyph)},
        {"titleMaxGlyph",     ColorToJson(s.titleMaxGlyph)},
        {"titleExitGlyph",    ColorToJson(s.titleExitGlyph)},
        {"titleCloseGlyph",   ColorToJson(s.titleCloseGlyph)},
        {"iconBoardTop",      ColorToJson(s.iconBoardTop)},
        {"iconBoardBottom",   ColorToJson(s.iconBoardBottom)},
        {"iconPaper",         ColorToJson(s.iconPaper)},
        {"iconMarginLine",    ColorToJson(s.iconMarginLine)},
        {"iconRuledLines",    ColorToJson(s.iconRuledLines)},
        {"opacityKnobFill",   ColorToJson(s.opacityKnobFill)},
        {"opacityKnobRing",   ColorToJson(s.opacityKnobRing)},
        {"scrollbarBg",           ColorToJson(s.scrollbarBg)},
        {"scrollbarGrab",         ColorToJson(s.scrollbarGrab)},
        {"scrollbarGrabHover",    ColorToJson(s.scrollbarGrabHover)},
        {"scrollbarGrabActive",   ColorToJson(s.scrollbarGrabActive)},
        {"showScrollbars",   s.showScrollbars},
        {"scrollbarSize",    s.scrollbarSize},
        {"scrollbarRounding",s.scrollbarRounding},
        {"scrollbarPadding", s.scrollbarPadding},
        {"popupRounding",    s.popupRounding},
        {"popupButtonRowPadding", s.popupButtonRowPadding},
        {"popupButtonColumnPadding", s.popupButtonColumnPadding},
        {"controlRounding",  s.controlRounding},
    };
}

SavedAppearanceTheme SavedThemeFromJson(const json& item) {
    SavedAppearanceTheme saved;
    saved.name = item.value("name", saved.name);
    LoadColorFields(saved, item);
    return saved;
}

json SavedThemeToJson(const SavedAppearanceTheme& saved) {
    json result = SaveColorFields(saved);
    result["name"] = saved.name;
    return result;
}

json BindingToJson(const KeyBinding& b) {
    return {
        {"ctrl", b.ctrl},
        {"shift", b.shift},
        {"alt", b.alt},
        {"vkey", b.vkey},
        {"action", static_cast<int>(b.action)},
        {"data", b.data},
        {"ctrlSide", static_cast<int>(b.ctrlSide)},
        {"shiftSide", static_cast<int>(b.shiftSide)},
        {"altSide", static_cast<int>(b.altSide)},
        {"physicalModifiers", b.physicalModifiers},
        {"exactModifiers", b.exactModifiers},
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
    b.ctrlSide = static_cast<ModifierSide>(std::clamp(
        j.value("ctrlSide", static_cast<int>(b.ctrlSide)), 0, 2));
    b.shiftSide = static_cast<ModifierSide>(std::clamp(
        j.value("shiftSide", static_cast<int>(b.shiftSide)), 0, 2));
    const int legacyAltSide = j.value("leftAlt", false)
        ? static_cast<int>(ModifierSide::Left)
        : static_cast<int>(b.altSide);
    b.altSide = static_cast<ModifierSide>(std::clamp(
        j.value("altSide", legacyAltSide), 0, 2));
    b.physicalModifiers = static_cast<uint8_t>(std::clamp(
        j.value("physicalModifiers", static_cast<int>(b.physicalModifiers)), 0, 63));
    b.exactModifiers = j.value("exactModifiers", b.exactModifiers);
    return b;
}

json SlotBankToJson(const SlotBankSettings& bank) {
    return {
        {"enabled", bank.enabled},
        {"chord", BindingToJson(bank.chord)},
        {"numberKeys", bank.numberKeys},
        {"letterKeys", bank.letterKeys},
        {"functionKeys", bank.functionKeys},
    };
}

SlotBankSettings SlotBankFromJson(const json& value,
                                  const SlotBankSettings& fallback) {
    SlotBankSettings bank = fallback;
    if (!value.is_object()) return bank;
    bank.enabled = value.value("enabled", bank.enabled);
    if (value.contains("chord") && value["chord"].is_object())
        bank.chord = BindingFromJson(value["chord"], bank.chord);
    bank.chord.vkey = 0;
    bank.numberKeys = value.value("numberKeys", bank.numberKeys);
    bank.letterKeys = value.value("letterKeys", bank.letterKeys);
    bank.functionKeys = value.value("functionKeys", bank.functionKeys);
    return bank;
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
    config.appearance.popupOutlineEffect =
        std::clamp(a.value("popupOutlineEffect",
                           config.appearance.popupOutlineAnimated ? 1 : config.appearance.popupOutlineEffect),
                   0, 3);
    config.appearance.popupOutlineAnimated = config.appearance.popupOutlineEffect != 0;
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
    config.appearance.fontPath    = a.value("fontPath",    config.appearance.fontPath);
    config.appearance.fontSize    = std::clamp(a.value("fontSize", config.appearance.fontSize), 9.0f, 32.0f);
    config.appearance.exeIconPath      = a.value("exeIconPath",      config.appearance.exeIconPath);
    config.appearance.exeIconThemeHash = a.value("exeIconThemeHash", config.appearance.exeIconThemeHash);
    config.appearance.uiScale = 1.0f;
    config.appearance.customColors = a.value("customColors", config.appearance.customColors);
    config.appearance.customThemeName = a.value("customThemeName", config.appearance.customThemeName);
    LoadColorFields(config.appearance, a);
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
    config.hotkeys.hiddenPasteCtrlSides = static_cast<uint8_t>(std::clamp(
        h.value("hiddenPasteCtrlSides",
                config.hotkeys.hiddenPasteCtrl ? 3 : 0), 0, 3));
    config.hotkeys.hiddenPasteShiftSides = static_cast<uint8_t>(std::clamp(
        h.value("hiddenPasteShiftSides",
                config.hotkeys.hiddenPasteShift ? 3 : 0), 0, 3));
    config.hotkeys.hiddenPasteAltSides = static_cast<uint8_t>(std::clamp(
        h.value("hiddenPasteAltSides",
                config.hotkeys.hiddenPasteAlt ? 3 : 0), 0, 3));
    config.hotkeys.hiddenPasteCtrl = config.hotkeys.hiddenPasteCtrlSides != 0;
    config.hotkeys.hiddenPasteShift = config.hotkeys.hiddenPasteShiftSides != 0;
    config.hotkeys.hiddenPasteAlt = config.hotkeys.hiddenPasteAltSides != 0;
    const json& banks = h.value("slotBanks", json::object());
    config.hotkeys.popupHistoryBank = SlotBankFromJson(
        banks.value("popupHistory", json::object()), defaults.popupHistoryBank);
    config.hotkeys.globalHistoryBank = SlotBankFromJson(
        banks.value("globalHistory", json::object()), defaults.globalHistoryBank);
    config.hotkeys.pinnedHistoryBank = SlotBankFromJson(
        banks.value("pinnedHistory", json::object()), defaults.pinnedHistoryBank);
    config.hotkeys.profileBank = SlotBankFromJson(
        banks.value("profiles", json::object()), defaults.profileBank);
    config.hotkeys.hotkeyDoubleTaps =
        h.value("hotkeyDoubleTaps", defaults.hotkeyDoubleTaps);
    if (!h.contains("slotBanks")) {
        KeyBinding migrated;
        migrated.ctrl = config.hotkeys.hiddenPasteCtrl;
        migrated.shift = config.hotkeys.hiddenPasteShift;
        migrated.alt = config.hotkeys.hiddenPasteAlt;
        auto migrateFamily = [&](uint8_t sides, uint8_t leftBit,
                                 uint8_t rightBit, ModifierSide& side) {
            if (sides == 1) {
                migrated.physicalModifiers |= leftBit;
                side = ModifierSide::Left;
            } else if (sides == 2) {
                migrated.physicalModifiers |= rightBit;
                side = ModifierSide::Right;
            } else if (sides == 3) {
                migrated.exactModifiers = false;
                side = ModifierSide::Any;
            }
        };
        migrated.exactModifiers = true;
        migrateFamily(config.hotkeys.hiddenPasteCtrlSides,
                      ModifierState::LeftCtrlBit, ModifierState::RightCtrlBit,
                      migrated.ctrlSide);
        migrateFamily(config.hotkeys.hiddenPasteShiftSides,
                      ModifierState::LeftShiftBit, ModifierState::RightShiftBit,
                      migrated.shiftSide);
        migrateFamily(config.hotkeys.hiddenPasteAltSides,
                      ModifierState::LeftAltBit, ModifierState::RightAltBit,
                      migrated.altSide);
        config.hotkeys.globalHistoryBank.chord = migrated;
        config.hotkeys.globalHistoryBank.enabled =
            config.hotkeys.hiddenPasteCtrlSides != 0 ||
            config.hotkeys.hiddenPasteShiftSides != 0 ||
            config.hotkeys.hiddenPasteAltSides != 0;
        config.hotkeys.globalHistoryBank.functionKeys =
            config.hotkeys.hiddenPasteFunctionKeys;
    }
    config.hotkeys.passthroughHotkeys.clear();
    if (h.contains("passthroughHotkeys") && h["passthroughHotkeys"].is_array()) {
        for (const json& item : h["passthroughHotkeys"]) {
            if (item.is_string()) {
                std::string value = item.get<std::string>();
                if (!value.empty())
                    config.hotkeys.passthroughHotkeys.push_back(std::move(value));
            }
        }
    } else {
        config.hotkeys.passthroughHotkeys = defaults.passthroughHotkeys;
    }

    if (!h.contains("bindings") || !h["bindings"].is_array())
        return;

    for (const json& item : h["bindings"]) {
        HotkeyAction action = static_cast<HotkeyAction>(item.value("action", 0));
        const int data = item.value("data", 0);
        auto it = std::find_if(config.hotkeys.bindings.begin(), config.hotkeys.bindings.end(),
            [&](const KeyBinding& b) {
                return b.action == action &&
                       (action != HotkeyAction::PasteNamedSlot || b.data == data);
            });
        if (it != config.hotkeys.bindings.end())
            *it = BindingFromJson(item, *it);
        else if (action != HotkeyAction::None)
            config.hotkeys.bindings.push_back(BindingFromJson(item, DefaultBindingForAction(action)));
    }
}

void LoadUi(const json& root, AppConfig& config) {
    const json& ui = root.value("ui", json::object());
    config.ui.showHelperText = ui.value("showHelperText", config.ui.showHelperText);
    config.ui.helperDelayMs = std::clamp(ui.value("helperDelayMs", config.ui.helperDelayMs), 0, 5000);
    config.ui.helperDurationMs = std::clamp(ui.value("helperDurationMs", config.ui.helperDurationMs), 500, 30000);
}

void LoadEditor(const json& root, AppConfig& config) {
    const json& e = root.value("editor", json::object());
    EditorSettings& s = config.editor;
    s.enabled = e.value("enabled", s.enabled);
    s.provider = std::clamp(e.value("provider", s.provider), 0, 1);
    s.alwaysOnTop = e.value("alwaysOnTop", s.alwaysOnTop);
    s.openWithClipboard = e.value("openWithClipboard", s.openWithClipboard);
    s.copyOnClose = e.value("copyOnClose", s.copyOnClose);
    s.confirmClose = e.value("confirmClose", s.confirmClose);
    s.showLineNumbers = e.value("showLineNumbers", s.showLineNumbers);
    s.showStatusBar = e.value("showStatusBar", s.showStatusBar);
    s.allowTabInput = e.value("allowTabInput", s.allowTabInput);
    s.externalWaitForExit = e.value("externalWaitForExit", s.externalWaitForExit);
    s.externalReadBackToClipboard = e.value("externalReadBackToClipboard", s.externalReadBackToClipboard);
    s.mode = std::clamp(e.value("mode", s.mode), 0, 4);
    s.width = std::clamp(e.value("width", s.width), 520, 3840);
    s.height = std::clamp(e.value("height", s.height), 360, 2160);
    s.externalPath = e.value("externalPath", s.externalPath);
    s.externalArguments = e.value("externalArguments", s.externalArguments);
    if (s.externalArguments.empty())
        s.externalArguments = EditorSettings{}.externalArguments;
    s.externalTempExtension = e.value("externalTempExtension", s.externalTempExtension);
}

void LoadAndroid(const json& root, AppConfig& config) {
    const json& a = root.value("android", json::object());
    config.android.deviceEndpoint = a.value("deviceEndpoint", config.android.deviceEndpoint);
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

void LoadImages(const json& root, AppConfig& config) {
    const json& im = root.value("images", json::object());
    ImageSettings& s = config.images;
    s.captureImages   = im.value("captureImages",   s.captureImages);
    s.format          = static_cast<ImageFormat>(std::clamp(im.value("format", 0), 0, 2));
    s.jpegQuality     = std::clamp(im.value("jpegQuality",   s.jpegQuality),   1, 100);
    s.scaleDown       = im.value("scaleDown",       s.scaleDown);
    s.maxDimension    = std::clamp(im.value("maxDimension",  s.maxDimension),  64, 16384);
    s.skipSmallImages = im.value("skipSmallImages", s.skipSmallImages);
    s.minWidth        = std::clamp(im.value("minWidth",      s.minWidth),      1, 4096);
    s.minHeight       = std::clamp(im.value("minHeight",     s.minHeight),     1, 4096);
    s.maxImages       = std::clamp(im.value("maxImages",     s.maxImages),     0, 100000);
}

CustomFilterMode CustomFilterModeFromInt(int value) {
    return static_cast<CustomFilterMode>(std::clamp(value, 0, 3));
}

CustomFilterTarget CustomFilterTargetFromInt(int value) {
    return static_cast<CustomFilterTarget>(std::clamp(value, 0, 4));
}

json CustomFilterToJson(const CustomFilter& filter) {
    return {
        {"id", filter.id},
        {"name", filter.name},
        {"enabled", filter.enabled},
        {"mode", static_cast<int>(filter.mode)},
        {"target", static_cast<int>(filter.target)},
        {"pattern", filter.pattern},
        {"caseSensitive", filter.caseSensitive},
        {"multiline", filter.multiline},
        {"dotMatchesNewline", filter.dotMatchesNewline},
        {"routeToProfile", filter.routeToProfile},
        {"routeMove", filter.routeMove},
        {"routeProfileId", filter.routeProfileId},
    };
}

CustomFilter CustomFilterFromJson(const json& item) {
    CustomFilter filter;
    filter.id = item.value("id", "");
    filter.name = item.value("name", "");
    filter.enabled = item.value("enabled", filter.enabled);
    filter.mode = CustomFilterModeFromInt(item.value("mode", 0));
    filter.target = CustomFilterTargetFromInt(item.value("target", 0));
    filter.pattern = item.value("pattern", "");
    filter.caseSensitive = item.value("caseSensitive", filter.caseSensitive);
    filter.multiline = item.value("multiline", filter.multiline);
    filter.dotMatchesNewline = item.value("dotMatchesNewline", filter.dotMatchesNewline);
    filter.routeToProfile = item.value("routeToProfile", filter.routeToProfile);
    filter.routeMove = item.value("routeMove", filter.routeMove);
    filter.routeProfileId = item.value("routeProfileId", filter.routeProfileId);
    if (filter.id.empty())
        filter.id = NewCustomFilterId();
    if (filter.name.empty())
        filter.name = "Custom filter";
    return filter;
}

void LoadCustomFilters(const json& root, AppConfig& config) {
    config.customFilters.clear();
    if (!root.contains("customFilters") || !root["customFilters"].is_array())
        return;

    for (const json& item : root["customFilters"]) {
        CustomFilter filter = CustomFilterFromJson(item);
        if (!filter.pattern.empty())
            config.customFilters.push_back(std::move(filter));
    }
}

void LoadPopupButtonOrder(const json& root, AppConfig& config) {
    config.popupButtonOrder.clear();
    if (!root.contains("popupButtonOrder") || !root["popupButtonOrder"].is_array())
        return;

    for (const json& item : root["popupButtonOrder"]) {
        if (!item.is_string())
            continue;
        std::string token = item.get<std::string>();
        if (!token.empty())
            config.popupButtonOrder.push_back(std::move(token));
    }
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
    config.profilesStoredInDatabase =
        root.value("profileStorage", std::string{}) == "encrypted-sqlite";
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
        LoadUi(root, config);
        LoadEditor(root, config);
        LoadAndroid(root, config);
        LoadDeveloper(root, config);
        LoadImages(root, config);
        LoadCustomFilters(root, config);
        LoadPopupButtonOrder(root, config);
        config.newItemsAtTop = root.value("newItemsAtTop", config.newItemsAtTop);
        if (root.contains("history") && root["history"].is_object()) {
            const json& history = root["history"];
            config.activeHistoryLimit = std::clamp(
                history.value("activeLimit", config.activeHistoryLimit),
                1, 500);
            config.deduplicateHistory =
                history.value("deduplicate", config.deduplicateHistory);
            config.vaultUnlimited = history.value("vaultUnlimited", config.vaultUnlimited);
            config.vaultLimitMB = std::clamp(
                history.value("vaultLimitMB", config.vaultLimitMB), 1, 102400);
        }
        config.appendNewlineAfterPaste = root.value("appendNewlineAfterPaste", config.appendNewlineAfterPaste);
        config.hidePopupOnOutsideClick = root.value("hidePopupOnOutsideClick", config.hidePopupOnOutsideClick);
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
      json passthroughHotkeys = json::array();
      for (const std::string& value : config.hotkeys.passthroughHotkeys)
          passthroughHotkeys.push_back(value);
      json slotBanks = {
          {"popupHistory", SlotBankToJson(config.hotkeys.popupHistoryBank)},
          {"globalHistory", SlotBankToJson(config.hotkeys.globalHistoryBank)},
          {"pinnedHistory", SlotBankToJson(config.hotkeys.pinnedHistoryBank)},
          {"profiles", SlotBankToJson(config.hotkeys.profileBank)},
      };

    json savedThemes = json::array();
    for (const SavedAppearanceTheme& saved : config.appearance.savedThemes)
        savedThemes.push_back(SavedThemeToJson(saved));

    json clipboards = json::array();
    if (!config.profilesStoredInDatabase) {
        for (const ClipboardProfileConfig& profile : config.clipboards) {
            clipboards.push_back({
                {"id", profile.id},
                {"name", profile.name},
                {"createdAt", profile.createdAt},
                {"updatedAt", profile.updatedAt},
                {"processName", profile.processName},
            });
        }
    }

    json customFilters = json::array();
    for (const CustomFilter& filter : config.customFilters)
        customFilters.push_back(CustomFilterToJson(filter));
    json popupButtonOrder = json::array();
    for (const std::string& token : config.popupButtonOrder)
        popupButtonOrder.push_back(token);

    json root = {
        {"version", 1},
        {"appearance", {
            {"theme", ThemeToInt(config.appearance.theme)},
            {"popupOpacity", config.appearance.popupOpacity},
            {"popupOutlineStrength", config.appearance.popupOutlineStrength},
            {"popupOutlineEffect", config.appearance.popupOutlineEffect},
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
            {"fontPath",    config.appearance.fontPath},
            {"fontSize",    config.appearance.fontSize},
            {"exeIconPath",      config.appearance.exeIconPath},
            {"exeIconThemeHash", config.appearance.exeIconThemeHash},
            {"uiScale", 1.0f},
            {"customColors", config.appearance.customColors},
            {"customThemeName", config.appearance.customThemeName},
            {"savedThemes", savedThemes},
        }},
         {"hotkeys", {
             {"bindings", bindings},
             {"passthroughHotkeys", passthroughHotkeys},
             {"hiddenPasteCtrl", config.hotkeys.hiddenPasteCtrl},
            {"hiddenPasteShift", config.hotkeys.hiddenPasteShift},
            {"hiddenPasteAlt", config.hotkeys.hiddenPasteAlt},
             {"hiddenPasteFunctionKeys", config.hotkeys.hiddenPasteFunctionKeys},
             {"hiddenPasteCtrlSides", config.hotkeys.hiddenPasteCtrlSides},
             {"hiddenPasteShiftSides", config.hotkeys.hiddenPasteShiftSides},
             {"hiddenPasteAltSides", config.hotkeys.hiddenPasteAltSides},
             {"slotBanks", slotBanks},
             {"hotkeyDoubleTaps", config.hotkeys.hotkeyDoubleTaps},
         }},
        {"ui", {
            {"showHelperText", config.ui.showHelperText},
            {"helperDelayMs", config.ui.helperDelayMs},
            {"helperDurationMs", config.ui.helperDurationMs},
        }},
        {"editor", {
            {"enabled", config.editor.enabled},
            {"provider", config.editor.provider},
            {"alwaysOnTop", config.editor.alwaysOnTop},
            {"openWithClipboard", config.editor.openWithClipboard},
            {"copyOnClose", config.editor.copyOnClose},
            {"confirmClose", config.editor.confirmClose},
            {"showLineNumbers", config.editor.showLineNumbers},
            {"showStatusBar", config.editor.showStatusBar},
            {"allowTabInput", config.editor.allowTabInput},
            {"externalWaitForExit", config.editor.externalWaitForExit},
            {"externalReadBackToClipboard", config.editor.externalReadBackToClipboard},
            {"mode", config.editor.mode},
            {"width", config.editor.width},
            {"height", config.editor.height},
            {"externalPath", config.editor.externalPath},
            {"externalArguments", config.editor.externalArguments},
            {"externalTempExtension", config.editor.externalTempExtension},
        }},
        {"android", {
            {"deviceEndpoint", config.android.deviceEndpoint},
        }},
        {"developer", {
            {"enabled", config.developer.enabled},
            {"cliEnabled", config.developer.cliEnabled},
            {"showSourceProcess", config.developer.showSourceProcess},
            {"eventLogEnabled", config.developer.eventLogEnabled},
        }},
        {"images", {
            {"captureImages",   config.images.captureImages},
            {"format",          static_cast<int>(config.images.format)},
            {"jpegQuality",     config.images.jpegQuality},
            {"scaleDown",       config.images.scaleDown},
            {"maxDimension",    config.images.maxDimension},
            {"skipSmallImages", config.images.skipSmallImages},
            {"minWidth",        config.images.minWidth},
            {"minHeight",       config.images.minHeight},
            {"maxImages",       config.images.maxImages},
        }},
        {"newItemsAtTop", config.newItemsAtTop},
        {"history", {
            {"activeLimit", config.activeHistoryLimit},
            {"deduplicate", config.deduplicateHistory},
            {"vaultUnlimited", config.vaultUnlimited},
            {"vaultLimitMB", config.vaultLimitMB},
        }},
        {"appendNewlineAfterPaste", config.appendNewlineAfterPaste},
        {"hidePopupOnOutsideClick", config.hidePopupOnOutsideClick},
        {"pasteMoveTarget", config.pasteMoveTarget},
        {"profileStorage", config.profilesStoredInDatabase
            ? "encrypted-sqlite" : "config"},
        {"autoSwitchClipboardByProcess", config.autoSwitchClipboardByProcess},
        {"autoCreateClipboardByProcess", config.autoCreateClipboardByProcess},
        {"customFilters", customFilters},
        {"popupButtonOrder", popupButtonOrder},
    };
    if (!config.profilesStoredInDatabase) {
        root["activeClipboardId"] = config.activeClipboardId;
        root["clipboards"] = std::move(clipboards);
    }
    root["appearance"].update(SaveColorFields(config.appearance));

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
