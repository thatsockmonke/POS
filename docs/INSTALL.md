# Install POS on a Cardputer ADV

POS 0.3.5-dev is an **experimental foundation**, not the complete master roadmap.
The installer and runtime build for ESP32-S3 and have host tests; actual ADV
flashing, peripherals, factory boot recovery and electrical power-loss behavior
have not been tested here. Read [STATUS.md](STATUS.md) before choosing to install.
Only use the **Cardputer ADV** hardware profile, not the original Cardputer.

## 1. Download the complete matching package

From [POS on GitHub](https://github.com/thatsockmonke/POS), open
[Downloads](DOWNLOADS.md). Download **pos-0.3.5-dev.zip** and its `.sha256` file,
then extract the ZIP on your computer. Use an ordinary directory such as
`~/Downloads/pos-0.3.5-dev`. You need these files from the **same build**:

| Item | What you do with it |
| --- | --- |
| `pos-installer-usb.bin` | Flash to internal flash at **0x0**, once for initial installation |
| `pos-install.zip` | Extract onto the microSD root; the installer reads it there |
| `card/pos-install/` | Already-extracted alternative to `pos-install.zip` |
| `tools/posctl.py` | USB probe, verified full-flash factory backup/restore and serial capture |
| `SHA256SUMS` | Verify all the extracted files before use |
| This guide and hardware/recovery/status documents | Keep a computer copy for recovery |

`pos-install/firmware.bin` is the final resident runtime. The installer flashes it
for you **after** verifying the SD system. Do not flash that file at address zero.
GitHub's automatic **Source code.zip** is source; it is not the ready-to-install
bundle. Download the named POS package. If no published package is visible, see
[Downloads](DOWNLOADS.md) for building or running the GitHub workflow.

On Linux, verify and extract the complete download:

```sh
cd ~/Downloads
sha256sum -c pos-0.3.5-dev.zip.sha256
mkdir pos-0.3.5-dev
unzip pos-0.3.5-dev.zip -d pos-0.3.5-dev
cd pos-0.3.5-dev
sha256sum -c SHA256SUMS
```

All listed files should report `OK`. Stop if they do not. Checksums detect damaged
files; these development packages are not cryptographically signed.

## 2. Prepare your Linux Framework computer

You need Python 3, `venv`, an SD reader and a USB **data** cable. Prebuilt installation
does **not** require ESP-IDF or compiling POS. From the extracted package directory:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install 'esptool==4.12.0'
python -m serial.tools.list_ports
```

If `venv` is missing on Ubuntu/Debian, install `python3-venv` with your normal
package manager first. `esptool` supplies the serial Python dependency too.
Keep this terminal open: later commands assume the venv is active and you are in
the extracted package directory.

Connect Panputer. Use the actual listed serial port in every command below;
`/dev/ttyACM0` is an **example**, not a guaranteed name. Close other serial monitors.
For a permission error, grant your Linux user access to the port's group (often
`dialout`) and log out/in; do not run the entire installation as root.

On Chromebook/Crostini, share the connected USB device with Linux in ChromeOS's
Linux development environment USB settings. If it never appears inside Linux,
perform flashing on the Framework rather than changing POS firmware to compensate.

## 3. Preserve and prove factory recovery before experimental flashing

Confirm the board is the ADV revision supported by [HARDWARE.md](HARDWARE.md),
then read its identity, flash capacity and security state:

```sh
python tools/posctl.py probe --port /dev/ttyACM0
python tools/posctl.py factory-backup --port /dev/ttyACM0 --output panputer-factory.bin
```

The backup command reads **all internal flash twice**, requires matching hashes,
and saves `panputer-factory.bin` plus `.json` metadata. Copy **both** to a safe
location on your computer. They can contain personal device data; keep them private.
A failed read stays `.part`, never a completed trusted backup.

The development layout uses the first 4 MiB and requires at least that much flash.
Do not bypass a chip/capacity/security error. This restore tool supports unencrypted
development flash with Secure Boot disabled; use the vendor's documented recovery
for protected configurations. It does not burn eFuses or use `--force`.

Obtain the official factory image/recovery instructions for your exact ADV revision.
Prove the USB download-mode entry and that the original factory software actually
boots after restoration. To restore the matching verified private backup:

```sh
python tools/posctl.py factory-restore --port /dev/ttyACM0 --backup panputer-factory.bin --confirm-restore
```

This explicitly replaces internal flash and verifies readback. **Observe the factory
application boot yourself.** A successful readback is not that observation.

If automatic USB bootloader entry fails, use the vendor's ADV download-mode
procedure. On ESP32-S3 hardware with G0/BOOT connected to GPIO0, holding it while
resetting/reconnecting USB requests the ROM downloader; verify the correct button
and reset/power sequence against your actual ADV revision. The OS shortcut G0 is
not a replacement for knowing the ROM recovery procedure.

## 4. Prepare the 64-GB microSD

Back up existing SD files to your computer. The card must be **FAT32**; a new 64-GB
card often arrives as exFAT, which this build does not support. On Linux you can
use Disks/GParted to create a FAT32 partition on the **correct removable card**.
Changing/reformatting the filesystem erases its contents; POS itself never formats
or automatically wipes your card. A card already using readable FAT32 need not
be erased to copy the installer files.

Extract `pos-install.zip` to the card's top level, or copy `card/pos-install/` from
the download. You should see exactly this structure:

```text
SD card root/
  pos-install/
    manifest.pos
    firmware.bin
    payload/
      System/
        Assets/apps.icons
        Config/defaults.json
        README.txt
```

There must not be an extra `pos-install/pos-install/` or download-folder wrapper.
The ZIP file alone on the card is **not** enough; the firmware needs the extracted
folder. Do not manually create an empty `/POS` tree or move `firmware.bin` outside
its package. Keep your computer copy of the original package for exact-package repair.
Safely eject the card, power Panputer off, and insert it.

## 5. Flash the small installer over USB

Reconnect the device in the appropriate USB download mode. From the extracted
package directory with the venv active:

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 pos-installer-usb.bin
python -m esptool --chip esp32s3 --port /dev/ttyACM0 verify_flash 0x0 pos-installer-usb.bin
```

This changes the internal bootloader/partition table, factory installer and initial
OTA metadata. It does not erase the SD. Do **not** add `erase_flash`, flash an
individual runtime at 0x0, mix versions, or overwrite the factory installer slot
with the final runtime. Close the serial program and reset/power-cycle if needed.

## 6. Let the installer install POS

1. The LCD shows **pink → yellow → blue once**, about 180 ms per color, then the
   yellow stripe slides into the middle over blue. When yellow is halfway across,
   pink starts sliding into the top. Large centered **POS** fades in across all three
   stripes, holds for **one second**, then normal boot status follows. The sequence runs once on each
   firmware boot, including recovery boots. Setup, SD validation and recovery
   checks still run before the launcher opens.
2. In **POS Installer / Recovery**, choose **Install POS / resume** with W/S and Enter.
3. The installer checks package/model/version, file sizes/hashes, runtime header,
   destination ownership, flash capacity and conservative SD free space.
4. Read the summary, then type **INSTALL** exactly and press Enter. **Ctrl+U**
   clears a text field; a character counter shows its limit. Fn+backtick
   goes back; Fn+Space requests Home/cancellation. No formatting option is offered.
5. Leave power and SD connected. Progress labels distinguish **Prepare SD system**,
   **Verify files**, **Write runtime firmware** and **Verify runtime firmware**.
   POS streams/verifies `/POS.installing`, commits
   the SD system as `/POS`, writes an inactive runtime slot, verifies flash and
   selects that slot for next boot. **You do not manually flash the runtime.**
6. When **Installation complete** appears, power off and back on as instructed.
   Keep the SD in the device. Do not remove installer files yet.
7. The runtime validates its SD files and firmware before confirming a good boot.
   If a filesystem backup is present, it offers restore before setup.
8. Complete display/user name, device name (default PANPUTER), timezone and brightness.
   A display name uses 1–48 printable ASCII characters; a device name uses 1–24
   letters, digits or hyphens. Invalid names are explained and retried in place.
   Cancelling before save preserves the previous settings. Wi-Fi is optional.
   Passwords stay masked and are session-only; reconnect after reboot.
9. The launcher opens. Try Counter, Settings, Diagnostics, Files, Recovery and About.

After a verified normal boot, POS removes only known, unchanged obsolete installer
files. A pending/failed restore retains the installation media. Unknown files and
backups are not recursively deleted. The SD is the system drive: leave it inserted
for normal use. Music/maps/editor/radio features are not yet included in this release.

## 7. Normal use and recovery

| Action | Control |
| --- | --- |
| Launcher | A/D or left/right arrows move; W or Enter opens the centered app |
| Menus | W/S or arrows, Enter |
| Color picker | WASD moves cursor; Q/E hue; Shift fine steps; Enter accepts; Esc cancels |
| Clear a text field | Ctrl+U (also byte 0x15 from a raw USB terminal) |
| Back | Fn+backtick (Escape) |
| Return Home | Fn+Space or a short G0 press |
| Escape a stalled foreground app | Hold Fn+Space or G0 for two seconds to restart into recovery |
| See measured hardware data | Diagnostics; JSON files go to `/Diagnostics` |
| Repair/reinstall | Recovery → Repair / reinstall (factory) |
| Restore original factory software | Use the saved private `.bin` **and** `.json` with factory-restore |

Files shows a folder page number, fixed Previous/Next actions and an explicit
`Empty folder` message. At a boundary, the actions say `(first)` or `(last)` and
keep you on that page. Returning from a file preview keeps the current folder page.

Sleep is deep sleep, not the physical power switch; actual wake behavior still
needs testing. The warm-reset failure counter uses RTC memory and may reset on cold
power loss. USB ROM recovery remains necessary if the bootloader cannot start.

USB fallback/measurement capture, from the same package directory:

```sh
python tools/posctl.py logs --port /dev/ttyACM0 --seconds 30 --output first-boot.log
```

This captures logs; it does not provide interactive typing. For interactive USB
fallback, use a 115200-baud serial terminal with **local line editing disabled** so
keys are sent immediately. An ESP-IDF checkout can use `idf.py monitor`; Ctrl-]
exits that monitor. Do not run capture and an interactive monitor on the same port.

