# Clipboard++ Agent Guide

This file is for coding agents working in this repository. It captures the project shape, current implementation details, known pitfalls, and the workflow that has been working for this codebase.

## Project Summary

Clipboard++ is a Windows clipboard manager written in C++17 using:

- Win32 API
- Dear ImGui (docking branch, vendored at `third_party/imgui`)
- DirectX 11 (swap chain per window, no compute shaders)
- nlohmann/json (vendored at `third_party/nlohmann`)
- CMake 3.20+ / Visual Studio 2022

The single executable `clipboardpp.exe` serves three roles depending on arguments:
- **GUI mode**: tray app + main settings window + popup window + hotkeys + clipboard monitor
- **CLI mode**: short-lived process that talks to the running GUI instance via Win32 IPC
- **Single-instance guard**: running without args when an instance exists brings that instance's settings window to the foreground and exits

---

## Build

### Preferred commands (from repo root)

```powershell
# Configure (first time, or after CMakeLists.txt changes)
cmake -S . -B build

# Clean + Release build
cmake --build build --config Release --clean-first

# Incremental Release build
cmake --build build --config Release

# Debug build
cmake --build build --config Debug
```

Output is at `build\Release\clipboardpp.exe` or `build\Debug\clipboardpp.exe`.

### CMakeLists.txt behavior

`CLIPBOARDPP_RESTART_AFTER_BUILD` is `ON` by default. The CMake `PRE_BUILD` step runs:

```powershell
Stop-Process -Name clipboardpp -Force -ErrorAction SilentlyContinue
```

The `POST_BUILD` step launches the newly built exe. This means:
- Always kill any running instance before linking
- The new build launches automatically after link success

`imgui_demo.cpp` is intentionally **not included** in `IMGUI_SOURCES` — the demo is dead weight for this app. Do not add it back.

The static MSVC runtime is used (`MultiThreaded` / `MultiThreadedDebug`), so no MSVC redistributable is needed to distribute the binary.

Do not assume the user sees raw command output. Summarize build failures and key output in responses.

---

## Source Layout

```text
src/main.cpp                     Entry point, CLI dispatch, single-instance logic
src/app/Application.*            Central owner: D3D, windows, hotkeys, monitor, config, profiles
src/app/ConfigStore.*            JSON config persistence
src/app/TrayIcon.*               System tray icon and right-click menu
src/ui/MainWindow.*              Settings window (General, Hotkeys, Appearance, History, Privacy, Developer, About)
src/ui/PopupWindow.*             Always-on-top quick paste popup (the main UX)
src/ui/TrayPopupWindow.*         Small ImGui overlay launched from tray icon
src/ui/DebugWindow.*             Floating debug output console window
src/ui/Appearance.*              Theme definitions, AppearanceSettings struct, color application
src/hotkeys/HotkeyManager.*      Low-level keyboard/mouse hooks, configurable bindings, hidden paste
src/clipboard/ClipboardMonitor.* Win32 clipboard listener and format reader
src/clipboard/ClipboardHistory.* Mutex-protected in-memory history with pin/move/search/dedup
src/clipboard/ClipboardItem.*    Item struct: id, hash, type, tags, text/image/file data, timestamps
src/clipboard/ClipboardHistoryStore.* Per-profile JSON persistence under %APPDATA%\Clipboard++\history\
src/clipboard/ContentDetector.*  Tags copied content with 30+ type/format labels
src/cli/CLI.*                    CLI command dispatch
src/ipc/IpcClient.*              IPC helpers: find instance, signal, send clipboard text
src/util/Win32Util.*             Shared Win32 helpers
resources/app.rc                 Windows resources (manifest, icon)
third_party/imgui/               Vendored Dear ImGui
third_party/nlohmann/            Vendored nlohmann/json
```

---

## Architecture Overview

### Application owns everything

