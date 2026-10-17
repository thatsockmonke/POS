# POS 0.6.7-dev

The full follow-up includes the updater manifest-memory fix, direct OpenStreetMap
region downloads and editable pansexual/nonbinary wallpaper presets. A fresh USB/SD install
does not require installing the bridge first.

- **Background:** P → Presets → Pansexual flag (pink/yellow/blue) or Nonbinary
  flag (yellow/white/purple/black). Edit any stripe with Enter; S saves.
- **Maps:** OpenStreetMap's official map API is available for explicit 500 m
  regions. An SD-backed node index resolves ordinary OSM references, filters
  non-roads and retains access/barrier nodes and restriction relations. Larger
  areas can use selected VK/Germany Overpass services. The failing Private.coffee
  service is removed. Downloads are bounded at 8 MiB; original OSM remains readable.
- **HTTPS:** actual HTTP status and TLS/verification/socket/heap diagnostics;
  smaller HTTP buffers and dynamic TLS record buffers, with 16-KiB receive record
  compatibility. Certificate and hostname validation remain enabled. Public
  servers can still fail; on-device TLS interoperability needs physical testing.
- **Pending full-update features included:** integer microphone gain 1–4096,
  streamed offline world overview centered on a current GPS pin, regional road
  search/offline routes/turn guidance, and removal of the old-passphrase/import UI.
  Known Networks stays encrypted. The world overview is coastline geometry,
  not a full-world street database.
- **Updater:** exact-size manifest text/file-record allocation and earlier release
  of nested history metadata. Signatures, hashes, rollback and factory recovery
  remain. Old firmware that cannot install this fix must use the matching USB
  installer and SD package. Firmware and installed SD System must match.

## Fresh installation with one USB port

Download this release's **pos-setup**, replacing the old executable. Keep the
original factory `.bin` and `.json` backup on the computer. Copy any user files
wanted from the SD before formatting it to FAT32; formatting erases its contents.

```sh
"/mnt/shared/MyFiles/Downloads/pos-setup" --release v0.6.7-dev --sd-folder "/mnt/shared/removable/POS Drive"
```

1. With the FAT32 card connected/shared with Linux, choose **2 — Prepare the
   microSD only**. Verify **0.6.7-dev** is selected, then eject using ChromeOS Files.
2. Connect Panputer, run the same command, choose **3 — Flash Cardputer only**.
   Select the earlier original factory backup and follow recovery/flash checks.
3. Insert the prepared card into the powered-off ADV, boot and choose
   **Install POS / resume**. Type **INSTALL** and keep power/card connected.
4. When prompted, power-cycle and complete first-time setup. System Information
   should show **0.6.7-dev**. Choose your wallpaper preset in Background.

USB flashing also resets internal NVS settings such as Wi-Fi credentials. You
must flash the USB installer as well as preparing the SD; copying new SD files
alone does not replace running firmware. No manual runtime flash is needed.
For a file-preserving reinstall, follow [EASY_INSTALL.md](EASY_INSTALL.md) instead
of formatting. Use the existing backup/reinstall/restore workflow.

## Validation and limits

See [BUG_REPORT_0.6.7.md](BUG_REPORT_0.6.7.md) for root causes, software tests,
real downloaded OSM data and physical checks still needed. Both firmware roles
retain the existing partition sizes, factory recovery and inactive OTA slot.
All 382 host tests pass; matching installer/runtime builds and simulated
backup/reinstall/restore pass. Bundle checks verify images, versions and hashes.
No physical Cardputer/TLS/GPS/SD or power-cut results are claimed.

This is an experimental prerelease. Meshtastic compatibility remains separate
and unfinished; POS radio messaging is not Meshtastic interoperability.
