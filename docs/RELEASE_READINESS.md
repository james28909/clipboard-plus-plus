# Release-readiness checklist

Use this checklist against the exact packaged release candidate. Record the version, commit, Windows build, tester, date, and evidence links. A successful build alone does not pass manual behavior checks.

Automated prerequisite coverage and its limits are recorded in the [data-safety verification matrix](DATA_SAFETY_VERIFICATION.md). Run that suite first, then perform the candidate-specific checks below.

## Candidate identity

- [ ] `VERSION`, executable file version, product version, About text, tag, release title, and archive names agree.
- [ ] Working tree is clean and the candidate was built from the tagged commit.
- [ ] Release notes list migrations, security/storage changes, known issues, and rollback guidance.
- [ ] SHA-256 hashes are published for every executable/package.
- [ ] Authenticode signatures and timestamps validate on every shipped executable, or the unsigned status is clearly disclosed.

## Build matrix

- [ ] Clean-build Debug and Release with `build.ps1 -Clean -Config Both`.
- [ ] Launch and smoke-test `clipboardpp.exe` and `clipboardppd.exe`.
- [ ] Launch/open representative files in `sqlite_editor.exe`, `sqlite_editord.exe`, `json_viewer.exe`, and `json_viewerd.exe`.
- [ ] Run every automated test executable from both configurations.
- [ ] Verify the static runtime on a supported Windows machine without Visual Studio installed.
- [ ] Scan packaged binaries and archives with the chosen malware/security tooling.

## Install, upgrade, and data migration

- [ ] Clean install creates working DPAPI-keyed encrypted clipboard and image databases.
- [ ] Upgrade from every supported public beta preserves settings, themes, profiles, active/pinned history, images, vault records, named slots, filters, transforms, templates, actions, and hotkeys.
- [ ] Legacy JSON, plaintext SQLite, and DPAPI-history migrations retain a recoverable original until validation succeeds.
- [ ] A deliberately unreadable database/key enters actionable safe mode instead of silently creating empty history.
- [ ] Encrypted backup and restart-time restore round-trip the complete application state.
- [ ] Interrupted migration/backup/restore can be retried or rolled back without data loss.

## Windows integration and lifecycle

- [ ] Start-with-Windows registration enables, launches, disables, and removes the expected current executable path.
- [ ] Single-instance activation and CLI-to-GUI IPC bring the existing process forward.
- [ ] Tray icon, taskbar icon, Alt+Tab icon, notifications, and custom title bars render correctly at supported DPI/scaling values.
- [ ] SQLite/JSON shell registration installs correct quoted paths, opens files, and can be fully removed.
- [ ] Uninstall removes executables, shortcuts, startup entries, shell integration, and application registration.
- [ ] Uninstall offers an explicit choice to retain or remove `%APPDATA%\Clipboard++`; no code/data is silently left elsewhere.
- [ ] Reinstall over retained data succeeds.

## Clipboard, focus, and hotkey smoke tests

- [ ] Copy/paste text, Unicode, HTML, RTF, files, folders, images, and audited native formats in representative applications.
- [ ] Test popup, hidden, pinned, profile, named-slot, template, transform, multi-item, and paste-as routes against the intended foreground target.
- [ ] Test exact left/right Ctrl, Alt, and Shift chords with number, letter, and F-key slots.
- [ ] Test double-tap overlap, release-to-single behavior, held-key repeat, pass-through, and binding conflicts.
- [ ] Verify filtered visible positions use the displayed rows and popup focus returns correctly.
- [ ] Verify incognito, pause/exclusion, deduplication, clipboard contention, rapid bursts, and shutdown do not create unrelated duplicates.

## Privacy, recovery, and support

- [ ] Review [clipboard data boundary audit](SECURITY_AUDIT.md) against the candidate.
- [ ] Confirm logs, support bundles, crash summaries, rollback files, and temporary files contain no unintended plaintext clipboard content.
- [ ] Confirm plaintext vault/state exports warn before creation and encrypted backups remain encrypted at rest.
- [ ] Exercise safe-mode retry, quarantine, fresh-storage creation, and recovery-folder access.
- [ ] Create a support ZIP, inspect every manifest entry, attach it to a test issue, and verify no upload happens without the user.

## Sign-off

- [ ] All release-blocking defects are closed or the release is stopped.
- [ ] Known non-blocking defects are documented with workarounds.
- [ ] At least one clean machine and one upgrade machine pass the checklist.
- [ ] Maintainer records final go/no-go approval and preserves the completed checklist with the release artifacts.
