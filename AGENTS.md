# Clipboard++ Agent Guide

This file is for coding agents working in this repository. It captures the project shape, current implementation details, known pitfalls, and the workflow that has been working for this codebase.

## Project Summary

Clipboard++ is a Windows clipboard manager written in C++17 using:

- Win32 API
- Dear ImGui docking branch
- DirectX 11
- nlohmann/json
- CMake / Visual Studio

The executable is `clipboardpp.exe`. It runs primarily as a tray app with:

- a main settings GUI
- a separate always-on-top popup clipboard UI
- global hotkeys
- clipboard monitoring
- CLI/IPC support
- multiple in-memory clipboard profiles

The app is still under active development. Some planned areas, such as encrypted persistent history/vault storage and fuller developer options, are not finished yet.

## Build

The user usually builds from Visual Studio. The command that has worked from this repo root is:

```powershell
Stop-Process -Name clipboardpp -Force -ErrorAction SilentlyContinue
& cmd.exe /c '"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out\build\x64-Debug --config Debug'
```

`CMakeLists.txt` has `CLIPBOARDPP_RESTART_AFTER_BUILD` enabled by default. On Windows it:

- stops a running `clipboardpp.exe` before build
- launches the newly built executable after build

Do not assume the user sees command output. Summarize build failures and important output in responses.

## Source Layout

Important files and directories:

- `src/main.cpp`: process entry point, GUI launcher, CLI dispatch, single-instance behavior.
- `src/app/Application.*`: central application owner for Win32 main window, D3D, render loop, config, tray, popup, hotkeys, monitor, profile switching, IPC messages.
- `src/app/ConfigStore.*`: JSON config persistence in `%APPDATA%\Clipboard++\config.json`.
- `src/app/TrayIcon.*`: tray menu and tray double-click behavior.
- `src/ui/MainWindow.*`: main settings GUI.
- `src/ui/PopupWindow.*`: always-on-top popup list, search, paste, queue, drag/drop, right-click item menu.
- `src/ui/Appearance.*`: themes, popup toggle colors, font/style application.
- `src/hotkeys/HotkeyManager.*`: low-level keyboard and mouse hooks, configurable hotkeys, visible/hidden paste logic.
- `src/clipboard/ClipboardMonitor.*`: Win32 clipboard listener and clipboard format reading.
- `src/clipboard/ClipboardHistory.*`: in-memory history list, dedupe, move, queue support, pinned/delete mutations.
- `src/clipboard/ClipboardItem.*`: item metadata, timestamps, content hash, preview.
- `src/clipboard/ContentDetector.*`: tags copied content by content/type/extension.
- `src/cli/CLI.*`: command-line interface.
- `src/ipc/IpcClient.*`: IPC helpers for finding/signaling the running GUI and sending clipboard text.
- `third_party/imgui`: vendored Dear ImGui.
- `third_party/nlohmann`: vendored JSON library.
- `resources/app.rc`: Windows resources.

## Main Architecture

`Application` owns almost everything:

- main Win32 HWND
- D3D11 device/context/swap chain
- tray icon
- popup window
- clipboard monitor
- hotkey manager
- config
- clipboard profile metadata
- one `ClipboardHistory` per active clipboard profile

The main window and popup are separate Win32 windows and separate ImGui contexts. Be careful to switch ImGui contexts correctly in popup code.

The popup has its own swap chain and render target. It is rendered from `Application::RenderFrame()` after the main window render.

## Main Window Details

The main settings window is custom chrome:

- created as `WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`
- no native title bar
- custom hit testing in `Application::WndProc`
- custom title buttons in `MainWindow::DrawTitleBar`

Focus gotcha: the settings window had a bug where reopening it required two clicks before ImGui controls reacted. The working fix includes:

- `Application::ShowMainWindow()` clears main ImGui active/input state via `ClearMainInputState()`.
- `Application::HideMainWindow()` also clears main ImGui active/input state.
- `WM_MOUSEACTIVATE` is handled before `ImGui_ImplWin32_WndProcHandler`.
- settings-open routes go through `Application::OpenSettingsWindow()` where appropriate.

