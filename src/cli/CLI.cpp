#include "CLI.h"
#include "HistoryCli.h"
#include "VaultCli.h"

#include "app/Application.h"
#include "app/ConfigStore.h"
#include "ipc/IpcClient.h"
#include "util/Win32Util.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::wstring ToWide(const std::filesystem::path& path) {
    return path.wstring();
}

std::wstring ToWide(const std::string& value) {
    return win32util::Utf8ToWide(value);
}

} // namespace

namespace {

void DetachConsoleForCli() {
    std::wcout.flush();
    std::wcerr.flush();
    fflush(stdout);
    fflush(stderr);
}

bool CliGateExempt(int argc, wchar_t** argv) {
    if (argc <= 1)
        return true;
    return win32util::EqW(argv[1], L"--help") || win32util::EqW(argv[1], L"-h") || win32util::EqW(argv[1], L"/?") ||
           win32util::EqW(argv[1], L"status") || win32util::EqW(argv[1], L"config") || win32util::EqW(argv[1], L"set") ||
           win32util::EqW(argv[1], L"--set") || win32util::EqW(argv[1], L"--config-path") ||
           win32util::EqW(argv[1], L"--open-config-folder") || win32util::EqW(argv[1], L"--reset-config") ||
           win32util::EqW(argv[1], L"--diagnostics");
}

void PrintHelp() {
    std::wcout <<
LR"(Clipboard++ command line

Usage:
  clipboardpp.exe [command] [options]
  clipboardpp.exe config <operation> [value]
  clipboardpp.exe get <operation> [options]
  clipboardpp.exe set <operation> [value]
  clipboardpp.exe vault <operation> [options]

Window commands:
  --show
      Show/focus the main settings window in the running app.

  --popup
      Show the quick paste popup in the running app.

  --settings
      Alias for --show.

Status and diagnostics:
  status [--format text|json]
      Print app status. Text is the default format.

  --diagnostics
      Print useful paths and whether a Clipboard++ instance is currently running.

Configuration shortcuts:
  --config-path
      Print the full path to config.json.

  --open-config-folder
      Open the Clipboard++ configuration folder in File Explorer.

  --reset-config
      Replace config.json with default settings, then ask the running app to reload.

  --set key=value
      Update one config value and ask the running app to reload.

Clipboard shortcuts:
  --clipboard set <text-or-paths>
      Set the Windows clipboard. If the value is one or more existing file or
      folder paths, the clipboard is populated as a file drop for Explorer.

  --clipboard get
      Print text or file paths from the current Windows clipboard.

  --clipboard insert <text-or-paths> [--top|--bottom|--index 1-500] [--system]
      Insert text into Clipboard++ history. Existing file/folder path lists are
      stored as file-drop entries. Top is the default. Requires the tray app.

  --clipboard <text-or-paths> [--top|--bottom|--index 1-500] [--system]
      Backward-compatible alias for --clipboard insert <text-or-paths>.

  --clipboard-file <path> [--top|--bottom|--index 1-500] [--system]
      Read a UTF-8 text file and insert its contents into Clipboard++ history.
      Requires the Clipboard++ tray app to be running.

Configuration command:
  config --path
      Print the full path to config.json.

  config --open-folder
      Open the configuration folder in File Explorer.

  config --reset
      Replace config.json with defaults and ask the running app to reload.

  config --reset-font
      Restore the default Segoe UI font and default size.

  config --list
      Print all supported config keys and their current values.

  config --get <key>
      Print one config value.

  config --set <key> <value>
  config --set key=value
      Update one config value and ask the running app to reload.

      Supported keys:
        theme
          Theme name or index. Names may omit spaces/case.
          Examples: DarkDefault, Dracula, Nord, OneDarkPro, TokyoNight,
                    SolarizedDark, GitHubDark, GitHubLight, SolarizedLight,
                    VSLight, QuietLight

        popup.opacity
          Floating point value from 0.10 to 1.00.

        popup.width
          Popup default width, minimum 360.

        popup.height
          Popup default height, minimum 260.

        font.path
          Full path to a .ttf or .otf font file. User-selected fonts are copied
          into the managed fonts folder before saving.

        font.size
          Floating point font size from 9.0 to 32.0.

        font.dir
          Read-only path to the managed fonts folder.

        history.newAtTop
          true/false. Controls whether new clipboard items go to the top.

        history.deduplicate
          true/false. Consolidates repeated content when enabled.

        paste.newline
          true/false. Appends a newline after pasted text.

        paste.move
          keep, top, or bottom.

History commands:
  get --list [--profile <id-or-name>] [--limit N] [--format text|json]
      List items in the current stored history order.

  get --search <query> [--profile <id-or-name>] [--limit N]
               [--format text|json]
      Search active history while preserving each item's original list number.

  get --item <n> [--profile <id-or-name>] [--format text|json]
      Print the full stored text for the one-based item number. JSON includes
      metadata as well as the full text field.

Clipboard command:
  set --delete <n> [--profile <id-or-name>]
      Delete the item at the current one-based history position.

  set --pin <n> [--profile <id-or-name>]
  set --unpin <n> [--profile <id-or-name>]
      Change the pin state of the item at the current history position.

  set --clear [--profile <id-or-name>]
      Clear active history for one profile. The vault is not changed.

  set --clipboard <text-or-paths> [--top|--bottom|--index 1-500] [--system]
      Insert text or existing file/folder path lists into Clipboard++ history.
      Requires the Clipboard++ tray app to be running.

Vault commands:
  vault count [--profile <id-or-name>] [--format text|json]
      Count archived items in the active or selected profile.

  vault search <query> [--profile <id-or-name>] [--limit N]
               [--format text|json]
      Search archived text, paths, and source metadata.

  vault export --output <path> [--format files|json|binary]
               [--profile <id-or-name>] [--search <query>]
      Export decrypted stored payloads. Image bytes retain their exact stored
      PNG, JPEG, or DIB representation.

  vault backup --output <empty-directory>
      Create consistent encrypted backups of clipboard.db and images.db.

  set --clipboard-file <path> [--top|--bottom|--index 1-500] [--system]
      Read a UTF-8 text file and insert its contents into Clipboard++ history.
      Requires the Clipboard++ tray app to be running.

Exit codes:
  0   Command succeeded.
  1   Command failed, used an invalid value, or referenced an unknown key.

Current scope:
  This CLI manages the running window and app configuration available today.
  The --clipboard commands insert text or file-drop path lists into history.
  Use --system when you also want the real Windows clipboard changed.
  History insert commands fail if the Clipboard++ tray app is not running.
  Vault queries read the encrypted SQLite database through the Clipboard++ VFS.

Help:
  --help, -h, /?
      Show this help.

Examples:
  .\clipboardpp.exe --show
  .\clipboardpp.exe --popup
  .\clipboardpp.exe --config-path
  .\clipboardpp.exe --set theme=TokyoNight
  .\clipboardpp.exe config --list
  .\clipboardpp.exe config --get popup.opacity
  .\clipboardpp.exe config --set paste.newline true
  .\clipboardpp.exe config --reset-font
  .\clipboardpp.exe --clipboard set "C:\Temp\report.pdf"
  .\clipboardpp.exe --clipboard get
  .\clipboardpp.exe --clipboard insert "hello from the CLI" --top
  .\clipboardpp.exe --clipboard "hello from the CLI" --top
  .\clipboardpp.exe --clipboard "archive this lower" --bottom
  .\clipboardpp.exe --clipboard "put me at slot 7" --index 7
  .\clipboardpp.exe --clipboard-file "C:\Temp\note.txt" --bottom --system
  .\clipboardpp.exe --set popup.opacity=0.85
  .\clipboardpp.exe --set font.path="C:\Windows\Fonts\consola.ttf"
  .\clipboardpp.exe --set font.size=16
  .\clipboardpp.exe --set paste.move=bottom
  .\clipboardpp.exe status --format json
  .\clipboardpp.exe get --list --limit 20
  .\clipboardpp.exe get --search "invoice"
  .\clipboardpp.exe get --item 3 --format json
  .\clipboardpp.exe set --pin 3
  .\clipboardpp.exe set --delete 5
  .\clipboardpp.exe vault count
  .\clipboardpp.exe vault search "invoice" --limit 20
  .\clipboardpp.exe vault export --output "C:\Temp\vault-export" --format files
  .\clipboardpp.exe vault backup --output "D:\Backups\Clipboard++"

Notes:
  Running clipboardpp.exe with no arguments starts the tray app.
  Command-line invocations run in their own short-lived process.
  Window commands signal an already-running instance.
  Commands that modify config work even when the app is not running.
  If the app is running, config-changing commands send it a reload message.
)";
}

