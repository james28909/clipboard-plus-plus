#pragma once

#include "../hotkeys/HotkeyManager.h"
#include "../ui/Appearance.h"
#include "../filters/CustomFilter.h"
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

// Values are persisted in config.json - do not renumber existing entries.
enum class ImageFormat {
    PNG  = 0,  // convert to PNG (lossless, much smaller than raw DIB) - default
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

struct UiSettings {
    bool showHelperText{true};
    int helperDelayMs{450};
    int helperDurationMs{3500};
};

struct EditorSettings {
    bool enabled{true};
    int provider{1}; // 0=built-in popup, 1=external executable
    bool alwaysOnTop{true};
    bool openWithClipboard{true};
    bool copyOnClose{false};
    bool confirmClose{true};
    bool showLineNumbers{true};
    bool showStatusBar{true};
    bool allowTabInput{true};
    bool externalWaitForExit{false};
    bool externalReadBackToClipboard{false};
    int mode{0}; // 0=text, 1=PowerShell, 2=batch, 3=JSON, 4=Markdown
    int width{760};
    int height{520};
    std::string externalPath;
    std::string externalArguments{"--file {file} --mode {mode}"};
    std::string externalTempExtension;
};

struct AndroidSettings {
    std::string deviceEndpoint;
};

struct AppConfig {
    AppearanceSettings appearance{};
    HotkeySettings hotkeys{HotkeyManager::DefaultSettings()};
    DeveloperSettings developer{};
    UiSettings ui{};
    EditorSettings editor{};
    AndroidSettings android{};
    ImageSettings images{};
    std::vector<CustomFilter> customFilters;
    std::vector<std::string> popupButtonOrder;
    bool newItemsAtTop{true};
    int activeHistoryLimit{500};
    bool deduplicateHistory{true};
    bool vaultUnlimited{true};
    int vaultLimitMB{256};
    bool appendNewlineAfterPaste{false};
    bool hidePopupOnOutsideClick{false};
    int pasteMoveTarget{0}; // 0=keep, 1=top, 2=bottom
    std::string activeClipboardId{"default"};
    bool profilesStoredInDatabase{false};
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