## Troubleshooting and reinstall

| Symptom | Next step |
| --- | --- |
| No serial port | Check data cable, board power, actual ADV ROM download sequence and Crostini USB sharing |
| Permission denied | Fix Linux serial-port group access and reconnect/log in again |
| Missing/unreadable SD | Check FAT32, contacts and `/pos-install` nesting; boot manager can retry. Without a detect switch, missing media and electrical I/O faults cannot always be distinguished |
| No display/keyboard | Use USB fallback and validate the actual ADV pins/revision; see HARDWARE.md |
| Package/hash/model/version error | Re-extract the complete matching trusted build; do not edit the manifest hashes to accept corruption |
| Insufficient space | Copy files to your computer; POS never deletes user files to make room |
| Existing confirmed POS tree | Use repair or Back Up & Reinstall, not a fresh-install overwrite |
| Unknown/torn staging or journal | Preserve it on your computer for inspection; the installer will not guess ownership |
| Runtime validation fails | Factory recovery is selected; keep the SD/package and use repair or USB recovery |

**Repair** requires the exact original package; type REPAIR after validation.
**Back Up & Reinstall** shows source size/temporary/free space, creates or verifies
a ZIP/folder backup, asks you to keep an off-device copy, then requires REINSTALL.
A matching journal binds the backup and new package before `/POS` becomes
`/POS.previous`. Original backups and the previous tree are retained after restore.
Copy the **entire** `POS-Filesystem-Backup/` directory for a folder backup, including
its inventory/metadata. See [RECOVERY.md](RECOVERY.md) for resume and migration.

For meaningful physical tests and evidence, follow [VALIDATION.md](VALIDATION.md)
and [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md). Host process-interruption tests
do not prove FAT survives torn sectors or actual electrical power loss.

## Set your launcher background

From the launcher, use **A/D** to center **Background**, then press **W**.
Choose **Stripe count** (1-12), then open each stripe's color picker. **A/D**
changes saturation; **W/S** changes brightness; **Q/E** changes hue along the
rainbow bar. Hold **Shift** for finer steps. The cursor and swatch show your
selection. **Enter** accepts a color; **Esc** cancels that color edit.

Choose **Preview launcher** to inspect the draft. Press Enter/Esc to return,
then choose **Save & Apply** to save and return home. Changes remain local until
that save succeeds. **Cancel**, Esc from the main Background menu, or **Fn+Space**
discards unsaved edits. New stripes start white; **Pan flag preset** restores
pink/yellow/blue in the draft. Saved backgrounds do not change the boot animation.

Only one foreground app runs. **Fn+Space** (or a short G0 press) closes it from
any screen and returns to the launcher. **Fn+backtick** is Escape: back within
an app, or close it from its main screen. Background system services may continue.