std::wstring Normalize(std::wstring s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) {
        return c == L' ' || c == L'-' || c == L'_';
    }), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return s;
}

bool ParseBool(const std::wstring& value, bool& out) {
    const std::wstring v = Normalize(value);
    if (v == L"true" || v == L"1" || v == L"yes" || v == L"on") {
        out = true;
        return true;
    }
    if (v == L"false" || v == L"0" || v == L"no" || v == L"off") {
        out = false;
        return true;
    }
    return false;
}

bool ParseTheme(const std::wstring& value, ThemeId& out) {
    wchar_t* end = nullptr;
    long index = wcstol(value.c_str(), &end, 10);
    if (end && *end == 0 && index >= 0 && index < static_cast<long>(ThemeId::Count)) {
        out = static_cast<ThemeId>(index);
        return true;
    }

    const std::wstring needle = Normalize(value);
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        const char* name = ThemeName(static_cast<ThemeId>(i));
        std::wstring wide;
        while (*name) wide.push_back(static_cast<unsigned char>(*name++));
        if (Normalize(wide) == needle) {
            out = static_cast<ThemeId>(i);
            return true;
        }
    }
    return false;
}

std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring ReadUtf8FileAsWide(const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string bytes = ss.str();
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   bytes.c_str(), static_cast<int>(bytes.size()),
                                   nullptr, 0);
    if (size <= 0) return {};

    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        bytes.c_str(), static_cast<int>(bytes.size()),
                        out.data(), size);
    ok = true;
    return out;
}