`Application` is a singleton (`Application::Get()`). It owns:
- Main Win32 HWND (invisible message loop window used for app lifetime, hotkey messages, WM_COPYDATA IPC)
- D3D11 device + device context (shared between main window and popup)
- `PopupWindow` — has its own swap chain + render target
- `TrayPopupWindow` — has its own swap chain + render target
- `DebugWindow` — rendered into the main window's swap chain
- `MainWindow` — rendered into the main window's D3D swap chain
- `TrayIcon`
- `HotkeyManager`
- `ClipboardMonitor`
- `AppConfig` + `ConfigStore`
- `vector<unique_ptr<ClipboardHistory>>` — one per clipboard profile, loaded from disk at startup
- `ClipboardHistory* m_history` — pointer to the active profile's history (mutable, not owned)

### Two ImGui contexts

The main window and popup each have their own `ImGuiContext`. Always switch contexts correctly before making ImGui calls. The popup's `Render()` switches to its own context internally. Do not mix calls across contexts.

### Render loop

`Application::RenderFrame()` runs on every frame (via `PeekMessage` loop with idle throttling). It renders:
1. Main window (settings UI) — only when visible
2. `PopupWindow::Render()` — only when popup is visible
3. `TrayPopupWindow::Render()` — only when tray popup is visible
4. `DebugWindow` — rendered inside the main window frame

Idle throttling: when all windows are hidden, the loop sleeps to avoid burning CPU. `WS_EX_NOACTIVATE` on the popup means it never gets `WM_ACTIVATE`, so the popup cannot rely on activation messages to trigger redraws.

---

## Main Window Details

Created with `WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX` — no native title bar. Custom chrome drawn by ImGui. Custom hit-testing in `Application::WndProc` handles resize borders and title bar dragging.

**Settings pages** (sidebar navigation):
- **General**: new-items-at-top toggle, deduplication toggle, start-with-Windows (stub), clipboard profile management
- **Hotkeys**: table of all hotkey bindings with capture/reset per row; hidden paste modifier configuration (Ctrl/Shift/Alt, F-key toggle); append-newline and paste-move-target options
- **Appearance**: theme selector (12 built-in + saved custom); full color customization (14 colors); popup opacity + outline strength sliders; animated outline parameters; default window sizes; font import + size; scrollbar controls; corner rounding; save/delete custom theme
- **History**: max items slider (1–8192); persist-history toggle + session-only sub-option; vault size; live history preview with inline tag badges
- **Privacy**: secret detection toggle + auto-discard; clear-on-lock; process exclusion list text area
- **Developer** (enabled via config): developer mode toggle; CLI toggle; show source process metadata; event log; advanced clipboard routing controls; full runtime diagnostics panel; clipboard item inspector (first 25 items, tree view with all metadata); developer event log textarea
- **About**: version, built-with list, license stub

**Focus gotcha**: the settings window had a bug where reopening required two clicks before ImGui controls responded. The working fix:
- `Application::ShowMainWindow()` and `HideMainWindow()` both call `ClearMainInputState()` to clear ImGui active/input state
- `WM_MOUSEACTIVATE` is processed before `ImGui_ImplWin32_WndProcHandler` in `Application::WndProc`
- Settings-open paths go through `Application::OpenSettingsWindow()`

Do not remove any of those pieces without carefully retesting the two-click regression.

---

## Popup Window Details

`PopupWindow` is intentionally unusual. Win32 extended styles:
- `WS_EX_TOPMOST` — always on top
- `WS_EX_LAYERED` — enables opacity via `SetLayeredWindowAttributes`
- `WS_EX_NOACTIVATE` — does not steal keyboard focus from the target app

Because `WS_EX_NOACTIVATE` means the popup never receives `WM_ACTIVATE` or normal keyboard messages, all keyboard input is forwarded from the low-level keyboard hook in `HotkeyManager`.

### Popup layout (top to bottom)

1. **Title bar** (`DrawTitleBar`):
   - Close button (X)
   - Two circular opacity knobs (window opacity + outline strength), adjustable by mouse wheel
   - Clipboard profile combo input — click to open dropdown list, type to name a new profile, Save button creates a new profile; right-click in the list shows Duplicate/Delete context menu with confirmation modal for Delete

