# POS file transfer and radio formats (version 1)

All multibyte integers below are little-endian. These are deliberately documented
so a computer can recover user data and implement an interoperable client.

## Common file offer

`pos_share_encode/decode` serializes an offer into exactly 158 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | Format version, 1 |
| 1 | 8 | Stable transfer ID |
| 9 | 4 | Uncompressed file length |
| 13 | 81 | Filename, up to 80 bytes plus NUL; zero-padded |
| 94 | 64 | Lowercase ASCII SHA-256 of the complete file |

Names must be simple FAT-compatible user filenames without directories, traversal,
leading dot or device-reserved names. Completed targets go under Downloads.
The ID is the first 64 bits of the SHA-256 (hex interpreted as an integer), XOR
the filename CRC-32. It identifies resumable data; it is not an authentication key.

Receiver work files are `.pos-share-%016llx.part` and that path plus `.offer`.
The offer file is the exact binary record above, and the partial is raw file bytes.
Their metadata must match before append/resume. The saved offset is the synced
partial file's size. Sequential chunks are written/synced before acknowledgment;
a repeated already-written range is acknowledged without duplicating bytes.
Final publication requires the full declared size and a matching read-back SHA-256.
An existing identical completed target can be verified and acknowledged again;
different existing content is never overwritten. Inputs/staging files remain on
failure. Use the original source to retry damaged partials, or copy partial bytes
to a new path for manual recovery; no proprietary decoder is needed.

## POS BLE GATT service

- Service: `73f5ae10-5c12-4b83-9d48-bae023504f53`
- Sender writes commands to `73f5ae11-5c12-4b83-9d48-bae023504f53`.
- Sender reads receiver status from `73f5ae12-5c12-4b83-9d48-bae023504f53`.
- Both characteristics require authenticated encrypted access. POS uses LE Secure
  Connections numeric comparison, explicit user confirmation and no bonding.
- Advertisement carries the service UUID; scan response carries the device name.
  Discovery/advertising are explicit app sessions, not always-on endpoints.

The sender is the GATT central/client; the receiving POS is the peripheral/server.
MTU negotiation prefers 185. The protocol also works at the default MTU 23.

| Command | Payload |
| --- | --- |
| `O` | 1-byte fragment sequence (starting at zero), then 1–18 offer bytes |
| `D` | 4-byte file offset, then 1–128 raw file bytes (limited by negotiated MTU) |
| `F` | No payload; finish and verify |
| `C` | Cancel; preserve partial data |

Every status is five bytes: status character plus a 4-byte offset/value.

| Status | Meaning/value |
| --- | --- |
| `W` | Idle |
| `M` | Next expected offer-fragment sequence |
| `P` | Offer complete, waiting for explicit user acceptance |
| `A` | Accepted, resume at returned file offset |
| `K` | Chunk committed, returned offset is next byte |
| `V` | Final verification in progress |
| `Z` | Complete and verified, returned offset equals file size |
| `E` | Refused, invalid request or failed transfer |

The client uses acknowledged GATT writes, then reads status until the application
acknowledgment is visible. It sends one chunk at a time, retries a chunk up to
three times, and preserves work when a connection/request fails. Offer acceptance
and final verification have a five-minute wait bound with user cancellation.
Retrying a completed same-file offer returns `Z` after verifying the existing target,
which recovers a lost final acknowledgment. The sender's source is never deleted.
Current resumable source seeks are below 2 GiB.

## POS LoRa packet

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `POSR` |
| 4 | 1 | Format version, 1 |
| 5 | 1 | Type |
| 6 | 4 | Sender node ID |
| 10 | 4 | Destination node ID, 0 means broadcast |
| 14 | 4 | Nonzero message ID |
| 18 | 1 | Text length N (0–160) |
| 19 | N | Printable ASCII text |
| 19+N | 4 | Standard IEEE CRC-32 over all preceding bytes |

Types: MSG=1, PING=2, ACK=3, LOC=4, NOTE=5, STATUS=6. LOC text is decimal
`latitude,longitude`, sent only with explicit consent and a valid fix. ACK echoes
the message ID, names the original sender as destination and has empty text.
Only a matching intended peer can acknowledge a pending unicast. Broadcasts are
not ACKed. Recent duplicate suppression still re-ACKs retransmitted unicasts.

