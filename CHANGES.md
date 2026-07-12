# Changes

## Beta 6 - Android Accessibility Sync

- Added **Clipboard++ Clipboard Sync**, an Android Accessibility Service that detects likely Copy/Cut actions while Gboard or another keyboard remains active.
- Routed Android sync triggers through an invisible foreground sync activity so the app can read the current primary clipboard and push missing items to Windows without showing a dialog or toast.
- Kept the Clipboard++ Capture Keyboard / IME and floating sync button as fallback/manual sync tools.
- Updated Android app setup copy and documentation to make the accessibility sync path the default workflow.

## Android Clipboard Bridge

Clipboard++ now includes an experimental Android clipboard bridge built around a companion Android app and a Windows-side sync server.

Highlights:

- Added the `clipboardpp-android-api` Android app.
- Added Android clipboard capture through the Clipboard++ Capture Keyboard / IME path.
- Added a floating Android sync flow that captures the current Android clipboard and pushes new items to Windows.
- Added a Windows HTTP sync server on port `8766`.
- Added a Windows Android device client for sending Clipboard++ text items to the Android app.
- Added a dedicated Android list in the Clipboard++ popup.
- Added Android endpoint persistence in `%APPDATA%\Clipboard++\config.json`.
- Added Settings -> Android endpoint controls for saving, testing, and clearing the Android app endpoint.
- Added popup sync/test controls and endpoint setup fallback.
- Added right-click actions to send one or many Clipboard++ profile items to Android.
- Added the configurable global `Ctrl+Alt+Shift+Z` default hotkey for sending highlighted Windows text to Android.
- Added missing-item reconciliation so captured Android items can be pushed again when Windows does not currently have them.

## Supporting Work

- Added custom filter plumbing and filter matcher tests.
- Vendored PCRE2 for regex-backed filter matching.
- Vendored Scintilla as the source base for the bundled editor/IDE work.
- Added the `clipboardpp_ide` build target.
- Added screenshot tracking, toast/editor UI files, popup UI refinements, and appearance/config persistence updates.
- Added new README screenshots for the Android sync settings and popup Android entry point.