2. **Filter strip** (`DrawFilterStrip`):
   - 10 toggle filter buttons: All, Text, Image, URL, File, Code, Secret, JSON, Email, Color
   - Queue mode toggle
   - Paste All button (visible in queue mode)
   - Newline-after-paste toggle
   - Paste move-target selector
   - Settings gear button

3. **Search bar** (`DrawSearchBar`):
   - Hint text "Search... Shift+Enter for web"
   - Keyboard focus managed via `m_searchCapture` / `m_focusSearchOnOpen`

4. **Item list** (`DrawItemList`):
   - "Pinned entries" collapsible header, then pinned items
   - "History" header, then regular items
   - Each item shows: slot label (`1`–`9`, `a`–`z`), optional `[#]` queue position, optional `[P]` pin marker, preview text
   - Secret-tagged items text is styled in red
   - Drag-and-drop reordering via `DrawItemDragDrop`
   - Right-click → `DrawItemContextMenu`: Paste, Copy, Add/Remove Queue, Move Top/Bottom, Pin/Unpin, Delete

### Keyboard capture state

`PopupWindow` maintains several capture flags to correctly route keys:
- `m_keyboardCapture` — general keyboard capture active
- `m_searchCapture` — search box has focus
- `m_searchActive` — search text is non-empty
- `m_focusSearchOnOpen` — set when opening with `Ctrl+Shift+S`, causes search to receive focus on next frame
- `m_dialogTextCapture` — clipboard name input in title bar has focus

`IsKeyboardCaptureActive()` returns true when any of these are set. `HotkeyManager` checks this before processing slot-paste keys so typing in a search box or name field does not trigger pastes.

### Profile dropdown implementation

The profile picker is an integrated combo box, not a native ImGui `Combo`. It is built from:
- `ImGui::InputText("##clipboard_name", ...)` — editable field
- A floating `ImGui::Begin("##clipboard_profile_dropdown", ...)` window anchored below the input using `ImGuiWindowFlags_NoFocusOnAppearing | NoNav | NoTitleBar | NoResize | NoMove | NoScrollbar | NoSavedSettings`

The `NoFocusOnAppearing` flag is critical — without it the dropdown window steals keyboard focus from the InputText on the same frame it opens, making text entry impossible.

**Context menu routing gotcha**: `BeginPopupContextItem` inside a `Begin` window with `NoFocusOnAppearing` and `NoNav` breaks ImGui's input routing — the popup renders but MenuItem clicks never register. The fix is:
- Detect right-clicks manually inside the dropdown with `IsItemHovered() && IsMouseReleased(Right)`
- Store profile ID, name, and mouse position to `m_contextMenuProfileId/Name/X/Y` + set `m_openProfileContextMenu = true`
- After `ImGui::End()` for the dropdown (back in the `##popup` window context), call `ImGui::OpenPopup("##profile_ctx_menu")` and `BeginPopup` from there — the parent window context has normal input routing

### Paste flow

`PopupWindow::PasteItemKeepOpen(item)` is the primary paste path when the popup stays open:
1. Writes the item to the system clipboard via `WriteToClipboard`
2. Calls `RestoreFocusAndPaste(m_prevForeground)` which:
   - Calls `WaitForForeground` to ensure the target window is ready
   - Sends `Ctrl+V` via `SendInput` (tagged with `kClipboardPasteMagic` to suppress the hook re-capturing it)
3. Updates `lastUsedAt` and moves item per the paste-move-target setting

`m_prevForeground` is captured when the popup opens (the foreground window at that point is the paste target).

---

## Hotkey System

`HotkeyManager` installs a `WH_KEYBOARD_LL` hook via `SetWindowsHookEx`. All keystrokes system-wide pass through this hook while the app is running.

### Default bindings (`HotkeyManager::DefaultBindings()`)

