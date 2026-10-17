# POS feature status — 0.5.7-dev

**The complete master specification is not implemented.** This release provides
an experimental operating-environment foundation. Source builds and host tests
are evidence of software behavior; installation and normal display/keyboard operation were reported working by the
user on 0.5.6-dev. New recording/display-off changes still require physical
validation; other peripherals retain the limits below.

| Area | Current implementation | Remaining work |
| --- | --- | --- |
| Boot appearance | Pink → yellow → blue flashes (180 ms each), yellow slides into the middle over blue, pink follows halfway through; large POS fades across all stripes, holds 1 second, then normal boot | Observe orientation/color on actual ADV |
| Display/input | ST7789 output, TCA8418 decode, arrow-key/context routing, global Home, held recovery shortcut, nonblocking USB fallback | Verify all 56 positions, wiring, timing and stalled-app recovery physically |
| Core/UI/apps | Central event loop, streamed icons, fifteen-app ribbon, one foreground app, lifecycle/cleanup and paginated menus; launcher workspace is heap-owned | Additional scoped apps; physical stack/heap measurements |
| Background app | 1-12 horizontal stripes, visual arrow-key picker, Q/E hue, Shift fine steps, preview, Save & Apply and protected persistence | Optional richer styling later |
| Setup/settings | Direct profile/brightness/date-time/background controls, regional/custom POSIX timezones, manual validated UTC, SNTP, preference reset confirmation and remembered menu selection and manual LCD/backlight-off with consumed wake input; checksummed redundant settings | Physical UX/time-sync validation; preset rule changes when legislation changes |
| SD/files | Safe file manager, previews/editor/music, create/rename, copy/cut/paste, streamed verified folder copies, Trash/restore and confirmed permanent deletion | Optional sorting/search refinements; real-card performance |
| Installer | USB factory installer, package validation, SD staging/readback, inactive runtime write/readback and boot selection | Physical flashing/SD/OTA rollback/power-cut validation |
| Repair/reinstall | Exact-package repair, backup-bound journal, previous tree retained, resume/refusal at interrupted states | Real card-removal/low-space/power-cut experiments and recovery UX refinement from results |
| Backup/restore | Standard small ZIPs, scalable readable folder backups, owned folder resume, full verification, conflict preservation and reports | Measure large real-card performance; archives/settings from unknown formats remain preserved, not guessed compatible |
| Settings migration | Strict current schema, latest valid generation, format-1 settings compatibility and legacy format-0 migration, current destination takes priority | Versioned app-save envelopes, stepwise migrations and concrete Counter migrations implemented; future app-specific converters are added when formats exist |
| Diagnostics | Heap/stack, firmware/flash/SD, boot/frame metrics, battery ADC, raw keyboard coverage, SD test, app cycles, evidence tooling | Actual measured hardware baseline and battery calibration |
| Wi-Fi | Shared scan/connect/status, passphrase-encrypted NVS profiles, SNTP, verified HTTPS GET/POST/downloads, temporary authenticated LAN file transfer | Physical radio/TLS interoperability validation, broader Share integration |
| Firmware updates | Settings Software Update, built-in official feed/public key, trusted P-256 signatures, HTTPS, semantic/minimum versions, signed sequences, bounded downloads, asset-only updates, previous-System journal recovery and inactive-slot write/readback; online OTA waits for a healthy 60-second UI interval | Official feed and pinned key included; physical TLS/OTA/rollback/power-cut validation |
| Guided installer | Executable `pos-setup`: latest/private GitHub download, streamed checksum verification, card detection/FAT32 with exact erase confirmation, shared-folder route, factory backup/restore guidance, checked USB flash/readback and recovery menu | Crostini backup/restore and 0.5.6-dev installation/display/keyboard reported working. Exhaustive keys, power-loss/SD/OTA reliability remain to validate; shared folders cannot format a physical card |
| USB developer tools | Build/package, checksum/backup-checked flash/readback, probe, detected-capacity double-read factory backup with bounded ROM fallback, matching-device restore/readback, logs and evidence reports | Factory restore/readback and observed boot reported; repeat with corrected verify-before-boot ordering |
| Battery / recovery | Always-visible percentage/icon on black, smoothing/hysteresis and near-full voltage indication; RTC panic snapshot, persistent JSON crash logs, NVS failure counter and explicit Safe Mode exit; existing manual deep sleep retained | Battery percentage/full indication is an estimate; calibrate on hardware. Automatic power management is removed from scope |
| Text editor / huge documents | SD-backed editor and streamed saves/search/line navigation; 32-level session undo/redo, highlighted selection, SD clipboard, word/page/line/document navigation and recoverable drafts | Physical large-file responsiveness; session history restarts after reopening/recovery |
| Music/audio | Streamed MP3 and PCM 16-bit WAV, folders/library, Files-only Audio Player, playback controls, shuffle, M3U playlists and ES8311/I2S output | Physical audio/timing validation; other codecs optional |
| BLE/shared transfer | POS peer discovery, encrypted numeric-comparison pairing, accept/reject, chunk ACK/retry, resume, SHA-256 and lost-final-ACK recovery; common local/Wi-Fi/BLE Share entry point | Physical peer interoperability/throughput validation; third-party clients |
| GPS/LoRa cap | Verified published pins, Semtech driver, shared SPI, UART NMEA location service, Settings detection/enablement panel and GPS-based Maps gate | Actual cap/revision/peripheral coexistence validation |
| Range test / messaging | Radio messaging, IDs, bounded retries/duplicate suppression, verified SD outbox, opt-in location and range-test CSVs with packet IDs/GPS/RSSI/SNR | Physical range measurements; future coverage visualizations |
| Maps/navigation | GPS-presence gate defined; Maps app not implemented | On-device region selection, automatic Wi-Fi downloads, offline maps and navigation; no separate computer required |
| Recorder / IR / IMU | Dedicated streamed PCM WAV Recorder, interrupted WAV recovery, Files-only Audio Player; shared ES8311 microphone with a dedicated capture task and 32,000-byte/one-second mono buffer, pre-capture WAV creation, BMI270 configuration/scaled readings and 38 kHz NEC IR transmit; Settings screens | User reported an overrun in the former synchronous capture path; new buffered capture needs a physical retest. Microphone channel/gain/timing, IMU orientation/calibration and IR waveform validation; no built-in IR receiver |
| Developer tools / wireless deploy | `posctl flash` with matching verified factory backup/readback; streamed `files`; temporary LAN `deploy` requests through the signed HTTPS updater, target/build-bound evidence checks and boot confirmation polling | Recovery/OTA physical tests must pass before firmware deployment; manually reopen a temporary session after reboot for verification. No always-on deployment server |

No LoRa range, RAM size after networking, SD throughput, battery accuracy or
unreported factory recovery result has been invented. User reports are distinguished
from cloud simulation and do not establish full hardware coverage. Development uses the first 4 MiB of
flash, assumes no PSRAM, and retains the factory installer. Keep user data and
private factory backups recoverable on ordinary computers.

Read [INSTALL.md](INSTALL.md) for installation, [RECOVERY.md](RECOVERY.md) for
backup/migration limits, and [VALIDATION.md](VALIDATION.md) for physical evidence. Read [UPDATES.md](UPDATES.md)
for update use, maintainer signing setup, custom source trust and recovery.


## Remaining software roadmap (current priorities)

Recorder, Files Audio Player, IR/IMU services, developer tooling and versioned
save migrations are implemented and removed below. Hardware validation remains separate. Games, a POS terminal,
installable app/package management and automatic power management are removed from
scope; existing legacy user folders are never erased.

- Offline maps and navigation, with region selection and automatic Wi-Fi downloads directly on the device.
- Meshtastic compatibility: messaging, encrypted channels, mesh routing and voluntarily shared locations.
