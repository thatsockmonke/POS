POS 0.5.8-dev corrects silent microphone capture and adds signal diagnostics.

- The pinned official M5Unified ADV profile uses the right I2S input. POS
  previously saved only the left slot. Capture now extracts right and applies
  the stock mono path's effective 8x PCM gain with saturation, preserving the
  analog profile and 16 kHz mono WAV format. This is a source-verified correction;
  acoustic input and playback still need confirmation on the physical ADV.
- Recorder shows decimal peak percentage plus raw L/R peaks before gain. A
  quiet input below 1% is distinguishable from truly zero samples. Repeated
  all-zero readouts show No input detected without stopping or fabricating audio.
- Stable clock and ADC registers are checked after codec configuration. A failed
  readback stops cleanly. Capture shutdown tracks channel enablement, so pause,
  draining and cleanup do not disable an already stopped I2S channel.
- Dedicated buffered capture, saved PCM tails, recoverable partial WAVs and the
  manual Turn display off option from 0.5.7-dev remain. The user reports buffering
  stopped the overrun, but the previous input selection produced silent files.

Host regressions use silent left/active right data, active left/silent right,
all-zero input, gain/clipping extremes, low-level meters, codec readback failures,
channel-state cleanup, and delayed SD/UI consumers. Both ESP32-S3 roles build.
Update a working install through Software Update; no SD format or factory restore
is needed. Existing zero-filled recordings cannot have missing audio recovered.
Experimental prerelease; retain verified USB recovery backups.
