#include "ConfigStore.h"

#include <json.hpp>
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace {

std::filesystem::path ResolveConfigPath() {
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
}

int ThemeToInt(ThemeId theme) {
    return std::clamp(static_cast<int>(theme), 0, static_cast<int>(ThemeId::Count) - 1);
}

ThemeId ThemeFromInt(int value) {
    return static_cast<ThemeId>(std::clamp(value, 0, static_cast<int>(ThemeId::Count) - 1));
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

void LoadAppearance(const json& root, AppConfig& config) {
    const json& a = root.value("appearance", json::object());
    config.appearance.theme = ThemeFromInt(a.value("theme", ThemeToInt(config.appearance.theme)));
    config.appearance.popupOpacity = std::clamp(a.value("popupOpacity", config.appearance.popupOpacity), 0.1f, 1.0f);
    config.appearance.popupWidth = std::max(360, a.value("popupWidth", config.appearance.popupWidth));
    config.appearance.popupHeight = std::max(260, a.value("popupHeight", config.appearance.popupHeight));
    config.appearance.fontPath = a.value("fontPath", config.appearance.fontPath);
    config.appearance.fontSize = std::clamp(a.value("fontSize", config.appearance.fontSize), 9.0f, 32.0f);
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

} // namespace

namespace ConfigStore {

AppConfig Load() {
    AppConfig config;
    const std::filesystem::path path = ResolveConfigPath();
    std::ifstream in(path);
    if (!in) return config;

    try {
        json root = json::parse(in, nullptr, true, true);
        LoadAppearance(root, config);
        LoadHotkeys(root, config);
        config.newItemsAtTop = root.value("newItemsAtTop", config.newItemsAtTop);
        config.appendNewlineAfterPaste = root.value("appendNewlineAfterPaste", config.appendNewlineAfterPaste);
        config.pasteMoveTarget = std::clamp(root.value("pasteMoveTarget", config.pasteMoveTarget), 0, 2);
    } catch (...) {
        return AppConfig{};
    }

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

    json root = {
        {"version", 1},
        {"appearance", {
            {"theme", ThemeToInt(config.appearance.theme)},
            {"popupOpacity", config.appearance.popupOpacity},
            {"popupWidth", config.appearance.popupWidth},
            {"popupHeight", config.appearance.popupHeight},
            {"fontPath", config.appearance.fontPath},
            {"fontSize", config.appearance.fontSize},
        }},
        {"hotkeys", {
            {"bindings", bindings},
            {"hiddenPasteCtrl", config.hotkeys.hiddenPasteCtrl},
            {"hiddenPasteShift", config.hotkeys.hiddenPasteShift},
            {"hiddenPasteAlt", config.hotkeys.hiddenPasteAlt},
            {"hiddenPasteFunctionKeys", config.hotkeys.hiddenPasteFunctionKeys},
        }},
        {"newItemsAtTop", config.newItemsAtTop},
        {"appendNewlineAfterPaste", config.appendNewlineAfterPaste},
        {"pasteMoveTarget", config.pasteMoveTarget},
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

} // namespace ConfigStore