bool PushClipboardSmart(const std::wstring& text) {
    std::vector<std::wstring> paths = win32util::ExistingPathList(text.c_str());
    if (!paths.empty())
        return win32util::SetClipboardFileDrop(nullptr, paths);
    return win32util::SetClipboardUnicodeText(nullptr, text.c_str(), text.size());
}

int PrintSystemClipboard() {
    if (!OpenClipboard(nullptr)) {
        std::wcerr << L"Failed to open the Windows clipboard.\n";
        return 1;
    }

    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE h = GetClipboardData(CF_HDROP);
        if (!h) {
            CloseClipboard();
            return 1;
        }
        auto* hDrop = static_cast<HDROP>(h);
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH]{};
            if (DragQueryFileW(hDrop, i, path, MAX_PATH))
                std::wcout << path << L"\n";
        }
        CloseClipboard();
        return 0;
    }

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            auto* text = static_cast<const wchar_t*>(GlobalLock(h));
            if (text) {
                std::wcout << text << L"\n";
                GlobalUnlock(h);
                CloseClipboard();
                return 0;
            }
        }
    }

    CloseClipboard();
    std::wcerr << L"No supported text or file-list clipboard data found.\n";
    return 1;
}

struct ClipboardCliOptions {
    int position{0};
    bool setSystemClipboard{false};
};

