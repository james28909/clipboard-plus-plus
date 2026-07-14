# Clipboard++ startup performance

This document defines the startup acceptance targets and records evidence from the built-in timestamped profiler. `%APPDATA%\Clipboard++\startup_profile.log` is replaced after each successful first frame, and the same data is available under **Developer > Diagnostics > Startup profile** in Debug builds.

## Acceptance targets

- Tray icon and main application shell visible within **500 ms** on the reference SSD.
- Popup usable with the active profile's history within **1,000 ms**.
- Profile names and active-profile metadata available before inactive histories finish loading.
- Inactive histories, image browsing, Android integration, Developer tooling, cleanup, pruning, and integrity work must not block the first usable frame.
- Startup changes must be compared using at least five cold launches and five warm launches. Record the median and slowest run, configuration, database sizes, and profile/item/image/vault counts.
- A performance change must not weaken migration safety, encryption, clipboard capture, profile switching, or clean shutdown.

## Initial diagnostic baseline - 2026-07-14

Reference system:

- Windows 11 Pro, build 26200
- Intel Core i5-8400
- 31.9 GB RAM
- WD Blue SN580 1 TB SSD
- Debug build, five clipboard profiles

This is an initial warm diagnostic sample, not the final repeatable benchmark:

| Measurement | Before duplicate-rebuild removal | After removal |
|---|---:|---:|
| Total to first rendered frame | 10,523.6 ms | 4,785.2 ms |
| First profile-manager rebuild | 5,646.7 ms | 4,420.7 ms |
| Duplicate second rebuild | 4,438.4 ms | Removed |
| Remaining UI/services after histories | ~120 ms | ~332 ms, including D3D and first render |

The duplicate rebuild removal reduced this sample's time to first frame by approximately **54.5%**. The remaining dominant work is:

| Stage | Duration |
|---|---:|
| Encrypted clipboard DB, VFS/DPAPI, schema, and profile metadata | 1,926.8 ms |
| Deserializing five profile histories | 2,493.9 ms |
| Direct3D device and swap chain | 197.2 ms |
| First frame render | 35.8 ms |

## Deferred-startup result - 2026-07-14

The startup path now presents the Win32/Direct3D shell and tray first, then advances a main-thread deferred queue one phase per rendered frame. Profile metadata is opened before the active history; inactive histories remain unloaded until selected or targeted by a route. Images and clipboard capture start after the active history, Android starts afterward, vault pruning is last, and the Debug window, tray popup, and built-in editor are created on demand.

A representative warm Debug run on the reference system produced:

| Measurement | Result |
|---|---:|
| Total to first rendered frame | 412.0 ms |
| Profile metadata ready | 1,189.3 ms |
| Active history ready | 2,797.4 ms |
| Active history JSON decode | 1,603.2 ms |
| Active history JSON encode benchmark | 424.0 ms |
| Active history | 500 items / 7,759,360 JSON bytes |
| Clipboard database | 15,667,200 bytes |
| Image database | 5,697,536 bytes / 16 images |

This sample improves first-frame time by approximately **96.1%** from the original 10,523.6 ms baseline and **91.4%** from the duplicate-rebuild-removal sample. The shell target is met in this run. The one-second active-history target is not met by this large Debug-build JSON payload; normalization or chunked/lazy item storage is therefore the recommended future storage optimization. The current change deliberately keeps the proven on-disk format and encryption behavior intact.

## Benchmark procedure

Run `tools/benchmark-startup.ps1 -Mode Warm -Runs 5` for a repeatable warm set. Use `-Mode Cold -Runs 5` only after arranging a genuine cold-cache condition before each prompted run (an approved standby-list tool or a reboot). The script refuses to run while Clipboard++ is already open, waits for the deferred-startup report to finish, stops only the process it launched, and writes `startup-benchmark.csv` with:

- first-frame and active-history-ready times;
- serialization and deserialization times;
- profile, active-item, image, and vault counts;
- clipboard/image database sizes and active-history JSON size.

Stable-release evidence should include the median and slowest value from five cold and five warm **Release** runs. Debug builds remain useful for finding stage regressions but are not the public-release acceptance build.

## Initialization safety audit

- Deferred startup is an explicit main-thread state machine. SQLite connections and ImGui/D3D objects never cross worker threads.
- No startup worker survives shutdown, so cancellation and connection handoff are not required.
- The clipboard listener starts only after both the active history and image store are ready; captures are not accepted into a half-initialized store.
- Existing encrypted databases no longer reparse every profile merely to verify it on every launch. Full integrity verification remains part of the one-time legacy migration transaction, where legacy data is retained until verification succeeds.
- Required schema/key migrations run only after the first frame. Image protection migration runs when the deferred image store opens. Vault pruning runs in the final deferred phase.
- There is currently no automatic orphan-image cleanup job. If one is introduced, it must use its own SQLite connection or remain on this main-thread maintenance phase, and it must honor shutdown and protected-image references.
- Named slots, transforms, templates, and vault counts use in-memory caches. Successful mutations and profile/vault changes explicitly invalidate the relevant cache.
