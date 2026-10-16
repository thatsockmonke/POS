POS 0.3.8-dev adds Files, Music, BLE sharing, GPS/LoRa and Radio Range Test.

- File manager: folders, previews/editor/music opening, copy/cut/paste with a
  destination name, rename, verified staged folder copies, Trash/restore and
  permanent deletion requiring DELETE. Protected system data and existing targets
  are preserved.
- Music: SD-streamed MP3 / PCM 16-bit WAV, folder library, pause/next/previous,
  volume, shuffle and readable M3U playlists; ES8311/I2S audio output.
- Common Share interface in Files, Text Editor and Share: local copy, temporary
  Wi-Fi export or BLE transfer. BLE discovers compatible peers, requires matching
  numeric pairing and explicit offer acceptance, resumes partial chunks, verifies
  SHA-256 and handles a lost final acknowledgment without overwriting files.
- GPS/Location and Semtech SX1262 service using the published ADV cap interfaces.
  Radio messaging includes IDs, duplicate suppression, ACK/retry and verified SD
  outbox persistence. Precise-location sharing is opt-in; radio messages are unencrypted.
- Radio Range Test records normal CSV files with GPS/RSSI/SNR, packet IDs,
  transmit/receive/ACK/timeout counts and periodic position samples.
- Settings > Expansion hardware shows cap/GPS presence, fix, LoRa feature switch,
  configured frequency and separate antenna/TX consent. Radio/Range require an
  enabled detected cap. Maps has a GPS-presence gate; Maps/navigation itself remains
  future work. GPS detection and LoRa enablement are independent.
- TX defaults off after reboot, uses +10 dBm and conservative 1% airtime scheduling,
  and requires a locally permitted configured frequency plus antenna consent.
- Games and a POS terminal are removed from project scope, including the old
  recovery-terminal menu and automatic Games-folder creation.
- Keeps the pink/yellow/blue boot animation, stripe backgrounds and one foreground
  app; the launcher now has fourteen black icons outlined in white.
- Includes app controls, installation/recovery instructions, protocol documentation,
  third-party notices and compiled stack-size guards. MP3 workspace is heap-owned,
  with output checked against the unmodified upstream decoder.

Download pos-0.3.8-dev.zip and its checksum, extract it and read INSTALL.md/APPS.md.
Extract the inner pos-install.zip onto FAT32 SD; flash only pos-installer-usb.bin at
0x0. The installer validates and installs the runtime itself. Preserve factory
recovery and user data, and keep the exact matching package for repair.

Experimental prerelease: both firmware images and host behavior are checked, but
actual ADV audio, BLE pairing/throughput, cap coexistence, GPS and RF operation
still require physical validation. No hardware measurements or LoRa range are claimed.
