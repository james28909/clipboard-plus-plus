# Clipboard data boundary audit

This audit records where clipboard data is encrypted, where plaintext is unavoidable, and what Clipboard++ deliberately excludes from diagnostics. It is a release review aid, not a claim that Windows clipboard contents can be made secret from software running as the same user.

| Boundary | Current behavior | Protection / required caution |
|---|---|---|
| Source process | Stored with its item inside the encrypted clipboard database. Shown by developer/inspector UI and included in deliberate decrypted vault exports. | Capture diagnostics no longer log process names or item previews. Support bundles redact process identifiers and never include history rows. |
| Incognito transition | Disables the listener, synchronizes the Windows clipboard sequence number, and clears self-write suppression state. Delayed screenshot pairing is discarded while incognito is active. | Incognito does not erase existing history or clear the Windows clipboard. An item whose capture completed before the toggle remains history. |
| Vault export | `files`, JSON, and binary exports deliberately decrypt selected vault records. Binary means byte-preserving, not encrypted. | The CLI warns before writing. Treat every export as sensitive plaintext. Encrypted online backup is the safe restore format. |
| Logs and diagnostics | Paste logs contain event type, byte counts, format names/sizes, window handles, and status only. Capture events contain type, tags, byte count, and format count. | Clipboard text, file paths, image IDs, named-slot values, templates, action bodies, and endpoints are excluded. `paste_debug.log` is always excluded from support ZIPs. |
| Crash information | Clipboard++ does not create memory dumps. Support bundles count `.dmp`/`.crash` files but never include them. | A dump created by Windows or an external debugger can contain process memory and clipboard data; inspect it before sharing. |
| Windows clipboard after paste | The selected/generated value is placed on the normal Windows clipboard so the destination can paste it. It remains there until another application replaces or clears it. | Clipboard++ does not race the destination by automatically restoring an older value. Use Windows clipboard clearing and incognito for sensitive workflows. |
| External editor | An external process needs a plaintext scratch file. With **Wait for external editor to exit** enabled, Clipboard++ optionally reads the result, overwrites the visible file with zeroes, and removes it after exit. Without Wait, scratch files older than 24 hours are removed on a later editor launch; immediate deletion could race a GUI launcher that handed the file to another process. | Deletion is best effort. Filesystem journals, backups, antivirus, SSD wear leveling, or a crash/power loss can retain data. The built-in editor avoids this scratch-file boundary. |
| Migration, rollback, and restore | Encrypted database migration and backup use encrypted destinations and protected key sidecars. Restore staging and rollback keep configuration DPAPI-protected and database files encrypted. Interrupted plaintext-SQLite migration restores its rollback source before retrying and removes it only after the encrypted replacement validates. | A legacy plaintext source is necessarily sensitive while migration is pending. User-created plaintext state exports remain sensitive until the user removes them. Quarantined storage is retained for recovery rather than silently deleted. |

## Review findings closed

- Removed clipboard previews and paths from capture and paste diagnostics.
- Removed exact generated-paste process names from support-eligible developer events while retaining correct inspector provenance.
- Added external-editor scratch cleanup after waited exits, on launch failure, and for stale unwaited files.
- Prevented delayed screenshot-pair insertion after incognito is enabled.
- Verified support bundles include summaries and manifests, not raw databases, keys, dumps, clipboard payloads, or secret-bearing definitions.
- DPAPI-protected restore rollback configurations; no plaintext `config.json` is retained in restore rollback folders.
