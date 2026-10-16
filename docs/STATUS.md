# POS feature status — 0.3.8-dev

**The complete master specification is not implemented.** This release provides
an experimental operating-environment foundation. Source builds and host tests
are evidence of software behavior; actual ADV behavior is still unverified.

| Area | Current implementation | Remaining work |
| --- | --- | --- |
| Boot appearance | Pink → yellow → blue flashes (180 ms each), yellow slides into the middle over blue, pink follows halfway through; large POS fades across all stripes, holds 1 second, then normal boot | Observe orientation/color on actual ADV |
| Display/input | ST7789 output, TCA8418 decode, WASD/context routing, global Home, held recovery shortcut, nonblocking USB fallback | Verify all 56 positions, wiring, timing and stalled-app recovery physically |
| Core/UI/apps | Central event loop, streamed icons, fourteen-app ribbon launcher, A/D navigation and W to open, black icons outlined in white, one foreground app, lifecycle and cleanup, paginated menus/notices/browser with page counts and empty state | More apps, user-installable package format, sandboxing where practical |
| Background app | 1-12 horizontal stripes, visual WASD picker, Q/E hue, Shift fine steps, preview, Save & Apply and protected persistence | Optional richer styling later |
| Setup/settings | Display/device name, timezone presets, brightness, optional Wi-Fi, checksummed redundant preferences, in-place name validation, Ctrl+U and field limits | Broader preferences and timezone choices |
| SD/files | Safe file manager, previews/editor/music, create/rename, copy/cut/paste, streamed verified folder copies, Trash/restore and confirmed permanent deletion | Optional sorting/search refinements; real-card performance |
| Installer | USB factory installer, package validation, SD staging/readback, inactive runtime write/readback and boot selection | Physical flashing/SD/OTA rollback/power-cut validation |
| Repair/reinstall | Exact-package repair, backup-bound journal, previous tree retained, resume/refusal at interrupted states | Real card-removal/low-space/power-cut experiments and recovery UX refinement from results |
| Backup/restore | Standard small ZIPs, scalable readable folder backups, owned folder resume, full verification, conflict preservation and reports | Measure large real-card performance; archives/settings from unknown formats remain preserved, not guessed compatible |
| Settings migration | Strict current schema, latest valid generation, format-1 settings compatibility and legacy format-0 migration, current destination takes priority | App-specific migrations as actual app/save schemas are defined |
| Diagnostics | Heap/stack, firmware/flash/SD, boot/frame metrics, battery ADC, raw keyboard coverage, SD test, app cycles, evidence tooling | Actual measured hardware baseline and battery calibration |
| Wi-Fi | Shared scan/connect/status, passphrase-encrypted NVS profiles, SNTP, verified HTTPS GET/POST/downloads, temporary authenticated LAN file transfer | Physical radio/TLS interoperability validation, broader Share integration |
| Firmware updates | ESP-IDF OTA slot writing/verification and pending-boot confirmation in installer | Online Software Update manifests/downloads, asset-only updates, signatures/minimum-version policy; actual rollback tests |
| USB developer tools | Build/package, probe, double-read factory backup, matching-device restore/readback, logs and evidence reports | Physical factory boot recovery proof; automated deployment after recovery is proven |
| Power/recovery | G0/held shortcut, warm-reset failure counter, deep-sleep request and voltage reading | Actual sleep/wake tests, calibrated battery state, dimming/power policies and persistent crash diagnostics |
| Text editor / huge documents | SD-backed snapshot/piece-table editor, cursor, streamed search/line navigation/save, previous copies, Save As and recoverable user-owned working drafts; 2 GiB minus one byte seek limit | Undo/selection and richer document navigation; real-card responsiveness measurements |
| Music/audio | Streamed MP3 and PCM 16-bit WAV, folders/library, playback controls, shuffle, M3U playlists and ES8311/I2S output | Physical audio/timing validation; other codecs optional |
| BLE/shared transfer | POS peer discovery, encrypted numeric-comparison pairing, accept/reject, chunk ACK/retry, resume, SHA-256 and lost-final-ACK recovery; common local/Wi-Fi/BLE Share entry point | Physical peer interoperability/throughput validation; third-party clients |
| GPS/LoRa cap | Verified published pins, Semtech driver, shared SPI, UART NMEA location service, Settings detection/enablement panel and GPS-based Maps gate | Actual cap/revision/peripheral coexistence validation |
| Range test / messaging | Radio messaging, IDs, bounded retries/duplicate suppression, verified SD outbox, opt-in location and range-test CSVs with packet IDs/GPS/RSSI/SNR | Physical range measurements; future coverage visualizations |
| Maps/navigation | GPS-presence gate defined; Maps app not implemented | Framework map compiler, SD map packages, GPS following and routing |
| IR/IMU/other peripherals | Not integrated as POS services | Verify APIs/pins and add services as needed |
| Package manager/wireless deploy | Not implemented | Build after the core/recovery and update paths are trustworthy |

No LoRa range, RAM size after networking, SD throughput, battery accuracy or
factory recovery result has been invented. Development uses the first 4 MiB of
flash, assumes no PSRAM, and retains the factory installer. Keep user data and
private factory backups recoverable on ordinary computers.

Read [INSTALL.md](INSTALL.md) for installation, [RECOVERY.md](RECOVERY.md) for
backup/migration limits, and [VALIDATION.md](VALIDATION.md) for physical evidence.


## Remaining software roadmap (current priorities)

Files, Music, BLE sharing, GPS/LoRa and Range Test join the existing editor and
Wi-Fi services. Games and a POS terminal are removed from project scope. Hardware
validation is tracked separately from this software roadmap.

- Offline map packages, navigation and the Framework map compiler.
- Online Software Update, signatures, asset updates and minimum-version policy.
- Installable app packages and package management.
- Power management: dimming, sleep policies and battery reporting.
- Persistent crash records, failed-boot recovery and safe mode improvements.
- Further Settings/UI polish and broader timezone/preferences support.
- Microphone capture, IR and IMU services (speaker playback is implemented).
- Developer tooling and eventual wireless deployment after safe OTA.
- Future migrations as new app/save formats are introduced.
