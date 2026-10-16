# Files, Music, Share and expansion hardware

Use A/D to move the launcher ribbon and W/Enter to open an app. Menus use W/S
and Enter. Fn+backtick is Escape; Fn+Space requests closing the current app.
Only one foreground app runs. Wi-Fi and the enabled location/radio services can
continue in the background. Music and explicit file-transfer sessions stop on exit.

## Files

Files starts in `POS/Users/Owner`. Each directory has paginated entries, Parent,
Previous/Next and Folder actions. Enter on an entry opens its action menu.

- **Open:** enter a folder, play MP3/WAV, or choose Text Editor/Preview for a file.
- **Copy / Cut:** put the selected path on the app's clipboard. Navigate to the
  destination, choose Folder actions → Paste, then accept/edit the destination
  filename. Cut moves only after a successful paste; leaving Files clears the
  clipboard. Existing targets are refused, including case-insensitive collisions
  on FAT32. Use a new filename to keep both copies.
- **Rename / New folder:** enter a simple name. System containers, installer
  media, recovery backups and settings slots are protected from these operations.
- **Move to Trash:** the original moves into a unique
  `POS/Users/Owner/Trash/item-N/data`, with readable `restore.json` recording its
  original path. No user data is formatted or automatically erased.
- **Restore:** Folder actions → Open Trash, open the relevant item-N folder, then
  Folder actions → Restore this Trash item. Existing destinations are preserved;
  rename/move a conflicting file first. The original parent folder must exist.
- **Delete permanently:** type `DELETE` exactly. This can delete files/folders
  recursively and cannot be undone. A cancelled/failed deletion leaves the
  remaining files in place. Trash is the safer normal delete action.
- **Share:** send a selected file through the common Share screen. Folder transfer
  is currently local copy/move; BLE/Wi-Fi sharing selects one file at a time.

Copies stream through a 4-KiB buffer and verify the staged file before publishing
it. Folder copies remain under a unique `destination.pos-copy-N.part` name until
all contents are copied and verified. Interrupted/cancelled work stays accessible;
it is never presented as a successfully completed target. Original files remain
untouched. Copies do not silently resume or overwrite partial destinations.

Paths are limited to 240 bytes and 16 directory levels. Symlinks are refused by
native helpers. Deeper/longer paths or insufficient space produce an error and
retain the original. Directory listings follow storage order rather than sorting
the entire directory in RAM. The factory recovery browser remains read-only.

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
| Previous/next track | A/D or left/right arrows |
| Volume | W/S or up/down arrows, in 5% steps |
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
- **Maps GPS gate:** unavailable without a detected GPS; ready for a Maps app when
  GPS is detected even if it is still waiting for a position fix. Maps/navigation
  itself is not implemented in this release. The `pos_expansion_maps_available`
  capability API enforces that prerequisite for future Maps integration.

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
the same settings and compatible POS protocol; this is not Meshtastic interop.

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
