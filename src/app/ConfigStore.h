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

// Values are persisted in config.json — do not renumber existing entries.
enum class ImageFormat {
    PNG  = 0,  // convert to PNG (lossless, much smaller than raw DIB) — default
    JPEG = 1,  // convert to JPEG (lossy, smallest file size)
    Raw  = 2   // store exact clipboard bytes with no GDI+ conversion
};

struct ImageSettings {
    bool        captureImages{true};
    ImageFormat format{ImageFormat::PNG};
    int         jpegQuality{85};       // 1–100, JPEG only
    bool        scaleDown{false};
    int         maxDimension{1920};    // longest side; scales proportionally
    bool        skipSmallImages{true};
    int         minWidth{32};
    int         minHeight{32};
    int         maxImages{0};          // 0 = unlimited; oldest removed when limit exceeded
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
    ImageSettings images{};
    bool newItemsAtTop{true};
    bool appendNewlineAfterPaste{false};
    int pasteMoveTarget{0}; // 0=keep, 1=top, 2=bottom
    std::string activeClipboardId{"default"};
#ifdef NDEBUG
    bool autoSwitchClipboardByProcess{false};
    bool autoCreateClipboardByProcess{false};
#else
    bool autoSwitchClipboardByProcess{true};
    bool autoCreateClipboardByProcess{true};
#endif
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