Do not casually remove those pieces unless you are specifically retesting the two-click focus bug.

## Popup Window Details

`PopupWindow` is intentionally unusual:

- `WS_EX_TOPMOST`: stays above standard windows.
- `WS_EX_LAYERED`: opacity support.
- `WS_EX_NOACTIVATE`: popup does not steal focus from the target app.
- `WS_POPUP | WS_THICKFRAME`: borderless but resizable.

Because the popup does not activate, keyboard handling uses the low-level keyboard hook in `HotkeyManager`, which forwards keys into the popup when needed.

Popup behavior:

- `Ctrl+Shift+V`: toggles quick paste popup.
- `Ctrl+Shift+S`: opens/focuses popup search.
- visible popup supports 1-9/a-z quick paste of visible filtered items.
- visible popup supports mouse selection, queue mode, newline toggle, paste move mode, right-click item menu, drag/drop reordering.
- hidden paste uses configured modifiers, currently default `Ctrl+Alt+1-z` plus optional function keys.
- popup can stay visible while pasting multiple items.
- popup should not disturb the cursor/focus in the target app.

Popup context menu currently supports:

- Paste
- Copy to clipboard
- Add/remove queue
- Move to top
- Move to bottom
- Pin/unpin
- Delete

## Hotkeys And Slot Numbering

Default hotkeys are in `HotkeyManager::DefaultBindings()`:

- `Ctrl+Shift+V`: toggle popup
- `Ctrl+Shift+S`: focus popup search
- `Ctrl+Shift+I`: incognito placeholder
- `Ctrl+Shift+,`: open settings

Hidden paste defaults are:

- ctrl: true
- alt: true
- shift: false
- function keys enabled

The low-level keyboard hook ignores injected paste events tagged with `kClipboardPasteMagic`.

When popup is visible:

- normal slot keys paste visible slot if search is not active
- search input receives forwarded keys
- escape closes popup

When popup is hidden:

- regular history paste uses current target foreground window and then calls `PasteHistorySlot`.
- pinned paste uses current target foreground window and then calls `PastePinnedSlot`.
- hidden paste must sync the active clipboard to the target process before reading the slot.

Slot numbering is independent per section. Do not share one counter across pinned and regular history.

Slot order for every section is:

```text
1-9, then A-Z, then F1-F12
```

Pinned entries:

- section: `Pinned entries`
- hotkey: `Ctrl+Shift+1` through `Ctrl+Shift+9`, then `Ctrl+Shift+A-Z`, then `Ctrl+Shift+F1-F12`
- numbering starts at `1` inside pinned entries

Regular history:

- section: `History`
- hidden hotkey default: `Ctrl+Alt+1` through `Ctrl+Alt+9`, then `Ctrl+Alt+A-Z`, then `Ctrl+Alt+F1-F12`
- numbering starts at `1` inside regular history, regardless of how many pinned entries exist

Example:

```text
Pinned entries
1  pinned item

History
1  newest regular item
2  next regular item
```

Never render this as regular history starting at `2` just because one pinned item exists.

## Multiple Clipboard Profiles

Clipboard profiles are implemented as metadata in config plus persistent per-profile history files.

The storage split is important:

- `%APPDATA%\Clipboard++\config.json` stores settings and clipboard profile metadata only.
- `%APPDATA%\Clipboard++\history\<profile-id>.json` stores actual clipboard items for that profile.
- Pinned entries are not stored in `config.json`; they are stored under `pinned-history` in that profile's history JSON file.
- Regular, non-pinned entries are stored under `regular-history`.

Config fields:

- `activeClipboardId`
- `autoSwitchClipboardByProcess`
- `autoCreateClipboardByProcess`
- `clipboards[]`

Each profile has:

