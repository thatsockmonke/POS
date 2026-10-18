# Files, Audio Player, Recorder, Music and hardware

Use Left/Right arrows to move the launcher ribbon and Up/Enter to open an app. Menus use Up/Down
and Enter. Fn+backtick is Escape; Fn + apostrophe, Fn+Space or short G0 closes
the current app.
Only one foreground app runs. Wi-Fi and the enabled location/radio services can
continue in the background. Music and explicit file-transfer sessions stop on exit.

## Files

Files opens to POS/Users/Owner in a plain paginated text list. Up/Down moves,
Enter selects Parent/Previous/Next/Folder actions or opens an entry's action menu.
Select Open folder, Play in Audio Player, or Open file → Text Editor/Preview/Share.
Copy/cut/paste, rename, Trash/restore, folder operations and confirmed permanent
deletion retain their existing protections. No decorative folder-content scans
or icon carousel remain. Arrows work without Fn outside text entry.

## POS Music

Put tracks under `/Music`; subfolders are supported through Browse music. The
player supports MP3 and uncompressed signed 16-bit PCM WAV, mono or stereo,
8–48 kHz. Other WAV encodings (including float/24-bit) and AAC/FLAC/OGG are not
supported by this version. Playback uses the ADV's ES8311 codec and I2S audio path.
BLE sharing is unrelated to Bluetooth headphones; ESP32-S3 lacks Bluetooth Classic
A2DP support.

| Action | Key |
| --- | --- |
| Play/pause | Space or Enter |
| Previous/next track | Left/Right arrows |
| Volume | Up/Down arrows, in 5% steps |
| Shuffle | R |
| Add current track to a playlist | P |
| Controls help | H |
| Stop and return | Escape |
| Close app | Fn+Space |

Playback begins at 30% volume. The screen shows elapsed decoded-audio time and
byte progress, rather than inventing an exact MP3 duration. Next/previous uses
the current folder, or the selected playlist. End-of-track continues through
that collection; shuffle selects tracks from it. Opening dialogs pauses output,
and closing releases the decoder, stream, codec output and I2S channel.

P appends the current track to a named `.m3u` file under `/Music/Playlists`.
Open playlist plays its entries in order. POS-generated entries use SD-root paths
such as `/Music/album/track.mp3`; manually written `Music/...` paths also work.
These are readable text files, with no proprietary library database. Paths are
relative to the SD system drive, not a computer's filesystem root; edit the prefix
when using those playlists on a computer. Comments and invalid/protected paths
are ignored. Missing/unsupported tracks produce a clear error.

The input, decoder state, PCM frame and MP3 scratch workspace are bounded heap
allocations, freed on exit. Music files are not loaded wholesale. The MP3 decoder
adapter preserves the upstream decoding arithmetic; host tests compare decoded
PCM with the unmodified pinned upstream decoder. Files must be below 2 GiB due
to current filesystem seek limits. Actual speaker level, DMA timing and SD/audio
coexistence still need measurement on the ADV.

## Share

Files → Share, Text Editor → Esc → Share saved file, and the launcher Share app
use the same interface:

- **Save local copy:** enter a new SD-relative destination. It streams/verifies
  the copy and preserves existing files.
