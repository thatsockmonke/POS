# Meshtastic on POS

POS 0.7.0-stable adds a Meshtastic-compatible radio mode alongside the original
POS radio protocol. Open **Settings → Expansion hardware → Radio protocol /
Meshtastic**. The **Radio** launcher app follows the selected protocol.

## Connect to a mesh

1. Power off before fitting the official LoRa-1262/GPS cap and its correct antenna.
2. Enable LoRa features in Expansion hardware. Select **Meshtastic mesh** in
   Radio protocol. Choose your actual region, modem preset and frequency slot.
3. For the usual US default, choose **US**, **LongFast**, slot **0 (automatic)**,
   and the default primary channel. This calculates **906.875 MHz**, not the
   original POS radio's 915 MHz setting. All participating radios must agree.
4. Set your node name/short name. Under Channels / encryption, choose the same
   channel name and PSK as the other nodes. Private keys accept Meshtastic Base64
   or 32/64 hex characters for AES-128/AES-256. Secondary channels with an unset
   PSK inherit the primary key, as in Meshtastic; explicit disable uses `AA==`.
5. Alternatively, save an official `https://meshtastic.org/e/#…` channel URL in a
   plain text file on SD (at most 2 KiB). Channels → **Import channel URL from
   file** previews a confirmation before replacing the channels/radio settings.
   The imported file itself contains channel secrets; keep it private. POS does
   not enable MQTT, remote administration, TX or automatic GPS from a URL.
6. Confirm **Antenna / transmit**. TX consent lasts only for this boot; changing
   protocol, channels or radio settings revokes it. Receive mode remains usable.
7. Open Radio → **Announce / discover nodes**. Discovery sends your name and
   public key, not GPS. Wait for peers to announce/reply. A quiet mesh is not
   evidence that a particular person/device is absent, or that radio is working.
8. Use **Channel message**, **Direct message**, or **Nearby nodes**. Match node
   IDs in `!xxxxxxxx` form. The mesh node ID comes from the last four MAC bytes;
   it differs from the legacy POS radio ID. A direct message requires a previously
   discovered peer key. Announce both nodes before exchanging DMs.

The default channel uses a **publicly known key**. It offers compatibility, not
privacy or sender authentication. Use a private channel key for a closed group.
Channel AES-CTR has the same unauthenticated-encryption limitations as upstream;
an acknowledged packet is not proof that a human read it.

## Messages, identities and locations

- Channel text: standard `TEXT_MESSAGE_APP` protobuf data and AES-128/AES-256 CTR.
  POS composes up to 160 bytes with the current keyboard UI and receives payloads
  within the 255-byte radio frame bound. Received UTF-8 is retained in JSON;
  the current screen font cannot display every Unicode character.
- Direct text: X25519 shared secret → SHA-256 → AES-256-CCM, 8-byte tag plus the
  upstream 4-byte extra nonce. Authentication failure does not become plaintext.
  No automatic fallback to an insecure legacy direct message.
- Public keys are pinned on first observation and saved in checksummed redundant
  user saves. This is **trust on first use**, not verified identity. A changed
  advertised key blocks DMs while retaining the previous key. Compare the full
  public key through another trusted channel before using **Forget old public
  key** and rediscovering. Forget frees a node-cache slot and survives reboot.
- **Status / latest message** distinguishes sent, destination ACK, destination
  NAK and timeout. Relaying is not destination delivery. A channel broadcast is
  not individually acknowledged. Retries are bounded to four TX attempts.
- Received text must be saved and synced before POS ACKs it. Duplicate DMs are
  re-ACKed so a lost ACK can recover, without duplicate message-log entries.
- **Share GPS location once** requires explicit confirmation and a current fix.
  POS sends one Position protobuf. It never automatically broadcasts coordinates
  or answers location requests. Nearby nodes shows only locations voluntarily
  received this session, with peer timestamp; they may be stale. RSSI is not distance.
- Messages/status are ordinary readable JSON lines in
  `/POS/Users/Owner/Radio/meshtastic.jsonl`. Pending encrypted packets are in
  `Radio/mesh-outbox`; incomplete `.part` files remain for recovery. Retry is
  explicit and only accepts the original node/configuration and valid CRC.
  Changing settings never silently deletes an old outbox.
