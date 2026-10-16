POS 0.4.0-dev implements signed updates, battery status, persistent crash recovery,
editor improvements and expanded Settings.

- Settings > System / software update > Software Update checks an HTTPS manifest,
  validates a trusted P-256 signature, model, semantic/minimum versions and signed
  sequence, then streams verified updates. Asset-only updates do not reflash firmware.
  Configure your publisher URL and explicitly verified public key first; there is
  no default public update feed. See UPDATES.md.
- Update preparation retains current/user data; a journal protects the System
  swap and keeps the previous System. Inactive firmware is written and read back
  before boot selection. Online OTA waits for a healthy 60-second UI interval and
  another validation before confirmation; failed boots can restore System/rollback.
- Battery percentage and changing icon stay visible in a small black box at the
  top right, including startup and dialogs. Readings use smoothing and hysteresis;
  unavailable ADC data shows --%. Green full / 100% is a sustained-voltage estimate,
  not a charger-termination signal. No automatic power-management milestone remains.
- SDK panic handling keeps its normal USB report/backtrace while a bounded RTC
  snapshot survives a warm reset. The next SD mount exports a verified readable
  JSON; existing reports are never replaced. NVS tracks repeated failed boots,
  and Safe Mode requires an explicit normal-boot choice before optional services.
- Editor: 32-level session undo/redo, Shift+arrows selection, Ctrl+A/C/X/V, SD-backed
  clipboard, word/page/line/document navigation and an expanded controls menu.
  Large documents/selections remain streamed; reopening restarts session history.
- Settings: direct profile/brightness controls, regional/custom POSIX timezone
  rules, UTC/local clock display, manual real-date-validated UTC setting, SNTP,
  preferences-only reset, battery information, crash reports and safe-mode requests.
- Keeps all existing Files, Music, BLE sharing, GPS/LoRa, Range Test, background
  customization, fourteen-icon launcher and pink/yellow/blue boot animation.
- Remaining scope now tracks on-device map downloads/navigation, Meshtastic
  compatibility, a dedicated Recorder app/Recordings folder, IR/IMU, developer
  deployment tools and future data migrations. Games, terminal and installable
  apps/package management have been removed from scope.

Download pos-0.4.0-dev.zip and its checksum, extract it and read INSTALL.md/APPS.md.
Extract the inner pos-install.zip onto FAT32 SD; flash only pos-installer-usb.bin
at 0x0. Existing 0.3.x installations need the USB recovery/backup/reinstall flow
to bootstrap this updater; retain backups and the exact matching installer.
Online update signatures do not enable ESP secure boot or sign the USB bootstrap.

Experimental prerelease: builds/native tests and package verification do not
establish physical ADV behavior. USB/factory recovery, percentage calibration,
actual panic capture, TLS/radio coexistence, OTA rollback and electrical SD
power-cut behavior still need device validation. The existing 4-MiB partition
offsets are retained; runtime flash headroom remains limited.
