#include <windows.h>
#include <shellapi.h>

#include "app/Application.h"
#include "app/ConfigStore.h"

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

bool Eq(const wchar_t* a, const wchar_t* b) {
    return _wcsicmp(a, b) == 0;
}

std::wstring ToWide(const std::filesystem::path& path) {
    return path.wstring();
}

std::wstring ToWide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
    return out;
}

HWND FindRunningInstance() {
    return FindWindowW(L"ClipboardPlusPlus_Main", nullptr);
}

void SignalRunning(UINT message) {
    if (HWND hwnd = FindRunningInstance())
        PostMessageW(hwnd, message, 0, 0);
}

void AttachConsoleForCli() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    FILE* out = nullptr;
    freopen_s(&out, "CONOUT$", "w", stdout);
    FILE* err = nullptr;
    freopen_s(&err, "CONOUT$", "w", stderr);
}

void DetachConsoleForCli() {
    std::wcout.flush();
    std::wcerr.flush();
    fflush(stdout);
    fflush(stderr);
    FreeConsole();
}

void PrintHelp() {
    std::wcout <<
LR"(Clipboard++ command line

Usage:
  clipboardpp.exe [command] [options]
  clipboardpp.exe config <operation> [value]
  clipboardpp.exe set <operation> [value]

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
  --clipboard <text> [--top|--bottom|--index 1-500] [--system]
      Insert text into Clipboard++ history. Top is the default. If --system is
      supplied, also put the text onto the Windows clipboard.

  --clipboard-file <path> [--top|--bottom|--index 1-500] [--system]
      Read a UTF-8 text file and insert its contents into Clipboard++ history.

Configuration command:
  config --path
      Print the full path to config.json.

  config --open-folder
      Open the configuration folder in File Explorer.

  config --reset
      Replace config.json with defaults and ask the running app to reload.

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
          Full path to a .ttf or .otf font file.

        font.size
          Floating point font size from 9.0 to 32.0.

        history.newAtTop
          true/false. Controls whether new clipboard items go to the top.

        paste.newline
          true/false. Appends a newline after pasted text.

        paste.move
          keep, top, or bottom.

Clipboard command:
  set --clipboard <text> [--top|--bottom|--index 1-500] [--system]
      Insert text into Clipboard++ history.

  set --clipboard-file <path> [--top|--bottom|--index 1-500] [--system]
      Read a UTF-8 text file and insert its contents into Clipboard++ history.

Exit codes:
  0   Command succeeded.
  1   Command failed, used an invalid value, or referenced an unknown key.

Current scope:
  This CLI manages the running window and app configuration available today.
  The --clipboard commands insert text into the running Clipboard++ history.
  Use --system when you also want the real Windows clipboard changed.
  Direct history/vault commands are planned in SPEC.md, but require the encrypted
  history/vault IPC layer before they can be safely exposed here.

Help:
  --help, -h, /?
      Show this help.

Examples:
  clipboardpp.exe --show
  clipboardpp.exe --popup
  clipboardpp.exe --config-path
  clipboardpp.exe --set theme=TokyoNight
  clipboardpp.exe config --list
  clipboardpp.exe config --get popup.opacity
  clipboardpp.exe config --set paste.newline true
  clipboardpp.exe --clipboard "hello from the CLI" --top
  clipboardpp.exe --clipboard "archive this lower" --bottom
  clipboardpp.exe --clipboard "put me at slot 7" --index 7
  clipboardpp.exe --clipboard-file "C:\Temp\note.txt" --bottom --system
  clipboardpp.exe --set popup.opacity=0.85
  clipboardpp.exe --set font.path="C:\Windows\Fonts\consola.ttf"
  clipboardpp.exe --set font.size=16
  clipboardpp.exe --set paste.move=bottom
  clipboardpp.exe status --format json

Notes:
  Running clipboardpp.exe with no arguments starts the tray app.
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

bool PushClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr))
        return false;

    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }

    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    std::memcpy(data, text.c_str(), bytes);
    GlobalUnlock(mem);

    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

struct ClipboardCliOptions {
    int position{0};
    bool setSystemClipboard{false};
};