- `id`
- `name`
- `createdAt`
- `updatedAt`
- `processName`

Important behavior:

- `Application::GetHistory()` returns the active profile's history.
- `Application::SwitchClipboardForProcess()` switches to a profile bound to a process.
- If auto-create is enabled and no profile exists for a process, a new profile is created and bound automatically.
- User-created clipboards should allow naming before creation.
- Deleting a clipboard must remove the profile metadata, in-memory history, and `%APPDATA%\Clipboard++\history\<profile-id>.json`.
- Do not allow deleting the final remaining clipboard profile.
- Popup render calls `SyncClipboardForForegroundProcess()`.
- External mouse clicks outside the popup call `SyncClipboardForWindow()`.
- Hidden paste syncs to the target HWND before using the requested slot.
- Copy events also switch by `ClipboardItem::sourceProcess`.

Current limitation:

- profile history contents persist as JSON, but are not encrypted yet.
- future vault/history storage should migrate these per-profile items into encrypted storage.

## Clipboard History

`ClipboardHistory` is a mutex-protected in-memory vector of `ClipboardItem`.

At runtime, pinned and regular items are held in the same `ClipboardHistory` for the active profile, but the container enforces pinned-first ordering. UI and hotkeys must treat pinned and regular items as separate sections with separate numbering.

Important operations:

- `Push`
- `Insert`
- `Get` / `GetCopy`
- `GetById` / `GetByIdCopy`
- `MoveItem`
- `MoveItemById`
- `MoveItemsByIdBefore`
- `RemoveItemById`
- `SetPinnedById`
- `Clear`

Deduplication uses `ClipboardItem::contentHash`. Moving items should not duplicate them. Prefer moving by item ID instead of index when UI filters/search are involved.

Search/filter UI uses visible indices, but paste/move/delete operations should prefer stable item IDs.

Pinned behavior:

- `ClipboardItem::pinned` is persisted per item.
- Pinned items stay at the top of the profile history.
- Pinned items render under `Pinned entries`.
- Regular items render under `History`.
- Overflow trimming should prefer removing regular entries before pinned entries.
- Pinning/unpinning should preserve item identity and should not duplicate the item.
- Pinned slot hotkeys read only pinned items.
- Regular history hotkeys read only regular, non-pinned items.

## Clipboard Item Metadata

`ClipboardItem` includes:

- stable `id`
- `contentHash`
- `type`
- `tags`
- text/image/file data
- source process
- timestamps:
  - `timestamp`
  - `createdAt`
  - `updatedAt`
  - `lastUsedAt`
- `pinned`

Content detection tags copied data with URL, email, code, JSON, file, folder, document, archive, media, color, secret, etc. File/folder classification uses path/extension checks.

## CLI and IPC

`src/main.cpp` dispatches to CLI unless the internal `--clipboardpp-run-gui` argument is present.

Running `clipboardpp.exe` without args:

- if an instance is already running, signals it to show settings and exits
- otherwise starts a detached GUI process and exits the shell

The GUI single instance uses mutex `Local\ClipboardPlusPlus`.

IPC:

- `ipc::FindRunningInstance()` finds `ClipboardPlusPlus_Main`.
- `ipc::SignalRunning()` sends app messages to the GUI.
- `ipc::SendClipboardHistoryText()` sends text insertion data via `WM_COPYDATA`.

Current command-line behavior includes settings/config/status and `--clipboard` insertion paths. `--clipboard set` writes the real Windows clipboard. History insertion requires the running Clipboard++ GUI process.

## Config

Config path:

```text
%APPDATA%\Clipboard++\config.json
```

`config.json` stores settings and profile metadata, including:

- appearance settings
- hotkey settings
- paste behavior settings
- active clipboard profile ID
- process auto-switch/auto-create settings
- clipboard profile names, IDs, timestamps, and bound process names

`config.json` does not store actual copied items and does not store pinned entries directly.

History path:

```text
%APPDATA%\Clipboard++\history
```

