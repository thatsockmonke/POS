# Offline maps and navigation — POS 0.6.9-dev

POS can search downloaded road names, calculate routes entirely on Panputer,
save/resume routes, and show GPS turn guidance without Internet. Region choice,
map download and graph compilation all run on the device. GPS receiver detection
is required to open Maps; a satellite fix is needed for live guidance, not for
browsing or planning from the saved region center.

## Pan and zoom controls (both map views)

- **Arrow keys** pan. On the ADV, use the printed arrow keys without Fn in Maps.
- **P** cycles **Normal → Large → Fine → Normal**. The value beside P is the
  movement per key press, automatically reduced as you zoom in. This preference
  is shared between world and regional views for this boot; reboot resets Normal.
- **+** or **=** zooms in; **−** zooms out. These controls already existed.
- **C** centers on a current GPS fix and resumes GPS follow. Arrow movement
  pauses follow so the view stays where you pan. Without a fix, pan/zoom still
  work offline; C waits for a fix in the world view and explains it regionally.
- **Enter** returns to the world menu or opens regional map options; **Esc**
  goes back. **0** fits the whole planet in the world overview only.

Regional Fine/Normal/Large steps are **1/16, 1/4 and 1/2** of the current map
width in meters. At a 1 km view, those are 62.5 m, 250 m and 500 m. The world
overview uses **7.5°, 30° and 60° divided by its zoom multiplier**. World steps
are angular distances, not a promise of equal ground distance at every latitude.
Normal preserves the previous movement amount. No new settings format or map
redownload is required. Pressing P updates labels without rereading the map.

## Download and use

1. Fit the official GPS/LoRa cap with power off. Check **Settings → Expansion
   hardware → GPS detected**, then open **Maps → Download a region**.
2. Choose your GPS position or decimal coordinates, a 500 m / 1 km / 2 km / 4 km
   square, and a name. Choose a provider and review the area and exact service
   URL before confirming. POS connects Wi-Fi and syncs time if necessary; HTTPS
   certificate validation stays enabled. Public services can be busy: try later,
   another selected provider, or a smaller region. There is no silent fallback
   or background location upload.
   **OpenStreetMap (worldwide)** uses the official OSM map API for one explicitly
   selected square, from 500 m through 4 km. VK and Overpass API also cover the
   world; provider labels describe coverage rather than server location. The consistently failing Private.coffee endpoint has been removed.
   Direct OSM responses include buildings and other objects: POS builds a
   sorted node index on SD, resolves road references, retains access/barrier
   nodes and restriction relations, and filters non-road ways/relations.
   It does not bulk-download the editing API or fetch regions in the background.
3. POS saves the original XML, a compact road map, and a routing graph after
   checking them. Allow at least 256 MiB free for larger downloads, normalized XML, temporary sort
   runs and route calculation; dense areas can require more. Files stream through reusable buffers.
4. Open **Saved regions (offline)**. Arrows pan, **+/-** zoom, **C** follows GPS.
   Blue is your current position; pink is the selected destination. The screen
   is north up. **Enter** opens map options.
5. Set **Travel mode: Walking / Cycling / Driving** before searching or calculating.
   **Search road name** accepts part of a name, case-insensitively, and shows up to
   eight distinct nearby matches usable in that mode. Choose a result, then
   **Route to this road**, or show it on the map first. Refine broad searches to
   find other matches. Text currently uses the display's ASCII font; unsupported
   characters appear as `?`. This is road-name search, not an address/geocoder.
6. Alternatively, pan the yellow crosshair to a destination and choose **Route to
   map crosshair**. The start is your current GPS fix, or the saved region center
   if no fix is available. Review the selected travel mode and confirm calculation.
   Start/end positions must be within 75 m of an eligible road node. The result
   ends at that snapped road point; it is not a promised route to a building door.