- Peer identity saves are under `Saves/mesh-xxxxxxxx/slot-{0,1}.json`. Channel
  secrets and the device private key stay in internal NVS, not on SD. Settings
  use AES-GCM with a random NVS device key, without a passphrase. Unencrypted
  physical flash/full-flash backups can still expose those keys. No eFuses or
  partition changes are made. Full-flash rollback also rolls back cryptographic
  state: do not treat restoring an old factory/POS snapshot as a safe way to
  preserve a live private mesh identity/channel indefinitely; rekey with peers.

## Mesh behaviour and limits

POS behaves as a bounded client with delayed, randomized flooding: hop-limit
decrement, origin/packet duplicate cache, duplicate relay cancellation, next-hop
selection respect, opaque encrypted forwarding, destination ACK/NAK processing
and retries. It uses flooding fallback for its own DMs rather than maintaining
upstream's adaptive next-hop table. An unknown encrypted payload can be relayed
without knowing its key. A single radio cannot listen to POS radio and Meshtastic
at the same time. The existing POS CSV Range Test uses legacy POS mode.

The SX1262 uses Meshtastic sync word `0x2b` (register bytes `24 b4`), explicit
headers, CRC, normal IQ, 16-symbol preamble, automatic LDRO and the selected
preset. Carrier activity detection and randomized backoff precede TX. All local
messages, relays, ACKs and failed transmissions share a conservative **36-second
rolling-hour airtime budget** at +10 dBm. ACKs may follow quickly within that
budget; exhaustion delays packets. The budget is RAM state and restarts on boot,
so it is not a regulatory compliance mechanism. Follow local radio rules.

The cap's published RF range is **868–923 MHz**. This implementation supports
US and EU_868 plans, with only slots whose entire bandwidth fits that hardware
range. Other regions, deprecated VeryLongSlow, custom modem parameters and RF
frequency offsets/overrides are refused rather than silently mistranslated.
LongFast and the standard presets whose bandwidth fits the region are supported;
the selected preset may require a supported explicit slot if its automatic hash
lands outside the cap's range. POS does not extend the cap to 433 MHz or 2.4 GHz.

Bounded RAM: 8 channels, 32 retained nodes, 128 recent packet IDs and a shared
12-packet TX queue. Old packets expire, retries stop, queue-full is reported, and
nodes must be explicitly forgotten when the identity cache is full. The mesh
workspace is heap allocated only when Meshtastic is selected and released when
returning to POS radio. Large logs and resources remain on SD.

This is **POS interoperability**, not the complete upstream Meshtastic firmware.
It does not provide the Meshtastic mobile/USB client API, MQTT, remote admin,
telemetry modules, traceroute UI, store-and-forward, compressed-text decoding,
key-verification handshake or every upstream routing role/module. POS handles
standard text, node info, position and routing acknowledgements; unsupported
application payloads are not presented as decoded text. POS/PC BLE file sharing
continues to use its own protocol. Do not assume the Meshtastic phone app can
connect to POS or that all third-party module traffic is understood.

## Validation and provenance

Baseline: official Meshtastic firmware **v2.7.26.54e0d8d**, commit
`54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`, notably RadioInterface, MeshRadio,
Channels, CryptoEngine and the generated Data/User/Position/Routing schemas:
https://github.com/meshtastic/firmware/tree/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb

POS implements that wire format independently using existing Mbed TLS. No
upstream firmware implementation or generated protobuf code is linked/copied
into the distributed firmware. Independent test vectors use the official PyPI
`meshtastic==2.7.8` generated protobufs and `cryptography==50.0.2`. Tests run
against OpenSSL and the actual firmware Mbed TLS path, with malformed data,
encryption tampering, three-node simulated relaying, storage failures, restart
and identity persistence. The same pinned SDK Mbed TLS is built natively for CI.

**Physical Meshtastic interoperability, cap RF/CAD timing, range, congestion,
regulatory behaviour, stack/heap minima and power-loss behaviour have not been
tested in the cloud.** The requested `0.7.0-stable` name/publication channel is
not a hardware certification. Before relying on it, exchange both channel and
direct messages with a current genuine Meshtastic node, test an intervening relay,
check ACK/retry behaviour and verify GPS remains private until requested.
