# POS 0.7.0-stable implementation and validation

## Changes and findings

| Finding / trigger | Cause | Change and regression coverage |
| --- | --- | --- |
| POS radio cannot communicate with Meshtastic nodes | POSR framing, private sync word, SF7/125 kHz and a fixed frequency differ from the Meshtastic protocol | Separate Meshtastic mode: 16-byte radio header, bounded protobuf handling, region/name-hashed frequency, presets, `0x2b` sync, AES-CTR and modern X25519/SHA-256/AES-CCM DMs. Frozen independent official-protobuf/cryptography vectors pass on OpenSSL and the actual firmware Mbed TLS path. |
| Treating a relay as DM delivery would be misleading | An origin can overhear a forwarded copy without the destination receiving it | DMs complete only on a matching destination ACK/response, with the correct origin/destination/request ID. Simulated three-node relay, wrong-peer ACK, duplicate ACK recovery and timeout tests cover this. |
| Lost ACKs or failed SD writes could lose a message | Duplicate suppression before durable receive would suppress a retry | Receive logging syncs before ACK/duplicate commitment. A simulated disk failure suppresses ACK; retry then delivers once. Duplicate delivered DMs are re-ACKed. |
| Reusing a packet ID after reboot would reuse an AES-CTR nonce | Volatile counters reset across boots | Reserve ID blocks atomically in NVS before using them; skip unused reservations on restart. Real service code is tested across separate native processes. Reservation failure queues/sends nothing. Old full-flash snapshots still roll cryptographic state backward; see MESHTASTIC.md. |
| Mesh flooding could consume memory or airtime indefinitely | Unbounded packet/node structures or queues and unlimited rebroadcast | Fixed queues/caches, hops, randomized delay/CAD, duplicate relay cancellation, 4-attempt retries and expiry. 61 minute buckets retain up to 36 seconds of TX airtime for at least a full hour; ACKs can be sent promptly within that budget. Malformed protobuf/fuzz/budget tests cover software bounds. |
| First-observed node keys could change silently | Automatic replacement of advertised public keys | TOFU pins are saved in checksummed redundant files. Changed keys block DMs and preserve the old key; deliberate forget persists a tombstone, frees the slot and requires rediscovery. Tests exercise actual service persistence, changed-key blocking and reboot. |
| App code initially exceeded existing OTA slots | The previous firmware had only about 12 KiB spare | Keep runtime assertion checks/panics but omit repeated assertion file/expression strings using the SDK's supported SILENT setting. Backtraces remain available for symbolization. No assertions are disabled, no TLS curves or certificate checks removed, and no recovery/OTA partitions changed. |
| Several identical Wi-Fi entries on a mesh router | Scan exposed one entry per BSSID instead of per SSID/security class | Group same-name/same-security APs, retaining the strongest signal, and scan up to 64 physical APs for a bounded 16-entry menu. Case-distinct names and open/secured networks stay separate. Explicit connection scans all channels and sorts by signal without pinning a BSSID. Tests cover duplicates, stronger updates, full-menu replacement and security separation. This is initial selection, not a new 802.11 fast-roaming implementation. |
| POS Transfer prints only `timed out` before any data | TCP connection establishment or a later HTTP response could time out with the same exception text | Explicitly establish TCP first and report connect versus HTTP-status timeout separately, with key-free connectivity instructions. Tests verify both errors and ensure keys are not printed in errors. The user's key-free curl and Chrome checks both failed to connect; exact router/device root cause is not established in cloud. |
| Idle browser sockets could exhaust a two-socket LAN server | Small server limit without LRU reclaim | Enable HTTP server LRU purge for idle sessions. Actual physical socket/ESP-IDF scheduling behaviour still needs validation. |
| Mesh AP downstream buffering may affect an explicit transfer | Wi-Fi modem power saving and AP buffering can interact | Hold Wi-Fi awake only during the temporary transfer/deployment session and restore the previous mode on close/start failure. This is a mitigation, not proof that it fixes the user's router. Router guest/client isolation cannot be bypassed by POS. |

## Validation

Run the ordinary host suite with the pinned ESP-IDF environment activated.
CI additionally builds that SDK's Mbed TLS natively and sets
`POS_MESH_MBEDTLS_HOST`, so both crypto backends and actual device-service
storage tests execute rather than skip. `tests/fixtures/meshtastic-v2.json`
records independent vector provenance; native tests do not require installing
the Meshtastic Python client or accessing a radio.

Coverage includes scalar wire/crypto vectors, tampered ciphertext/tag/nonce,
weak public keys, malformed and duplicate protobuf fields, queue limits, hop
limits, relays, ACK ownership, retries, receive storage failure, channel URL/key
import, encrypted settings, failed writes, corrupt preferences, ID reservations,
peer-key persistence, confirmed forget, protocol/channel UI, GPS consent and
Wi-Fi grouping/transfer errors. All pre-existing installer, recovery, signed
update, file, editor, audio and map tests remain required.

Both actual ESP32-S3 firmware roles must build and fit their unchanged slots.
Release bundles, image descriptors, System payload hashes, public downloads and
the signed OTA feed are verified separately before completion. Exact final counts
and image sizes are recorded in RELEASE.md after those checks finish.

## Physical verification still required

No physical Cardputer, LoRa transmission, real Meshtastic peer/relay, CAD timing,
RF range, GPS sharing, mesh Wi-Fi interoperability, DTIM/power-save behaviour,
large concurrent SD load, measured stack/heap minima or power-cut test was
performed in this cloud workspace. Do not interpret a test fixture as a radio
or a native filesystem as FAT32 hardware. The `0.7.0-stable` release name/channel
was explicitly requested; it does not certify these unperformed checks.

The implementation covers POS's Meshtastic messaging/channel/mesh/location
roadmap, with the explicit supported-region/module scope in MESHTASTIC.md. It
is not the entire upstream Meshtastic firmware or a mobile-app-compatible server.