| Action | Default |
|---|---|
| TogglePopup | Ctrl+Shift+V |
| ShowPopupSearch | Ctrl+Shift+S |
| Incognito | Ctrl+Shift+I |
| OpenSettings | Ctrl+Shift+, |
| LaunchClipboardWebSearch | Ctrl+Shift+G |
| ToggleDebugWindow | Alt+Shift+D |

### Hidden paste slot system

Slot keys produce pastes without opening the popup. Slots are numbered `1–9`, `A–Z`, `F1–F12` (in that order) — 44 slots per category.

- `Ctrl+Alt+[slot]` → paste regular history slot N
- `Ctrl+Shift+[slot]` → paste pinned entry slot N  
- `Alt+Shift+[slot]` → activate clipboard profile slot N

Modifiers are configurable (any combination of Ctrl/Shift/Alt). F1–F12 can be enabled/disabled independently.

Slot numbering is **per-section**. Pinned entries start at slot 1 independently of regular history. Regular history starts at slot 1 independently of how many pinned entries exist. Never share a counter across the two sections.

Injected paste keystrokes are tagged with `kClipboardPasteMagic` so the hook ignores them and does not re-capture the pasted content.

### Key forwarding to popup

When the popup is visible and `WS_EX_NOACTIVATE` prevents normal keyboard delivery, the hook forwards relevant keys to `PopupWindow` via direct method calls on the `Application::Get()->GetPopup()` pointer. Search keys and slot keys are forwarded only when `IsKeyboardCaptureActive()` correctly reflects the UI state.

---

## Clipboard Profile System

### Storage model

```text
%APPDATA%\Clipboard++\config.json            Profile metadata + all other settings
%APPDATA%\Clipboard++\history\<id>.json      Per-profile clipboard items
```

`config.json` contains profile metadata (`id`, `name`, `createdAt`, `updatedAt`, `processName`) and `activeClipboardId` but never the actual item content.

History file format:
```json
{
  "version": 1,
  "profileId": "default",
  "nextId": 10,
  "pinned-history": [],
  "regular-history": []
}
```

Only write fields relevant to each item type. Do not write image fields on text items.

### Runtime model

`Application` maintains `vector<unique_ptr<ClipboardHistory>> m_histories` — one per profile entry in `config.clipboards`. The active profile's history is referenced by the raw `m_history` pointer. This pointer is re-set every time the active profile changes. **Do not store `m_history` across profile switches** — fetch it near use via `Application::GetHistory()`.

### Key profile operations

- `CreateClipboardProfile(name, processName)` — generates UUID, creates a new `ClipboardHistory`, appends to `m_histories` and `m_config.clipboards`, sets as active, saves config
- `DeleteActiveClipboardProfile()` — requires `size() > 1`; removes from vectors, deletes history file on disk, switches to next profile
- `SetActiveClipboardProfile(id)` — saves current history to disk, switches `m_history` pointer, triggers monitor sync
- `RenameActiveClipboardProfile(name)` — updates name + updatedAt, saves config
- `SyncClipboardForForegroundProcess()` — called each time the popup opens; if `autoSwitchClipboardByProcess` is enabled and the foreground app matches a different profile's `processName`, switches to that profile

---

## Clipboard History

`ClipboardHistory` is mutex-protected (`std::mutex m_mutex`). All public methods acquire this lock.

### Important operations

- `Push(item)` — adds to top or bottom based on config; deduplicates by content hash (moves existing item instead of inserting duplicate)
- `Insert(item, index)` — position insert with deduplication
- `MoveItemById(id, MoveTarget)` — moves to Top/Bottom/None; updates `lastUsedAt`
- `MoveItemsByIdBefore(ids, beforeId)` — batch reorder for drag-drop
- `SetPinnedById(id, bool)` — pin/unpin; pinned items always sort to the front
- `RemoveItemById(id)` — delete by stable ID
- `GetPinnedCopy(slot)` / `GetRegularCopy(slot)` — 1-indexed slot access for hotkeys
- `Snapshot()` / `LoadSnapshot()` — full serialization for persistence