bool ParseClipboardOptions(int argc, wchar_t** argv, int start, ClipboardCliOptions& options) {
    for (int i = start; i < argc; ++i) {
        if (win32util::EqW(argv[i], L"--top")) {
            options.position = 0;
        } else if (win32util::EqW(argv[i], L"--bottom")) {
            options.position = -1;
        } else if (win32util::EqW(argv[i], L"--index")) {
            if (i + 1 >= argc) {
                std::wcerr << L"--index requires a value from 1 to 500.\n";
                return false;
            }
            try {
                options.position = std::clamp(std::stoi(argv[++i]), 1, 500);
            } catch (...) {
                std::wcerr << L"--index requires a value from 1 to 500.\n";
                return false;
            }
        } else if (win32util::EqW(argv[i], L"--system")) {
            options.setSystemClipboard = true;
        } else {
            std::wcerr << L"Unknown clipboard option: " << argv[i] << L"\n";
            return false;
        }
    }
    return true;
}

int InsertClipboardText(const std::wstring& text, const ClipboardCliOptions& options) {
    if (text.empty()) {
        std::wcerr << L"Clipboard text cannot be empty.\n";
        return 1;
    }

    if (!ipc::SendClipboardHistoryText(text, options.position, options.setSystemClipboard)) {
        std::wcerr << L"Failed to insert text into Clipboard++ history. Is the tray app running?\n";
        return 1;
    }

    std::wcout << L"Inserted text into Clipboard++ history.\n";
    if (options.setSystemClipboard)
        std::wcout << L"Updated Windows clipboard.\n";
    return 0;
}

std::wstring BoolText(bool value) {
    return value ? L"true" : L"false";
}

std::wstring PasteMoveText(int value) {
    switch (value) {
    case 1: return L"top";
    case 2: return L"bottom";
    default: return L"keep";
    }
}

std::wstring ConfigValue(const AppConfig& config, const std::wstring& rawKey, bool& found) {
    found = true;
    const std::wstring key = Normalize(rawKey);
    if (key == L"theme")
        return ToWide(std::string(ThemeName(config.appearance.theme)));
    if (key == L"popup.opacity" || key == L"popupopacity")
        return std::to_wstring(config.appearance.popupOpacity);
    if (key == L"popup.width" || key == L"popupwidth")
        return std::to_wstring(config.appearance.popupWidth);
    if (key == L"popup.height" || key == L"popupheight")
        return std::to_wstring(config.appearance.popupHeight);
    if (key == L"font.path" || key == L"fontpath")
        return ToWide(config.appearance.fontPath);
    if (key == L"font.dir" || key == L"fontdir")
        return ToWide(ConfigStore::FontsDirectory());
    if (key == L"font.size" || key == L"fontsize")
        return std::to_wstring(config.appearance.fontSize);
    if (key == L"history.newattop" || key == L"historynewattop")
        return BoolText(config.newItemsAtTop);
    if (key == L"history.deduplicate" || key == L"historydeduplicate")
        return BoolText(config.deduplicateHistory);
    if (key == L"paste.newline" || key == L"pastenewline")
        return BoolText(config.appendNewlineAfterPaste);
    if (key == L"paste.move" || key == L"pastemove")
        return PasteMoveText(config.pasteMoveTarget);
#ifndef NDEBUG
    if (key == L"developer.enabled" || key == L"developerenabled" || key == L"dev.enabled")
        return BoolText(config.developer.enabled);
    if (key == L"developer.cli" || key == L"developerclienabled" || key == L"cli.enabled")
        return BoolText(config.developer.cliEnabled);
    if (key == L"developer.sourceprocess" || key == L"developersourceprocess")
        return BoolText(config.developer.showSourceProcess);
    if (key == L"developer.eventlog" || key == L"developereventlog")
        return BoolText(config.developer.eventLogEnabled);
    if (key == L"clipboard.autoswitch" || key == L"clipboardautoswitch")
        return BoolText(config.autoSwitchClipboardByProcess);
    if (key == L"clipboard.autocreate" || key == L"clipboardautocreate")
        return BoolText(config.autoCreateClipboardByProcess);
#endif
    found = false;
    return {};
}

