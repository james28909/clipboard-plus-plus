# Clipboard++ - Feature Specification v0.1

**Executable:** `clipboardpp.exe`  
**Platform:** Windows 10+  
**Build:** MSVC + CMake, fully static (/MT), no runtime dependencies  
**UI:** Dear ImGui (docking branch) + DirectX 11  

---

## 1. Core Clipboard Monitoring

- Win32 `AddClipboardFormatListener` for change events
- Supported content types: plain text, rich text (RTF), HTML, images (bitmap/DIB), URLs, file paths
- Auto-tag each item on capture: `URL` | `Email` | `Code` | `JSON` | `XML` | `SQL` | `Image` | `ColorHex` | `FilePath` | `PhoneNumber` | `SecretPattern`
- **Deduplication:** if incoming item matches existing, move existing to configured position (top or bottom) rather than adding a new copy
- **History direction:** configurable - new items go to top or bottom of list
- **Max active history:** configurable 1–500 (default: 100)
- **Persistence:** ON by default; DPAPI-encrypted blob at `%APPDATA%\ClipboardPlusPlus\history.enc`

---

## 2. Vault (Archive)

- Items beyond the active history limit overflow to `%APPDATA%\ClipboardPlusPlus\vault.enc`
- **Vault size cap:** configurable or unlimited (default: unlimited)
- Same DPAPI encryption as active history
- Searchable from a dedicated **Vault** section in the main UI (lazy-loaded, not in memory)
- User can promote any vault item back to the active list
- Manual clear option for the vault

---

## 3. Popup Window

### Trigger
- Configurable hotkey (default: `Ctrl+Shift+V`)
- Also triggerable via tray right-click menu or CLI (`clipboardpp.exe --popup`)

### Appearance
- Borderless, always-on-top overlay
- Resizable with mouse drag on edges; default size configurable in main GUI
- Transparency: configurable 0–100% opacity (default: 95%)
- Theme-aligned (colors, fonts, accents from active theme)
- Follows mouse cursor on open - clamped to the active monitor's work area via `GetCursorPos` + `MonitorFromPoint` + `GetMonitorInfo`

### Layout (left to right, top to bottom)
```
┌-----------------------------------------┐
│  [ Search...                          ] │  ← search bar
│  [All][Txt][Img][URL][Queue][⚙]         │  ← filter/mode strip
├-----------------------------------------┤
│  1  First copied item preview...        │
│  2  Second item preview...              │
│  3  https://example.com                 │
│  ...                                    │
│  9  Item nine                           │
│  a  Item ten                            │
│  b  Item eleven                         │
│  ...                                    │
│  z  Item thirty-five                    │
└-----------------------------------------┘
```
- Items numbered **1–9** then **a–z** (35 visible; scrollable beyond 35)
- Each row: index key | content-type icon | truncated preview text
- In developer mode: small source process tag on each row

### Filter/Mode Strip Icons
| Icon | Action |
|------|--------|
| All  | Show all item types |
| Txt  | Text only |
| Img  | Images only (thumbnail or filename, configurable) |
| URL  | URLs only |
| Queue | Toggle queue mode |
| ⚙   | Open main settings window |

---

## 4. Paste Modes

### Standard Mode (default)
- Hold configured **modifier key** + press item key (1–9, a–z) -> instant paste at cursor
- Modifier key configurable (default: `Ctrl+Alt`)

### Queue Mode
- Enter by clicking Queue icon in popup strip, or via hotkey
- In popup: hold **Ctrl** and click/key items -> each gets a numbered checkmark in selection order
- Release Ctrl -> items paste sequentially with configured **inter-paste delay** (0 ms = instant, default: 50 ms)

### Immediate Multi-Select Mode
- Hotkey activates select mode within the popup
- Click items one by one -> each pastes **immediately** at the cursor's current position as soon as it is selected
- No batching; each selection triggers a paste immediately

---

## 5. Hotkeys (all configurable in main GUI)

| Action | Default |
|--------|---------|
| Toggle popup | `Ctrl+Shift+V` |
| Paste item 1 (first in list) | `Ctrl+Shift+1` |
| Multi-paste modifier (hold + 1–9/a–z) | `Ctrl+Alt` |
| Toggle incognito mode | `Ctrl+Shift+I` |
| Open main settings window | `Ctrl+Shift+,` |
| Open popup pre-filtered to images | `Ctrl+Shift+G` |

