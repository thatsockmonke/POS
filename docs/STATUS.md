# POS development status — 0.8.0-dev

**The complete master specification is not implemented.** This release provides
an experimental operating-environment foundation. Source builds and host tests
are evidence of software behavior; installation and normal display/keyboard operation were reported working by the
user on 0.5.6-dev. The user confirms microphone input works in 0.5.8-dev. New saved gain, animated
layouts and display-off still require physical validation; other peripherals retain the limits below.

| Area | Current implementation | Remaining work |
| --- | --- | --- |
| Boot appearance | Black start; pink, yellow and blue slide in with each starting halfway through the previous stripe; large POS fades in after the flag completes, holds 1 second, then normal boot | Observe orientation/color on actual ADV |
| Display/input | ST7789 output, TCA8418 decode, arrow-key/context routing, global Home, held recovery shortcut, nonblocking USB fallback | Verify all 56 positions, wiring, timing and stalled-app recovery physically |
| Core/UI/apps | Central event loop, native 32×32 icons, seventeen-app ribbon, one foreground app, lifecycle/cleanup, stationary focus with an elapsed-time spring ribbon, optimized scanlines and partial/pipelined LCD transfers, zoom/fade entry/return, shared wallpaper accents and paginated menus; launcher workspace is heap-owned | Additional scoped apps; physical stack/heap measurements |
| Background app | 1-12 horizontal stripes, visual arrow-key picker, Q/E hue, Shift fine steps, live striped canvas, white selection pulse, 12 common stripe-only pride presets (P), typed hex colours (H), S to save and protected persistence | Optional richer styling later |
| Setup/settings | Direct profile/brightness/date-time/background controls, regional/custom POSIX timezones, manual validated UTC, SNTP, preference reset confirmation and remembered menu selection and manual LCD/backlight-off with consumed wake input; checksummed redundant settings | Physical UX/time-sync validation; preset rule changes when legislation changes |
| SD/files | Safe file manager, previews/editor/music, restored text list/actions, create/rename, copy/cut/paste, streamed verified folder copies, Trash/restore and confirmed permanent deletion | Optional sorting/search refinements; real-card performance |
| Installer | USB factory installer, package validation, SD staging/readback, inactive runtime write/readback and boot selection | Physical flashing/SD/OTA rollback/power-cut validation |
| Repair/reinstall | Exact-package repair, backup-bound journal, previous tree retained, resume/refusal at interrupted states | Real card-removal/low-space/power-cut experiments and recovery UX refinement from results |
| Backup/restore | Standard small ZIPs, scalable readable folder backups, owned folder resume, full verification, conflict preservation and reports | Measure large real-card performance; archives/settings from unknown formats remain preserved, not guessed compatible |
| Settings migration | Strict current schema, latest valid generation, format-1/2 settings compatibility, format-3 gain preferences and format-4 sound preferences and legacy format-0 migration, current destination takes priority | Versioned app-save envelopes, stepwise migrations and concrete Counter migrations implemented; future app-specific converters are added when formats exist |
| Diagnostics | Heap/stack, firmware/flash/SD, boot/frame metrics, battery ADC, raw keyboard coverage, SD test, app cycles, evidence tooling | Actual measured hardware baseline and battery calibration |
| Wi-Fi | Shared scan/connect/status, device-key encrypted Known Networks, optional background auto-connect, SNTP, verified HTTPS GET/POST/downloads, temporary authenticated LAN file transfer | Physical radio/TLS interoperability validation, broader Share integration |
| Firmware updates | Settings Software Update, built-in official feed/public key, trusted P-256 signatures, HTTPS, semantic versions, signed sequences, no minimum-version cutoff/display, strict seven-field metadata after the mandatory 0.6.0 transition, archived bridge feed, up to 15 signed firmware/System history pairs on SD and deliberate journaled rollback, bounded downloads, asset-only updates, previous-System journal recovery and inactive-slot write/readback; online OTA waits for a healthy 60-second UI interval | Official feed and pinned key included; physical TLS/OTA/rollback/power-cut validation |
| Guided installer | Executable `pos-setup`: three-choice Prepare/Flash/Restore menu, concise USB progress, helper-action resume without duplicate menus, latest/private GitHub download, streamed checksum verification, card detection/FAT32 with exact erase confirmation, shared-folder route, factory backup/restore guidance, checked USB flash/readback and recovery menu | Crostini backup/restore and 0.5.6-dev installation/display/keyboard reported working. Exhaustive keys, power-loss/SD/OTA reliability remain to validate; shared folders cannot format a physical card |
| USB developer tools | Build/package, checksum/backup-checked flash/readback, probe, detected-capacity double-read factory backup with bounded ROM fallback, matching-device restore/readback, logs and evidence reports | Factory restore/readback and observed boot reported; repeat with corrected verify-before-boot ordering |
| Battery / recovery | Always-visible percentage/icon on black, smoothing/hysteresis and near-full voltage indication; RTC panic snapshot, persistent JSON crash logs, NVS failure counter and explicit Safe Mode exit; existing manual deep sleep retained | Battery percentage/full indication is an estimate; calibrate on hardware. Automatic power management is removed from scope |
| Calculator | Scientific keypad/functions, saved variables/Ans, bounded polynomial CAS and numeric real roots through degree 8; exact fractions, graphs, 2–4-variable linear systems, 4×4 matrices/vectors, units, programmer mode, persistent history and Files-openable worksheets | Checked 64-bit exact mode; double precision elsewhere; no unrestricted symbolic algebra/arbitrary precision or higher-degree complex roots; physical UX/heap/stack checks remain |
| Text editor / huge documents | SD-backed editor, bounded nearby-read cache, queued-typing batches, change-only rendering, confirmed Trash deletion and streamed saves/search/line navigation; 32-level session undo/redo, highlighted selection, SD clipboard, word/page/line/document navigation and recoverable drafts | Physical large-file responsiveness; session history restarts after reopening/recovery |
| Music/audio | Streamed MP3 and PCM 16-bit WAV, folders/library, Files-only Audio Player, playback controls, shuffle, M3U playlists and ES8311/I2S output | Physical audio/timing validation; other codecs optional |
| BLE/shared transfer | POS peer discovery, encrypted numeric-comparison pairing, accept/reject, chunk ACK/retry, resume, SHA-256 and lost-final-ACK recovery; common local/Wi-Fi/BLE Share entry point | Physical peer interoperability/throughput validation; Linux Bluetooth/Wi-Fi Send/Receive tool implemented; physical BlueZ/adapter and Cardputer pairing still needs verification |
| GPS/LoRa cap | Verified published pins, Semtech driver, shared SPI, UART NMEA location service, Settings detection/enablement panel and GPS-based Maps gate | Actual cap/revision/peripheral coexistence validation |
| Range test / messaging | Radio messaging, IDs, bounded retries/duplicate suppression, verified SD outbox, opt-in location and range-test CSVs with packet IDs/GPS/RSSI/SNR | Physical range measurements; future coverage visualizations |
| Maps/navigation | Arrow pan, +/- zoom, shared Fine/Normal/Large pan steps (P), GPS recenter/follow (C), GPS-gated on-device region downloads, direct OSM (up to 4 km) plus selected Overpass services, white saved-region boundary, bounded SD node-reference conversion and explicit HTTP/TLS diagnostics; SD-backed road geometry/names/graph; offline mode-aware road search and shortest-distance A*; walking/cycling/driving access, barriers, one-way and node-via turn rules; conservative conditional/via-way blocking; saved/resumed routes, route overlay, next-turn arrow/road name, GPS progress/off-route/arrival, explicit recalculation | Physical GPS/SD/heap/TLS validation; cross-region routing, Unicode fonts/search, lane/exit guidance and richer OSM/vehicle constraints are further refinements. Old 0.6.3 maps need fresh downloads for routing. See [MAPS.md](MAPS.md) |
| Recorder / IR / IMU | Dedicated streamed PCM WAV Recorder, interrupted WAV recovery, Files-only Audio Player; shared ES8311 microphone with a dedicated capture task and 64,000-byte/two-second mono buffer (32,000-byte fallback), pre-capture WAV creation, right-slot extraction, saturated 8x PCM gain, codec readback and saved live integer 1x–4096x gain and a clean themed mono meter, BMI270 configuration/scaled readings and 38 kHz NEC IR transmit; Settings screens | User confirms the 0.5.8-dev microphone works; new gain/UI changes need retesting. Microphone channel/gain/timing, IMU orientation/calibration and IR waveform validation; no built-in IR receiver |
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

