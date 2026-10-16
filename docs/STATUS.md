# POS feature status — 0.3.6-dev

**The complete master specification is not implemented.** This release provides
an experimental operating-environment foundation. Source builds and host tests
are evidence of software behavior; actual ADV behavior is still unverified.

| Area | Current implementation | Remaining work |
| --- | --- | --- |
| Boot appearance | Pink → yellow → blue flashes (180 ms each), yellow slides into the middle over blue, pink follows halfway through; large POS fades across all stripes, holds 1 second, then normal boot | Observe orientation/color on actual ADV |
| Display/input | ST7789 output, TCA8418 decode, WASD/context routing, global Home, held recovery shortcut, nonblocking USB fallback | Verify all 56 positions, wiring, timing and stalled-app recovery physically |
| Core/UI/apps | Central event loop, streamed icons, nine-app ribbon launcher, A/D navigation and W to open, black icons outlined in white, one foreground app, lifecycle and cleanup, paginated menus/notices/browser with page counts and empty state | More apps, user-installable package format, sandboxing where practical |
| Background app | 1-12 horizontal stripes, visual WASD picker, Q/E hue, Shift fine steps, preview, Save & Apply and protected persistence | Optional richer styling later |
| Setup/settings | Display/device name, timezone presets, brightness, optional Wi-Fi, checksummed redundant preferences, in-place name validation, Ctrl+U and field limits | Broader preferences and timezone choices |
| SD/files | FAT32 layout, standard user/system boundary, streaming copies, basic browser/preview | A full file manager and richer file operations |
| Installer | USB factory installer, package validation, SD staging/readback, inactive runtime write/readback and boot selection | Physical flashing/SD/OTA rollback/power-cut validation |
| Repair/reinstall | Exact-package repair, backup-bound journal, previous tree retained, resume/refusal at interrupted states | Real card-removal/low-space/power-cut experiments and recovery UX refinement from results |
| Backup/restore | Standard small ZIPs, scalable readable folder backups, owned folder resume, full verification, conflict preservation and reports | Measure large real-card performance; archives/settings from unknown formats remain preserved, not guessed compatible |
| Settings migration | Strict current schema, latest valid generation, format-1 settings compatibility and legacy format-0 migration, current destination takes priority | App-specific migrations as actual app/save schemas are defined |
| Shell | ls, pwd, cat preview, streamed cp, free, reboot, shutdown | cd, mv, rm, mkdir, sysinfo and the rest of the proposed service commands |
| Diagnostics | Heap/stack, firmware/flash/SD, boot/frame metrics, battery ADC, raw keyboard coverage, SD test, app cycles, evidence tooling | Actual measured hardware baseline and battery calibration |
| Wi-Fi | Shared scan/connect/status, passphrase-encrypted NVS profiles, SNTP, verified HTTPS GET/POST/downloads, temporary authenticated LAN file transfer | Physical radio/TLS interoperability validation, broader Share integration |
| Firmware updates | ESP-IDF OTA slot writing/verification and pending-boot confirmation in installer | Online Software Update manifests/downloads, asset-only updates, signatures/minimum-version policy; actual rollback tests |
| USB developer tools | Build/package, probe, double-read factory backup, matching-device restore/readback, logs and evidence reports | Physical factory boot recovery proof; automated deployment after recovery is proven |
| Power/recovery | G0/held shortcut, warm-reset failure counter, deep-sleep request and voltage reading | Actual sleep/wake tests, calibrated battery state, dimming/power policies and persistent crash diagnostics |
| Text editor / huge documents | SD-backed snapshot/piece-table editor, cursor, streamed search/line navigation/save, previous copies, Save As and recoverable user-owned working drafts; 2 GiB minus one byte seek limit | Undo/selection and richer document navigation; real-card responsiveness measurements |
| Music/audio | Not implemented | Offline streamed playback, library/playlists, actual audio-driver validation |
| BLE/shared transfer | Not implemented | Discovery/offers/chunking/integrity/resume and common Share API |
| GPS/LoRa cap | Not implemented | Actual cap revision/pins/conflicts, GPS location service, SX1262 radio service and packet protocol |
| Range test / messaging | Not implemented | GPS/RSSI/SNR logging, acknowledgements/retries, optional opt-in location sharing |
| Maps/navigation | Not implemented | Framework map compiler, SD map packages, GPS following and routing |
| IR/IMU/other peripherals | Not integrated as POS services | Verify APIs/pins and add services as needed |
| Games/package manager/wireless deploy | Not implemented | Build after the core/recovery and update paths are trustworthy |

No LoRa range, RAM size after networking, SD throughput, battery accuracy or
factory recovery result has been invented. Development uses the first 4 MiB of
flash, assumes no PSRAM, and retains the factory installer. Keep user data and
private factory backups recoverable on ordinary computers.

Read [INSTALL.md](INSTALL.md) for installation, [RECOVERY.md](RECOVERY.md) for
backup/migration limits, and [VALIDATION.md](VALIDATION.md) for physical evidence.


## Remaining software roadmap (current priorities)

Text Editor and Wi-Fi services are implemented in this release. Games, terminal
expansion and hardware testing are excluded from this requested list.

- Full file manager: rename/move/delete, folders and richer browsing.
- POS Music: streamed playback, library controls and playlists.
- BLE file sharing and a common Share interface across transports.
- GPS location and SX1262 LoRa services, messaging, ACKs/retries and opt-in location sharing.
- Radio Range Test with GPS/RSSI/SNR/packet-loss logs.
- Offline map packages, navigation and the Framework map compiler.
- Online Software Update, signatures, asset updates and minimum-version policy.
- Installable app packages and package management.
- Power management: dimming, sleep policies and battery reporting.
- Persistent crash records, failed-boot recovery and safe mode improvements.
- Further Settings/UI polish and broader timezone/preferences support.
- Audio/microphone, IR and IMU services.
- Developer tooling and eventual wireless deployment after safe OTA.
- Future migrations as new app/save formats are introduced.
