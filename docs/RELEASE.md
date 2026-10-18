# POS 0.8.2-dev

Fixes “Not enough memory for streaming audio” when playing WAV recordings.

- WAV playback uses a 2 KiB in-place PCM buffer, without MP3 decoding workspaces.
- MP3 decoder, scratch, input and PCM buffers are separate allocations to tolerate
  fragmented heap. Partial allocations are released on every failure.
- Files allocates checkbox/clipboard storage only when used and shares filename
  storage with its labels, reducing idle memory use by about 18 KiB.
- Playback waits for interface cues to release their hardware/buffers and keeps
  cues suspended through pause/dialogs, restoring sound preferences on exit.

The [memory audit](BUG_REPORT_0.8.2.md) covers the root causes, constrained-heap
regressions, sanitizer/source analysis and remaining physical checks.

Update through Settings → System → Software Update. Your files, wallpaper and
settings are preserved; no SD formatting, USB reinstall or partition change is
required. The independent recovery environment, both OTA slots, signed-update
trust key and existing features remain. This is a development prerelease; no
physical hardware tests were performed.