- Physical verification remains required; it is separate from software scope.
  Meshtastic messaging, encrypted channels/public-key DMs, bounded client relaying,
  node discovery and opt-in location are now implemented. Upstream phone/USB API,
  remote admin, telemetry modules, store-and-forward, compressed text and
  adaptive next-hop routing are not implemented; see [MESHTASTIC.md](MESHTASTIC.md).

0.6.2-dev fixes zero-tick audio handoff, adds randomized cues/startup melody,
optimizes the ribbon and text editor, adds Known Networks and signed SD firmware
history. See [the full bug report](BUG_REPORT_0.6.2.md) for causes, verification and
hardware limitations. This release does not reduce the safe internal OTA layout.

0.6.3-dev begins the Maps roadmap; it does not claim complete navigation. Unused
TLS server handshakes are excluded to preserve the existing OTA slot/recovery
layout; validated HTTPS clients and temporary LAN HTTP sharing remain.

0.6.5-dev implements the requested road search, cached routes, turn-by-turn prompts
and fully offline regional routing. Unsupported rules fail conservatively; this
is not universal OSM/vehicle coverage. No partition migration is required.

0.6.5-dev adds numeric Recorder gain (1–4096), a streamed offline world coastline
overview centered on a live GPS pin with pan/zoom/follow controls, and removes the
legacy Wi-Fi import/old-passphrase UI. Current Known Networks remain encrypted.
The global overview is not a full-world road database. Hardware validation of
new gain levels, GPS pin/follow behavior and display readability is still pending.

