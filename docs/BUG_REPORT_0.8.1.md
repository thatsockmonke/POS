# POS 0.8.1 verification and limits

## Changes and safeguards

- Files gains checkbox selection, keyboard clipboard and recursive name/content
  search. Preflight prevents a later collision from moving an earlier source.
  Copies use existing staged CRC verification; moves use rename. A partial batch
  is reported and keeps only uncompleted cut sources in its clipboard. Tests
  exercise collisions, protected/nested selections, copy/move data and matches
  spanning read boundaries; symlinks and system content remain excluded.
- Image viewing streams baseline JPEG through the ESP32-S3 ROM decoder and BMP
  through 1-KiB blocks into one RGB565 viewport. Dimensions/format/file extent
  are checked before display. Host fixtures verify row order, BGR conversion,
  fit/zoom mapping, truncated data and hostile dimensions. PNG/progressive JPEG
  and other unsupported formats are explicitly documented, not silently decoded.
- Recorder edits scan small PCM blocks, normalize only the selected interval and
  always create a new WAV. Reopening the generated WAV and comparing transformed
  samples catches malformed/truncated writes before publication. Tests include
  -32768, silence, stereo, exact frame trims, occupied names and invalid intervals.
  Original files are unchanged; partial files are never presented as completed.
- Graphs gain three curves and bounded root/intersection/extremum/integral tools.
  A sign change alone cannot certify a zero at a pole; a small residual is also
  required. Integral convergence/domain and interval checks reject unreliable
  results. Analytic fixtures cover roots, intersections, extrema, sine integrals,
  singularities and bad domains. Numerical methods retain documented limits.
- Telemetry accepts official device/environment/power protobufs, checks types,
  finite floats and oneof consistency, and distinguishes absent from zero values.
  Actual voltage/uptime replies require opt-in, are rate limited and contain no
  location. An official empty device-metric request is handled, with no need for
  an invented sensor or a new LoRa owner.
- Store-and-forward uses standard port-65 requests/history/text. Derived SD cache
  slots are bounded, checksummed and verified before replacement; private DMs
  are excluded. Replies use fresh server packet IDs/nonces and carry original
  attribution in text, avoiding CTR nonce reuse across different plaintexts.
  Scope fingerprints prevent old channel history crossing a changed identity.
  Replay has queue backpressure, age windows, duplicate tracking, abort and
  explicit server consent. Tests cover disk retention/expiry/corruption scope,
  official protobuf fixtures, a simulated service, reply consent and attribution.
- Confirmed raster cleanup issue: the ordinary message helper skips rendering
  after global Home. Using it before freeing borrowed image/graph/waveform
  pixels could leave the LCD's cached view with a dangling pointer. These exits
  now unconditionally submit a view without borrowed pixels before freeing
  storage, matching Maps' existing cleanup. UI regressions press Home from a
  displayed image and waveform and assert the display no longer borrows pixels.
- The tiny-image regression also caught a fit/zoom mismatch: a 2x2 image's
  automatic fit exceeded the renderer's 32x zoom ceiling. Fit now uses that same
  ceiling, so tiny images display normally instead of reporting an invalid view.
- GitHub's fortified native build rejected an ignored `fgets` result in the
  history duplicate-log tail reader. The result is now checked and a failed
  tail read stops processing; the native service test build explicitly enables
  `_FORTIFY_SOURCE=3` to reproduce that check locally.
- Files shortcut handling bounds ASCII keys before calling character functions;
  special Ctrl+key events cannot be mistaken for the end of a shortcut string.
  Image/raster callers submit a view without the borrowed pixels before freeing
  storage. Editing/image loads hold and release audio cues on all exit paths.

- Confirmed existing Radio Settings Back bug: the 10-item menu checked index 15
  for Back, so choosing its actual Back item (index 9) looped back into settings.
  The exit index is corrected and a UI regression verifies return without a
  configuration write. New menu entries keep Back last.

## Flash and memory

The existing factory recovery and two OTA slots are retained. GCC 14.2 optimizes
POS-owned C translation units with size optimization and link-time optimization;
prebuilt hardware and SDK libraries retain their ABI/build settings. Both roles
must pass the ordinary ESP-IDF image-size checks. No flash-partition migration,
security-check removal or boot-from-SD assumption is introduced.

Unused enterprise Wi-Fi enrollment support is excluded (POS has no EAP identity
or enrollment API). BLE warnings/errors remain, verbose BLE informational logs
are omitted. The SDK-wide symbolic error-name table is omitted; contextual POS
errors, numeric SDK error codes, assertions, panic snapshots and backtraces
remain. HTTPS certificate validation, crypto curves and signed updates remain.

Memory is bounded independently of source file size. Files has a 32-item
selection/clipboard and 64-result search cap. Graph rasters use 2640 bytes;
images use a 42,240-byte viewport plus small decode/I/O buffers. An image may be
refused when other resident services leave insufficient contiguous heap. JPEG
pan/zoom decodes again, so CPU and SD speed affect latency. There is no claim of
unlimited image formats, dimensions, recordings, mesh history or graph accuracy.

## Verification

Host regression tests use undefined-behavior sanitization where supported,
independent WAV/BMP/protobuf fixtures, actual portable service code and mocked
flash/device APIs. Firmware builds, packaging/checksums, backup/reinstall/restore,
BLE D-Bus integration and the existing signed-release workflow are checked.
Local verification: 503 tests passed, with no skipped tests; the private D-Bus
BLE transfer smoke test passed, along with workflow lint and Python syntax
checks. Runtime is 1,506,272 bytes (1,056 bytes free in each 1,507,328-byte OTA
slot); installer is 822,336 bytes. Post-LTO static frames for the new image UI,
WAV editor and mesh-module receive handler are 416, 1440 and 976 bytes. These
compiler figures are not measured task high-water marks or available heap.
Package/reinstall checks and CI/publication results are recorded with the release.

No physical hardware tests were performed for this release. ROM JPEG decoding,
real SD/audio timing, visual legibility, keyboard chords, loaded heap, LoRa
interoperability with external telemetry/history nodes, packet loss, CAD and
power-interruption behavior still need Cardputer ADV validation. A host test or
successful build does not establish those physical properties.