Pinned items are stored at the head of the vector. Overflow trimming removes from the tail (non-pinned items first).

Always prefer stable item IDs over visible indices for move/delete operations, especially when search/filter is active, because filters change what index N refers to.

---

## Content Detection (`ContentDetector`)

`ContentDetector::Detect(text)` returns a set of string tags. Tag constants are defined in `ClipboardItem.h`.

The 30+ tags in detection order (roughly):

```
TAG_SECRET, TAG_URL, TAG_EMAIL, TAG_IP, TAG_UUID,
TAG_JSON, TAG_XML, TAG_HTML, TAG_CSV, TAG_MARKDOWN, TAG_SQL, TAG_CODE, TAG_COMMAND,
TAG_CONFIG, TAG_PATH, TAG_FILE, TAG_FOLDER,
TAG_IMAGE_FILE, TAG_DOCUMENT, TAG_ARCHIVE, TAG_EXECUTABLE, TAG_SCRIPT, TAG_DATA, TAG_AUDIO, TAG_VIDEO,
TAG_HEX, TAG_DATE, TAG_BASE64, TAG_LOG, TAG_PHONE
```

Secret detection runs first, before any other tag. Secret patterns include: AWS access keys, GitHub tokens (`ghp_`, `ghs_`, `gho_`, `ghr_`), PEM blocks, JWTs, Slack tokens (`xox*`), and generic `*_api_key*` / `*_secret*` patterns.

Path detection distinguishes `TAG_FILE` vs `TAG_FOLDER` vs `TAG_PATH` by whether the path refers to a file with a recognized extension vs a directory-like path.

File extension tags (`TAG_IMAGE_FILE`, `TAG_DOCUMENT`, `TAG_ARCHIVE`, etc.) are applied when a path matches the corresponding extension list.

---

## Appearance System

### AppearanceSettings struct (key fields)

```cpp
ThemeId      theme;                    // Built-in theme selector
float        opacity;                  // Popup window opacity (0.1–1.0)
float        outlineStrength;          // Outline brightness multiplier (0.0–1.0)
bool         animatedOutline;          // Enable multicolor animated outline
float        outlineAnimSpeed;         // Animation speed multiplier
float        outlineColorSpread;       // Hue range of animation
float        outlineSharpness;         // Edge sharpness of outline
float        outlineSaturation;        // Color saturation
float        outlineBrightness;        // Base brightness
bool         outlineReverseDir;        // Reverse animation direction
int          popupW, popupH;           // Default popup size
int          mainW, mainH;             // Default settings window size
std::string  fontPath;                 // Path to imported .ttf/.otf
float        fontSize;                 // Font size in points (9–32)
bool         useCustomColors;          // Enable per-field color overrides
// 14 ImVec4 color fields: windowBg, panelBg, text, mutedText, accent,
//   hover, selectedTab, buttonOff, buttonOn, closeButton,
//   closeButtonHover, closeButtonText, opacityKnobFill, opacityKnobRing
bool         showScrollbars;
float        scrollbarSize, scrollbarRounding, scrollbarPadding;
ImVec4       scrollbarBg, scrollbarGrab, scrollbarGrabHovered, scrollbarGrabActive;
float        popupRounding;            // Popup window corner rounding
float        controlRounding;          // Button/input corner rounding
```

### Built-in themes (ThemeId enum)

`Dark_Default`, `Dracula`, `Nord`, `Monokai`, `OneDarkPro`, `TokyoNight`, `SolarizedDark`, `GitHubDark`, `GitHubLight`, `SolarizedLight`, `VSLight`, `QuietLight`

Each theme sets a full palette via `Appearance::Apply(AppearanceSettings, ImGuiContext*)`.

### Custom themes

Saved as `vector<SavedAppearanceTheme>` in `config.json`. Each entry stores: name + the color/shape subset the user customized. Applied by loading the saved fields back into `AppearanceSettings` and calling `Apply`.

