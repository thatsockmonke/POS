# POS 0.8.0-dev

Adds Meshtastic conversations, contacts, notifications, shared-node map markers,
traceroute, channel export and an optional TLS MQTT text bridge. Calculator gains
graphing, checked exact fractions, linear systems, matrices/vectors, units,
programmer mode, persistent history and named worksheets that open from Files.
Wallpaper gains 12 common stripe-only pride presets and hex stripe entry; the
startup status accent follows the saved wallpaper. Stripe fills are 40% faster
(240 ms per stripe), retaining the halfway stagger, logo fade and one-second hold.

## Use the additions

- **Background → P:** Pansexual, Nonbinary, Rainbow, Lesbian, Gay men, Bisexual,
  Transgender, Asexual, Aromantic, Genderqueer, Genderfluid and Agender.
  **H** enters `#RRGGBB` for the selected stripe, also from the colour picker.
  **S** saves; Escape cancels the draft. Stripes remain independently editable.
- **Radio → Conversations / unread:** channel and direct threads with message
  statuses; **Contacts / favourites:** aliases, favourites and quick messages.
  **Notifications:** banner + sound, banner only or off. Alert sounds obey the
  existing global sound settings and audio-resource ownership.
- **Radio → Traceroute:** choose a node, then check Status for its reply's paths.
  **Export channel URL:** writes a new Documents text file after confirmation.
  Shared peer locations appear as map diamonds; node details show age/distance.
- **Radio → MQTT bridge:** opt-in TLS broker session, one matching channel/root,
  text uplink or explicit text downlink. Credentials are RAM-only. GPS, private
  DMs and other modules are excluded. See [MESHTASTIC.md](MESHTASTIC.md) for setup,
  interoperability limits and bounded thread/unread history.
- **Calculator → Tab → More modes / worksheets:** all new tools. Example system:
  `2*x+y=3;x-y=0`. Matrix: `1,2;3,4`. Exact: `0.1+0.2` → `3/10`.
  Graph the current expression with arrow pan, +/- zoom and 0 reset. Named `.calc`
  worksheets open from Files. See [CALCULATOR.md](CALCULATOR.md) for precise
  supported syntax, limits and persistent-history behavior.

## Update / install

Existing POS: **Settings → System → Software Update → Check for Updates**.
The signed package updates firmware and matching System resources together.
**No SD reformat, map redownload, repartition or forced clean installation is
required.** Keep ordinary backups. New devices can use the matching `pos-setup`
from this release; [EASY_INSTALL.md](EASY_INSTALL.md) has the three-step menu.

The independently bootable factory installer and both original OTA slots are
retained. Unused WebSocket transports and the unused TLS digital-signature
hardware-key path are excluded; ordinary verified TLS, key algorithms, panic
handling and rollback remain. Longer app help paragraphs are verified SD assets,
loaded on demand. User files and existing settings remain recoverable normally.

## Validation

This is a development prerelease. Native regression tests, independent official
Meshtastic protobuf fixtures, both crypto backends, the real storage/service
code, UI event scripts, SDK-adapter MQTT event tests and both ESP32-S3 firmware
builds are checked. See [BUG_REPORT_0.8.0.md](BUG_REPORT_0.8.0.md).

**No physical Cardputer, TLS broker session on the device, multi-node RF/MQTT
bridge, GPS, sound/readability, measured heap/stack or power-cut tests were
performed in cloud.** The CAS is bounded: exact arithmetic is checked 64-bit,
polynomials reach degree 8, higher-degree solving returns numeric real roots,
and systems/matrices have at most four unknowns/rows/columns. It is not a claim of
unrestricted symbolic algebra or complete upstream Meshtastic module coverage.
