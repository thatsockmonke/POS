# Offline maps and navigation — POS 0.6.6-dev

POS can search downloaded road names, calculate routes entirely on Panputer,
save/resume routes, and show GPS turn guidance without Internet. Region choice,
map download and graph compilation all run on the device. GPS receiver detection
is required to open Maps; a satellite fix is needed for live guidance, not for
browsing or planning from the saved region center.

## Download and use

1. Fit the official GPS/LoRa cap with power off. Check **Settings → Expansion
   hardware → GPS detected**, then open **Maps → Download a region**.
2. Choose your GPS position or decimal coordinates, a 500 m / 1 km / 2 km / 4 km
   square, and a name. Choose a provider and review the area and exact service
   URL before confirming. POS connects Wi-Fi and syncs time if necessary; HTTPS
   certificate validation stays enabled. Public services can be busy: try later,
   another selected provider, or a smaller region. There is no silent fallback
   or background location upload.
3. POS saves the original XML, a compact road map, and a routing graph after
   checking them. Keep roughly 32 MiB free for larger downloads, temporary sort
   runs and route calculation. Files stream through reusable buffers.
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
- `region-XXXXXXXX.pmap`: `PMP1` road geometry, still readable by 0.6.3 firmware.
- `region-XXXXXXXX.pmap.pnav`: `PNV1` node/edge/name/restriction graph bound to that
  exact `.pmap` CRC. Compilation uses 256-record sort batches and sequential
  endpoint joins; the region graph never needs to fit in RAM.
- `region-XXXXXXXX.pmap.route-XXXXXXXX`: a completed `PRT1` route bound to the graph
  CRC. New routes have new names and never overwrite an existing known-good one.
- `Map-data-license.txt`: attribution and ODbL URL; keep this and the source XML
  when sharing derived map data.

Downloads remain capped at 2 MiB XML / 30,000 geometry segments, latitude between
-80 and +80 degrees, and no date-line crossing. The catalog is paged. Road and
route viewports each use 2,640 bytes. A* labels and an indexed priority queue live
on SD with two 4-KiB RAM cache pages per mutable table; read-only graph tables use
five 1-KiB cache pages. RAM remains bounded as graph/route size grows. Actual
latency and available heap depend on the physical card and networking state.

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
No third-party map dataset is bundled in the firmware; previews use generated
example state.

Providers: VK Maps (`maps.mail.ru`, Russia), Private.coffee (Austria) and Overpass
API (Germany), listed in https://wiki.openstreetmap.org/wiki/Overpass_API.
A download sends the selected bounding box to the explicitly chosen provider.
Queries request road geometry, restriction relations and tagged highway nodes,
with a 64 MiB **server-side** working-memory cap; the device accepts at most
2 MiB of response bytes. There is no bulk tile scraping or automatic retry loop.

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
