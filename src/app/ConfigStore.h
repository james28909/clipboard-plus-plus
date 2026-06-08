#pragma once

#include "../hotkeys/HotkeyManager.h"
#include "../ui/Appearance.h"
#include <filesystem>
#include <string>
#include <vector>

struct ClipboardProfileConfig {
    std::string id;
    std::string name;
    std::string createdAt;
    std::string updatedAt;
    std::string processName;
};

struct DeveloperSettings {
    bool enabled{false};
    bool cliEnabled{true};
    bool showSourceProcess{false};
    bool eventLogEnabled{false};
};

struct AppConfig {
    AppearanceSettings appearance{};
    HotkeySettings hotkeys{HotkeyManager::DefaultSettings()};
    DeveloperSettings developer{};
    bool newItemsAtTop{true};
    bool appendNewlineAfterPaste{false};
    int pasteMoveTarget{0}; // 0=keep, 1=top, 2=bottom
    std::string activeClipboardId{"default"};
    bool autoSwitchClipboardByProcess{true};
    bool autoCreateClipboardByProcess{true};
    std::vector<ClipboardProfileConfig> clipboards;
};

namespace ConfigStore {
    AppConfig Load();
    bool Save(const AppConfig& config);
    std::filesystem::path Path();
    std::filesystem::path Directory();
    std::filesystem::path FontsDirectory();
    std::filesystem::path ImportFontFile(const std::filesystem::path& source);
}
