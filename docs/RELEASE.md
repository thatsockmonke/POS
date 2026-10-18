# POS 0.8.1-dev

Adds the six requested features and Files keyboard clipboard/multi-selection.
The other last-turn suggestions are removed from the software roadmap.

- **Files:** Space checkboxes, Ctrl+A (up to 32), Ctrl+C/X/V; batch preflight,
  no overwrites, streamed verified copies and safe moves. Ctrl+F searches names
  or streamed document contents, with up to 64 results.
- **Image viewer:** Files opens baseline JPEG and uncompressed 24/32-bit BMP;
  arrow pan, +/- zoom and 0 fit. Only a viewport and small I/O/decode buffers
  occupy RAM. PNG/progressive JPEG and other formats are unsupported.
- **Recorder:** waveform, numeric start/end trim and optional normalization into
  a new verified PCM WAV in Recordings. Sources are never replaced.
- **Calculator graphs:** three curves, interval-based roots/intersections,
  extrema and numerical integration. Tab opens graph controls.
- **Meshtastic telemetry:** received device/environment/power metrics, requests,
  one-time actual voltage/uptime sharing and opt-in rate-limited replies.
- **Meshtastic store-and-forward:** request/abort missed-message history; an
  opt-in SD broadcast cache, bounded replay, channel-scope checks and fresh
  server packet IDs/nonces. Private DMs are excluded from server caching.

Use the [Files](FILES.md), [Image viewer](IMAGE_VIEWER.md),
[Recorder editing](RECORDER_EDITING.md), [Calculator](CALCULATOR.md) and
[Meshtastic](MESHTASTIC.md) guides for controls and exact limits.
[STATUS.md](STATUS.md) reflects the requested scope removals.

Upgrade through Settings → System → Software Update, or use the same
[guided pos-setup installer](EASY_INSTALL.md) for a matching fresh USB/SD install.
The factory recovery/two-OTA partition layout and update trust key are unchanged.
This is a development prerelease. No physical hardware tests were performed;
see the [verification report](BUG_REPORT_0.8.1.md) for software results and
remaining device checks.
