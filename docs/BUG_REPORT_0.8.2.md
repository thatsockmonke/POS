# POS 0.8.2-dev: audio memory fix and resource audit

No physical Cardputer tests were performed. The report distinguishes native
allocation budgets, source analysis and firmware builds from device evidence.

## Confirmed causes and fixes

1. **WAV playback allocated the MP3 decoder.** `pos_music_open` allocated one
   structure containing decoder state, a roughly 16 KiB decode scratch area,
   an 8 KiB compressed-input buffer and a full MP3 PCM frame even for WAV files.
   The native structure occupied 35,784 bytes in one allocation. Low or
   fragmented heap therefore rejected ordinary Recorder WAVs before playback.
   WAV now uses a small state and 2,048-byte PCM allocation. It reads 1,024 bytes
   at a time and expands mono samples backwards in that buffer, preserving
   stereo channels and volume scaling without overwriting unread input.
2. **MP3 required the same large contiguous allocation.** Decoder, scratch,
   compressed input and PCM are now allocated separately. MP3 still requires
   its actual decode working set; no unsupported promise of arbitrary low-heap
   MP3 playback is made. Any partial allocation is cleaned up, including the
   open file, before reporting failure.
3. **Files reserved unused selection memory behind child apps.** The native
   Files workspace occupied 22,888 bytes, including two 32-path arrays and
   duplicate filename/label storage. Its idle workspace is now 4,160 bytes:
   filenames occupy the existing label buffers, and selection/clipboard arrays
   are created only when needed. Clearing selection, completing a cut and
   closing Files free their owned buffers. A failed allocation leaves files
   unchanged and displays a notice. An active selection/clipboard still uses
   memory until cleared or Files closes.
4. **Interface cues could overlap decoder allocation.** An in-flight cue could
   still own speaker DMA memory while a player opened its stream; paused-player
   dialogs could also start cues before playback resumed. The player now uses
   the existing nested feedback hold: wait for the cue worker to release its
   resources, allocate/decode, keep cues suspended through dialogs/pause, stop
   speaker output, free the stream, then restore feedback on every exit.
   This is a preventative resource-contention fix; the specific timing window
   was not reproduced on hardware.

## Audit scope

Reviewed app/child-app ownership in Files, Audio Player, Recorder/editing, image
viewing, graphs, launcher and text editing; microphone task shutdown and stream
buffer disposal; speaker/cue ownership; HTTP client/session cleanup; bounded
search/copy buffers; app-save cleanup and Meshtastic cache state.

GCC `-fanalyzer` examined 32 POS C translation units across media, Files,
editor, app saves, graphing, UI, install, update and recovery. It found no
confirmed additional memory defect. Its one uninitialized-index warning in
`choose_channel` was reviewed: the enabled-channel loop assigns each index
before increasing `count`, and selection reads are bounded by that count.
This is a false positive, not a suppressed production check.

The existing image/graph/waveform cleanup continues to submit a non-raster view
before freeing borrowed pixel buffers, including Home exits. No reported
microphone leak was assumed: its actual stop path joins the capture task before
freeing I2S, stream buffers and semaphore resources. Existing timed capture
regressions check buffer overrun, failure, restart and output handoff.

## Verification

- The new constrained-heap WAV and fragmented-heap MP3 regressions failed with
  the old implementation and pass with this fix.
- Mono/stereo WAV output and MP3 fixture output match independent PCM reference
  bytes. Playback is repeated three times in one process with zero remaining
  tracked allocations after every close.
- WAV passes a 3,072-byte POS allocation budget and 2,048-byte maximum allocation;
  MP3 passes a 40,000-byte budget with no allocation above 20,000 bytes.
- Every player allocation is individually failed; partial decoder state is
  released. Low-heap failure is clean. Real Files-to-player and recording-editor
  playback paths have 8 KiB and 10 KiB POS allocation budgets respectively.
- Files selection/clipboard allocation failures preserve source bytes and
  release all allocations on exit; failed player opening restores cue holds.
- AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer exercised 178
  media/UI/audio/heap tests without findings. The existing large-WAV address-space
  limit case is excluded from this sanitizer run because ASan reserves a large
  virtual shadow range; it remains in the ordinary full regression suite.

These budgets track POS/cJSON allocations, excluding libc/crypto internals,
allocator metadata, RTOS tasks, SDK network heaps and DMA. Native structure
sizes are reproducible comparisons, not claims about total free ESP32 RAM.

Measured native allocation peaks (POS allocations only):

| Path | Peak bytes | Largest allocation |
| --- | ---: | ---: |
| WAV decoder | 2,144 | 2,048 |
| MP3 decoder | 35,820 | 16,256 |
| Files → WAV Audio Player | 6,304 | 4,160 |
| Recording editor → WAV Audio Player | 7,120 | 4,976 |

Reproduce the extra sanitizer run with the pinned environment active:

```sh
POS_TEST_ASAN=1 ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1:halt_on_error=1 \
  PYTHONPATH=tests python -m unittest test_apps test_features test_audio_capture test_manifest_heap -v
```

The ordinary full suite also runs the 64 MiB WAV streaming/address-space case.
Final software checks:

- **508 full regression tests passed**, including native firmware crypto/service
  tests with the pinned Mbed TLS host libraries; no ordinary-suite skips.
- **178 additional sanitizer tests passed** (179 discovered, one documented
  ASan/address-space skip). No address, leak or undefined-behavior finding.
- Private D-Bus Bluetooth transfer/cleanup smoke test passed; no physical radio.
- Both ESP-IDF 5.5.5 firmware roles built. Runtime: **1,506,512 bytes**, leaving
  **816 bytes** in the unchanged 1,507,328-byte runtime slot. Installer:
  **822,336 bytes**, below its 1,048,576-byte factory slot. Flash margin is small
  and remains a separate constraint from heap; neither OTA slot was removed.
- Installation bundle: ZIP CRCs, 72 internal hashes, matching runtime/installer
  versions, payload hashes, tool checksums and all 11 previews verified.
- Simulated installation from the published 0.6.4 bundle, verified backup,
  reinstall and restore to 0.8.2 preserved documents, recordings, music, maps,
  settings, the previous tree and the known-good ZIP. Flash/boot hooks are mocks.

The existing release workflow builds the tag and publishes matching USB/SD
assets. Its signing workflow retains the pinned P-256 key and seven-field update
metadata. The official signed feed is verified separately before announcing
that the release is ready.

## Physical checks and limits

Retest playback from Files and Recorder editing, repeated start/stop, pause,
track changes and Home. Repeat with Wi-Fi/Meshtastic enabled and measure free
heap, largest internal block and task stack high-water marks. Test real SD
latency and simultaneous radio work. Native sanitizers do not model DMA,
ESP-IDF scheduling, physical FAT32 faults or radio/TLS memory fragmentation.

Flash capacity and heap are different constraints. This release keeps the
existing independent factory recovery and both 0x170000-byte OTA runtime slots;
firmware still runs from internal flash. No partition migration or card format
is required, and this audit does not establish that every possible bug is gone.
