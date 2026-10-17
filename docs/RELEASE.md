POS 0.6.3-dev — first on-device offline Maps milestone.

- New GPS-gated Maps app, with a matching 32×32 launcher icon.
- Choose a region around your current GPS fix or enter decimal coordinates;
  select a 500 m / 1 km / 2 km / 4 km square and a public Overpass provider.
  Review the service/area, then download directly over Wi-Fi. No computer needed.
- Original OSM XML and a verified compact map remain in POS/Maps. Bounded streaming
  compilation, completion checks, CRC, new-file publication and cancellation keep
  incomplete maps out of the catalog and preserve existing maps.
- Saved regions work offline: arrow-key pan, +/- zoom, C for GPS follow, Enter
  to guide toward the center crosshair. Blue = GPS, pink = waypoint. North up;
  distance/cardinal direction/bearing use a current fix. No location broadcast.
- Paged catalog and a 2,640-byte raster keep map size out of RAM. Unused TLS server
  support and unused legacy/static TLS suites/curve tables are removed to keep
  the existing factory/two-slot OTA layout. Validated
  HTTPS clients, encrypted saved networks and explicit LAN HTTP sharing remain.
- Source/derived map data: © OpenStreetMap contributors, ODbL 1.0. Attribution,
  provider choice, controls and limits: docs/MAPS.md (also inside the install ZIP).

This starts the roadmap item. It does not implement turn-by-turn routing, routing
restrictions, road-name/address search, terrain, cached routes or a routing graph.
Waypoints are session-only; straight-line guidance may cross impassable terrain.
Downloads are capped at 2 MiB XML / 30,000 segments. Public services can be busy;
try later, a different selected provider, or a smaller region. No silent fallback.

From 0.6.2-dev: Settings → System → Software Update → Check for Updates.
Keep the SD inserted; no format or USB reinstall needed. If older than 0.6.0-dev,
upgrade through 0.6.0-dev first. Signed update metadata remains seven fields.

Validation: 332 host tests passed and both ESP-IDF firmware roles built locally
within their unchanged flash partitions. Maps has enforced stack-frame guards;
the largest Maps UI frame is 2,640 bytes. The tag workflow repeats builds and
tests. A live public Overpass road download was compiled and rendered by
the actual portable map engine. No physical Cardputer testing is claimed: GPS
outdoors, heap with networking, device TLS, SD speed and map readability need
hardware verification. Existing audio/editor/update regressions remain covered.