Implemented via `SetWindowsHookEx(WH_KEYBOARD_LL, ...)`.

---

## 6. Privacy & Security

### Exclusion List
- Configured by Windows process name (e.g., `KeePass.exe`, `1Password.exe`)
- Items copied while an excluded process is the foreground owner are silently skipped

### Secret Pattern Detection
- Patterns checked on every incoming item:
  - AWS access keys (`AKIA[0-9A-Z]{16}`)
  - GitHub tokens (`gh[pousr]_[A-Za-z0-9]{36}`)
  - PEM headers (`-----BEGIN ... KEY-----`)
  - JWT format (`xxxxx.yyyyy.zzzzz` with base64url segments)
  - Generic high-entropy strings (Shannon entropy > threshold, configurable)
- On detection: toast notification + choice dialog: **Store / Store Encrypted / Discard**
- Config option: **Auto-discard all detected secrets** (default: OFF)

### Incognito Mode
- Suspends all clipboard capture while active
- Tray icon changes to indicate incognito state
- Toggled via hotkey or tray menu
- Persists until manually toggled off

### Auto-Clear on Windows Lock
- Configurable (default: OFF)
- If ON: active history and vault are cleared when the workstation locks (`WM_WTSSESSION_CHANGE`)

### Storage Encryption
- All disk files (`history.enc`, `vault.enc`) encrypted with Windows DPAPI (`CryptProtectData`)
- Tied to the current Windows user account - not portable across users/machines

---

## 7. Theme System

### Format
Themes are JSON files in `%APPDATA%\ClipboardPlusPlus\themes\` (user-defined) or embedded as string literals in the binary (built-in).

```json
{
  "name": "Dracula",
  "background":     "#282a36",
  "background_alt": "#1e1f29",
  "foreground":     "#f8f8f2",
  "text_secondary": "#6272a4",
  "accent":         "#bd93f9",
  "accent_hover":   "#cfa9ff",
  "selection":      "#44475a",
  "border":         "#6272a4",
  "item_hover":     "#383a4a",
  "item_selected":  "#44475a",
  "scrollbar":      "#44475a",
  "tag_url":        "#8be9fd",
  "tag_code":       "#50fa7b",
  "tag_image":      "#ff79c6",
  "tag_secret":     "#ff5555"
}
```

### Built-in Presets (embedded in binary)
**Dark:** Dracula, Nord, Monokai, One Dark Pro, Tokyo Night, Solarized Dark, GitHub Dark  
**Light:** GitHub Light, Solarized Light, VS Light, Quiet Light

### Custom Themes
- User creates a JSON file following the format above
- Dropped into `%APPDATA%\ClipboardPlusPlus\themes\`
- Appears in theme picker immediately (hot-reload)
- In-app theme editor: live preview with color pickers
- Instructions shown in the Appearance settings panel

---

## 8. Main Settings Window

Shown on first launch. After that: hidden until opened from tray or hotkey. Rendered in ImGui with a left sidebar navigation.

| Section | Contents |
|---------|----------|
| **General** | Startup with Windows, history direction, deduplication behavior, first-launch reset |
| **Hotkeys** | All bindings, with live-capture input fields |
| **Appearance** | Theme picker (swatches + live preview), popup default size, opacity, anchor, font size |
| **History** | Active store size (1–500), vault size cap, persist toggle, session vs persistent mode |
| **Privacy** | Exclusion list editor, secret pattern detection settings, incognito config, auto-clear on lock |
| **Developer** | Dev mode toggle, CLI toggle, named slots editor, regex transforms, event log viewer |
| **About** | Version, license, links |

---

## 9. System Tray

- App lives in tray after first launch
- Left double-click -> open main settings window
- Right-click menu:

```
Open Clipboard++
Show Popup
--------------
✓ Incognito Mode
--------------
About
Exit
```

- Tray icon changes (dimmed / lock badge) when incognito mode is active

---

## 10. CLI Interface

Requires the tray app to be running (communicates via named pipe `\\.\pipe\clipboardpp`).  
Enabled by default; can be disabled in Developer settings.

```
clipboardpp.exe <command> [options]

Commands:
  get       Retrieve items from active clipboard history
  set       Modify clipboard history
  config    Read or write configuration values
  vault     Search and manage the encrypted archive
  status    Show current app state

