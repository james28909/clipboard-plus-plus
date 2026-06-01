#pragma once

#include "../hotkeys/HotkeyManager.h"
#include "../ui/Appearance.h"
#include <filesystem>

struct AppConfig {
    AppearanceSettings appearance{};
    HotkeySettings hotkeys{HotkeyManager::DefaultSettings()};
    bool newItemsAtTop{true};
    bool appendNewlineAfterPaste{false};
    int pasteMoveTarget{0}; // 0=keep, 1=top, 2=bottom
};

namespace ConfigStore {
    AppConfig Load();
    bool Save(const AppConfig& config);
    std::filesystem::path Path();
    std::filesystem::path Directory();
    std::filesystem::path FontsDirectory();
    std::filesystem::path ImportFontFile(const std::filesystem::path& source);
}
