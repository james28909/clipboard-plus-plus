# Data-safety verification matrix

This document records the automated engineering evidence behind the stable-release data-safety gate. The tests protect against regressions, but they do not replace running `RELEASE_READINESS.md` against the exact packaged Debug and Release candidate on clean and upgraded Windows installations.

## Automated evidence

| Requirement | Automated evidence |
|---|---|
| Clean encrypted storage | `clipboard_database_tests` and `image_encryption_tests` create fresh databases, require DPAPI key sidecars, run integrity checks, reopen the data, and confirm secrets are absent from database/WAL bytes. |
| Supported beta settings | `clipboard_database_tests` loads compatibility fixtures for public beta.1 through beta.7 and verifies profiles, active profile, process bindings, physical hotkeys, theme/appearance, images, filters, history limits, ordering, and paste behavior. Database round trips separately cover active/pinned history, native formats, vault records, named slots, transforms, templates, and workflow actions. |
| Legacy migration | `history_encryption_tests` covers plaintext JSON and DPAPI history import, exact payload preservation, wrong-profile rejection, tamper handling, and source retention until validation. `encrypted_sqlite_vfs_tests` covers plaintext SQLite migration and recovery from an interrupted migration rollback. |
| Visible failures | Malformed configuration tests require an error while preserving the original bytes. Startup enters read-only safe mode for unreadable configuration. Damaged DPAPI keys are rejected with an error without replacing the encrypted database. Persistence failures surface through safe mode and diagnostics. |
| Encrypted backup/restore | `encrypted_sqlite_vfs_tests` creates and validates an online backup of both databases with independent key sidecars and DPAPI-protected settings, stages it without touching live state, restores both snapshots, retains encrypted rollback data, and rejects invalid backups. |
| Interruption recovery | Tests cover SQLite transaction rollback, an interrupted plaintext migration, simulated mid-restore failure with automatic rollback and successful retry, pending-restore cancellation, and destruction immediately after a queued history mutation. Atomic temporary-directory staging keeps an incomplete backup or restore from replacing a valid destination. |
| Plaintext boundaries | Tests search encrypted databases, WALs, backups, key sidecars, protected settings, and rollback files for known secrets. `SECURITY_AUDIT.md` documents unavoidable Windows clipboard and external-editor plaintext boundaries; logs/support bundles exclude payloads, and Clipboard++ does not generate crash dumps. |

## Debug verification

Build after every change:

```powershell
.\build.ps1 -Target clipboardpp -Config Debug
```

Run the complete application test set from `build\Debug\clipboardpp`:

```powershell
.\dpapi_protection_tests.exe
.\encrypted_sqlite_vfs_tests.exe
.\filter_matcher_tests.exe
.\history_encryption_tests.exe
.\image_encryption_tests.exe
.\clipboard_database_tests.exe
.\support_bundle_tests.exe
```

## Candidate work that remains manual

Do not check the stable-release acceptance boxes merely because these tests pass. The packaged candidate still needs clean-install and real upgrade-machine trials, an actual backup/restart/restore exercise, inspection of generated temporary/rollback/log files, and the full Debug/Release matrix in `RELEASE_READINESS.md`.
