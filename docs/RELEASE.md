POS 0.6.2-dev — audio handoff, smoother rendering, editor, Known Networks and firmware history.

- Fix “Stop recording before playing audio” after recording: positive scheduler
  ticks and elapsed-time waits let interface sounds release the codec. Actual
  driver tests reproduce the configured 100-Hz scheduler and resource handoffs.
- Fixed launcher focus with continuous spring momentum and responsive arrows;
  direct ribbon rasterization, dirty bands and alternating DMA buffers reduce
  LCD work. Native 32×32 coherent icons replace magnified 8×8 assets.
- Varied musical key/interface pitches and a gentle 400-ms C–E–G–C startup melody.
  Existing master/volume/key/app settings apply. Recording/playback stay exclusive.
- Editor nearby-read cache, no idle redraw, layout-only page navigation and queued
  typing batches. Esc → Delete document confirms a reversible Trash move; changed
  originals/protected paths are refused. Original/save/draft validation remains.
- Wi-Fi → Known Networks: saved SSIDs, Connect, Forget and Auto-connect (default
  Off). No repeated vault passphrase. Explicit import preserves the old vault.
  Authenticated credential blobs use a random device key in NVS; this does not
  protect against an attacker reading unencrypted physical flash.
- Settings → System → Firmware history: up to 15 signed, verified firmware/System
  pairs on SD, safe retention, import of a matching previous OTA/System pair, and
  confirmed rollback through inactive-slot writes and existing journals. History
  starts with updates installed by this runtime; imports need exact retained bytes
  and trusted metadata. Factory recovery and both OTA slots remain for safety.
- Fix editor flush-error descriptor leak and Wi-Fi scan-result cleanup on low
  memory/errors. Full causes, regression evidence and limitations:
  docs/BUG_REPORT_0.6.2.md and docs/FIRMWARE_HISTORY.md.

Update through 0.6.0-dev first if older. From a working 0.6.0/0.6.1 installation:
Settings → System → Software Update → Check for Updates. No SD format/USB reflash.
Keep the microSD inserted through reboot and healthy-boot confirmation.

Both firmware roles and the host suite are checked before publication. Host
simulations cover audio ownership, cache/UI behavior, credential storage and
history/update interruptions. No hardware tests are claimed: real animation FPS,
SD throughput, radio behavior, audio loudness and power-cut recovery need checking
on a physical ADV. User files stay in place; rollback does not downgrade their
settings/save formats or lower the accepted signed-sequence watermark.