### Font management

`Application::RequestAppearance()` handles font import:
- Copies the font file into `%APPDATA%\Clipboard++\fonts\` if it is not already there
- Rebuilds the ImGui font atlas
- Applies the new atlas to both the main window and popup contexts

---

## Config (`AppConfig` / `ConfigStore`)

### Key fields in `config.json`

```json
{
  "appearance": { ... AppearanceSettings ... },
  "hotkeys": [ { "action": ..., "ctrl": ..., "shift": ..., "alt": ..., "vkey": ... } ],
  "developer": { "enabled": false, "cliEnabled": false, "showSourceProcess": false, "eventLogEnabled": false },
  "newItemsAtTop": true,
  "appendNewlineAfterPaste": false,
  "pasteMoveTarget": 0,
  "activeClipboardId": "default",
  "autoSwitchClipboardByProcess": false,
  "autoCreateClipboardByProcess": false,
  "clipboards": [
    { "id": "default", "name": "Default", "createdAt": "...", "updatedAt": "...", "processName": "" }
  ],
  "savedThemes": [ { "name": "My Theme", ... } ]
}
```

`ConfigStore::Load()` fills in missing fields with defaults, so the file is forward-compatible — adding new fields with defaults does not break existing config files.

`ConfigStore::Save()` is called by `Application::SaveConfig()` which is triggered after any mutation.

---

## IPC / CLI

`src/main.cpp` checks for `--clipboardpp-run-gui` to decide whether to run in GUI mode. Without that flag:
- if `--show`, `--popup`, `config`, `status`, `--clipboard` etc. are present, dispatch to `CLI`
- if no args and an instance is running → signal it to show settings and exit
- if no args and no instance → launch a detached GUI process with `--clipboardpp-run-gui` and exit

Single-instance mutex: `Local\ClipboardPlusPlus`

IPC window class: `ClipboardPlusPlus_Main`

`ipc::SignalRunning(WM_SHOWCPP_MAIN)` or `WM_SHOWPOPUP` — sends window messages to the GUI HWND.

`ipc::SendClipboardHistoryText(text, position, setSystemClipboard)` — sends a `WM_COPYDATA` message with a `ClipboardTextCommand` payload. The GUI inserts the text into the active profile's history at the requested position.

---

## ImGui Conventions and Gotchas

### Window types used

| Type | Focus behavior | Use case |
|---|---|---|
| `BeginPopup` | Steals keyboard focus | Context menus, option popups |
| `BeginPopupModal` | Blocks all input behind it | Confirmation dialogs |
| `Begin` with `NoFocusOnAppearing` | Does not steal focus | Dropdown overlays, floating lists |

### Profile dropdown pattern

The popup title bar profile picker is NOT a native `ImGui::Combo`. It is an `InputText` plus a floating `Begin` window with `NoFocusOnAppearing`. This is because `ImGui::Combo` does not support editable text input.

**Critical**: `BeginPopupContextItem` called inside a `Begin` window carrying `NoFocusOnAppearing | NoNav` will render the popup but MenuItem clicks will silently not register. Always open context menus from the outermost reliable window context (`##popup`) using the stored-state pattern:

```cpp
// Inside dropdown Begin window:
if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
    m_contextMenuProfileId   = profile.id;
    m_contextMenuProfileName = profile.name;
    m_contextMenuX           = ImGui::GetIO().MousePos.x;
    m_contextMenuY           = ImGui::GetIO().MousePos.y;
    m_openProfileContextMenu = true;
}
// ... ImGui::End() for dropdown ...

// Back in ##popup context:
if (m_openProfileContextMenu) {
    ImGui::SetNextWindowPos({m_contextMenuX, m_contextMenuY}, ImGuiCond_Always);
    ImGui::OpenPopup("##profile_ctx_menu");
    m_openProfileContextMenu = false;
}
if (ImGui::BeginPopup("##profile_ctx_menu")) { ... }
```

### Size constraints for auto-resize windows

