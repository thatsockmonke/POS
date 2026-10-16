POS 0.5.0-dev adds Recorder, a Files-only Audio Player, IR/IMU services, versioned
app-save migrations and evidence-gated developer deployment.

- Recorder is the fifteenth launcher app. It streams 16 kHz mono signed-16 PCM WAV
  into /Recordings, with pause/resume, time/size/peak, stop/save and global close.
  Finished recordings pass full PCM CRC readback; interrupted .wav.part files can
  be explicitly recovered. Microphone/speaker ownership is exclusive.
- Files > select MP3/WAV > Play in Audio Player opens a transient player and
  returns at EOF. Audio Player has no launcher icon; Music remains the library app.
- Settings > IR & motion provides live BMI270 accel/gyro/temperature and explicit
  standard NEC sends through GPIO44. Shared APIs upload/verify the BMI270 config
  and expose scaled sensor axes. The ADV has an emitter, no IR receiver/compass.
- posctl flash checks installer checksum/images/layout, matching double-read
  factory backup and security state before writing/readback. posctl files streams
  SHA-256-verified user transfers through an explicit trusted-LAN session.
- posctl deploy verifies local signatures/payloads, hosted HTTPS manifest, target
  trust/identity/health and rehashed physical recovery/OTA evidence. A temporary
  developer session queues the same signed updater and requires device approval.
  Firmware reboot closes it; reopen it and refresh the private key file for boot
  confirmation polling. No startup server or unattended deployment is enabled.
- Portable app-save envelopes provide checked generation slots, incremental schema
  migration, readback, preserved originals and future-schema refusal. Counter has
  concrete legacy text and schema-0-to-1 migrations. Unknown future formats stay
  recoverable, with converters added when those formats exist.
- Keeps the existing partition layout and factory recovery. Uses the SDK common
  certificate roots and WARN library logging for capacity; TLS validation stays
  enabled. Unsupported roots fail closed. No PSRAM or additional flash is assumed.

Download pos-0.5.0-dev.zip and its checksum. Verify, extract and follow INSTALL.md.
Full app controls, migrations and developer usage are in APPS.md, MIGRATIONS.md and
DEVELOPMENT.md. Existing 0.4.0-dev supports signed OTA when a publisher/feed is
configured; otherwise use the documented backup/USB reinstall path.

Experimental prerelease: host tests/builds are not physical hardware proof. Actual
microphone quality, I2S timing, IMU orientation, IR signals, USB recovery, networking,
OTA rollback and SD power-cut behavior need device validation. No physical pass
has been recorded; wireless firmware deployment remains gated until it is.
Remaining software roadmap: on-device region downloads/offline maps/navigation
and Meshtastic compatibility. Games, terminal, installable app/package management
and automatic power management remain removed from scope.