## 0.6.7-dev full follow-up

The user explicitly lifted the bridge release hold and chose a fresh installation
instead of installing 0.6.6-dev first. The full release includes its manifest-memory
fix and retains signed updates, inactive-slot OTA and the independently bootable
factory installer. Partition sizes are unchanged.

Adds editable pansexual/nonbinary wallpaper presets, direct OpenStreetMap downloads
for explicit 500 m regions, SD-backed node-reference import and useful HTTP/TLS
failure diagnostics. Removes the failing Private.coffee endpoint. It also includes
numeric microphone gain, the streamed GPS-centered world overview and removal of
the old Wi-Fi passphrase/import UI. These software checks do not replace physical
Cardputer/TLS/GPS/SD or interruption validation. See [MAPS.md](MAPS.md),
[release notes](RELEASE.md) and [bug report](BUG_REPORT_0.6.7.md).

## 0.6.8-dev map controls

Both map views retain arrow-key pan and +/- zoom. P cycles Normal → Large → Fine
→ Normal and displays the distance per key press (meters for regional maps,
degrees for the world overview). Steps scale with zoom, stay shared across both
views during this boot, and reset to Normal after reboot. P refreshes labels
without rebuilding the map raster. Arrow movement pauses GPS follow; C recenters
on a current fix and resumes follow. World C now recenters even when the previous
offset was smaller than one display pixel. See [MAPS.md](MAPS.md).

## 0.6.9-dev installer and map management

POS Setup has three main choices: Prepare the microSD, Flash Cardputer, Restore
backup. Ctrl+C cancels; --doctor and --verbose retain optional connection/USB
diagnostics. Installing the helper resumes the chosen action without repeating
the banner/menu. Device identity, original factory recovery, hashes, formatting
confirmation and verified flash readback remain required.

Map options includes Delete region, with a default Keep region confirmation.
The selected map, its matching OSM/normalized sources, graph and saved routes
move to Files > Trash. Routes close before moving their files; the map catalog
file moves last, and failures preserve originals or recoverable Trash entries.
Other regions, unrelated names and the world asset remain. Empty pages rescan.
Hardware FAT32/removal/power-cut behavior still requires physical validation.

## 0.6.10-dev PC transfer tool and map boundaries