void PrintConfigList(const AppConfig& config) {
    const wchar_t* keys[] = {
        L"theme",
        L"popup.opacity",
        L"popup.width",
        L"popup.height",
        L"font.path",
        L"font.size",
        L"font.dir",
        L"history.newAtTop",
        L"history.deduplicate",
        L"paste.newline",
        L"paste.move",
#ifndef NDEBUG
        L"developer.enabled",
        L"developer.cli",
        L"developer.sourceProcess",
        L"developer.eventLog",
        L"clipboard.autoSwitch",
        L"clipboard.autoCreate",
#endif
    };

    for (const wchar_t* key : keys) {
        bool found = false;
        std::wcout << key << L"=" << ConfigValue(config, key, found) << L"\n";
    }
}

std::wstring JsonEscape(std::wstring value) {
    std::wstring out;
    for (wchar_t c : value) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out.push_back(c);
    }
    return out;
}

void PrintStatus(const std::wstring& format) {
    const bool json = Normalize(format) == L"json";
    const bool running = ipc::FindRunningInstance() != nullptr;
    const AppConfig config = ConfigStore::Load();
    if (json) {
        std::wcout << L"{\n";
        std::wcout << L"  \"running\": " << (running ? L"true" : L"false") << L",\n";
#ifndef NDEBUG
        std::wcout << L"  \"developerMode\": " << (config.developer.enabled ? L"true" : L"false") << L",\n";
        std::wcout << L"  \"cliEnabled\": " << (config.developer.cliEnabled ? L"true" : L"false") << L",\n";
#endif
        std::wcout << L"  \"configPath\": \"" << JsonEscape(ToWide(ConfigStore::Path())) << L"\",\n";
        std::wcout << L"  \"configFolder\": \"" << JsonEscape(ToWide(ConfigStore::Directory())) << L"\"\n";
        std::wcout << L"}\n";
        return;
    }

    std::wcout << L"Running: " << (running ? L"yes" : L"no") << L"\n";
#ifndef NDEBUG
    std::wcout << L"Developer mode: " << (config.developer.enabled ? L"yes" : L"no") << L"\n";
    std::wcout << L"CLI enabled: " << (config.developer.cliEnabled ? L"yes" : L"no") << L"\n";
#endif
    std::wcout << L"Config: " << ToWide(ConfigStore::Path()) << L"\n";
    std::wcout << L"Config folder: " << ToWide(ConfigStore::Directory()) << L"\n";
}

