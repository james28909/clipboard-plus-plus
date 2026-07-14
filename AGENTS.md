# Clipboard++ Agent Guide

This file is for coding agents working in this repository. It captures the project shape, current implementation details, known pitfalls, and the workflow that works for this codebase.

---

## Projects in this repository

| Directory | Executable | Description |
|---|---|---|
| `.` (root) | `clipboardpp.exe` | Main clipboard manager - Win32 tray app, ImGui UI, DirectX 11 |
| `dbviewer/` | `sqlite_editor.exe` | Standalone SQLite database viewer |
| `jsonviewer/` | `json_viewer.exe` | Standalone JSON file viewer |
| `icons/` | - | SVG sources + ICO build tooling (Node.js + Python) |

All three share the same `third_party/` vendors (ImGui, nlohmann/json, SQLite3 is bundled in dbviewer).

---

## Build

### Preferred: build script

```powershell
.\build.ps1                        # all targets, Release
.\build.ps1 -Target clipboardpp   # single target
.\build.ps1 -Config Both          # Release + Debug in one pass
.\build.ps1 -Clean                # clean rebuild
```

Output:
```
build\Release\clipboardpp\clipboardpp.exe
build\Release\sqlite_editor\sqlite_editor.exe
build\Release\json_viewer\json_viewer.exe
build\Debug\<name>\<name>.exe
```

### Manual CMake

```powershell
# Clipboard++ (root)
cmake -S . -B build/.cmake/clipboardpp -G "Visual Studio 17 2022" -A x64
cmake --build build/.cmake/clipboardpp --config Release

# SQLite Editor
cmake -S dbviewer -B build/.cmake/sqlite_editor -G "Visual Studio 17 2022" -A x64
cmake --build build/.cmake/sqlite_editor --config Release

# JSON Viewer
cmake -S jsonviewer -B build/.cmake/json_viewer -G "Visual Studio 17 2022" -A x64
cmake --build build/.cmake/json_viewer --config Release
```

Do NOT pass `-DCMAKE_BUILD_TYPE` when using a Visual Studio generator - it is silently ignored and produces a CMake warning. Configuration is set at build time via `--config`.

### CMakeLists.txt behavior (clipboardpp)

`CLIPBOARDPP_RESTART_AFTER_BUILD` is `ON` by default:
- `PRE_BUILD`: kills any running `clipboardpp.exe`
- `POST_BUILD`: launches the newly built exe

`imgui_demo.cpp` is intentionally **excluded** from `IMGUI_SOURCES`. Do not add it back.

Static MSVC runtime (`MultiThreaded` / `MultiThreadedDebug`) - no redistributable needed.

### Releasing

```powershell
.\release.ps1 -Notes "What changed."
```

This increments `VERSION` (`0.1.0-beta.N` → `0.1.0-beta.N+1`), patches the version string in `MainWindow.cpp`, builds all projects Release+Debug, renames debug exes with a `d` suffix (`clipboardppd.exe` etc.), commits + tags, pushes, and creates a GitHub pre-release with all six executables attached.

---

## Source layout (Clipboard++)

```text
src/main.cpp                      Entry point, CLI dispatch, single-instance logic
src/app/Application.*             Central owner: D3D, windows, hotkeys, monitor, config, profiles
src/app/ConfigStore.*             JSON config persistence
src/app/TrayIcon.*                System tray icon, right-click menu, ApplyTheme (GDI icon regen)
src/ui/MainWindow.*               Settings window (all tabs) + DrawClipboardIconAt() helper
src/ui/PopupWindow.*              Always-on-top quick paste popup
src/ui/TrayPopupWindow.*          Small ImGui overlay from tray
src/ui/DebugWindow.*              Floating debug console
src/ui/Appearance.*               Theme definitions, AppearanceSettings, ThemeDefaults, pipeline
src/ui/ImGuiWidgets.*             Shared widget helpers
src/hotkeys/HotkeyManager.*       WH_KEYBOARD_LL hook, configurable bindings, hidden paste
src/clipboard/ClipboardMonitor.*  Win32 clipboard listener + format reader
src/clipboard/ClipboardHistory.*  Mutex-protected in-memory history
src/clipboard/ClipboardItem.*     Item struct: id, hash, type, tags, text/image/file, timestamps
src/clipboard/ClipboardHistoryStore.*  Per-profile JSON persistence
src/clipboard/ContentDetector.*   30+ content type tags
src/clipboard/ImageStore.*        Image capture, dedup, disk storage, in-app browser
src/ipc/IpcClient.*               IPC helpers (CLI → GUI)
resources/app.rc                  Windows resources (manifest, icon)
third_party/imgui/                Vendored Dear ImGui (docking branch)
third_party/nlohmann/             Vendored nlohmann/json
```

