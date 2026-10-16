# POS — Pan Operating System

POS is an ESP-IDF/FreeRTOS operating environment for Panputer, the M5Stack
Cardputer ADV. Resident firmware provides the core and recovery machinery;
ordinary FAT32 microSD files hold the system resources and user data.

**0.3.3-dev adds a one-second full-flag POS splash after the boot color flashes.** Both firmware images compile
for ESP32-S3. Native tests exercise the actual portable installer, UI, app,
settings and recovery code. No physical Panputer has been flashed or tested here.

## Download and install

[GitHub Releases](https://github.com/thatsockmonke/POS/releases) · [Download the complete package](docs/DOWNLOADS.md) · [Full installation guide](docs/INSTALL.md) · [Feature status / remaining roadmap](docs/STATUS.md)

The master roadmap is **not all implemented**: music, the paged editor, BLE sharing,
GPS/LoRa, maps, online updates and wireless deployment are future work. This release
contains the core, launcher, setup, storage, installer/recovery and diagnostics.

For the prebuilt bundle, you need Python/esptool on your Linux computer, a FAT32
microSD and a USB data cable. Back up/prove factory recovery; copy the extracted
`pos-install/` folder onto SD; flash `pos-installer-usb.bin` at **0x0**; boot the
installer and type **INSTALL**; power-cycle when prompted and complete setup.
The installer flashes the final runtime for you. Keep the SD in Panputer.
Follow the full guide for exact commands, checks, USB permissions and recovery.


Implemented:

- A single pink → yellow → blue sequence at boot (180 ms per color), then the full
  three-stripe flag with large centered **POS** for one second, followed by normal boot.
- Native ST7789 display output and TCA8418 ADV keyboard input, with USB fallback.
- A two-page graphical launcher with icons streamed from SD; WASD navigation.
- Central input routing, protected Home shortcut, and a separate input supervisor.
- One foreground application with initialize/foreground/event/update/render/save/
  exit callbacks and resource release. Counter demonstrates a persistent app;
  Settings, Diagnostics, Files, Recovery and About also use the app manager.
- First-time name/device/timezone/brightness setup, plus optional session Wi-Fi.
  Passwords stay in RAM and are never written to SD/NVS or displayed in plaintext.
- Redundant, checksummed settings; formal system/user boundaries and standard folders.
- On-device installer confirmation, verification/progress/cancellation, factory
  recovery and separate OTA runtime slots. Files and flash are read back before boot selection.
- Repair from the exact original package; verified ZIP/folder backups and journaled reinstall.
  The old tree stays in `POS.previous`; the backup is never deleted automatically.
- Streamed ZIP/folder restore, conflict preservation under `POS/Recovered`, and
  shared settings validation/migration with JSONL recovery reports. Unknown preferences stay accessible.
- Diagnostics for heap, firmware size, boot/frame timing, battery voltage, keyboard
  positions, input drops, main-stack headroom, frame min/mean/max and a measured 1 MiB SD write/read integrity test.
  Device/SD measurements are saved as JSON; Wi-Fi and app-cycle samples can be compared by build and boot session.
- Linux USB probe, double-read factory backup and identity/hash-checked factory
  restore tools, private serial capture, hardware evidence reports, native backup/restore and a launcher renderer.
- Paginated notices and file browsing with folder page counts, fixed Previous/Next controls
  and an explicit empty-folder state; owned partial folder backups can resume.
- Ctrl+U clears text fields; character counters show limits and passwords stay masked.
  Setup checks each name before moving on and preserves saved settings when cancelled.

Software-rendered preview of the one-second splash:

![POS flag splash](downloads/0.3.3-dev/boot-splash.png)

This is unsigned development firmware. Physical display orientation, all key
positions, SD performance, battery calibration, USB recovery and power-cut safety
still require [hardware validation](docs/HARDWARE_VALIDATION.md).

## Build and test

Use ESP-IDF **v5.5.5**, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`.
On the prepared cloud machine:

```sh
cd /workspace/POS
. /workspace/pos-cloud/activate.sh
python tools/posctl.py build
python -m unittest discover -s tests -v
python tools/posctl.py bundle --output dist/pos-0.3.3-dev --zip-output dist/pos-0.3.3-dev.zip
python tools/posctl.py preview --output dist/launcher.png
python tools/posctl.py preview --screen boot --output dist/boot-splash.png
```

On Linux Framework/Crostini, follow Espressif's
[Linux setup](https://github.com/espressif/esp-idf/blob/v5.5.5/docs/en/get-started/linux-macos-setup.rst),
clone the pinned SDK and submodules, run `./install.sh esp32s3`, and source
`export.sh`. GCC and OpenSSL development headers are needed for native services
(Debian/Ubuntu: `build-essential libssl-dev`); ESP-IDF provides Python/esptool,
CMake, Ninja and cJSON. All third-party code used by POS is in the SDK or the
vendored public-domain font; no Arduino board framework is required.

Builds use four Ninja jobs and separate ignored `build/installer` and
`build/runtime` directories and sdkconfigs. When upgrading generated 0.1 configs,
set main task stack to 16384 and FAT filesystem locks to 32, or preserve the old
configs and use a fresh `--build-root` so the new defaults apply.

`bundle`, `package`, preview and backup commands refuse existing output names.
A bundle contains the merged `pos-installer-usb.bin`, `pos-install.zip`, an
expanded `card/pos-install` tree, `INSTALL.md` and `SHA256SUMS`. Do not flash the
standalone runtime at the factory address. Read [INSTALL.md](docs/INSTALL.md).

## Controls

| Context | Keys |
| --- | --- |
| Launcher/menus | WASD or Fn+arrow keys, Enter selects |
| Text fields/terminal | WASD are ordinary letters; Shift, Backspace, Enter; Ctrl+U clears |
| Back | Fn+backtick (Escape) |
| Global launcher | Fn+Space or a short G0 button press |
| Recover from a stalled app | Hold Fn+Space or G0 for two seconds; restart into recovery |
| USB fallback | WASD, Enter, Escape, and byte 0x1d for Home; IDF monitor itself reserves Ctrl-] |

Name validation runs when Enter accepts a field: display names use 1–48 printable
ASCII characters; device names use 1–24 letters, digits or hyphens. Invalid input
returns to the same field, and cancelling before save preserves existing settings.
File browsing shows `Folder page N/M`; Previous and Next stay at the top and show
first/last boundaries. A separate `Menu N/M` appears if the current list spans
more than one screen.

Home propagates through nested dialogs and cancels active file operations safely.
Built-in apps are trusted C callbacks, not a sandbox for hostile native code.
Sleep stops the display/Wi-Fi and requests deep sleep with G0 wake; use the physical
power switch for a true power-off. Peripheral sleep/wake behavior is unverified.

## Storage and recovery

- `/POS/System`: immutable bundled files, installed manifest, boot marker and
  standard Apps/Assets/Fonts/Sounds/Config directories.
- `/POS/Users/Owner`: documents, saves and diagnostics. Owner is a stable profile
  directory; changing the display name does not rename your files.
- `/POS/Users/settings.0.json` and `.1.json`: generation/checksum-based settings slots.
- `/Downloads`, `/Music`, `/Games`, `/POS/Maps`, `/POS/UserApps`: ordinary user data.
- `/POS/Temp`, `/POS/Cache`, `/POS/Updates` and `/.POS-Recovery-Temp`: disposable system work.
- `/POS.installing`: owned staging transaction; `/POS.previous`: retained previous system.
- `/POS-Filesystem-Backup.zip` or `/POS-Filesystem-Backup/`: verified user backup.
- `/Diagnostics`: device and SD measurement JSON; `/pos-reinstall.pos`: reinstall journal.

Other SD files are treated as user data. Backup excludes only the documented
system/staging/cache/installer/backup/transaction paths. Restore never blindly
writes old System files over the new runtime. Conflicts and unsupported preferences
remain under `POS/Recovered`, and the original ZIP remains available.

Small collections use a standard stored ZIP (up to 2048 files, below 2 GiB).
Larger collections, files above the ZIP seek limit, or empty directories automatically
use a plain folder backup with an inventory and per-file SHA-256. Folder backups
stream files through FAT32's individual-file limit of **4 GiB minus one byte**,
have no 2048-file or total 2 GiB cap, and preserve empty directories. Paths are
still limited to 240 bytes and 16 directory levels. Aggregate sizes use 64-bit
counters. No POS program is needed to read `files/` on a normal computer.

Both forms require enough free space for another copy plus conservative FAT and
metadata overhead; a mostly full card cannot hold its own full backup. They do
not erase data to make room. See [recovery](docs/RECOVERY.md) for resuming a folder
backup, migration reports and limitations, and [evidence collection](docs/VALIDATION.md)
for the actual-device workflow.

See [format and transaction details](docs/FORMAT.md),
[recovery semantics](docs/RECOVERY.md), [hardware profile](docs/HARDWARE.md) and
[third-party notices](THIRD_PARTY.md). The card is never formatted or automatically
wiped. Music, text editing, maps, GPS/LoRa/audio/IMU/IR services, installable apps,
signed updates and wireless deployment remain later milestones.
