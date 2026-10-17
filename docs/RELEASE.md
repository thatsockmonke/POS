POS 0.6.4-dev — offline road search, route calculation and turn guidance.

- Maps → Enter → Travel mode: Walking / Cycling / Driving. Search part of a road
  name offline, choose a result, or route to the map crosshair. Routes calculate
  on Panputer from a current GPS fix, or the region center for no-fix planning.
- SD-backed graph and A*: directed roads, OSM node-ID connectivity, profile access,
  barriers, one-way/reverse-one-way rules, bicycle exceptions, and incoming-edge
  node-via no/only turn restrictions. Unsupported conditional/via-way rules block
  the affected way/profile rather than guessing permission.
- Yellow route overlay, large maneuver arrow, next road name, distance to the
  maneuver and remaining distance. Fresh GPS matching reports off-route/arrival;
  explicit Recalculate uses the current fix. Straight-line guidance stays separate.
- New route caches never replace a known-good route. Resume saved route works
  after reboot without Wi-Fi. Whole-file CRCs and map/graph binding reject corrupt,
  incomplete or mismatched data. Cancellation/SD failure preserve completed files.
- Bounded external sorting and sequential endpoint joins build graphs on SD;
  indexed A* heap/labels use SD pages instead of loading a region into RAM.
- Unused SDK Wi-Fi hotspot support is excluded. Existing station networking,
  sharing, validated HTTPS, factory recovery and both OTA slots remain.

Update normally from 0.6.3: Settings → System → Software Update → Check for Updates.
No SD format, repartition or USB reflash. Keep the card inserted through reboot.
Download regions again afterward: 0.6.3 did not request restriction/barrier data.
Existing road maps remain viewable. Older-than-0.6.0 installations must pass
through the 0.6.0 transition first. Signed update metadata remains seven fields.

Scope: routing stays inside the cached regional graph; endpoints snap to eligible
road nodes within 75m. No road/address geocoder, cross-region stitching, lane/exit
counts or special-vehicle constraints. Road names use the ASCII display font.
OSM data is © OpenStreetMap contributors / ODbL 1.0. See docs/MAPS.md for controls,
formats, unsupported-rule behavior, provider privacy and full limits.

Validation: 361 host tests passed, including route/search/turn/UI regressions and an independent shortest-
path comparison, multi-page routing state, corruption, cancellation, pre-existing
scratch preservation and SD exhaustion. A real public OSM query was compiled,
searched and routed by this engine. Both pinned ESP-IDF firmware roles and stack
frame guards are checked before publication. No physical Cardputer tests claimed:
real GPS matching, SD latency, radio/heap coexistence, TLS and readability need
hardware verification. User data and existing signed-update/history checks remain.
