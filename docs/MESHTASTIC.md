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
It does not provide the Meshtastic mobile/USB client API, remote admin,
compressed-text decoding,
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

## 0.8.0-dev conversations and contacts

**Radio → Conversations / unread** groups channel broadcasts and direct messages.
Choose a message to read its full text/status, or write/send a message. Outgoing
text is retained before radio transmission. Status distinguishes queued, radio
sent, destination acknowledged, rejected and timed out; a broadcast has no
individual delivery guarantee. A restored old outbox may lack its original text
because older firmware did not retain it.

Each thread displays up to 16 messages from a bounded 64-KiB journal tail. Unread
badges count this retained tail. Leaving the displayed thread saves its read
position; older messages remain available in the complete readable
`POS/Users/Owner/Radio/meshtastic.jsonl` file. Retry duplicates do not create a
second visible entry. A truncated log is isolated before subsequent appends.

**Contacts / favourites** offers a friendly alias, favourite marker (sorted first),
an editable quick message and a conversation shortcut. Contact/read metadata uses
redundant versioned SD saves; peer cryptographic key pins remain separate.
**Notifications** selects banner + alert sound, banner only, or off. Incoming text
shows a five-second banner without stealing input. Sound still follows global
sound/alert settings and microphone/music ownership; hiding the screen does not
wake it for a notification.

Peer-shared coordinates appear as cyan diamonds on both Maps views. Hollow grey
markers are older than 15 minutes since receipt. Nearby nodes → shared coordinates
shows age and GPS-derived distance when a local GPS fix exists. Signal strength
is never converted into distance. Positions are not assumed current across boots.

**Traceroute** sends standard port-70 RouteDiscovery requests; Status shows the
latest reply's outbound/back relay IDs. POS relays append their own ID on the
appropriate path and endpoints answer requests. Paths can differ and may omit
unknown/unsupported hops; no guaranteed route or continuous ping flood is implied.

**Export channel URL** writes an official ChannelSet URL to a new text file in
Documents after explicit confirmation. It includes channel secrets. Enabled
channels are packed in order, so disabled gaps are omitted. Imports retain their
existing conservative radio-parameter checks. URL export does not publish a
private node identity key.

## Optional MQTT text bridge

**Radio → MQTT bridge** starts/stops a session, or shows its status. First connect
Wi-Fi, choose a channel, broker `mqtts://hostname:8883`, matching topic root
(e.g. `msh/US`), and any broker username/password. Public brokers can require
credentials; consult that broker's policy. The default host is a convenience,
not a guarantee of availability. Broker certificates and hostnames are checked
against the ESP-IDF CA bundle; a correct clock and sufficient TLS heap are needed.
HTTP/MQTT plaintext and disabled certificate checking are not supported.

Choose uplink-only, or explicitly allow downlink to the radio. A matching remote
Meshtastic gateway must enable encrypted MQTT on the same channel/root. The wire
format is standard binary ServiceEnvelope/MeshPacket on
`<root>/2/e/<channel-name>/<gateway-id>`. Only **channel text broadcasts** are
bridged: automatic positions, private DMs, node information and other module data
are excluded. Channel AES-CTR follows Meshtastic and is not an authenticated
cipher; a private key alone is not a guarantee against message forgery.

Credentials stay in the SDK client's RAM for this session, never plaintext on SD;
start again after reboot. Changing radio configuration stops the bridge. Buffer,
outbox and duplicate caches are bounded. QoS 0 text forwarding is best effort;
retained, fragmented, own-gateway and malformed envelopes are ignored. Generation,
channel decryption and payload checks precede inbound processing. MQTT-origin
packets are marked so they cannot loop back onto MQTT. Radio downlink continues
through the existing relay setting, TX consent gate, CAD/backoff and airtime budget.

The bridge needs live device/broker and multi-node testing. Native tests exercise
the actual event-handler/configuration code with an SDK adapter and independent
protobuf fixtures; they do not establish an on-device TLS/radio test result.

Checking software updates closes the MQTT session to free its TLS heap. Start
the bridge again afterwards if the check does not install/reboot.

## 0.8.1 telemetry

**Radio → Telemetry → Received telemetry** shows the latest eight node/type
reports, their age and available device, environment or power readings with
units. Missing values stay absent. Unknown telemetry variants are identified
without fabricating sensors. **Request device telemetry** targets a node;
**Share device telemetry once** broadcasts actual battery voltage and uptime.
POS does not claim a fuel-gauge reading or environmental sensors it lacks.

Automatic replies to direct device-metric requests are off by default; enable
explicitly in Telemetry. Responses are limited to one per minute and obey the
existing TX consent, queue, CAD and airtime gates. Telemetry never includes GPS,
never enters the text-only MQTT bridge and uses standard port 67 protobufs.
Reports are a bounded session cache; the app does not promise archival telemetry.

## 0.8.1 store-and-forward

**Radio → Store and forward → Request missed messages** selects a matching
channel and server node, then a 1–1440 minute window. Abort stops a POS server's
pending replay. Responses appear in Status and Conversations. A server must be
reachable and enabled; a quiet/lossy mesh does not guarantee retrieval.

The optional **SD history server** caches up to 128 received broadcast texts on
one explicitly selected channel. It excludes private/direct messages and texts
over 198 bytes, and returns at most 20 per request with at least five seconds
between queued replies, within the existing airtime budget. One client is served
at a time; another receives Busy. Disable stops pending history replies. Channel
identity changes disable serving until the user enables it for the new channel.

Cache slots are fixed-size, versioned, CRC checked, staged and read back before
replacement. They are derived data in `Radio/store-forward`; original messages
remain in ordinary `Radio/meshtastic.jsonl`. No known-good original is removed to
make cache space. Valid UTC enforces the requested age window (at most 24 hours);
without valid UTC only the current boot's records can be replayed. Cache files
remain when disabled, so users can deliberately remove them through Files.

Port 65 uses the official History, heartbeat and text protobuf envelope. POS
accepts upstream history text with original sender/packet headers. Its server
uses fresh server-owned packet IDs and a `!sender [original-id]: ` text prefix,
avoiding nonce reuse when turning old text into a new encrypted envelope.
POS restores attribution in its local conversation log; it never changes peer
key identities from that prefix. Other clients can see the prefix. Bounded
persistent duplicate tracking prevents repeated POS history requests from
re-adding recent messages. It is not an unlimited historical archive or adaptive
next-hop routing. Remote phone/admin APIs remain outside this release's scope.

These modules use the official meshtastic 2.7.8 protobuf schemas independently
from upstream implementation code. Tests cover wire fixtures and a simulated
service; actual over-the-air interoperability and SD timing require hardware.
