POS 0.6.1-dev — adjustable sounds, text Files and Recorder throughput improvements.

Update through 0.6.0-dev first. This release removes obsolete minimum-field parsing,
old official-source migration and obsolete package-generation branches. Active
updates contain exactly seven fields. The archived 0.6.0 bridge stays untouched.
Signatures, HTTPS, file hashes, signed sequences and rollback remain enforced.

- Settings → Sounds: master On/Off, volume 0–100%, key presses, app/notification
  feedback and a preview. Defaults are enabled at 20%. Preferences survive restart
  and backup/restore; prior settings load without losing identity, wallpaper or gain.
- Short ramped beeps/boops for keys, navigation, selection/back, app open/close and
  notices. One pending cue coalesces bursts. A separate worker owns the codec only
  while idle; feedback is silent during microphone recording or audio playback.
- Files returns to the original text browser and Enter action menus. Editor,
  Files-only Audio Player, copy/move, Trash, protected paths and confirmations stay.
  The folder carousel, content-decoration scans and unused renderer are removed.
- Recorder now drains queued audio in batches before drawing, defers the meter
  while catching up, paces redraws after frame completion and buffers SD writes
  in 4 KiB. Prefer two seconds of mono buffering, fall back to one when needed.
  Drain already-captured PCM before reporting an overrun; partial WAVs remain
  recoverable. This addresses the identified one-small-block-per-redraw bottleneck.
- Launcher focus stays fixed in the center while icons slide beneath it on an
  elapsed-time spring. Repeated arrows preserve momentum, reversals settle
  naturally, and a small offscreen icon cache prevents blank ribbon edges. Input
  remains responsive during navigation. Opening/closing zoom and fades remain.
- Existing gain controls and boot flag/logo animation remain.

Both firmware roles build; native tests exercise waveform volume/mute/categories,
codec ownership, sustained simulated UI/storage stalls, ordered PCM and drain/error
paths, settings migration/backup and text Files operations. Real speaker loudness,
recording performance and electrical interruption behavior still need a device check.

Install via Settings → System → Software Update → Check for Updates. No SD format
or USB reflash is needed from a working 0.6.0-dev installation. Keep old partial
recordings: Recorder → Recover interrupted WAV can recover persisted PCM.