bool ApplySet(AppConfig& config, const std::wstring& assignment, std::wstring& error) {
    const size_t eq = assignment.find(L'=');
    if (eq == std::wstring::npos) {
        error = L"--set expects key=value";
        return false;
    }

    const std::wstring key = Normalize(assignment.substr(0, eq));
    const std::wstring value = assignment.substr(eq + 1);

    try {
        if (key == L"theme") {
            ThemeId theme{};
            if (!ParseTheme(value, theme)) {
                error = L"Unknown theme.";
                return false;
            }
            config.appearance.theme = theme;
        } else if (key == L"popup.opacity" || key == L"popupopacity") {
            config.appearance.popupOpacity = std::clamp(std::stof(value), 0.1f, 1.0f);
        } else if (key == L"popup.width" || key == L"popupwidth") {
            config.appearance.popupWidth = std::max(360, std::stoi(value));
        } else if (key == L"popup.height" || key == L"popupheight") {
            config.appearance.popupHeight = std::max(260, std::stoi(value));
        } else if (key == L"font.path" || key == L"fontpath") {
            std::filesystem::path imported = ConfigStore::ImportFontFile(value);
            config.appearance.fontPath = Narrow(imported.empty() ? value : imported.wstring());
        } else if (key == L"font.size" || key == L"fontsize") {
            config.appearance.fontSize = std::clamp(std::stof(value), 9.0f, 32.0f);
        } else if (key == L"history.newattop" || key == L"historynewattop") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"history.newAtTop expects true/false.";
                return false;
            }
            config.newItemsAtTop = b;
        } else if (key == L"history.deduplicate" || key == L"historydeduplicate") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"history.deduplicate expects true/false.";
                return false;
            }
            config.deduplicateHistory = b;
        } else if (key == L"paste.newline" || key == L"pastenewline") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"paste.newline expects true/false.";
                return false;
            }
            config.appendNewlineAfterPaste = b;
        } else if (key == L"paste.move" || key == L"pastemove") {
            const std::wstring v = Normalize(value);
            if (v == L"keep" || v == L"none") config.pasteMoveTarget = 0;
            else if (v == L"top") config.pasteMoveTarget = 1;
            else if (v == L"bottom") config.pasteMoveTarget = 2;
            else {
                error = L"paste.move expects keep, top, or bottom.";
                return false;
            }
#ifndef NDEBUG
        } else if (key == L"developer.enabled" || key == L"developerenabled" || key == L"dev.enabled") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"developer.enabled expects true/false.";
                return false;
            }
            config.developer.enabled = b;
        } else if (key == L"developer.cli" || key == L"developerclienabled" || key == L"cli.enabled") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"developer.cli expects true/false.";
                return false;
            }
            config.developer.cliEnabled = b;
        } else if (key == L"developer.sourceprocess" || key == L"developersourceprocess") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"developer.sourceProcess expects true/false.";
                return false;
            }
            config.developer.showSourceProcess = b;
        } else if (key == L"developer.eventlog" || key == L"developereventlog") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"developer.eventLog expects true/false.";
                return false;
            }
            config.developer.eventLogEnabled = b;
        } else if (key == L"clipboard.autoswitch" || key == L"clipboardautoswitch") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"clipboard.autoSwitch expects true/false.";
                return false;
            }
            config.autoSwitchClipboardByProcess = b;
        } else if (key == L"clipboard.autocreate" || key == L"clipboardautocreate") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"clipboard.autoCreate expects true/false.";
                return false;
            }
            config.autoCreateClipboardByProcess = b;
#endif
        } else {
            error = L"Unknown config key: " + assignment.substr(0, eq);
            return false;
        }
    } catch (...) {
        error = L"Invalid value for key: " + assignment.substr(0, eq);
        return false;
    }

    return true;
}

bool SaveAndReload(const AppConfig& config) {
    if (!ConfigStore::Save(config)) {
        std::wcerr << L"Failed to save config.\n";
        return false;
    }
    ipc::SignalRunning(WM_RELOAD_CONFIG);
    return true;
}