Four pending packets are scheduled in RAM; durable outbox files are exact wire
packets, committed through unique `.part` staging and readback before publication.
An exhausted retry remains saved for manual retry. The maximum is four TX attempts,
with nominal delays of 15, 30, 45 and 60 seconds before the next decision. The
conservative global 1% airtime budget can delay transmission and ACKs. Actual
transmit attempts, ACKs, duplicates and timeouts must be distinguished in analysis.

Radio settings: SF7, BW125 kHz, CR4/5, 12-symbol preamble, explicit header, hardware
CRC, private sync register bytes `14 24` and +10 dBm. Frequency is configured
within the cap's supported 868–923 MHz range and must be permitted locally.
These settings describe the legacy unencrypted POS protocol. Meshtastic uses a
separate mode; see [MESHTASTIC.md](MESHTASTIC.md). Neither mode is LoRaWAN. Received bytes
are never treated as code, commands or authority to change device settings.

`messages.jsonl` stores readable JSON records with sender, ID, type and text.
Range CSVs record packet IDs, event kinds and real measurements; their schema is
described in [APPS.md](APPS.md). No signal-to-distance conversion is performed.

## POS trusted-LAN file transfer

During an explicitly opened Wi-Fi file-transfer screen, port 8080 accepts
`Authorization: Bearer <16-hex-session-key>`. This is local HTTP, not TLS; use
a trusted LAN. `POST /upload?name=<percent-encoded-basename>` accepts an ordinary
body with Content-Length and `X-POS-SHA256`; POS streams, verifies and publishes
only to Downloads, preserving existing files. `GET /download` exposes only the
file selected on-device and returns `X-POS-SHA256`, plus `X-POS-Filename` in
0.6.10-dev (percent-encoded UTF-8 basename). Clients validate names, stream and
verify before no-overwrite publication. HTTP redirects are not followed with the
key. Wi-Fi transfers do not currently resume. Closing the POS screen stops the
server and discards its temporary key. The Linux client is `pos-transfer`.

## POS USB file transfer v1

An explicitly opened on-device USB session owns the USB serial console input.
Frames are `0x1e`, ASCII `POS1`, lowercase hex of the little-endian uint32
request ID plus payload plus little-endian CRC32, then LF. The PC decoder also accepts the console driver's CRLF terminator. CRC covers the ID
and payload. Payloads are 1–517 bytes; decoders discard oversized, malformed
or bad-CRC frames and resynchronize on 0x1e. ID 0 is a periodic greeting:
`H`, protocol byte 1, `R` (device receives) or `S` (device sends),
`m5stack-cardputer-adv`, NUL, firmware version. PC waits for this before writing.

Nonzero IDs identify checked requests/responses. `O` + the existing 158-byte
POS offer requires on-device acceptance; `A`+offset or `Z`+size accepts/resumes
or confirms an identical completed file. `D`+uint32 offset+up to 512 bytes writes
and receives `K`+committed offset. `F` verifies and publishes, returning `Z`+size.
For device export, `G` returns `O`+offer; `R`+offset returns `D`+offset+data from
only the user-selected file. `Q` closes the session, returning `K`+offset.
`E`+UTF-8 text reports refusal/error. Interim `V`+uint32 done+uint32 total
reports hash/SD progress for the same request. The most recent checked request's
reply is cached so a lost ACK can be retried without applying the operation twice.

This protocol grants no raw block, firmware, arbitrary remote path, delete or
terminal-command access. Uploads use protected Downloads staging/metadata and
SHA-256 publication. Export paths are selected locally and checked as user-owned.
CRC handles accidental framing damage; SHA-256 verifies the completed file.
USB physical access and explicit on-device session/offer approval are the access
boundary; this is not an encrypted transport or a replacement for authenticated
firmware updates. Late frames are swallowed outside the session rather than
being interpreted as keyboard input. Console release also discards the remaining
line when cancellation consumed a frame prefix; after a checked Q response, the
PC sends one bare LF to clear this quarantine. With an interrupted serial-console
session, press Enter once to resume USB keyboard fallback. Recovery/OTA partitions are unchanged.