7. The yellow line shows the calculated route. A large arrow and text show the
   next maneuver, distance to it, next road name and remaining route distance.
   With no fix, the route remains available for offline viewing. If GPS moves
   more than 40 m away, POS shows **Off route**; **Enter → Recalculate route**
   calculates again from a current fix. It does not silently reroute or invent a
   connector across missing data. Arrival means within 20 m of the cached route
   end with less than 15 m remaining along the route.
8. **Resume saved route** restores the newest cached route for this exact map,
   including after reboot, without Wi-Fi. **Clear guidance** stops it. Files can
   remove unwanted caches. **Straight-line guide** is retained as an explicit
   separate option. Close Maps with **Fn+' / G0** as with other apps.

**Existing 0.6.3 maps remain viewable. Download the region again after updating**
to add routing data: the earlier downloads did not request restriction relations
or tagged access/barrier nodes. POS does not silently treat an incomplete old
cache as a routing graph. Old maps and user data are preserved.

## Delete a downloaded region

Open **Saved regions → select the region → Enter → Delete region**. The
confirmation names the region and counts its files; **Keep region** is selected
by default. Confirm **Delete … files to Trash** to remove it from the catalog.

Deletion includes this region's `.pmap`, matching original/normalized `.osm`,
`.pnav` graph and `.route-XXXXXXXX` saved routes. It does not remove other maps,
attribution, unknown temporary workspaces, similarly named notes or the bundled
world map. Protected paths/symlinks are refused before anything moves.

Files go to **Files → Trash**, with their original paths recorded. Restore the
map and its companions there to use it again. Trash still occupies SD space;
delete entries permanently through Files to reclaim that space. No permanent
erasure happens from Maps. If an operation stops, moved files remain in Trash
and remaining files stay in Maps. The catalog file moves last, so an interrupted
region can still be opened or deleted again. Active route handles are closed
before their files move.

## Routing rules and scope

The graph connects roads using **OSM node IDs**, never coordinate coincidence.
A bridge crossing a road without a shared node therefore does not become a turn.
Shortest-distance A* uses directed edges, independent walking/cycling/driving
permissions, one-way and reverse-one-way rules, roundabout direction, bicycle
exceptions, mode-specific/directional access and tagged node barriers. More
specific mode permissions take priority independently of XML tag ordering.
Generic `access=yes` does not make motorways walkable or footways driveable.

Node-via `no_*` / `only_*` turn restrictions, profile-specific restrictions and
exceptions are evaluated using the incoming edge. Same-way no-U-turn restrictions
do not forbid continuing forward. Unsupported via-way or conditional restrictions
conservatively block the affected way/profile. Conditional access/one-way rules
are blocked rather than guessing whether their conditions apply. Unsupported
ambiguous multi-from/to restrictions can refuse graph preparation. Unknown road
classes, private/destination-only access, construction/proposed ways and area
polygons are excluded conservatively unless supported explicit mode permissions
apply. This can yield **No route** even where a person with local knowledge could
legally travel. Different downloaded regions are not stitched together.

Routes are limited to the downloaded graph and its available OSM data. A larger
area may be needed to connect two nearby points; disconnected roads do not get
fabricated links. Turn prompts use road changes and geometry; they do not include
lane guidance, spoken directions or roundabout exit counts. Driving means an
ordinary motorcar, without vehicle-height/weight or specialist-vehicle routing.
Road signs, closures and real conditions take priority over cached map data.
These are regional offline routes, not continent-scale navigation.

## Storage, memory and interruption safety

All map data lives in ordinary **POS/Maps** user storage and survives normal
updates, backups and firmware rollback:

- `region-XXXXXXXX.osm`: original readable OpenStreetMap XML, including attribution.
- `region-XXXXXXXX-roads.osm`: direct-OSM import with resolved coordinates,
  road ways and restriction relations. The original download remains untouched.