---

## Appearance system

### AppearanceSettings struct - key color fields

Every color field added here must be wired through **four** locations:
1. `SavedAppearanceTheme` struct (same field, same default)
2. `ThemeDefaults()` in `Appearance.cpp` - derive from theme palette after the switch
3. `EffectiveSettings()` - copy `theme.<field>` into `effective.<field>`
4. `ToSavedTheme()` / `ApplySavedTheme()` - copy field both directions
5. `LoadColorFields()` / `SaveColorFields()` in `ConfigStore.cpp` - JSON persist

Current color fields (beyond the standard palette):

```cpp
// Title bar window buttons
ImVec4 titleMinBase, titleMaxBase, titleCloseBase;   // transparent = no fill
ImVec4 titleMinHover, titleMaxHover, titleCloseHover;

// Clipboard icon (theme-driven, runtime-rendered)
ImVec4 iconBoardTop, iconBoardBottom;   // board gradient
ImVec4 iconPaper;                       // paper background
ImVec4 iconMarginLine;                  // red vertical line
ImVec4 iconRuledLines;                  // horizontal ruled lines

// Opacity knob
ImVec4 opacityKnobFill, opacityKnobRing;

// Scrollbar
ImVec4 scrollbarBg, scrollbarGrab, scrollbarGrabHover, scrollbarGrabActive;
bool   showScrollbars;
float  scrollbarSize, scrollbarRounding, scrollbarPadding;
```

### ThemeDefaults() derivations (after switch)

After the theme switch block, icon colors are derived:
```cpp
settings.iconBoardTop    = settings.hover;
settings.iconBoardBottom = Mix(settings.accent, Color(8, 12, 30), 0.55f);
settings.iconPaper       = Color(255, 255, 255);
settings.iconMarginLine  = settings.closeButtonHover;
settings.iconRuledLines  = settings.accent;
```

### DrawClipboardIconAt (MainWindow.cpp)

```cpp
static void DrawClipboardIconAt(ImDrawList* dl, ImVec2 pos, float sz,
                                const AppearanceSettings& ap);
static void DrawClipboardIcon(float sz, const AppearanceSettings& ap);
```

`DrawClipboardIconAt` draws the full clipboard shape procedurally via ImDrawList - no texture. Use it anywhere you need a live-themed clipboard icon:
- `DrawTitleBar()` - small icon in the title bar strip
- `DrawAbout()` - 64px icon in the About page
- `DrawAppearance()` - 48px live preview beside the icon color pickers

### TrayIcon::ApplyTheme

Called from `Application::ApplyAppearanceNow()` whenever the theme changes. Generates a new HICON via GDI (`BuildHIcon`) and calls `Shell_NotifyIconW(NIM_MODIFY)` to update the system tray icon in real time. The compiled `.ico` in `resources/app.rc` is used only for taskbar/Alt+Tab/shell - it is not updated at runtime.

---

## Sub-project details

### SQLite Editor (`dbviewer/`)

Standalone Win32 app built on Dear ImGui + DirectX 11. Opens `.db`, `.sqlite`, `.sqlite3` files for browsing and querying.

