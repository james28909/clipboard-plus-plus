# Clipboard++ TODO

## Quick wins (UI stubs that need wiring)
- [ ] Incognito mode — toggle exists in tray popup and Ctrl+Shift+I hotkey but does NOT suspend capture
- [x] Start with Windows — checkbox on General tab wired to HKCU\Software\Microsoft\Windows\CurrentVersion\Run

## Security / storage
- [x] DPAPI-encrypted history — per-profile history uses current-user DPAPI protection with safe plaintext migration
- [x] Image store — SQLite DB at %APPDATA%\Clipboard++\images.db; image BLOBs use current-user DPAPI protection with transactional plaintext migration; query metadata remains plaintext; profile history stores UUID reference only
- [ ] SQLite VFS encryption — implement a custom SQLite VFS layer that encrypts/decrypts each DB page on-the-fly using AES-256; protect the AES key with DPAPI (CryptProtectData) stored alongside the DB; transparent to all callers once wired in
- [ ] Vault / archive — items that overflow active history limit currently vanish; spec defines a searchable overflow archive with promote-back support

## CLI completeness
- [ ] clipboardpp vault commands (search, count, export) — not implemented
- [ ] get --list, get --search, get --item <n> — not implemented
- [ ] set --delete <n>, set --pin <n>, set --unpin <n>, set --clear — not implemented

## Developer mode (advanced features — all stubs)
- [ ] Format inspector — show all Win32 clipboard formats for a selected item
- [ ] Hex viewer — raw bytes view for any item
- [ ] Named persistent slots — named non-sensitive text snippets unaffected by history limits
- [ ] Regex transform engine — named pattern/replacement transforms applied before paste
- [ ] Template paste — {{slot:name}} / {{1}} interpolation
- [ ] Diff view — side-by-side diff between two selected items
- [ ] Auto pretty-print — detect JSON/XML/SQL and offer formatted paste

## Code consolidation / restructuring
- [x] Split Android popup panel out of `PopupWindow.cpp` into `PopupWindowAndroid.cpp`
- [x] Split image browser and thumbnail cache out of `PopupWindow.cpp` into `PopupWindowImages.cpp`
- [x] Add bulk history operations so multi-select popup actions mutate/save once
- [x] Cache visible popup history rows by history version, filter, and search text
- [x] Extract paste/hotkey diagnostics into shared `PasteDiagnostics` helper
- [x] Extract popup item selection into a focused selection model
- [x] Remove separate Queue mode; use multi-selection order as paste order
- [x] Extract popup history list rendering/context menu into `PopupWindowHistory.cpp`
- [x] Extract paste/clipboard/focus logic into a paste controller or `PopupWindowPaste.cpp`
- [x] Split settings tabs out of `MainWindow.cpp` into focused tab files
- [x] Extract Android Windows-side integration out of `Application.cpp`
- [x] Extract clipboard profile create/delete/switch logic out of `Application.cpp`