- `region-XXXXXXXX.pmap`: `PMP1` road geometry, still readable by 0.6.3 firmware.
- `region-XXXXXXXX.pmap.pnav`: `PNV1` node/edge/name/restriction graph bound to that
  exact `.pmap` CRC. Compilation uses 256-record sort batches and sequential
  endpoint joins; the region graph never needs to fit in RAM.
- `region-XXXXXXXX.pmap.route-XXXXXXXX`: a completed `PRT1` route bound to the graph
  CRC. New routes have new names and never overwrite an existing known-good one.
- `Map-data-license.txt`: attribution and ODbL URL; keep this and the source XML
  when sharing derived map data.

Downloads remain capped at 64 MiB XML / 30,000 geometry segments, latitude between
-80 and +80 degrees, and no date-line crossing. The catalog is paged. Road and
route viewports each use 2,640 bytes. A* labels and an indexed priority queue live
on SD with two 4-KiB RAM cache pages per mutable table; read-only graph tables use
five 1-KiB cache pages. RAM remains bounded as graph/route size grows. Actual
latency and available heap depend on the physical card and networking state.

Direct-OSM import accepts up to 60,000 node definitions/references and uses
256-record external-sort batches plus on-SD binary lookup; no whole-region
dictionary is allocated. Conflicting/duplicate IDs, missing references, invalid
coordinates, incomplete XML and existing destinations are refused. Cancellation
removes only its own work directory and preserves originals and completed maps.
Dense regions of any selected size may exceed these limits; choose another area or an
Overpass service. These public services have no availability guarantee.

HTTP failures display the actual status (including rate limiting/server errors).
TLS failures display verification flags, socket/TLS codes and available/largest
heap; USB logs retain the same diagnostics. Check UTC time first. HTTP buffers
are smaller and ESP-IDF dynamic TLS record buffers are enabled, retaining 16-KiB
receive records and certificate/hostname validation. Desktop HTTPS and real OSM
compilation have been tested; ESP32 certificate/TLS heap behavior still needs
hardware verification. No certificate-validation bypass is provided.

Graph and route files include counts, version magic, length checks and whole-file
CRC32. They are synced and checked before new-file publication, then checked when
opened. Corrupt, incomplete and wrong-map routes are rejected. Cancellation or
SD failure preserves previously completed files. Temporary work directories are
exclusively created; existing work directories are never erased by a retry. A
power cut can leave `.part` / `.work-*` files; these are not usable route caches
and can be removed through Files after verifying the completed files. Do not
remove original data or your only backup to make space.

`PMP1` has a 64-byte little-endian header: center latitude/longitude in signed
microdegrees at bytes 4/8, width at 12, segment count at 16, CRC at 20 and a
33-byte NUL-terminated ASCII name at 24. Its 16-byte records contain endpoint
latitude/longitude pairs in signed microdegrees. `PNV1` has a 64-byte header: map CRC at byte 4; node/edge/way/rule counts
at 8/12/16/20; format flag 1 at 28; whole-file CRC at 32 (zeroed for calculation).
Tables follow: 24-byte sorted nodes (OSM ID, microdegree coordinates, first edge,
mode mask and 16-bit edge count); 20-byte directed edges (from/to/way indices,
centimeter length, mode mask); 80-byte roads (OSM ID, 64-byte ASCII name, representative
coordinates); and 32-byte restrictions (from/to/via OSM IDs, modes and rule flags).
`PRT1` has a 64-byte header: graph CRC, point count, total centimeters, mode and
whole-file CRC at bytes 4/8/12/16/20. Its 80-byte points contain microdegree
coordinates, cumulative centimeters, node/way indices, a reserved word and a
56-byte outgoing-road name. CRC calculation zeroes its own field. These are data
formats, not executable app packages; the original XML remains recoverable
without POS. CRC protects against accidental damage, not malicious map providers.

## Data license, providers and validation