Key source: `dbviewer/src/DbViewer.cpp`, `DbViewer.h`  
Resource file: `dbviewer/res/sqlite_editor.rc` (references `../../icons/sqlite_editor.ico`)  
Shell integration: `dbviewer/sqlite_editor.reg` - installs right-click "Open with SQLite Editor" for `.db`, `.sqlite`, `.sqlite3`

Window setup pattern (shared with JsonViewer):
```cpp
wc.hIcon   = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
// after CreateWindowExW:
SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)LoadIconW(...));
SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(...));
```

### JSON Viewer (`jsonviewer/`)

Standalone Win32 app. Opens `.json` files with tree view and syntax highlighting.

Key source: `jsonviewer/src/JsonViewer.cpp`, `JsonViewer.h`  
Resource file: `jsonviewer/res/json_viewer.rc`  
Shell integration: `jsonviewer/json-viewer.reg`

---

## Icon build pipeline (`icons/`)

Source: one SVG per project (`clipboardpp.svg`, `sqlite_editor.svg`, `json_viewer.svg`)

Tools:
- `render_svg.js` - Node.js, uses `@resvg/resvg-js` (Rust renderer, no system deps), renders SVG at 7 sizes
- `make_ico.py` - Python 3, assembles 7 PNGs into a multi-resolution ICO using `struct.pack`

```powershell
cd icons && npm install   # first time
node render_svg.js clipboardpp.svg clipboardpp && python make_ico.py clipboardpp
```

See `icons/README.md` for full usage.

The SVG uses a **double-stroke technique** for lines (wide black outline drawn first, narrower color on top) to ensure visibility against any background. The board uses `linearGradient`. The clip housing uses a 4-stop metallic gradient.

---

## Architecture overview (Clipboard++)

### Application owns everything

`Application` is a singleton (`Application::Get()`). It owns:
- Main Win32 HWND (message loop, hotkeys, WM_COPYDATA IPC)
- D3D11 device + context (shared between main window and popup)
- `PopupWindow` - own swap chain + render target
- `TrayPopupWindow` - own swap chain + render target
- `MainWindow`, `DebugWindow` - rendered into the main swap chain
- `TrayIcon` - system tray, calls `ApplyTheme()` on appearance change
- `HotkeyManager`, `ClipboardMonitor`, `AppConfig`, `ImageStore`
- `vector<unique_ptr<ClipboardHistory>>` - one per profile
- `ClipboardHistory* m_history` - active profile pointer (not owned)

### Two ImGui contexts

Main window and popup each have their own `ImGuiContext`. Always switch contexts before making ImGui calls. Never mix calls across contexts.

### Render loop

`Application::RenderFrame()` runs on every frame (PeekMessage loop with idle throttling):
1. Main window - only when visible
2. PopupWindow - only when popup is visible
3. TrayPopupWindow - only when tray popup is visible
4. DebugWindow - inside the main window frame

`ApplyAppearanceNow()` runs when `m_appearanceDirty` is set. It applies to all windows + regenerates the tray icon.

---

## Main window (settings)

Created with `WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX` - no native title bar. Custom chrome via ImGui.

**Settings tabs:**
- General, Hotkeys, Appearance, History, Images, Privacy, Developer (debug only), About

**Custom title bar**: drawn by `DrawTitleBar()`, calls `DrawClipboardIcon()` for the title bar icon. Button colors (base + hover for min/max/close) are theme-driven via `ap.titleMinBase/Hover`, etc.

**About page**: calls `DrawClipboardIcon(64px, ap)` - no texture, fully theme-driven.

**Appearance tab - Icon section**: 5 color pickers (Board top, Board bottom, Paper, Margin line, Ruled lines) + live 48px preview.

**Focus gotcha**: reopening the settings window required two clicks before ImGui responded. Fixed by:
- `ClearMainInputState()` in both `ShowMainWindow()` and `HideMainWindow()`
- `WM_MOUSEACTIVATE` processed before `ImGui_ImplWin32_WndProcHandler`
- `OpenSettingsWindow()` always calls `ShowMainWindow()`
Do not remove any of these pieces.

