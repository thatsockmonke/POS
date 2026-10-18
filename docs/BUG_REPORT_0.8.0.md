# POS 0.8.0-dev implementation and verification report

## Confirmed findings and fixes

| Finding | Cause | Fix / regression coverage |
| --- | --- | --- |
| Outgoing text missing from conversations | Prior log callbacks only retained sent/ACK metadata; text was absent | Durable queued-text callback precedes LoRa transmission. Real service tests inspect the journal; thread tests merge sent/ACK by packet ID |
| Repeated received text after a storage retry | A failed durable write can be retried with the same packet ID | Thread reader suppresses duplicate sender/ID records; core still suppresses ACK on retention failure |
| Torn final journal record could consume the next record | Appending directly after an interrupted line concatenated JSON | Shared durable append helper isolates a partial last record before appending; no completed record is overwritten |
| Calculator history assumed the user folder existed | Prior ephemeral history did not need an independent journal directory | Create its parent before history writes; existing standalone Calculator UI tests reproduce the missing-parent case |
| MQTT status could regress to Connecting | A fast CONNACK could arrive before the UI start path wrote its status | Set Connecting before starting the client; actual event-handler test drives a synchronous CONNACK |
| MQTT status text could race with the display | Callback and UI could access a mutable string concurrently | Atomic status code, immutable display labels |
| MQTT replay/loop / malformed traffic risks | Bridging introduces an Internet input path | Reject retained/fragments/own gateway/wrong topic/duplicate or malformed envelopes; generation and channel/text validation, via-MQTT flag and existing LoRa gates. Tests cover transport adapter plus actual device-service/core code |
| MQTT could retain a competing TLS client during updates | A session might remain connected while the updater allocates TLS/manifest buffers | Stop the optional bridge before checking/downloading updates; restart it explicitly afterwards |
| Graph borrowed raster lifetime | The display retains the previous borrowed map raster | Submit an ordinary Calculator view before freeing graph memory, including cancellation |
| Untrusted worksheet/log JSON could recurse deeply | New SD JSON readers need bounded parsing | Reuse the eight-level JSON guard and require a complete parsed document; oversized/invalid data is preserved |
| Invalid exact/system/programmer inputs | New math grammars expose overflow, nonlinear and shift edge cases | Checked rational operations, explicit linear-term grammar, dimensional checks, fixed-width unsigned operations and validated shifts; independent Fraction/random arithmetic and adversarial-domain tests |
| Existing OTA slot capacity exceeded | New modules plus unused SDK transports overran the fixed slot | Exclude unused WebSocket transports and TLS DS hardware-key path, move long app help to signed/hashed SD resources; retain factory recovery and both original OTA slots |
| Startup status line used the default flag before reading settings | Theme was initialized after startup/setup screens | Mount SD, load the saved theme, then present the startup status; apply it before desktop/setup screens. Renderer tests cover every preset's footer colours |

## Scope and limits

Conversations retain a bounded tail (16 messages / 64 KiB); unread badges describe
that tail, and full JSONL logs stay accessible. Old outgoing packets may lack
text because older releases never logged it. Contacts use versioned redundant
metadata independent of identity key pins. LoRa airtime, keys, relays and explicit
location-sharing rules remain in force.

MQTT is optional, session-scoped, TLS-verified and text-broadcast-only on one
channel. It uses the SDK MQTT 3.1.1 client and official binary ServiceEnvelope /
MeshPacket fields. SDK-adapter tests establish configuration and event behavior,
not a real TLS socket or RF test. Broker credentials live only in SDK RAM until
stopped; they are not written to the card. A channel key does not add message
authentication to Meshtastic's AES-CTR protocol.

Calculator keeps small, explicit limits. Exact mode has signed 64-bit rational
bounds; scientific and matrix calculations use finite double precision. Linear
systems require a unique solution; higher-degree polynomial solving isolates
numeric real roots and does not provide complex roots beyond quadratics. This
release does not implement arbitrary-precision/general-purpose symbolic algebra.

## Checks

The final full native suite, both ESP32-S3 roles, package integrity and signed
update verification results are recorded in the release completion notes. Tests
include UBSan, both OpenSSL and pinned SDK Mbed TLS, actual NVS/SD mesh-service
code, independent official protobuf fixtures, calculator reference arithmetic,
UI scripts, restore/rollback/update and file-transfer regressions. No tests are
claimed passed before their completion.

Physical checks still required: peer interoperability (including multi-hop
traceroute and MQTT), broker/TLS memory alongside Wi-Fi/radio, display/keypad and
hex entry, graph responsiveness, GPS marker age/distance, audible notification
ownership during recording/music, actual heap/stack, SD performance and interrupted
writes/OTA. No such hardware results have been invented.

The faster intro is pixel-tested across each 240-ms stripe fill, halfway start,
logo fade and one-second hold; the complete scheduled intro is 1,880 ms.

## Local release results

491 native regression cases passed across the full-suite run and targeted reruns
of corrected/new cases. Both firmware roles built with the 3,072-byte frame
limits on the added modules. Runtime: 1,506,160 bytes, within each unchanged
1,507,328-byte OTA slot; installer: 836,432 bytes, within its 1,048,576-byte factory
slot. This is a tight flash budget; future additions must be measured again.
Actionlint, Python syntax checks and the private D-Bus BLE smoke test passed.
Release ZIP/SD payload and public signed-feed checks are performed before the
release completion message. No physical testing is included in these results.
