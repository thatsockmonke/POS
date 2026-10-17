# POS 0.6.2-dev bug investigation and verification

This report distinguishes confirmed code defects from performance causes found
by inspection and host tests. **No physical hardware tests were performed for
this release.** The user's reported microphone and display behavior is context,
not a cloud measurement of this build.

| Issue | Root cause | Fix and regression evidence |
| --- | --- | --- |
| Recorder WAV playback falsely says “Stop recording before playing audio” | The speaker claim/stop loops requested `pdMS_TO_TICKS(5)`. POS builds with `CONFIG_FREERTOS_HZ=100`, so that is zero ticks. Fixed-count retries could expire before an active interface cue released the codec. The old host fixture incorrectly modeled 1 ms ticks. | Real elapsed-tick deadline with a positive one-tick yield. The actual device driver now runs in a 100-Hz host fixture; capture → stop → cue → playback and playback → capture cycles verify handoff, resource cleanup and exclusion. Active recording still correctly refuses playback. |
| LoRa reset/poll delays can round to zero | The same 100-Hz scheduler made 1/2/5-ms waits zero ticks. The reset pulse could be too short and polling could spin. | A bounded ROM microsecond delay guarantees the 2-ms reset pulse; busy/TX polling yields a positive tick. Confirmed by configured tick conversion/code inspection; physical SX1262 timing/coexistence remains untested. |
| Ribbon animation has too few visible intermediate positions | Each changed view ran a general pixel renderer across all 32,400 pixels, with divisions, string/theme checks and 17 serial render-and-wait LCD transfers. Physics alone did not solve display cost. | Direct ribbon scanlines, dirty 8-row bands, and alternating DMA buffers overlap rendering with transfer. The existing elapsed-time spring/input loop keeps momentum on repeat/reversal. Fast raster output is compared pixel-for-pixel with the reference across offsets, patterns and all stripe counts. Physical FPS is not asserted. |
| Coarse, poorly recognizable icons | The old asset stored only 8×8 bits, magnified fourfold. | Native 32×32 silhouettes with consistent geometry and white outlines. Seven masks total 896 bytes; SD asset is 1,924 bytes. A dependency-free generator makes assets reproducible. The loader can also render archived old assets so historical pairs remain usable. |
| Editor idle/cursor/page lag | The UI rebuilt the viewport every 100-ms polling iteration, including releases/idle. Small cursor reads repeatedly reopened the piece index and backing files. Page movement could send many intermediate LCD frames. Undo rescanned all immutable working text. Every queued typed character created a separate durable transaction. | Change-only rendering, bounded 4-KiB nearby-text cache invalidated by generation, layout-only intermediate page movement, deferred full validation at save/recovery, and up-to-64-byte already-queued typing batches. A queued command is retained and processed in order. One burst is one undo unit. Tests prove idle does not redraw, commands/save remain ordered, 1,000 cached cursor reads succeed without reopening the SD index, and multi-megabyte editing/search/selection/undo remain bounded. |
| Editor lacks document deletion | The app offered only edit/save/close actions. | Confirmed deletion moves the saved original to existing protected Trash APIs. Size/CRC must still match the opened original; symlinks/protected paths are refused. The draft is discarded only after a successful move. Tests cover the UI, unsaved edits, successful recovery copy, changed source and protected paths. |
| Editor descriptor leak on an SD flush failure | `flush(out) || fclose(out)` short-circuited, skipping `fclose` when flush failed. | Flush and close are separate operations; close runs on both paths. This is a confirmed failure-path code defect. Electrical/driver I/O-failure injection remains a hardware follow-up. |
| Repeated Wi-Fi vault prompts hinder basic use | Saving/unlocking used a human passphrase every session. No background known-network policy existed. | Known Networks lists SSIDs with Connect/Forget and optional auto-connect (default Off). The new store uses random device-key authenticated encryption; old vaults remain for explicit import. Actual storage-code tests cover round-trip without a human passphrase, fresh salt/nonce, failed writes, and refusing corrupt ciphertext. UI tests cover saving, connecting, forgetting and the auto toggle. No credentials/device keys are written to SD. |
| Wi-Fi scan memory leak under allocation/error pressure | A completed driver scan retained its AP list if allocation failed before `esp_wifi_scan_get_ap_records`, or when fetching records failed. | Both error paths call `esp_wifi_clear_ap_list`. Confirmed against the ESP-IDF scan API's ownership contract; physical low-heap radio stress remains to test. |
| USB/OTA manifest whitespace can block history capture | USB bundles retain formatted JSON while OTA canonicalizes it. Byte-derived package IDs differ even when signed firmware/resources are identical. The check against real 0.6.1 release files exposed this before publication. | Compare exact authenticated version, firmware path/size/hash and every resource path/size/hash, independently of serialization; archive IDs/completion bind to the signed package. A formatted USB-manifest regression covers capture; released-file upgrade/history checks cover both paths. |
| Firmware history previously lacked a complete firmware/System archive | Existing update work preserved System trees/downloads, but did not provide bounded, authenticated release inventory or a paired rollback UI. | Up to 15 complete pairs in `POS/.History`; signed metadata, streamed flash/resource copying, full verification, completion markers and safe pruning. Deliberate rollback uses the existing inactive-slot/journal machinery. Tests exercise capture, retained pairs, corrupt firmware/resources/signatures, cancellation, partial work, selected-target retention, symlinks, boot-selection failure and simulated power interruption. |
| Pruning could remove a selected old rollback target while archiving current firmware | A naive “keep newest 15” policy can delete the item the user just selected. | Retention pins both the selected target and the newly verified current pair before pruning another eligible entry. A 17-release regression checks the limit and pin. |
| Same-version rollback has ambiguous boot identity | Boot recovery identifies the new/old runtime by version. Two different firmware binaries sharing one version cannot be reliably distinguished by that rule. | Reject different same-version firmware hashes; use an asset-only transaction when the resident image is identical. Repeated rollback uses a fresh transaction ID instead of reusing an old work directory. Normal online downgrade/sequence rules remain unchanged. |