---

## Popup window

`WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE` - never steals focus. All keyboard input is forwarded from the `WH_KEYBOARD_LL` hook.

Layout (top → bottom):
1. Title bar - close button, opacity knobs, profile combo
2. Filter strip - 10 type filters, queue mode, paste-all, settings gear
3. Search bar
4. Item list - pinned section, regular history, drag-drop reorder

Keyboard capture flags: `m_keyboardCapture`, `m_searchCapture`, `m_searchActive`, `m_focusSearchOnOpen`, `m_dialogTextCapture`. `HotkeyManager` checks `IsKeyboardCaptureActive()` before slot-paste keys.

**Profile dropdown context menu gotcha**: `BeginPopupContextItem` inside a `Begin` window with `NoFocusOnAppearing | NoNav` renders but MenuItem clicks never register. Fix: detect right-clicks manually, store state, open the popup from the `##popup` window context after closing the dropdown. See AGENTS.md for the full pattern.

---

## Hotkey system

`WH_KEYBOARD_LL` hook. 44 slot keys per category (1–9, A–Z, F1–F12).

- `Ctrl+Alt+[slot]` → paste regular history slot
- `Ctrl+Shift+[slot]` → paste pinned slot
- `Alt+Shift+[slot]` → activate clipboard profile slot

Injected paste keystrokes tagged with `kClipboardPasteMagic` to suppress hook re-capture.

---

## Config

`%APPDATA%\Clipboard++\config.json` - loaded/saved by `ConfigStore`. Missing fields are filled with defaults; adding new fields with defaults is forwards-compatible.

`AppConfig` contains: `appearance` (full `AppearanceSettings`), `hotkeys`, `developer`, behavioral flags, `activeClipboardId`, `clipboards` (profile metadata), `savedThemes`.

---

## IPC / CLI

Single-instance mutex: `Local\ClipboardPlusPlus`  
IPC window class: `ClipboardPlusPlus_Main`

`ipc::SignalRunning(WM_SHOWCPP_MAIN)` - brings GUI to foreground.
`ipc::SendClipboardHistoryText(text, position, setSystemClipboard)` - WM_COPYDATA with ClipboardTextCommand payload.

---

## Known gotchas

- **Popup keyboard**: `WS_EX_NOACTIVATE` means no `WM_ACTIVATE`, no normal key delivery. All keys via hook. Never call `SetForegroundWindow` on the popup.
- **Settings two-click**: fixed by `ClearMainInputState`. Do not remove.
- **GetHistory() pointer**: raw pointer re-pointed on profile switch. Never cache across function calls that may switch profiles.
- **Visible index vs item ID**: with search/filter active, visible index ≠ history index. Always use stable item IDs.
- **imgui_demo.cpp**: intentionally excluded. Do not add it back.
- **Build type warning**: never pass `-DCMAKE_BUILD_TYPE` with Visual Studio generators.
- **ThemeDefaults + EffectiveSettings**: every new color field must be wired through all 5 locations (see Appearance system section above). Missing one = custom themes silently lose the field.

---

## Verification checklist

After hotkey / popup / history changes:
- Popup opens/closes with Ctrl+Shift+V
- Search opens with Ctrl+Shift+S, search field focused
- Slot paste works for filtered positions
- Hidden paste targets correct foreground app
- Popup stays topmost and does not steal focus
- Queue mode shows [#] indicators; Paste All works

After appearance / theme changes:
- All windows update on theme switch
- System tray icon redraws with new colors
- Custom theme round-trip: save → reload → apply matches original
- Icon color pickers update live preview in Appearance tab

After config changes:
- Config loads with missing fields (defaults fill in)
- Config saves new fields correctly

After sub-project changes (dbviewer / jsonviewer):
- Shell .reg files reference correct exe path after install
- Window icon appears in taskbar, title bar, and Alt+Tab