int RunConfigCommand(int argc, wchar_t** argv) {
    if (argc < 3 || win32util::EqW(argv[2], L"--help") || win32util::EqW(argv[2], L"-h") || win32util::EqW(argv[2], L"/?")) {
        PrintHelp();
        return 0;
    }

    if (win32util::EqW(argv[2], L"--path")) {
        std::wcout << ToWide(ConfigStore::Path()) << L"\n";
        return 0;
    }
    if (win32util::EqW(argv[2], L"--open-folder")) {
        std::filesystem::create_directories(ConfigStore::Directory());
        const std::filesystem::path dir = ConfigStore::Directory();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (win32util::EqW(argv[2], L"--reset")) {
        return SaveAndReload(AppConfig{}) ? 0 : 1;
    }
    if (win32util::EqW(argv[2], L"--reset-font")) {
        AppConfig config = ConfigStore::Load();
        config.appearance.fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        config.appearance.fontSize = 15.0f;
        return SaveAndReload(config) ? 0 : 1;
    }
    if (win32util::EqW(argv[2], L"--list")) {
        PrintConfigList(ConfigStore::Load());
        return 0;
    }
    if (win32util::EqW(argv[2], L"--get")) {
        if (argc < 4) {
            std::wcerr << L"config --get requires a key. See --help.\n";
            return 1;
        }
        bool found = false;
        const std::wstring value = ConfigValue(ConfigStore::Load(), argv[3], found);
        if (!found) {
            std::wcerr << L"Unknown config key: " << argv[3] << L"\n";
            return 1;
        }
        std::wcout << value << L"\n";
        return 0;
    }
    if (win32util::EqW(argv[2], L"--set")) {
        if (argc < 4) {
            std::wcerr << L"config --set requires key/value. See --help.\n";
            return 1;
        }

        std::wstring assignment = argv[3];
        if (assignment.find(L'=') == std::wstring::npos) {
            if (argc < 5) {
                std::wcerr << L"config --set <key> requires a value. See --help.\n";
                return 1;
            }
            assignment += L"=";
            assignment += argv[4];
        }

        AppConfig config = ConfigStore::Load();
        std::wstring error;
        if (!ApplySet(config, assignment, error)) {
            std::wcerr << error << L"\n";
            return 1;
        }
        return SaveAndReload(config) ? 0 : 1;
    }

    std::wcerr << L"Unknown config operation: " << argv[2] << L"\n\n";
    PrintHelp();
    return 1;
}

int RunSetCommand(int argc, wchar_t** argv) {
    if (argc < 3 || win32util::EqW(argv[2], L"--help") || win32util::EqW(argv[2], L"-h") || win32util::EqW(argv[2], L"/?")) {
        PrintHelp();
        return 0;
    }

    if (win32util::EqW(argv[2], L"--delete") ||
        win32util::EqW(argv[2], L"--pin") ||
        win32util::EqW(argv[2], L"--unpin") ||
        win32util::EqW(argv[2], L"--clear")) {
        return RunHistoryMutationCli(argc, argv);
    }

    if (win32util::EqW(argv[2], L"--clipboard")) {
        if (argc < 4) {
            std::wcerr << L"set --clipboard requires text. See --help.\n";
            return 1;
        }
        ClipboardCliOptions options;
        if (!ParseClipboardOptions(argc, argv, 4, options))
            return 1;
        return InsertClipboardText(argv[3], options);
    }

    if (win32util::EqW(argv[2], L"--clipboard-file")) {
        if (argc < 4) {
            std::wcerr << L"set --clipboard-file requires a path. See --help.\n";
            return 1;
        }
        ClipboardCliOptions options;
        if (!ParseClipboardOptions(argc, argv, 4, options))
            return 1;
        bool ok = false;
        const std::wstring text = ReadUtf8FileAsWide(argv[3], ok);
        if (!ok) {
            std::wcerr << L"Failed to read UTF-8 text file: " << argv[3] << L"\n";
            return 1;
        }
        return InsertClipboardText(text, options);
    }

    std::wcerr << L"Unknown set operation: " << argv[2] << L"\n\n";
    PrintHelp();
    return 1;
}

int RunClipboardCommand(int argc, wchar_t** argv) {
    if (argc < 3 || win32util::EqW(argv[2], L"--help") || win32util::EqW(argv[2], L"-h") || win32util::EqW(argv[2], L"/?")) {
        PrintHelp();
        return 0;
    }

    if (win32util::EqW(argv[2], L"get")) {
        return PrintSystemClipboard();
    }

    if (win32util::EqW(argv[2], L"set")) {
        if (argc < 4) {
            std::wcerr << L"--clipboard set requires text or file/folder paths. See --help.\n";
            return 1;
        }
        if (!PushClipboardSmart(argv[3])) {
            std::wcerr << L"Failed to set the Windows clipboard.\n";
            return 1;
        }
        std::wcout << L"Set Windows clipboard.\n";
        return 0;
    }

    if (win32util::EqW(argv[2], L"insert")) {
        if (argc < 4) {
            std::wcerr << L"--clipboard insert requires text. See --help.\n";
            return 1;
        }
        ClipboardCliOptions options;
        if (!ParseClipboardOptions(argc, argv, 4, options))
            return 1;
        return InsertClipboardText(argv[3], options);
    }

    ClipboardCliOptions options;
    if (!ParseClipboardOptions(argc, argv, 3, options))
        return 1;
    return InsertClipboardText(argv[2], options);
}

} // namespace

