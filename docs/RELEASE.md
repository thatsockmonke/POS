# POS 0.6.10-dev

Adds a simple Linux PC file-transfer tool with Bluetooth and Wi-Fi, explicit
PC guidance in Share, white saved-map boundaries, and OpenStreetMap downloads
up to 4 km. Existing settings, maps and partition layout remain compatible.

## POS Transfer

Download **pos-transfer** from this release and run:

```sh
python3 ~/Downloads/pos-transfer
```

Choose **Send to Cardputer** or **Receive from Cardputer**, then **Wi-Fi** or
**Bluetooth**. Wi-Fi needs no Python helpers; use the temporary IP/session key
shown on POS. Bluetooth offers a one-time private helper installation and uses
numeric-comparison pairing. Transfers stream and verify SHA-256 before completion;
existing files are protected. Bluetooth supports same-file resume; Wi-Fi retries
start fresh. Received Linux files go into `~/Downloads/POS` by default.

The same tool is inside the USB/SD bundle. Share labels and help now acknowledge
POS/PC transfers; Bluetooth sender help explains Linux Receive mode, and Wi-Fi
screens mention POS Transfer alongside browser use. Wi-Fi exports preserve the
original filename for the new PC client. See [TRANSFER.md](TRANSFER.md).

Wi-Fi is local HTTP with a temporary key, for trusted LANs. Crostini usually can
use Wi-Fi but often has no Bluetooth access. Bluetooth receiving requires a BLE
peripheral-capable Linux adapter. No phone OBEX or Bluetooth Classic support is
claimed.

## Maps

All providers accept explicit **500 m, 1 km, 2 km and 4 km squares**. Direct OSM
uses one bounded API request; dense areas can still exceed the service's node
limit or POS geometry limits. XML downloads remain on SD, capped at 64 MiB;
node references use on-SD indexing/external sorting, and viewports use 2,640-byte
raster buffers. No whole map is loaded into RAM. No automatic bulk subdivision
or background location request is added. Provider labels clarify worldwide coverage.

A white square outlines each saved region in its geographic location. It pans
and zooms with the map; zoom out to see all four edges on the short screen.
Off-screen edges are not incorrectly pinned to the display border.

## Updates and testing

Use **Settings → System → Software Update**. No SD format or map redownload is
required. The PC tool is downloaded separately; device OTA cannot replace your
computer's files. Signed-update verification, factory recovery and two safe OTA
slots remain unchanged.

Host tests cover both transfer directions, partial resume/duplicate chunks,
hashes, cancellation, malformed offers, symlinks, existing-file/publication races,
Wi-Fi upload/download/authentication/hash/truncation/redirect protection,
map query sizes and boundary rendering. A real private D-Bus exercises the Linux
GATT/agent/advertisement interfaces against simulated BlueZ, including cleanup.
Both firmware roles and release bundles are checked before publication.
**Physical Linux/Cardputer BLE pairing, adapter/Crostini access, Wi-Fi/SD throughput,
GPS map placement and interrupted hardware transfers remain unverified in cloud.**
