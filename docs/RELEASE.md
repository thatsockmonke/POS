# POS 0.7.0-stable

Adds Meshtastic-compatible radio messaging, encrypted channels, public-key direct
messages, client mesh relaying, node discovery and explicitly shared GPS locations.
Also improves mesh Wi-Fi scanning, access-point selection and PC-transfer diagnostics.

## Meshtastic

Open **Settings → Expansion hardware → Radio protocol / Meshtastic**. Select
Meshtastic, your actual region/preset and matching channels. For US default
LongFast, automatic slot means 906.875 MHz. Confirm antenna/transmit separately;
then use **Radio → Announce / discover nodes** and exchange channel/direct messages.

The default LongFast channel key is public. Private channel keys accept Base64
or hex; channel URLs can be imported from a text file. Direct messages use
X25519/SHA-256/AES-CCM with first-observed pinned peer keys; changed keys block
DMs until explicitly forgotten/reverified. GPS is never automatically shared.

Messages and statuses are readable SD JSON; pending encrypted packets survive
reboot for explicit retry. Delivery status distinguishes TX from destination ACK,
and incoming text is synced before acknowledgement. Retries, hops, queue sizes,
duplicate relays, CAD/backoff and airtime are bounded. Original POS radio remains
selectable; a single SX1262 cannot receive both protocols simultaneously.

See [MESHTASTIC.md](MESHTASTIC.md) for setup, private-channel/identity guidance,
precise interoperability scope, memory bounds and hardware verification steps.
US and EU_868 preset configurations are supported only within this cap's published
868–923 MHz range. This is POS interoperability, not every upstream module or
the Meshtastic mobile/USB API, MQTT, remote administration or store-and-forward.

## Mesh Wi-Fi and file transfer

Repeated mesh/repeater access points with the same name/security appear once,
with their strongest signal. Connecting scans all channels and selects by signal,
without pinning a mesh node's BSSID. Open and secured networks remain separate.

Temporary file-transfer sessions hold Wi-Fi awake and restore its previous
power-save mode on close; idle browser sockets can be reclaimed. The PC tool
separates an unreachable TCP endpoint from a POS status-response timeout and gives
key-free connection diagnostics. Keep the Cardputer transfer screen open.

Replace the computer's old `pos-transfer` with this release's executable:

```sh
python3 "/mnt/shared/MyFiles/Downloads/pos-transfer"
```

The user's curl and Chrome tests both failed to connect to the Cardputer, before
a key could be checked. These changes are not proof that the router issue is
resolved: guest networks/client isolation or a stale/unavailable device IP must
still be checked. A healthy bridged mesh permits communication across its nodes.
See [TRANSFER.md](TRANSFER.md).

## Updating and recovery

Use **Settings → System → Software Update → Check for Updates**. No SD format,
map redownload or partition migration is required. The factory recovery firmware
and both inactive-slot OTA partitions remain intact. Signed update verification,
certificate validation and firmware/System version pairing remain mandatory.
Runtime assertion checks/panics remain enabled, using compact silent assertion
messages to fit the new code; crash backtraces remain symbolizable.

## Validation and release status

The complete native regression suite, both ESP32-S3 firmware roles, bundled
checksums and the signed publication are checked before release completion.
See [BUG_REPORT_0.7.0.md](BUG_REPORT_0.7.0.md) for causes, test coverage and limits.

**No physical Meshtastic/mesh Wi-Fi, RF/CAD/range, GPS, power-cut or measured
heap/stack tests were performed in cloud.** `0.7.0-stable` is the requested
version/publication channel, not a claim that all upstream features or physical
interoperability have been certified. Test against a genuine Meshtastic node
before depending on it.

Local validation: **473 host tests passed**, including both crypto backends and
actual service persistence tests. The pinned ESP32-S3 installer/runtime builds
fit the unchanged layout. CI repeats the clean builds and tests before publishing.