bool ParseClipboardOptions(int argc, wchar_t** argv, int start, ClipboardCliOptions& options) {
    for (int i = start; i < argc; ++i) {
        if (Eq(argv[i], L"--top")) {
            options.position = 0;
        } else if (Eq(argv[i], L"--bottom")) {
            options.position = -1;
        } else if (Eq(argv[i], L"--index")) {
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
        } else if (Eq(argv[i], L"--system")) {
            options.setSystemClipboard = true;
        } else {
            std::wcerr << L"Unknown clipboard option: " << argv[i] << L"\n";
            return false;
        }
    }
    return true;
}

bool SendClipboardHistoryText(const std::wstring& text, const ClipboardCliOptions& options) {
    HWND hwnd = FindRunningInstance();
    if (!hwnd)
        return false;

    const size_t headerBytes = offsetof(ClipboardTextCommand, text);
    const size_t textBytes = (text.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(headerBytes + textBytes);

    auto* cmd = reinterpret_cast<ClipboardTextCommand*>(buffer.data());
    cmd->position = options.position;
    cmd->setSystemClipboard = options.setSystemClipboard ? TRUE : FALSE;
    std::memcpy(cmd->text, text.c_str(), textBytes);

    COPYDATASTRUCT cds{};
    cds.dwData = CD_CLIPBOARD_TEXT;
    cds.cbData = static_cast<DWORD>(buffer.size());
    cds.lpData = buffer.data();

    DWORD_PTR result = 0;
    LRESULT sent = SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                                       SMTO_ABORTIFHUNG, 2000, &result);
    return sent != 0 && result == TRUE;
}