Each clipboard profile has a separate history file:

```text
%APPDATA%\Clipboard++\history\<profile-id>.json
```

Those history files store actual copied items, including:

- item ID
- text
- image data as base64
- content type
- tags
- content hash
- source process
- timestamps

History files use this top-level shape:

```json
{
  "version": 1,
  "profileId": "default",
  "nextId": 10,
  "pinned-history": [],
  "regular-history": []
}
```

Item JSON should only include fields relevant to that item type. Do not write image fields on text items, and do not write empty/default fields just because the C++ struct has those members.

Examples:

```json
{
  "id": 1,
  "type": "text",
  "contentHash": 123,
  "text": "copied text",
  "timestamps": {
    "captured": 1760000000000,
    "created": 1760000000000,
    "updated": 1760000000000
  }
}
```

```json
{
  "id": 2,
  "type": "image",
  "width": 640,
  "height": 480,
  "dibBase64": "..."
}
```

Fonts directory:

```text
%APPDATA%\Clipboard++\fonts
```

Font import copies `.ttf`/`.otf` files into the fonts directory when possible. Appearance changes rebuild the ImGui font atlas and apply to both main and popup contexts.

## UI Conventions

Keep UI dense and functional. This is a utility, not a landing page.

Use existing patterns:

- ImGui controls
- small buttons/toggles
- combo boxes for selection
- child windows for panes
- theme colors from `Appearance`

Avoid:

- large decorative layout changes
- unrelated visual rewrites
- nested card-like panels
- changing focus behavior casually

Popup controls should be compact because popup width can be small.

## Known Gotchas

The popup uses `WS_EX_NOACTIVATE`, so normal keyboard focus assumptions are wrong.

Do not call `SetForegroundWindow` on the popup as a general fix. It is intentionally non-activating.

The settings window two-click bug was caused by stale ImGui input/activation state. Do not remove `ClearMainInputState()` or early `WM_MOUSEACTIVATE` handling without a careful regression test.

The popup's `@` settings button historically behaved differently because it ran inside the popup ImGui click path. Settings-open behavior is now centralized, but this area is sensitive.

The build script restarts the app after linking. If testing build-only behavior, remember it may launch a new `clipboardpp.exe`.

`GetHistory()` returns the active profile history. If code stores that pointer for long, it may become stale after profile switching. Prefer fetching it near use.

Profile histories persist under `%APPDATA%\Clipboard++\history`. They are not encrypted yet.

When adding operations that alter list order while search/filter is active, use item IDs rather than visible indices.

## Editing Guidelines

Stay inside this project unless the user explicitly asks otherwise.

Prefer small, scoped changes that match existing patterns.

Use `rg` for searches.

Use `apply_patch` for manual edits.

Do not revert unrelated user changes.

Default to ASCII in source files unless the file already requires otherwise. Some older mojibake/comment cleanup has already happened; avoid reintroducing non-ASCII separators.

Run a build after code changes when practical.

## Good Verification Checklist

After hotkey/popup/history changes, test or at least reason through:

- popup opens with `Ctrl+Shift+V`
- popup search opens/focuses with `Ctrl+Shift+S`
- visible slot paste works for filtered list positions
- hidden paste targets the foreground app
- popup remains topmost and does not steal focus
- search input does not consume keys unexpectedly after paste
- context menu actions do not paste accidentally
- queue mode still works
- move top/bottom does not duplicate items
- process-bound clipboard switching follows the focused app
- settings window opens with first click working after close/reopen

After config/theme/font changes, check:

- config loads with missing fields
- config saves new fields
- default config is valid
- main and popup both update appearance/font

## Future Work Notes

Likely next major areas:

- encrypted vault/history storage using Windows protection APIs
- persistent per-profile history contents
- stronger developer options
- richer right-click item actions
- web search from popup search bar
- multiple named/loadable clipboards with better management UI
- more file type filters
- full CLI/IPC coverage for profile/history/vault commands