clipboardpp.exe get --help
  --item <n>          Get item at position 1-9 or a-z
  --list              List all active history items
  --count             Return item count
  --search <query>    Search active history
  --format <fmt>      Output format: text (default), json

clipboardpp.exe set --help
  --push <text>       Push new text item to top/bottom (per config)
  --push-file <path>  Push file contents as new item
  --clear             Clear active history
  --delete <n>        Delete item at position n
  --pin <n>           Pin item at position n
  --unpin <n>         Unpin item at position n

clipboardpp.exe config --help
  --get <key>         Get a config value by key
  --set <key> <val>   Set a config value
  --list              List all config keys and current values

clipboardpp.exe vault --help
  --search <query>    Search the vault archive
  --count             Return vault item count
  --export <path>     Export vault to plaintext file

clipboardpp.exe status [--format text|json]
  Shows: running state, active item count, vault count,
         incognito state, current theme, CLI enabled
```

Named pipe protocol: newline-delimited JSON request/response.  
If the tray app is not running, CLI exits with: `Clipboard++ is not running.`

---

## 11. Developer Mode

Toggled in Settings -> Developer. Off by default.

| Feature | Description |
|---------|-------------|
| **Format Inspector** | Shows all Win32 clipboard formats available for a selected item (CF_TEXT, CF_UNICODETEXT, CF_HTML, custom, etc.) |
| **Hex Viewer** | Raw bytes view for any clipboard item |
| **Source Process Tracking** | Each item tagged with the process name that copied it; filter by source |
| **Named Persistent Slots** | Named non-sensitive text slots (e.g. `$MY_EMAIL`); permanent, not affected by history limits or auto-clear |
| **Regex Transform Engine** | Named transforms (pattern -> replacement) applied to items before paste; accessible via right-click in popup |
| **Template Paste** | Templates with `{{slot:name}}` or `{{1}}` interpolation pasted as a single composed string |
| **Diff View** | Side-by-side character/word diff between any two selected items |
| **Auto Pretty-Print** | Detect JSON/XML/SQL and offer formatted paste |
| **Event Log** | Timestamped log of all clipboard events (time, source process, type, preview); exportable to CSV/JSON |
| **Named Pipe Docs** | In-app documentation of the IPC protocol for third-party integration |

---

## 12. Storage Paths

| File | Path |
|------|------|
| Config | `%APPDATA%\ClipboardPlusPlus\config.json` |
| Active history | `%APPDATA%\ClipboardPlusPlus\history.enc` |
| Vault archive | `%APPDATA%\ClipboardPlusPlus\vault.enc` |
| Custom themes | `%APPDATA%\ClipboardPlusPlus\themes\` |
| Named pipe | `\\.\pipe\clipboardpp` |

---

## 13. Build

- **Toolchain:** MSVC via VS2022 + CMake 3.20+
- **Standard:** C++17
- **Runtime:** `/MT` (static) - no MSVC redistributable needed
- **Release flags:** `/O2 /GL` + `/LTCG`
- **Dependencies:** Dear ImGui docking (vendored `third_party/imgui`), nlohmann/json (vendored `third_party/nlohmann/json.hpp`)
- **Minimum OS:** Windows 10 (1903+)
- **Target size:** < 5 MB release binary

---

## 14. Milestone Plan

| # | Deliverable | Key pieces |
|---|-------------|------------|
| 1 | **Skeleton** | CMake, Win32 window, DX11, ImGui renders, tray icon, single-instance check |
| 2 | **Clipboard Core** | Monitor, ClipboardItem, ClipboardHistory, ContentDetector |
| 3 | **Popup** | Borderless overlay, mouse-follow, numbered list, search bar, basic paste |
| 4 | **Hotkeys** | WH_KEYBOARD_LL, configurable bindings, standard paste mode |
| 5 | **Persistence** | DPAPI encrypt/decrypt, config.json, save/load history |
| 6 | **Named Pipe + CLI** | PipeServer/PipeClient, full CLI command set |
| 7 | **Main UI** | All settings panels wired up |
| 8 | **Themes** | ThemeManager, all built-in presets, custom theme editor |
| 9 | **Privacy** | Exclusion list, secret detection, incognito, auto-clear |
| 10 | **Developer Mode** | All dev features |