int InsertClipboardText(const std::wstring& text, const ClipboardCliOptions& options) {
    if (text.empty()) {
        std::wcerr << L"Clipboard text cannot be empty.\n";
        return 1;
    }

    if (!SendClipboardHistoryText(text, options)) {
        if (!options.setSystemClipboard && PushClipboardText(text)) {
            std::wcerr << L"Clipboard++ is not running; wrote text to the Windows clipboard only.\n";
            return 1;
        }
        if (options.setSystemClipboard && PushClipboardText(text)) {
            std::wcerr << L"Clipboard++ is not running; wrote text to the Windows clipboard only.\n";
            return 1;
        }
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
    if (key == L"font.size" || key == L"fontsize")
        return std::to_wstring(config.appearance.fontSize);
    if (key == L"history.newattop" || key == L"historynewattop")
        return BoolText(config.newItemsAtTop);
    if (key == L"paste.newline" || key == L"pastenewline")
        return BoolText(config.appendNewlineAfterPaste);
    if (key == L"paste.move" || key == L"pastemove")
        return PasteMoveText(config.pasteMoveTarget);
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
        L"history.newAtTop",
        L"paste.newline",
        L"paste.move",
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
    const bool running = FindRunningInstance() != nullptr;
    if (json) {
        std::wcout << L"{\n";
        std::wcout << L"  \"running\": " << (running ? L"true" : L"false") << L",\n";
        std::wcout << L"  \"configPath\": \"" << JsonEscape(ToWide(ConfigStore::Path())) << L"\",\n";
        std::wcout << L"  \"configFolder\": \"" << JsonEscape(ToWide(ConfigStore::Directory())) << L"\"\n";
        std::wcout << L"}\n";
        return;
    }

    std::wcout << L"Running: " << (running ? L"yes" : L"no") << L"\n";
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
            config.appearance.fontPath = Narrow(value);
        } else if (key == L"font.size" || key == L"fontsize") {
            config.appearance.fontSize = std::clamp(std::stof(value), 9.0f, 32.0f);
        } else if (key == L"history.newattop" || key == L"historynewattop") {
            bool b{};
            if (!ParseBool(value, b)) {
                error = L"history.newAtTop expects true/false.";
                return false;
            }
            config.newItemsAtTop = b;
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
    SignalRunning(WM_RELOAD_CONFIG);
    return true;
}

int RunConfigCommand(int argc, wchar_t** argv) {
    if (argc < 3 || Eq(argv[2], L"--help") || Eq(argv[2], L"-h") || Eq(argv[2], L"/?")) {
        PrintHelp();
        return 0;
    }

    if (Eq(argv[2], L"--path")) {
        std::wcout << ToWide(ConfigStore::Path()) << L"\n";
        return 0;
    }
    if (Eq(argv[2], L"--open-folder")) {
        std::filesystem::create_directories(ConfigStore::Directory());
        const std::filesystem::path dir = ConfigStore::Directory();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (Eq(argv[2], L"--reset")) {
        return SaveAndReload(AppConfig{}) ? 0 : 1;
    }
    if (Eq(argv[2], L"--list")) {
        PrintConfigList(ConfigStore::Load());
        return 0;
    }
    if (Eq(argv[2], L"--get")) {
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
    if (Eq(argv[2], L"--set")) {
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
    if (argc < 3 || Eq(argv[2], L"--help") || Eq(argv[2], L"-h") || Eq(argv[2], L"/?")) {
        PrintHelp();
        return 0;
    }

    if (Eq(argv[2], L"--clipboard")) {
        if (argc < 4) {
            std::wcerr << L"set --clipboard requires text. See --help.\n";
            return 1;
        }
        ClipboardCliOptions options;
        if (!ParseClipboardOptions(argc, argv, 4, options))
            return 1;
        return InsertClipboardText(argv[3], options);
    }

    if (Eq(argv[2], L"--clipboard-file")) {
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

int RunCLI(int argc, wchar_t** argv) {
    AttachConsoleForCli();

    int result = 0;

    if (argc <= 1 || Eq(argv[1], L"--help") || Eq(argv[1], L"-h") || Eq(argv[1], L"/?")) {
        PrintHelp();
        DetachConsoleForCli();
        return 0;
    }

    if (Eq(argv[1], L"--show") || Eq(argv[1], L"--settings")) {
        SignalRunning(WM_SHOWCPP_MAIN);
        DetachConsoleForCli();
        return 0;
    }
    if (Eq(argv[1], L"--popup")) {
        SignalRunning(WM_SHOWPOPUP);
        DetachConsoleForCli();
        return 0;
    }
    if (Eq(argv[1], L"status")) {
        std::wstring format = L"text";
        if (argc >= 4 && Eq(argv[2], L"--format"))
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
    if (Eq(argv[1], L"config")) {
        result = RunConfigCommand(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (Eq(argv[1], L"set")) {
        result = RunSetCommand(argc, argv);
        DetachConsoleForCli();
        return result;
    }
    if (Eq(argv[1], L"--clipboard")) {
        if (argc < 3) {
            std::wcerr << L"--clipboard requires text. See --help.\n";
            DetachConsoleForCli();
            return 1;
        }
        ClipboardCliOptions options;
        if (!ParseClipboardOptions(argc, argv, 3, options)) {
            DetachConsoleForCli();
            return 1;
        }
        result = InsertClipboardText(argv[2], options);
        DetachConsoleForCli();
        return result;
    }
    if (Eq(argv[1], L"--clipboard-file")) {
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
    if (Eq(argv[1], L"--config-path")) {
        std::wcout << ToWide(ConfigStore::Path()) << L"\n";
        DetachConsoleForCli();
        return 0;
    }
    if (Eq(argv[1], L"--open-config-folder")) {
        std::filesystem::create_directories(ConfigStore::Directory());
        const std::filesystem::path dir = ConfigStore::Directory();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        DetachConsoleForCli();
        return 0;
    }
    if (Eq(argv[1], L"--reset-config")) {
        result = SaveAndReload(AppConfig{}) ? 0 : 1;
        DetachConsoleForCli();
        return result;
    }
    if (Eq(argv[1], L"--set")) {
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
    if (Eq(argv[1], L"--diagnostics")) {
        PrintStatus(L"text");
        DetachConsoleForCli();
        return 0;
    }

    std::wcerr << L"Unknown command: " << argv[1] << L"\n\n";
    PrintHelp();
    DetachConsoleForCli();
    return 1;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc > 1) {
        int result = RunCLI(argc, argv);
        LocalFree(argv);
        return result;
    }
    LocalFree(argv);

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ClipboardPlusPlus");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        SignalRunning(WM_SHOWCPP_MAIN);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    Application app(hInstance);
    int result = app.Run();

    CloseHandle(hMutex);
    return result;
}
