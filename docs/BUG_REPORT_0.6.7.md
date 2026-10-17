# POS 0.6.7-dev investigation and verification

## Confirmed issues and changes

| Issue | Cause | Fix and software evidence |
| --- | --- | --- |
| Old updater reports insufficient manifest heap | 0.6.4 allocated 32 KiB for every manifest plus 64 file records and retained nested JSON/history data | Carries the tested 0.6.6 exact-size reader/record allocation and earlier release of history metadata. Heap-budget and allocation-failure tests remain. The old binary cannot execute its fix before installing it; USB/SD reinstall is the chosen escape path. |
| Public map-provider failures | Fresh desktop probes returned VK HTTP 504, Private.coffee HTTP 500 and a Germany timeout | Removed Private.coffee and added the official OSM map API for explicit 500 m regions. Larger Overpass regions remain available by explicit selection; no silent provider/location forwarding. Services have no uptime guarantee. |
| Direct OSM could not be compiled by the old path | Its way references lack inline coordinates; it also contains buildings and unrelated relations | Two-pass import builds/sorts a bounded on-SD node index, resolves 64-bit IDs and emits road ways, tagged nodes and restriction relations for both existing compilers. Tests cover unsorted input across multiple sort runs, missing/duplicate IDs, malformed/truncated/trailing data, road filtering, barriers, turn restrictions, cancellation and collisions. Originals remain unchanged. |
| Real direct-OSM data exceeded the old download cap | A real 500 m Los Angeles sample was 2,277,970 bytes, larger than 2 MiB | Raised the hard XML limit to 8 MiB, retained geometry/node/reference limits and updated UI/storage guidance. Direct API requests stay at 500 m; no background or bulk downloads. |
| All HTTPS failures looked like certificate errors | Generic messages hid server status, TLS verification flags, socket failures and heap fragmentation | Report actual HTTP status and TLS/socket/verification/heap values, plus USB log diagnostics. Keep certificate/hostname validation. Reduce HTTP buffer to 1 KiB and enable pinned ESP-IDF dynamic TLS buffers with full 16-KiB receive records. Physical TLS/heap checks remain necessary. |
| Responses without a length could be treated as empty | ESP-IDF returns zero header length for a response without Content-Length | Treat zero/unknown lengths as streamed-until-complete, still bounded and integrity checked. Header-fetch errors stop rather than masquerading as body data. |
| Quoted greater-than signs could split map XML tags | Geometry tokenizer ended tags at every `>` | Honor XML attribute quote state; a regression fixture with a quoted `>` preserves geometry. |
| Wallpaper presets were incomplete | Only an immediate pan reset existed | P opens named pansexual/nonbinary presets; drafts, cancellation, exact order/colors and saved settings round trips are checked. Both presets remain fully editable. |
| Installer instructions still required Fn for menu arrows | Host text lagged behind contextual navigation | Corrected wizard hints and live-canvas background instructions; existing-system users are directed to Back up & reinstall. |

## Software validation

Host tests include sanitizers where supported. Both ESP-IDF 5.5.5 firmware roles
are built with existing frame-size/stack guards and unchanged OTA partitions.
All **382 host tests pass**. Local padded images are **902,416 bytes** for the
installer (1,048,576-byte partition) and **1,496,144 bytes** for runtime
(1,507,328-byte partition). A simulated 0.6.4 → 0.6.7 backup/reinstall/restore
preserved documents, recordings, music, maps and preferences, validated new
System hashes, and retained the original tree and known-good ZIP. Flash and
boot in that check are simulated; no physical power-loss result is inferred.
The release bundle's outer/inner checksums, image descriptors, matching versions
and payload files are verified before publication. Signed OTA publication uses
the existing CI secret and pinned P-256 public key; unsigned updates are refused.

A live desktop HTTPS request to api.openstreetmap.org fetched the 500 m sample
above. The actual C importer compiled it into road geometry and a routing graph
while preserving the original XML. This establishes real provider-format handling
and desktop TLS access, not ESP32 TLS availability or routing correctness for all
OSM tags/regions.

## Physical verification still needed

- Real ADV HTTPS handshake/clock/trust behavior and minimum/largest heap with Wi-Fi.
- SD import/graph/routing speed, recording throughput and numeric gain/clipping.
- GPS pin centering/follow and real-world turn guidance; maps must respect signs.
- Wallpaper colors/readability, keyboard feel and display performance.
- Fresh USB install, firmware/System matching, healthy-boot OTA confirmation,
  rollback, card removal and power loss at transaction boundaries.

No physical hardware tests were performed in the cloud. The source/master
specification is not fully implemented. Provider availability can change; routing
is bounded regional guidance with the restrictions documented in MAPS.md.