`SetNextWindowSize({w, 0}, ImGuiCond_Always)` with height 0 resets to zero every frame — window becomes invisible. Use `SetNextWindowSizeConstraints({minW, 0}, {maxW, maxH})` instead to let the window auto-size to content with width constraints.

### IsItemActivated vs IsItemClicked

- `IsItemActivated()` fires only on the frame the item transitions from inactive to active (one shot)
- `IsItemClicked()` fires every click including while the item is already active (needed for toggle behavior)

Use `IsItemClicked` when implementing a toggle that should open/close on repeated clicks of the same item.

---

## Known Gotchas

**Popup keyboard**: `WS_EX_NOACTIVATE` means no `WM_ACTIVATE`, no normal keyboard delivery. All key handling goes through the `WH_KEYBOARD_LL` hook. Do not call `SetForegroundWindow` on the popup.

**Settings window two-click bug**: was caused by stale ImGui input state. Fixed by `ClearMainInputState()` in `ShowMainWindow`/`HideMainWindow` and early `WM_MOUSEACTIVATE` handling. Do not remove these without a regression test.

**GetHistory() pointer staleness**: `Application::GetHistory()` returns a raw pointer to the active profile's `ClipboardHistory`. This pointer is re-pointed on every profile switch. Never cache it across frames or function calls that may trigger a profile switch.

**Visible index vs item ID**: When search/filter is active, visible index N does not correspond to history index N. All move/delete/pin operations must use stable item IDs.

**History file on delete**: `DeleteActiveClipboardProfile()` must delete the `%APPDATA%\Clipboard++\history\<id>.json` file on disk, not just remove the in-memory entry.

**imgui_demo.cpp**: Intentionally excluded from `IMGUI_SOURCES` in `CMakeLists.txt`. `ShowDemoWindow` is never called. Do not add it back.

**Build output paths**: The standard paths are `build\Release\clipboardpp.exe` and `build\Debug\clipboardpp.exe`. Paths under `out\build\*` are Visual Studio-internal CMake integration paths and may exist in dev environments but are not the canonical build output.

---

## Verification Checklist

After hotkey / popup / history changes:
- Popup opens with `Ctrl+Shift+V`, closes with same or Escape
- Popup search opens with `Ctrl+Shift+S`, search field focused
- Visible slot paste (1-9, a-z) works for filtered list positions
- Hidden paste targets the correct foreground app without opening the popup
- Popup stays topmost and does not steal focus from target app
- Search input does not consume slot keys when text entry is not active
- Context menu actions do not trigger accidental pastes
- Queue mode items show `[#]` indicators, Paste All works in sequence
- Drag-drop reordering persists after drop
- Move top/bottom does not duplicate items
- Pinned slot hotkeys only paste pinned items; regular hotkeys only paste regular items
- Slot numbering resets to 1 within each section independently
- Profile combo opens/closes on click; text editing works while list is open
- Right-click on profile item shows context menu; Duplicate and Delete both work
- Delete confirmation modal blocks input; red Delete button and Cancel both work correctly
- Process-bound clipboard switching follows the focused app when enabled

After config / theme / font changes:
- Config loads with missing fields (defaults fill in)
- Config saves new fields correctly
- Both main window and popup reflect appearance changes
- Custom theme round-trip: save → reload → apply matches original

After profile operations:
- New profile appears in picker list and has empty history
- Duplicate profile gets name `"original - duplicate"` and copies content
- Delete (with confirmation) removes profile from list and does not allow deleting last profile
- History file for deleted profile is removed from `%APPDATA%\Clipboard++\history\`

---

## Future Work Notes

Likely next major areas:
- Encrypted vault / history storage using Windows DPAPI
- Full privacy exclusions UI (per-process rules, tag-based rules)
- Richer developer tools: raw format inspection, transforms, templates, export
- Full CLI/IPC coverage for profile and vault commands
- Expanded content filter types in popup
- Multiple paste workflows (paste as plain text, paste with formatting, etc.)