## Verification

The release is built with the repository's pinned ESP-IDF 5.5.5 toolchain for
both installer and runtime roles. Host C tests use undefined-behavior sanitization;
audio tests execute actual driver code against timed I2S and FreeRTOS stubs.
Tests also cover settings preservation, signature/HTTPS/hash checks, update
journals, user-file preservation, editor save/recovery, and existing app services.
Release packaging verifies image versions/sizes, every SD file and the ZIP hashes.
CI rebuilds the tag, publishes that exact bundle, then signs the matching OTA
package. Anonymous download/signature/hash checks verify the published artifacts.
The final test count and publication result are recorded in the release response
and CI run; this report does not represent unexecuted checks as measurements.

## Limits and hardware checks

- Measure actual animation FPS/frame times and heap on the ADV, especially with
  Wi-Fi, GPS/LoRa or BLE active. The cloud renderer is not an LCD timing test.
- Play several new/existing recordings immediately after stopping, changing gain,
  pausing/resuming and receiving a UI cue. Check speaker volume/startup jingle,
  codec transitions, long recordings and genuinely slow SD cards.
- Exercise Known Networks on real 2.4-GHz access points, including unavailable
  SSIDs, wrong credentials, reconnect/DHCP, cancellation, and repeated manual
  transitions while auto-connect is enabled. Auto-connect targets visible SSIDs;
  hidden networks still support manual connection.
- The device key and ciphertext both live in NVS. This is convenient encrypted
  storage, not physical security against reading unencrypted flash. No eFuses,
  flash-encryption mode or partition tables are changed.
- Firmware history begins with updates made by this runtime. The previous-install
  import needs a matching inactive image, retained System and trusted signed
  metadata. Earlier firmware cannot be reconstructed if those bytes are absent.
  Unknown/incomplete/corrupt archive material is preserved and may occupy extra
  space; safe pruning may stop rather than erase uncertain data.
- Validate history capture, pruning and rollback with actual power cuts/card
  removal at each boundary. Host filesystem/flash simulations do not prove FAT32
  controller durability, bootloader rollback, or electrical failure behavior.
- Factory recovery plus both OTA slots are deliberately retained. A single-runtime
  design would lose automatic runtime rollback and require SD-dependent recovery
  after an interrupted overwrite. Firmware is copied to internal flash and never
  executed directly from SD.
- Rollback preserves user data/preferences rather than downgrading their formats.
  Older apps may refuse newer saved formats; unknown files remain recoverable.
  The accepted-sequence watermark never decreases. Use History for an already
  archived newer release, or install a future higher-sequence online update.