int RunCLI(int argc, wchar_t** argv) {
    int result = 0;

    if (argc <= 1 || win32util::EqW(argv[1], L"--help") || win32util::EqW(argv[1], L"-h") || win32util::EqW(argv[1], L"/?")) {
        PrintHelp();
        DetachConsoleForCli();
        return 0;
    }

#ifndef NDEBUG
    const AppConfig gateConfig = ConfigStore::Load();
    if (!gateConfig.developer.cliEnabled && !CliGateExempt(argc, argv)) {
        std::wcerr << L"Clipboard++ CLI commands are disabled in Developer settings.\n";
        std::wcerr << L"Use --set developer.cli=true to re-enable them.\n";
        DetachConsoleForCli();
        return 2;
    }
#endif

    if (win32util::EqW(argv[1], L"--show") || win32util::EqW(argv[1], L"--settings")) {
        ipc::SignalRunning(WM_SHOWCPP_MAIN);
        DetachConsoleForCli();
        return 0;
    }
    if (win32util::EqW(argv[1], L"--popup")) {
        ipc::SignalRunning(WM_SHOWPOPUP);
        DetachConsoleForCli();
        return 0;
    }
    if (win32util::EqW(argv[1], L"status")) {
        std::wstring format = L"text";
        if (argc >= 4 && win32util::EqW(argv[2], L"--format"))
            format = argv[3];
        if (Normalize(format) != L"text" && Normalize(format) != L"json") {
            std::wcerr << L"status --format expects text or json.\n";
            DetachConsoleForCli();
            return 1;
        }
        PrintStatus(format);
        DetachConsoleForCli();
        return 0;
    }
    if (win32util::EqW(argv[1], L"config")) {
        result = RunConfigCommand(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"vault")) {
        result = RunVaultCli(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"get")) {
        result = RunHistoryCli(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"set")) {
        result = RunSetCommand(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"--clipboard")) {
        result = RunClipboardCommand(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"--clipboard-file")) {
        if (argc < 3) {
            std::wcerr << L"--clipboard-file requires a path. See --help.\n";
            DetachConsoleForCli();
            return 1;
        }
        ClipboardCliOptions options;
        if (!ParseClipboardOptions(argc, argv, 3, options)) {
            DetachConsoleForCli();
            return 1;
        }
        bool ok = false;
        const std::wstring text = ReadUtf8FileAsWide(argv[2], ok);
        if (!ok) {
            std::wcerr << L"Failed to read UTF-8 text file: " << argv[2] << L"\n";
            DetachConsoleForCli();
            return 1;
        }
        result = InsertClipboardText(text, options);
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"--config-path")) {
        std::wcout << ToWide(ConfigStore::Path()) << L"\n";
        DetachConsoleForCli();
        return 0;
    }
    if (win32util::EqW(argv[1], L"--open-config-folder")) {
        std::filesystem::create_directories(ConfigStore::Directory());
        const std::filesystem::path dir = ConfigStore::Directory();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        DetachConsoleForCli();
        return 0;
    }
    if (win32util::EqW(argv[1], L"--reset-config")) {
        result = SaveAndReload(AppConfig{}) ? 0 : 1;
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"--set")) {
        if (argc < 3) {
            std::wcerr << L"--set requires key=value. See --help.\n";
            DetachConsoleForCli();
            return 1;
        }
        AppConfig config = ConfigStore::Load();
        std::wstring error;
        if (!ApplySet(config, argv[2], error)) {
            std::wcerr << error << L"\n";
            DetachConsoleForCli();
            return 1;
        }
        result = SaveAndReload(config) ? 0 : 1;
        DetachConsoleForCli();
        return result;
    }
    if (win32util::EqW(argv[1], L"--diagnostics")) {
        PrintStatus(L"text");
        DetachConsoleForCli();
        return 0;
    }

    std::wcerr << L"Unknown command: " << argv[1] << L"\n\n";
    PrintHelp();
    DetachConsoleForCli();
    return 1;
}