`pos-transfer` is a Linux Send/Receive CLI with Wi-Fi and the existing authenticated POS
BLE protocol, numeric-comparison agent, explicit file acceptance, streamed
SHA-256, resumable partials and no-overwrite publication. It is a separate release
download and included in the USB/SD bundle. Crostini usually has no usable
Bluetooth adapter; native Linux and a BLE peripheral-capable adapter are needed
for receiving. BlueZ/GATT API exchange is tested on a real private D-Bus against
a simulated BlueZ service, not a physical radio. Wi-Fi uses the temporary keyed trusted-LAN service without Bluetooth dependencies.
Share screens explicitly explain POS/PC use. See [TRANSFER.md](TRANSFER.md).

All three map providers accept 500 m through 4 km. Direct OSM uses one explicit
bbox; density/server constraints may still refuse a request. XML remains on SD
(up to 64 MiB), node processing is external-memory, and rendering streams records
into a 2,640-byte viewport. Existing node/geometry/routing bounds remain. Saved
regional maps have a white geographic square outline that pans/zooms with the
region; off-screen edges are not falsely pinned to the screen.

## 0.6.11-dev Calculator and reliability follow-up

Adds the scientific [Calculator](CALCULATOR.md), with explicit typing/keypad modes
for the punctuation/arrow legends, saved variables and bounded polynomial CAS.
[POS Transfer](TRANSFER.md) shows the session key, authenticates before sending,
reports pre-transfer/hash waits and streams available download chunks. Missing
BlueZ produces an explanation and Wi-Fi fallback instead of an unexplained D-Bus
error. The absence of a Crostini Bluetooth adapter cannot be repaired by this tool.
On-device Wi-Fi sessions show transfer/verification/completion, and transient audio
cue holds cover map loading and LAN transfer work without changing preferences.

OSM import skips discarded-way lookups, uses a sparse index/page cache and bounded
object buffers/reused scratch; the normalized result matches the previous engine.
See [the bug report](BUG_REPORT_0.6.11.md) for measured I/O counts and limits.
To make room without changing the safe factory/two-OTA layout, unused IPv6 protocol
code is omitted: POS configures IPv4 STA/DHCP and IPv4 LAN sessions, with no IPv6
interface previously configured. IPv4 HTTPS validation and all signed-update checks
remain. No experimental link-time optimization is enabled.

## 0.7.0-stable Meshtastic and mesh Wi-Fi

Adds independently implemented Meshtastic v2 radio interoperability: encrypted
channels, modern public-key DMs, saved TOFU identities/change detection, standard
node/position/routing protobufs, durable text-before-ACK, bounded randomized
client flooding/retries, CAD, airtime accounting and a dedicated Radio UI.
Legacy POS radio remains selectable. The current hardware supports only
868–923 MHz; US/EU868 presets/slots must fit it. [Full scope](MESHTASTIC.md).
The requested stable label is not a hardware-interoperability certification.

Wi-Fi scan entries now group equal SSIDs/security and retain the strongest AP.
Explicit connection scans all channels and selects by signal. Temporary LAN
sessions disable modem sleep until exit and reclaim idle HTTP sockets; the PC
tool explains connect versus response timeouts. A blocked guest/client-isolated
LAN cannot be repaired by changing the scan list.

Compact assertion messages reclaim flash while preserving runtime checks, panics,
backtraces, both OTA slots and independent recovery. TLS checks/curves remain.


## 0.8.0-dev Calculator, conversations and wallpaper

Implemented conversations/unread/delivery states; contacts/aliases/favourites/quick
messages; notification banners with optional audio; shared-node map pins and
age/distance; traceroute; channel URL export; optional TLS MQTT text bridge.
Calculator adds graphing, checked exact fractions, numeric real roots through
degree 8, 2–4-variable linear systems, 4×4 matrices/vectors, unit conversions,
unsigned programmer mode, persistent recent expressions and Files-openable named
worksheets. Background has 12 common stripe flag presets and direct hex entry;
the startup status accent follows saved stripes. Long app help is streamed from
verified SD resources. See [release notes](RELEASE.md), [Calculator](CALCULATOR.md),
[Meshtastic](MESHTASTIC.md) and [verification report](BUG_REPORT_0.8.0.md).
The implementation has the explicit bounds documented there; physical validation
remains separate. Games, terminal, app packages and automatic power management
remain outside scope.

The boot intro fills each stripe in 240 ms instead of 400 ms; half-fill stagger,
logo fade and one-second hold are retained (total scheduled intro 1.88 s).