**© OpenStreetMap contributors**, https://www.openstreetmap.org/copyright.
Original and derived map databases use **ODbL 1.0**:
https://opendatacommons.org/licenses/odbl/1-0/. Retain attribution and source XML
when sharing them. Map-data licensing is independent of POS source-code licensing.
No road database is bundled in firmware; the world coastlines below ship on SD.
Regional-map previews use generated example state.

Providers: OpenStreetMap's official `api.openstreetmap.org` map endpoint (explicit
500 m through 4 km squares), VK Maps (`maps.mail.ru`, Russia) and Overpass API (Germany).
The Overpass instances are listed at https://wiki.openstreetmap.org/wiki/Overpass_API.
A download sends the selected bounding box to the explicitly chosen provider.
Overpass queries request road geometry, restriction relations and tagged highway
nodes with a 64 MiB **server-side** working-memory cap. Direct OSM downloads
include other objects that are filtered on SD. The device accepts at most
64 MiB of response bytes. OSM can reject dense areas under its own node limit
before download; no automatic subdivision, background crawl or larger memory
allocation bypasses those limits. There is no bulk tile scraping or automatic retry loop.

Host tests cover an independent shortest-path reference, multiple external-sort
runs and routing cache pages, restrictions/access/one-way/barrier edge cases,
corruption, wrong-map binding, cancellation, pre-existing scratch preservation,
SD exhaustion, and the actual Maps search/plan/resume/Home UI. Real public OSM
data was downloaded, compiled, searched and routed by the same portable engine.
Both firmware roles and stack-frame guards are checked before release. POS joins
existing Wi-Fi networks; unused SDK SoftAP support is omitted to preserve the
existing factory/two-slot OTA layout. No exposed POS hotspot feature was removed.

No physical Cardputer tests are claimed. GPS matching on real streets, ambiguous
intersections, actual signs, SD performance, heap with radios active, TLS and
turn-arrow readability require hardware validation.

## Offline whole-planet overview (0.6.5-dev)

Choose **World map**, highlighted when Maps opens. North is up. A fresh GPS fix
centers the camera and supplies a pink location pin. Without a fix, 0,0 is the
initial map center and no location is asserted. Arrows pan (suspending follow),
+/− zoom, C resumes GPS follow, 0 fits the entire planet around the current
camera, and Enter/Escape returns to the regional tools. No network request or
location broadcast is made by this view. Dateline wrapping and both poles are
supported; small unused margins let the entire globe fit while keeping your
position centered. This is an equirectangular coastline overview, not road detail
or global route coverage. For those, download regional OSM maps as before.

`POS/System/Assets/world.map` is a 39,612-byte streamed **PWM1** asset: magic,
little-endian uint32 segment count, CRC32 of records, then pairs of signed
little-endian 0.01-degree lon/lat coordinates (8 bytes/segment). Maximum 20,000
segments; malformed bounds, missing/truncated/trailing records and bad CRCs are
rejected. A fixed 512-byte read buffer and a 2,640-byte raster keep it bounded.
The signed installer/update manifest also authenticates this asset.

Source: **Natural Earth 1:110m land**, public domain. Terms:
https://www.naturalearthdata.com/about/terms-of-use/ . Pinned source:
https://github.com/nvkelso/natural-earth-vector/blob/ca96624a56bd078437bca8184e78163e5039ad19/geojson/ne_110m_land.geojson

Rebuild with `python tools/build_world_map.py ne_110m_land.geojson world.map`.
The compiler verifies its pinned SHA-256 before generating the asset.
Full provenance travels in `System/Assets/world-source.txt`.

## Saved-area outline

A thin white square marks the selected region's geographic bounds in Saved
regions. It moves with pan and scales with zoom, independent of road lines.
Zoom out with **-** to see the full square: at the default zoom the screen is
wider than it is tall, so the square's north/south edges can be off-screen.
The border is never clamped to the display when the actual edge lies elsewhere.
OSM ways crossing the selection can include geometry beyond the square; it marks
the requested coverage, not a guarantee that every outside road is unavailable.
The world coastline overview has no regional boundary.
