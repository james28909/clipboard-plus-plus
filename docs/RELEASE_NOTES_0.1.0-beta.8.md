## Clipboard++ 0.1.0-beta.8

This prerelease is a large reliability, performance, storage-safety, and workflow update. It remains a beta while the packaged candidate completes the stable 1.0 acceptance checklist.

### Startup and responsiveness

- Show the Win32/D3D shell and tray before storage-heavy initialization, then load profile metadata, active history, images, capture, Android, and maintenance through deferred startup stages.
- Open encrypted profile metadata and deserialize active history on background futures; inactive profiles load only when selected or routed.
- Persist history through a coalescing background worker and drain pending changes during rebuild and shutdown.
- Cache named slots, transforms, templates, custom actions, and vault counts to avoid repeated encrypted database queries during rendering.
- Add startup timing, memory/latency telemetry, database metrics, and a repeatable warm/cold benchmark tool.

### Popup and Settings polish

- Reorganize Settings into flat feature-owned tabs and compact, uniform cards/tables while keeping the established top-level names.
- Move templates, PCRE2 transforms, and structured paste tools out of Developer and into Clipboard - Paste Tools.
- Redesign the popup command bar into compact Filters, Actions, and Destinations groups with soft wrapping, configurable line colors/rounding, optional counters, and improved multi-selection actions.
- Fix first-launch popup sizing, settings foreground activation, tab/scroll ownership, and DPI scaling for popup controls and the native drag grip.
- Add an Open popup control and direct executable launcher settings.

### Configurable workflow actions

- Add encrypted, composable popup workflow buttons with ordered inputs, transforms, conditions, placement, exact hotkeys, confirmations, timeouts, and output routing.
- Support paste, copy, URL, Android, file, history move/tag/pin, and direct executable outputs without invoking a command shell.
- Add safe previews, validation, import/export warnings, file pickers, and protected handling of executable paths, arguments, templates, and action bodies.
- Fix workflow-action deletion with confirmation, database contention handling, and actionable error reporting.

### Paste correctness and hotkeys

- Use stable clipboard item IDs throughout popup mutations, filtering, double-click paste, multi-selection, and slot resolution.
- Centralize Clipboard++-generated text writes so templates, transforms, and formatting create one history item with correct `clipboardpp.exe` source provenance and separate destination diagnostics.
- Preserve ordered `{{1}}`, `{{2}}`, and later template interpolation for multiple selections.
- Expand suppression coverage for completed, coalesced, delayed, and unrelated clipboard notifications.
- Fix the pass-through hotkey capture abort for physical chords such as Left Alt+Tab and add a regression test.

### Encrypted backup, migration, and recovery

- Add online encrypted backup and verified restart-time restore for `clipboard.db`, `images.db`, their independent DPAPI-protected keys, and DPAPI-protected settings.
- Add versioned encrypted state-package transfer for profiles, named slots, transforms, templates, workflow actions, and settings with explicit conflict policies.
- Add safe-mode startup, storage retry/quarantine/fresh-storage recovery, and visible background persistence failures; malformed configuration is retained read-only instead of being silently replaced.
- Recover and retry interrupted plaintext SQLite migrations from their rollback source.
- Roll back simulated mid-restore interruption safely and keep the staged restore retryable.
- Store restore rollback settings as `config.json.dpapi` rather than plaintext.
- Add transaction rollback and immediate-shutdown flush coverage so interrupted or queued saves retain a recoverable encrypted database.

### Privacy, diagnostics, and release preparation

- Add a Release-visible Support & diagnostics tab that creates atomic, reviewable ZIP bundles and guided GitHub issue text without uploading automatically.
- Exclude clipboard/vault contents, images, database pages, keys, DPAPI blobs, raw config, action values, paths, endpoints, credentials, dumps, and paste-debug payloads from support bundles.
- Remove clipboard previews and sensitive paths from eligible logs, correct generated-paste provenance, suppress delayed screenshot pairing during incognito, and improve external-editor scratch cleanup.
- Add the clipboard data-boundary audit, data-safety verification matrix, Settings ownership guide, startup performance report, and full stable release-readiness checklist.
- Add GitHub issue forms for bugs, features, and security triage.

### Validation

- Debug builds pass all seven automated executables: DPAPI protection, encrypted SQLite VFS, filter matcher, history encryption, image encryption, clipboard database, and support bundle tests.
- Coverage now includes public beta.1-beta.7 configuration compatibility, legacy JSON/SQLite/DPAPI migration, encrypted backup/restore, corrupted keys, interrupted migration/restore/save, orderly shutdown flushing, exact physical hotkeys, generated-paste deduplication, stable filtered positions, large histories/images/vaults, and support-bundle redaction.
