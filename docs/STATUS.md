# POS feature status — 0.4.0-dev

**The complete master specification is not implemented.** This release provides
an experimental operating-environment foundation. Source builds and host tests
are evidence of software behavior; actual ADV behavior is still unverified.

| Area | Current implementation | Remaining work |
| --- | --- | --- |
| Boot appearance | Pink → yellow → blue flashes (180 ms each), yellow slides into the middle over blue, pink follows halfway through; large POS fades across all stripes, holds 1 second, then normal boot | Observe orientation/color on actual ADV |
| Display/input | ST7789 output, TCA8418 decode, WASD/context routing, global Home, held recovery shortcut, nonblocking USB fallback | Verify all 56 positions, wiring, timing and stalled-app recovery physically |
| Core/UI/apps | Central event loop, streamed icons, fourteen-app ribbon, one foreground app, lifecycle/cleanup and paginated menus; launcher workspace is heap-owned | Additional scoped apps; physical stack/heap measurements |
| Background app | 1-12 horizontal stripes, visual WASD picker, Q/E hue, Shift fine steps, preview, Save & Apply and protected persistence | Optional richer styling later |
| Setup/settings | Direct profile/brightness/date-time/background controls, regional/custom POSIX timezones, manual validated UTC, SNTP, preference reset confirmation and remembered menu selection; checksummed redundant settings | Physical UX/time-sync validation; preset rule changes when legislation changes |
| SD/files | Safe file manager, previews/editor/music, create/rename, copy/cut/paste, streamed verified folder copies, Trash/restore and confirmed permanent deletion | Optional sorting/search refinements; real-card performance |
| Installer | USB factory installer, package validation, SD staging/readback, inactive runtime write/readback and boot selection | Physical flashing/SD/OTA rollback/power-cut validation |
| Repair/reinstall | Exact-package repair, backup-bound journal, previous tree retained, resume/refusal at interrupted states | Real card-removal/low-space/power-cut experiments and recovery UX refinement from results |
| Backup/restore | Standard small ZIPs, scalable readable folder backups, owned folder resume, full verification, conflict preservation and reports | Measure large real-card performance; archives/settings from unknown formats remain preserved, not guessed compatible |
| Settings migration | Strict current schema, latest valid generation, format-1 settings compatibility and legacy format-0 migration, current destination takes priority | App-specific migrations as actual app/save schemas are defined |
| Diagnostics | Heap/stack, firmware/flash/SD, boot/frame metrics, battery ADC, raw keyboard coverage, SD test, app cycles, evidence tooling | Actual measured hardware baseline and battery calibration |
| Wi-Fi | Shared scan/connect/status, passphrase-encrypted NVS profiles, SNTP, verified HTTPS GET/POST/downloads, temporary authenticated LAN file transfer | Physical radio/TLS interoperability validation, broader Share integration |
| Firmware updates | Settings Software Update, trusted P-256 signatures, HTTPS, semantic/minimum versions, signed sequences, bounded downloads, asset-only updates, previous-System journal recovery and inactive-slot write/readback; online OTA waits for a healthy 60-second UI interval | Configure a publisher feed/key; physical TLS/OTA/rollback/power-cut validation |
| USB developer tools | Build/package, probe, double-read factory backup, matching-device restore/readback, logs and evidence reports | Physical factory boot recovery proof; automated deployment after recovery is proven |
| Battery / recovery | Always-visible percentage/icon on black, smoothing/hysteresis and near-full voltage indication; RTC panic snapshot, persistent JSON crash logs, NVS failure counter and explicit Safe Mode exit; existing manual deep sleep retained | Battery percentage/full indication is an estimate; calibrate on hardware. Automatic power management is removed from scope |
| Text editor / huge documents | SD-backed editor and streamed saves/search/line navigation; 32-level session undo/redo, highlighted selection, SD clipboard, word/page/line/document navigation and recoverable drafts | Physical large-file responsiveness; session history restarts after reopening/recovery |
| Music/audio | Streamed MP3 and PCM 16-bit WAV, folders/library, playback controls, shuffle, M3U playlists and ES8311/I2S output | Physical audio/timing validation; other codecs optional |
| BLE/shared transfer | POS peer discovery, encrypted numeric-comparison pairing, accept/reject, chunk ACK/retry, resume, SHA-256 and lost-final-ACK recovery; common local/Wi-Fi/BLE Share entry point | Physical peer interoperability/throughput validation; third-party clients |
| GPS/LoRa cap | Verified published pins, Semtech driver, shared SPI, UART NMEA location service, Settings detection/enablement panel and GPS-based Maps gate | Actual cap/revision/peripheral coexistence validation |
| Range test / messaging | Radio messaging, IDs, bounded retries/duplicate suppression, verified SD outbox, opt-in location and range-test CSVs with packet IDs/GPS/RSSI/SNR | Physical range measurements; future coverage visualizations |
| Maps/navigation | GPS-presence gate defined; Maps app not implemented | On-device region selection, automatic Wi-Fi downloads, offline maps and navigation; no separate computer required |
| Recorder / IR / IMU | Speaker playback implemented; microphone recording and IR/IMU services not implemented | Dedicated Recorder app saving standard audio under Recordings, accessible in Files; IR/IMU services |
| Developer tools / wireless deploy | USB tools implemented; installable apps/package management removed from scope | Improved posctl and wireless deployment after OTA/recovery are validated |

No LoRa range, RAM size after networking, SD throughput, battery accuracy or
factory recovery result has been invented. Development uses the first 4 MiB of
flash, assumes no PSRAM, and retains the factory installer. Keep user data and
private factory backups recoverable on ordinary computers.

Read [INSTALL.md](INSTALL.md) for installation, [RECOVERY.md](RECOVERY.md) for
backup/migration limits, and [VALIDATION.md](VALIDATION.md) for physical evidence. Read [UPDATES.md](UPDATES.md)
for initial HTTPS source/public-key trust setup and update recovery.


## Remaining software roadmap (current priorities)

The five selected update/battery/recovery/editor/Settings milestones are implemented
and removed below. Hardware validation remains separate. Games, a POS terminal,
installable app/package management and automatic power management are removed from
scope; existing legacy user folders are never erased.

- Offline maps and navigation, with region selection and automatic Wi-Fi downloads directly on the device.
- Meshtastic compatibility: messaging, encrypted channels, mesh routing and voluntarily shared locations.
- A dedicated Recorder app saving standard audio files to a Recordings folder accessible through Files.
- IR and IMU services.
- Improved posctl tooling and wireless deployment after OTA is reliable.
- Data migrations for future app and save formats.
