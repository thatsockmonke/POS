# Maps in POS 0.6.3-dev

This is the first Maps milestone: small offline road/path maps and straight-line
GPS waypoint guidance. Region choice, HTTPS download and map preparation all run
on Panputer. No separate map compiler, computer or account is needed.

## Use it

1. With power off, fit the official LoRa/GPS cap. Start POS and check **Settings →
   Expansion hardware → GPS detected**. Maps refuses to open without a detected
   receiver, independently of the LoRa enable/transmit switches. A satellite fix
   is only necessary for your current position and live guidance.
2. Open **Maps → Download a region**. Choose **Around my GPS position**, or
   **Enter coordinates** in decimal degrees. For example, latitude `34.0522`,
   longitude `-118.2437`. There is no address/place-name search yet.
3. Choose a square area: **500 m, 1 km, 2 km or 4 km** across. Start small in dense
   cities. Give it a name and select a public Overpass provider. The review screen
   shows the exact service URL and area. This sends that bounding box to the
   chosen service; there is no automatic background location sharing or silent
   fallback to another provider.
4. Confirm **Download region over Wi-Fi**. POS opens Wi-Fi if needed and syncs time
   when required for HTTPS. Certificate validation stays enabled. Download and
   preparation show progress; **Esc / Home** cancels safely. Public servers may
   be busy or unavailable: retry later, choose a different provider, or shrink
   the area. A successful transfer is compiled and checked before appearing in
   **Saved regions**.
5. Open **Saved regions (offline)**. **Arrow keys** pan; **+ / -** zoom. Press **C**
   to center on and follow a current GPS fix. The screen is north up. Blue marks
   your position, the yellow center crosshair selects a point, and pink marks
   the destination. **Enter → Guide to map crosshair** sets that destination.
   Distance, cardinal direction and bearing appear at the top when GPS has a fix.
   **Enter → Clear waypoint** ends guidance; **Center saved region** returns to
   the download center. **Fn+' / G0** closes Maps as with other apps.

Guidance points directly toward the destination. It does **not** calculate a road
route, obey one-way/access restrictions, avoid obstacles or give turn instructions.
The waypoint is session-only. Road names, search, terrain, route caching and a
fully offline routing graph remain future work. A blank area can mean that you
panned outside the downloaded coverage; it does not establish that no roads exist.
No destination is broadcast over LoRa or BLE.

## SD files and limits

Maps live in ordinary `POS/Maps` user storage, preserved by normal updates and
backups. Each download has a random filename pair:

- `region-XXXXXXXX.osm`: original, readable OpenStreetMap XML, including its source
  attribution. Keep this with the derived map if sharing it.
- `region-XXXXXXXX.pmap`: compact geometry for the device. The map name is stored
  inside. Files lists both; removing both through Files removes the download.
- `Map-data-license.txt`: attribution and ODbL link, written without replacing an
  existing file. Retain this notice when sharing map data.
- An interrupted `.part` is never listed as a usable map. Existing maps are never
  overwritten. A failed compile can leave the original `.osm` and a partial file
  for inspection; remove them through Files if no longer needed.

The current limits are 2 MiB of downloaded XML, 30,000 line segments, latitude
between -80 and +80 degrees, and no regions crossing the date line. Larger/dense
responses fail clearly; download a smaller area. RAM holds reusable I/O buffers
and a 2,640-byte monochrome viewport, never the whole region. The catalog is
paged eight regions at a time. Pan/zoom streams the compact file; GPS marker-only
changes reuse the cached raster. Actual SD/GPS/LCD performance requires hardware
validation. The firmware retains validated TLS 1.2 ECDHE-RSA/ECDHE-ECDSA
clients with P-256/P-384/P-521/X25519 curves. Unused TLS server support, static
RSA/ECDH suites and legacy/Brainpool curves are omitted to fit the existing
OTA slots. Unusual custom HTTPS endpoints needing those suites are unsupported.

The `PMP1` format is deliberately simple: 64-byte little-endian header (magic,
center latitude/longitude in signed microdegrees, width in meters, segment count,
CRC32, 33-byte NUL-terminated ASCII name and reserved zero bytes), followed by
16-byte records containing two signed microdegree latitude/longitude pairs.
CRC32 covers the entire file with the CRC field (bytes 20–23) zeroed. Length,
limits, coordinates, completion and CRC are checked before publication/opening.
CRC detects accidental damage; map data is not authenticated like signed firmware.
A small token parser streams the OSM response instead of constructing a DOM.

## Map data and service terms

**© OpenStreetMap contributors**, https://www.openstreetmap.org/copyright.
The original OSM data and derived map databases are available under the
**Open Database License (ODbL) 1.0**:
https://opendatacommons.org/licenses/odbl/1-0/.
Map data has this license independently of the POS source-code license.
The source XML remains available beside its derived map. Attribution is visible
in Maps and on the map screen; it must be retained when sharing map data.

Provider choices are VK Maps (`maps.mail.ru`, Russia), Private.coffee (Austria),
and Overpass API (Germany), listed by the OpenStreetMap Overpass API documentation:
https://wiki.openstreetmap.org/wiki/Overpass_API#Public_Overpass_API_instances.
They are third-party public services, not POS servers. Availability and their
terms may change. POS makes one bounded query per explicit download, with no
bulk tile scraping, retry loop or automatic region prefetch.

## Verification

Host regressions exercise streamed compilation, independent CRC verification,
no joining of separate ways, corrupt/truncated/oversized input, cancellation,
existing-file preservation, extreme-segment clipping, projection, bearing,
marker rendering, missing receiver/no-fix UI and Home cleanup. A real public
Overpass road response was downloaded in the cloud and processed by this code.
These are not physical Cardputer tests. GPS detection/fix outdoors, heap while
Wi-Fi is active, TLS interoperability, microSD speed and on-device readability
still require validation.

Local verification for this release: 332 host tests passed; both firmware roles
built with the pinned ESP-IDF. Runtime image: 1,503,767 bytes before image padding;
installer: 948,887 bytes. The unchanged OTA slot is 1,507,328 bytes. Maps' largest
reported UI stack frame is 2,640 bytes (the download frame is 2,160 bytes), below its enforced 3,072-byte guard. These
are compiler/cloud results, not measured device heap or performance. The server
query has a separate 64 MiB server-side working-memory cap; the device still
accepts at most 2 MiB of response bytes.
