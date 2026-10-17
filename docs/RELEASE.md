POS 0.5.7-dev fixes microphone capture starvation and adds manual display-off.

- Recorder creates and syncs its WAV header before enabling the microphone.
  A dedicated higher-priority task drains I2S into a bounded 32,000-byte mono
  PCM buffer (one second at 16 kHz). SD writes, syncs and UI rendering no longer
  prevent I2S reads. Capture errors or a prolonged full buffer still stop with
  a recoverable partial WAV; errors are never hidden by dropping samples.
  Pause and save stop acquisition and drain queued PCM before freeing the
  buffer/channel, preserving the recorded tail.
- Settings > System / software update > Turn display off switches off the LCD
  and backlight while the device, charging and services continue. A normal key
  or short G0 press restores the previous brightness and the same System menu
  selection. Wake input is consumed, including Home, and automatic battery
  redraw cannot turn the hidden display back on. Existing held recovery works.
- System submenu selection is remembered across its actions. Docs describe
  display-off, recovery of existing partial WAVs and signed on-device updates.
- User reports successful 0.5.6-dev installation and working display/keyboard.
  New microphone timing and LCD-off behavior still need physical confirmation.

Host regressions exercise the actual capture task with simulated UI/SD stalls,
ordered PCM, buffer saturation, read/DMA errors, allocation/task failures,
restart cleanup, and System-menu wake behavior. Both ESP32-S3 roles are built.
Experimental prerelease; keep the verified original USB recovery backup.