- **Wi-Fi file transfer:** starts an explicit trusted-LAN session with one
  selected export and Downloads-only uploads. It stops when leaving the screen.
  This local web page uses unencrypted HTTP and a random session key; use a trusted
  network. See [INSTALL.md](INSTALL.md#local-wi-fi-file-transfer).
- **Send over BLE:** discovers compatible POS receivers and uses the transfer
  protocol below. Wi-Fi is preferable for large/faster transfers.

### BLE between two Panputers

1. On the receiving device open Share → Receive BLE file. It advertises only
   while this explicit session is open.
2. On the sender choose a file and Send over BLE. Scan, select the receiver and
   wait for pairing. The other device must run the compatible POS protocol.
3. Compare the six-digit numbers on **both devices**. Choose Codes match only
   if they match. Rejecting/closing does not accept a file. The service requires
   authenticated encryption using LE Secure Connections; no automatic Just Works
   acceptance or stored bonding key is used.
4. The receiver sees filename and size and must explicitly Accept or Decline.
   Accepted files go only to Downloads. Existing different content is refused.
5. The sender streams acknowledged chunks. On completion the receiver closes,
   reads back and SHA-256-verifies the file before publishing it. Both screens
   report completion; close the session when done.

Interrupted/cancelled transfers retain `.pos-share-ID.part` and `.part.offer`
files in Downloads. Retry the **same source file with the same filename** to
resume from the receiver's saved offset. Metadata must match; damaged/different
partials are preserved and refused. If the final ACK was lost but the target is
already complete, reaccepting the offer verifies its size/hash and acknowledges
completion without overwriting it. A partial file is not a completed download.
Partial bytes and the binary offer remain readable on a computer; format details
are in [PROTOCOLS.md](PROTOCOLS.md).

BLE filenames are limited to 80 bytes, and source files must be below 2 GiB for
resumable seeks. Discovery and protocol behavior are POS-specific; this does not
implement Bluetooth Classic OBEX or an OS-wide phone file-transfer integration.
Closing cancels scan/advertising/connections and deinitializes the BLE session.
If the SDK reports a shutdown failure, resources are retained safely and the next
attempt asks for a restart. Physical two-device pairing/throughput remain untested.

## Settings → Expansion hardware

Attach/remove the official Cap LoRa-1262 with power off. Connect its correct
antenna before transmitting. This build uses the published Cardputer ADV/cap
profile in [HARDWARE.md](HARDWARE.md).

The panel separately shows:

- **LoRa cap detected:** the IO expander responds and the SX1262 command exchange
  identifies the configured LoRa packet type. This means local hardware presence,
  not a connection to another radio node.
- **LoRa features enabled:** the user preference controlling Radio and Range Test.
  The app manager refuses those apps when the cap is absent or the switch is off.
- **Transmit enabled:** a separate session permission. Enabling features starts
  receive-only operation; TX requires explicit antenna/frequency consent and resets
  to off after restarting. Disabling features stops radio receive/transmit work.
- **GPS detected:** valid, checksum-verified GNSS RMC/GGA/GSV sentences have arrived
  recently. An initialized UART alone does not count as detection.
- **GPS fix:** a current valid position, which is separate from receiver presence.
- **Maps GPS gate:** unavailable without a detected GPS; available when
  GPS is detected, even while waiting for a position fix. The Maps app enforces
  this prerequisite; see [Maps](MAPS.md) for regional downloads and offline guidance.

GPS is independent of the LoRa enable switch. A receiver without a fix can be
detected; stale fixes stop being valid after five seconds, and receiver presence
expires after ten seconds without valid sentences. GPS coordinates are not
broadcast automatically. HDOP is reported as dilution of precision, not falsely
converted to meter accuracy.

LoRa enablement and frequency are saved as a nonsensitive policy in internal NVS
when possible; no NVS erase is attempted on an error. A save failure explains
that the change applies only to the current session. This policy is not in SD
backups. TX consent and precise-location sharing consent are not persisted.

## Radio and GPS/Location

Location shows fix state, coordinates, altitude, speed, satellites and HDOP. It
uses UART1 at 115200 baud (TX13/RX15). GPS UART input and the enabled radio service
can continue while other foreground apps run.

Before radio messaging, enable a detected cap in Settings, set a frequency allowed
where you operate, attach the antenna and explicitly enable TX. POS uses +10 dBm,
SF7, 125-kHz bandwidth, coding rate 4/5, private sync word and a conservative 1%
airtime budget. These settings alone are not a declaration of regulatory
compliance. The cap's supported frequency range is 868–923 MHz. Peers must use
the same settings and compatible POS protocol in legacy mode. The separate
Meshtastic mode adds compatible encrypted channels/DMs, node discovery, client
relaying and one-time location sharing. See [MESHTASTIC.md](MESHTASTIC.md).

Radio shows the local eight-digit node ID derived from the device MAC. Enter the
other device's ID for acknowledged unicast, or leave it blank for an unacknowledged
broadcast. Compose sends up to 160 printable ASCII characters. Ping, status and
one-shot location messages use the same service. Share location once requires
separate consent and a current fix. Radio messages are unencrypted; do not send
credentials or other secrets.

Messages have IDs, a CRC, recent duplicate suppression, unicast ACKs and up to four
transmit attempts with increasing retry delays. Broadcasts have no ACK storm.
Outgoing messages are staged, synced and read-back-verified in
`POS/Users/Owner/Radio/outbox/*.posr` before the UI reports that they are saved.
Up to four are scheduled at once; additional saved messages remain in the outbox.
Retry saved outbox schedules retained messages, including after a reboot once TX
has been explicitly re-enabled. Only an ACK from the intended peer retires its
pending outbox file. Incoming user messages are synced to readable `messages.jsonl`
before their ACK is queued. Recent messages shows the last eight complete records.
Duplicate suppression covers 32 recent IDs in RAM; logs retain IDs for independent
deduplication after reboot. No received content is executed.

RSSI/SNR are signal measurements, not distance. A range in meters is never inferred
from them. The portable location service can calculate geographic distance only
from two valid coordinate pairs. No particular LoRa range is promised.

## Radio Range Test

Use two compatible nodes. The receiving node needs a detected/enabled cap and
TX consent if it is to send ACKs. Open Range Test → Receive / log only there.
On the other node choose Send ping probes + log, enter the receiver's node ID,
and keep the screen open. Probes are queued every 60 seconds; radio duty scheduling
and retries may delay their actual transmission. Escape/Home stops the run/log.

Logs are unique `POS/Users/Owner/RangeTests/range-N.csv` files containing event,
monotonic milliseconds, GPS UTC when available, fix/coordinates/altitude/satellites/
HDOP, peer/message ID/type, RSSI/SNR, transmit attempts, unique received messages,
ACKs and exhausted retries. GPS UTC 0 means unavailable; missing coordinates and
signal readings are blank. RX/ACK rows carry the actual received RSSI/SNR; TX
rows do not invent a signal value. Periodic SAMPLE rows record movement between
packets. Original CSVs are normal user data and are included in backups.

Use message IDs and ACK/TIMEOUT events to compare unique probes and eventual
loss. Retry attempts are not new probes; unresolved probes are not called lost
before their retry deadline. Copy CSVs through Files/Share or an SD reader for
analysis on the Framework. Physical GPS accuracy, range and packet-loss figures
must come from actual device tests, not software fixtures.

## Battery display

Every device screen has a small black box at the top right, including dialogs,
apps and startup. Percentage appears to the left of the battery icon. Samples are
taken at most every five seconds, smoothed with an IIR filter and a two-point
percentage deadband. Four icon levels have hysteresis around their thresholds.
Unavailable calibrated ADC data shows `--%`, never a fabricated reading.

Percentage uses an approximate Li-ion voltage curve. Sustained filtered voltage
at or above 4.18 V for 60 seconds produces a green full icon and 100%; that state
releases below 4.12 V. This is an **estimated near-full indication**, not a
measurement of charge termination. The verified ADV profile exposes battery ADC
but no confirmed charger-status signal/fuel gauge. Charging/load effects and the
actual battery need physical calibration. Settings → Battery information explains
this on-device. Preview images use an example 75% value, not hardware evidence.
Automatic dimming, sleep policies and a broader power manager are outside scope.

## Expanded Settings

Settings remembers the selected row when returning from a child screen. It offers
profile/device naming, numeric brightness (10–100%), Date & time, Background,
Wi-Fi, Expansion hardware, System/software updates, Battery information and
Restore default preferences. Cancelling edits leaves saved preferences intact.
Resetting preferences requires confirmation and preserves documents, Wi-Fi vault,
update trust and LoRa policy. First-time setup still offers optional Wi-Fi.

Date & time shows local time, UTC and the current POSIX rule. Choose regional
presets (including DST, half-hour and quarter-hour offsets), keep the current
rule, or enter a validated custom POSIX timezone. These are stored rules, not an
updatable IANA timezone database; local legislation can change. Wi-Fi time sync
uses SNTP. Manual time uses `YYYY-MM-DD HH:MM` in **UTC**, with real-date checking
for 2020–2099. The clock may need resetting after complete power loss.

System provides Software Update, readable crash reports and an explicit
safe-mode-on-next-boot switch. See [UPDATES.md](UPDATES.md) for initial trust/feed
configuration, and [RECOVERY.md](RECOVERY.md#crash-records-and-safe-mode) for recovery.

## Editor selection, clipboard and history

Text Editor's Escape menu includes the same actions as its shortcuts:

| Action | Key |
| --- | --- |
| Select while moving | Shift + Fn/arrows |
| Select all | Ctrl+A |
| Copy / cut / paste | Ctrl+C / Ctrl+X / Ctrl+V |
| Undo / redo | Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z |
| Previous / next word | Ctrl + Fn/left or right |
| Start / end of document | Ctrl + Fn/up or down |
| Page up / down | Ctrl+U / Ctrl+D |
| Start / end of logical line | Ctrl+L / Ctrl+E |
| Find / go to line | Ctrl+F / Ctrl+G |
| Save / controls menu | Ctrl+S / Escape |

Selected text has a contrasting highlight. Typing replaces it; Backspace deletes
it in one edit. Copy/cut require a nonempty selection. Undo and redo each retain
up to 32 edits in the current open session; a new edit clears redo. Saving does
not clear session history. Reopening/recovering preserves the last verified draft,
while session undo/redo counts restart. Undo does not promise to recover discarded
or permanently deleted files.

History saves piece indexes; clipboard text lives in the document's SD workspace.
Large copies and pastes use a reusable 4-KiB buffer with progress/cancellation and
readback checks. They do not allocate the selected text in RAM. Closing/discarding
a document clears its clipboard/history; this is a document-local clipboard.
All working text and indexes remain ordinary readable files if POS stops working.


## Recorder

Recorder → **Microphone gain** accepts any whole number from **1 to 4096**
(default 8). Press Ctrl+U to clear the value, type e.g. 128 or 256, then Enter.
During a recording, Left/Right halves/doubles it within that range; the setting
is committed after stopping capture. Try 32x for quiet speech; reduce it when
Clipping appears. The normal view has a wallpaper-colored microphone and one
mono level bar, elapsed time, percentage and gain. It does not show raw channel
values. Repeated all-zero input is labelled without inventing audio.

Space pauses/resumes; Enter or Escape stops/saves; Fn + apostrophe, Fn+Space or a
short G0 saves/closes. Queued PCM is drained on pause/stop. Resuming discards
analog warm-up. Capture runs in a dedicated bounded task only while this app
records, independently of SD/UI work. Playback and recording are exclusive.

Files are standard signed 16-bit mono PCM WAV at 16000 Hz under `/Recordings`,
with unique `Recording-00001.wav` names. Writes sync approximately every two
recorded seconds; stop verifies the complete PCM CRC before publication. Partial
`.wav.part` files remain recoverable through **Recover interrupted WAV**. Existing
files are never overwritten. A DMA overrun, full card or storage stall beyond the
bounded buffer remains an explicit error. The user confirms input works in
0.5.8-dev; gain/UI changes still need acoustic/usability confirmation.

## Files Audio Player

Select an MP3 or PCM WAV in Files, then **Play in Audio Player**. Space/Enter
pauses, Up/Down changes volume, Left/Right selects a neighboring track manually, P adds to a
standard Music playlist, Escape stops and returns to Files, and Fn+Space closes
the current foreground Files app. Playback stops at the selected file's EOF;
Music's automatic library advance/shuffle behavior stays in Music. The player
shares the same bounded decoder/audio service and supported formats described above.

## IR & motion

Settings → **IR & motion** offers live accelerometer/gyroscope/temperature readings
and explicit **Send infrared / NEC**. Enter the receiver's 8-bit address and command
in hex, point the emitter, and confirm one transmit. No automatic scanning or
unrequested transmission runs. The shared BMI270 and NEC APIs are available to
future apps; the IMU is stopped when its screen closes. Sensor axes are shown
without pretending to provide a calibrated compass. The ADV has no IR receiver.

## Visual controls and motion (0.5.9-dev)

Launcher: Left/Right springs between outlined black icons; Up/Enter zooms the
selected icon and fades quickly to black before the app. Return fades to the same
selection and reverses its zoom with a small overshoot. **Fn + apostrophe** and
short G0 are global close controls; Fn+Space remains supported. Escape backs out
of nested screens, and closes non-modal apps. Held recovery shortcuts remain.

Background edits its live striped canvas: Up/Down selects, Enter picks color,
Left/Right changes count, S saves, P resets the draft to the pan flag, Escape
cancels. Selected stripes pulse white briefly, at a slow interval. No Preview
button or separate preview state remains.

Files returned to a plain text list in 0.6.1-dev. Arrows select, Enter opens
actions, and existing file safety and pagination remain.

Counter uses a red pressable button with a jumping number: Enter/Space/Up adds,
Down subtracts. Music and the Files-only Audio Player use a disc/progress display.
Shared menus and app screens carry wallpaper accents. Dense technical screens
retain readable text; this is not a replacement of every diagnostic with icons.

Animations use short integer spring curves and streamed rendering. Slow draws
skip intermediate frames; frame-rate/response measurements require hardware.

## Arrow navigation without Fn (0.5.10-dev)

Menus and apps interpret the printed arrow legends directly: ; Up, , Left,
. Down and / Right. This includes the launcher, Files, Background/color picker,
Music volume/tracks and Recorder gain. Shift+arrows still adjusts color finely.
Fn+arrows and USB arrows continue to work. In editor/text fields, the raw keys
retain punctuation; Fn+arrows moves the text cursor. Global close shortcuts stay
Fn + apostrophe, Fn+Space or a short G0 press.

## UI sounds (0.6.1-dev)

Settings → Sounds offers master enable, 0–100% volume, key-press feedback,
app/notification feedback and a preview. Navigation/typing, selection/back,
app open/close and notices use short ramped synthesized tones. Rapid requests
coalesce to one pending cue; input never waits for sound generation. The codec
has one owner: cues are skipped during microphone capture and music playback,
then output is drained/muted before releasing ownership.

Launcher motion keeps the selection box stationary. Left/Right slides the icon ribbon underneath it with momentum and spring settling; another arrow can change the motion before it finishes. Only seven tiny icon sprites are cached, including offscreen neighbors.

## 0.6.2 app changes

- Launcher: fixed selection box; continuous elapsed-time horizontal spring,
  responsive arrow input, native 32×32 black icons with white outlines, and
  partial/pipelined display transfers. Seven tiny sprites are cached (896 bytes).
- Sounds: randomized pitches within a small palette; a 400-ms C–E–G–C startup
  melody with a quiet sustained C harmony. Master mute, volume and app/notification
  category also apply to startup. Recording/playback suppress interface sounds.
- Editor: Esc → Delete document → explanation → confirmation moves the original
  saved document into Files' Trash. Unsaved edits are discarded only after that
  succeeds. Changed originals, protected paths and symlinks are refused. A 4-KiB
  nearby-text cache, no idle re-render, layout-only page movement and batching of
  already queued typing improve responsiveness. One burst is one undo operation.
  Saves/recovery still validate complete data. No document-sized RAM buffer.
- Wi-Fi → Known Networks: select an SSID for Connect / Forget, or toggle
  Auto-connect. This starts disabled. Auto-connect retries visible known networks
  about every 30 seconds when offline and does not consume keyboard events.
  Manual Wi-Fi management pauses the background attempt; Disconnect disables
  auto-connect. Hidden SSIDs can still be connected manually.
- Known Networks has only saved SSIDs, Auto-connect and Back. The legacy import
  and old-passphrase prompts have been removed. Old encrypted bytes remain
  untouched; re-save any network not already in Known Networks. Capacity is four SSIDs.
- Settings → System → Firmware history: see [FIRMWARE_HISTORY.md](FIRMWARE_HISTORY.md).

Credentials are authenticated encrypted NVS blobs with a random per-device key.
The key is also on the device's NVS: this prevents casual plaintext disclosure,
not extraction by an attacker who can read unencrypted physical flash. Credentials
and device keys are never put on SD or in a filesystem backup. No eFuses are burned.

## Maps (0.6.3-dev)

A sixteenth launcher app adds on-device region selection, bounded Wi-Fi downloads,
SD-backed offline road maps and north-up GPS waypoint guidance. Arrows pan, +/-
zooms, C follows GPS, Enter chooses a destination at the crosshair. Global Home
releases the viewport. See [MAPS.md](MAPS.md) for exact controls, attribution,
provider choice and the remaining routing limitations.

## Maps routing (0.6.4-dev)

Enter on a saved map offers Travel mode, offline road-name search, route to the
crosshair, saved-route resume, clear/recalculate and an explicit straight-line
option. The map overlays a yellow route, large turn arrow, next road name and
remaining distance. Download regions again after 0.6.3 to include restrictions;
existing road maps remain viewable. See [MAPS.md](MAPS.md) for routing rules,
caching, controls and exact limits.

## World map (0.6.5-dev)

Maps highlights **World map** by default. Enter opens its offline Natural Earth
coastline overview, initially centered on your current GPS position. A pink pin
marks the live fix; without a fix it shows the planet centered on 0,0 with no pin.
A receiver is still required to enable Maps. Arrows pan, +/− zoom, **C** returns
to GPS follow, **0** fits the planet, and Enter/Escape returns to the Maps menu.
Saved regional roads, search, downloads and turn guidance remain available there.
The global overview is coastlines, not a planet-sized road/routing database.

## PC file transfers (0.6.10-dev)

Share explicitly labels POS/PC send and receive options. Linux **POS Transfer**
provides Send/Receive over Bluetooth and Wi-Fi. The Bluetooth sender help explains
Linux Receive mode; Wi-Fi sessions mention the terminal tool as well as the browser.
Wi-Fi exports provide a percent-encoded original filename header for PC clients.
See [TRANSFER.md](TRANSFER.md) for download, simple steps and Crostini limitations.

## Calculator (seventeenth launcher app)

[Calculator](CALCULATOR.md) provides a scientific keypad/function picker, saved
numeric variables, DEG/RAD, fraction/decimal output and polynomial CAS. Typing
keeps punctuation literal; Ctrl+K or the Tools menu switches to keypad arrows.
Tab opens tools, Enter evaluates/types a selected button, Esc/global Home closes.
See the dedicated guide for every function, examples and exact software limits.
