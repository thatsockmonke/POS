# Install POS on a Cardputer ADV

POS 0.6.1-dev is an **experimental foundation**, not the complete master roadmap.
The installer and runtime build for ESP32-S3 and have host tests; actual ADV
flashing, peripherals, factory boot recovery and electrical power-loss behavior
have not been tested here. Read [STATUS.md](STATUS.md) before choosing to install.
Only use the **Cardputer ADV** hardware profile, not the original Cardputer.

## Guided route (recommended for a first install)

Download **pos-setup** from the release, move it into your Linux home folder, run
`chmod +x ~/pos-setup`, then `~/pos-setup`. Choose **Install POS** and follow its
plain-English prompts. It downloads/verifies matching files, detects eligible
cards, offers FAT32 with explicit erase confirmation, keeps a double-read backup,
guides recovery proof and verifies USB flashing. Finish the on-device INSTALL and
power-cycle steps below. [EASY_INSTALL.md](EASY_INSTALL.md) is the beginner guide,
including Chromebook shared-folder/USB limits. The detailed manual route follows.

## 1. Download the complete matching package

From [POS on GitHub](https://github.com/thatsockmonke/POS), open
[Downloads](DOWNLOADS.md). Download **pos-0.6.1-dev.zip** and its `.sha256` file,
then extract the ZIP on your computer. Use an ordinary directory such as
`~/Downloads/pos-0.6.1-dev`. You need these files from the **same build**:

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
sha256sum -c pos-0.6.1-dev.zip.sha256
mkdir pos-0.6.1-dev
unzip pos-0.6.1-dev.zip -d pos-0.6.1-dev
cd pos-0.6.1-dev
sha256sum -c SHA256SUMS
```

All listed files should report `OK`. Stop if they do not. Checksums detect damaged
files; these development packages are not cryptographically signed.

## 2. Prepare your Linux computer

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
A failed read stays partial, never a completed trusted backup. If the fast USB
read fails, the tool retains it as `.part.stub-failed` (or
`.readback.part.stub-failed`) and retries that entire pass once with ROM-only,
64-byte request/response reads. It uses ROM mode for the remaining pass too.
Allow several minutes: both complete reads must cover the detected capacity
and match. The original application remains stopped between backup reads so
it cannot change NVS. A failed ROM retry stops before restore or POS flashing.
If the screen remains black after a stopped backup, unplug USB, switch OFF,
release G0/all keys, wait 10 seconds, then switch ON. Reconnect/share USB with
Linux before rerunning; no completed backup is inferred from partial files.

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
Changing/reformatting the filesystem erases its contents. The on-device firmware never formats your card. The optional Linux `pos-setup`
wizard can format a selected card only after exact typed erase confirmation. A card already using readable FAT32 need not
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
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --after no_reset write_flash 0x0 pos-installer-usb.bin
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --after no_reset verify_flash 0x0 pos-installer-usb.bin
```

After **both** commands succeed, power-cycle the Cardputer. Keep it in download
mode until verification finishes: the merged image contains settings storage
that POS initializes on first boot, so resetting before verification can produce
a misleading digest mismatch. The wizard performs this ordering automatically.

This changes the internal bootloader/partition table, factory installer and initial
OTA metadata. It does not erase the SD. Do **not** add `erase_flash`, flash an
individual runtime at 0x0, mix versions, or overwrite the factory installer slot
with the final runtime. Close the serial program and reset/power-cycle if needed.

## 6. Let the installer install POS

1. The LCD starts **black**, then pink slides in from the left at the top.
   Yellow starts in the middle when pink is halfway; blue starts at the bottom
   when yellow is halfway. Once all stripes are complete, large centered **POS**
   fades in, holds for **one second**, then normal boot status follows. The sequence runs once on each
   firmware boot, including recovery boots. Setup, SD validation and recovery
   checks still run before the launcher opens.
2. In **POS Installer / Recovery**, choose **Install POS / resume** with Up/Down and Enter.
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
   Passwords stay masked. The Wi-Fi app offers session-only connections or an
   encrypted saved-network vault; unlock saved networks after reboot.
9. The launcher opens. Try Files, Music, Share, Text Editor, Wi-Fi, Background and Settings.
   Settings > Expansion hardware shows detected cap/GPS and controls LoRa features.
   Radio/Range Test require a detected, enabled cap. Transmit and precise-location
   sharing each require explicit consent.

After a verified normal boot, POS removes only known, unchanged obsolete installer
files. A pending/failed restore retains the installation media. Unknown files and
backups are not recursively deleted. The SD is the system drive: leave it inserted
for normal use. Maps/navigation remain future work. See [APPS.md](APPS.md) for Files, Music,
BLE sharing and the LoRa/GPS cap.

## 7. Normal use and recovery

On the ADV, press **;** (Up), **,** (Left), **.** (Down), or **/** (Right)
directly in menus and apps; **Fn is not needed**. In text fields/editor these
keys type punctuation; hold Fn for editor cursor arrows. Enter also works while Fn is held. Hold **Aa/Shift** for
capitals/symbols; stock ADV also uses Fn+letters for capitals. The printed
**Del** key erases left; **Fn+Del** deletes ahead in the editor. Opt is exposed
as Meta. In USB terminals, use the normal arrow keys.

| Action | Control |
| --- | --- |
| Launcher | Left/Right arrows move; Up or Enter opens the centered app |
| Menus | Up/Down arrows, Enter |
| Color picker | Arrow keys move cursor; Q/E hue; Shift fine steps; Enter accepts; Esc cancels |
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

From the launcher, use **Left/Right** to center **Background**, then press **Up** or **Enter**.
Choose **Stripe count** (1-12), then open each stripe's color picker. **Left/Right**
changes saturation; **Up/Down** changes brightness; **Q/E** changes hue along the
rainbow bar. Hold **Shift** for finer steps. The cursor and swatch show your
selection. **Enter** accepts a color; **Esc** cancels that color edit.

Choose **Preview launcher** to inspect the draft. Press Enter/Esc to return,
then choose **Save & Apply** to save and return home. Changes remain local until
that save succeeds. **Cancel**, Esc from the main Background menu, or **Fn+Space**
discards unsaved edits. New stripes start white; **Pan flag preset** restores
pink/yellow/blue in the draft. Saved backgrounds do not change the boot animation.

Only one foreground app runs. **Fn+Space** (or a short G0 press) requests closing
the app from any screen. The editor asks what to do with unsaved changes. **Fn+backtick** is Escape: back within
an app, or close it from its main screen. Background system services may continue.


## Text Editor

Center **Text Editor** with Left/Right and press Up or Enter. Choose **New document**, **Open
text file** or **Recover working draft**. The file picker allows ordinary user
files, never protected system files. New files and Save As go to
`/POS/Users/Owner/Documents`; enter a simple filename including `.txt`.

- Type normally: WASD inserts letters. Shift and Backspace work; Tab inserts four spaces.
- **Fn+arrows** moves the cursor. **Ctrl+S** saves, **Ctrl+F** finds and **Ctrl+G**
  goes to a line. Search and line navigation stream through the document.
- **Esc** opens the editor menu: continue, save, save as, find, go to line,
  start/end or close. Enter selects a menu action.
- **Fn+Space** requests closing the app. With unsaved changes choose Save & close,
  Keep draft & close, Discard edits or Continue editing. Keeping a draft creates
  a normal `Draft-N.txt` document you can open on any computer.

Opening takes a verified SD snapshot; editing keeps text and piece indexes on SD,
not all in RAM. The source file stays untouched until a verified save. Saving an
existing document keeps its old contents as `filename.previous-N`. A changed
original is never silently overwritten: use Save As. Existing Save As targets
are refused. Staged files and originals remain available if a save fails.

Allow free SD space for the working snapshot, inserted text, new saved copy and
retained previous copy. The current file/seek limit is **2 GiB minus one byte**;
large files can take time to snapshot, verify, search and save. The physical
keyboard/font currently supports ASCII entry; existing UTF-8 is preserved, with
unsupported characters displayed as `?`. Undo/redo, highlighted selection and an SD-backed clipboard are available;
see [editor controls](APPS.md#editor-selection-clipboard-and-history). After an
interrupted session, use Recover working draft. Copy the
whole `/POS/Users/Owner/EditorDrafts` folder when recovering data elsewhere; see
[RECOVERY.md](RECOVERY.md#text-editor-working-drafts).

## Wi-Fi services

Open **Wi-Fi** from the ribbon, or choose **Set up Wi-Fi** at the end of
Settings/setup (which also offers **Skip Wi-Fi**). Choose **Connect / scan networks**
for nearby networks or **Manual / hidden network** to type an SSID. Use a
2.4-GHz network. An SSID is 1–32 bytes; a password is empty for open networks,
8–63 characters or a 64-digit hexadecimal PSK. Status shows association, IP and
signal strength. Closing the app keeps the shared connection available;
**Disconnect** or sleep stops it.

After connecting, **Use for this session only** is the default. **Remember
encrypted network** creates/unlocks a vault with a 12–64-character passphrase.
The first save asks twice. Up to four network profiles are encrypted using
AES-256-GCM with a PBKDF2-HMAC-SHA256-derived key (100,000 iterations) and fresh
salt/nonce. Only the encrypted blob is stored in internal NVS; neither passwords
nor the vault passphrase are written to SD. After restarting, open **Saved
networks**, unlock, and select Connect. Forget a profile from the same menu.
The vault is not part of an SD backup. Keep its passphrase somewhere safe; a
forgotten passphrase cannot be recovered. A vault error preserves existing data
and still permits session-only use; POS never erases NVS to repair it.

For Internet features, connect then choose **Sync time**. HTTPS requires a valid
clock and validates certificate chains, hostnames and dates using the ESP-IDF
certificate bundle; no insecure fallback is offered. **HTTPS download** asks for
a URL, a new filename in Downloads and an optional expected SHA-256. **HTTPS API**
supports GET or POST with a small JSON body (UI limit 256 characters; service
limit 4096 bytes). Responses stream to new files in `/Downloads`. GET may follow
up to three absolute HTTPS redirects; relative redirects require the final URL.
POST is not automatically replayed to another address.

Downloads use unique temporary files, check declared length where available,
read back their contents and verify any supplied SHA-256 before publishing.
Existing files are preserved. Esc/Home cancels between chunks; failed transfers
remain `.part-N` for inspection and are not presented as complete downloads.

### Local Wi-Fi file transfer

1. Connect Panputer and your computer/phone to the same **trusted local network**;
   Internet access is unnecessary.
2. Choose **Local file transfer**. Optionally select one user file to export, or
   enable uploads only. POS shows its IP, port 8080 and a temporary session key.
3. In a browser enter `http://IP:8080/?key=KEY` using those displayed values.
   Uploads go only to Downloads, require a simple filename and never overwrite
   existing files. The download button exports only the file selected on Panputer.
4. Enter/Esc stops sharing; Fn+Space closes the app and stops the server. Each new
   session has a new random key. No file server runs while this screen is closed.

This local transfer uses **unencrypted HTTP**, including the session key and
file contents. Use it only on a trusted network. Outbound Internet downloads
continue to use validated HTTPS. Wi-Fi file transfer is implemented; BLE sharing
is available through Share. See [APPS.md](APPS.md) for pairing, transfer resume
and the common local/Wi-Fi/BLE Share screen.


## Files, Music, Share and the LoRa/GPS cap

Read [APPS.md](APPS.md) for full controls, formats and recovery behavior.

- **Files:** select a file/folder with Up/Down and Enter. Copy or Cut, navigate to
  the destination, then Folder actions → Paste. Trash is reversible. Permanent
  deletion requires typing `DELETE` and cannot be undone.
- **Music:** put MP3 or PCM 16-bit WAV files in Music. Space pauses, Left/Right changes
  tracks, Up/Down changes volume, R toggles shuffle, P adds to a playlist. Audio stops
  when closing the app.
- **Share:** choose a user file, then local copy, Wi-Fi or BLE. On the receiving
  Panputer choose Receive BLE file, compare pairing numbers on both devices and
  explicitly accept the offer. Retry the same file to resume a partial transfer.
- **Expansion hardware:** power off before attaching/removing the official cap.
  Settings shows detected hardware separately from user enablement. Choose a
  locally permitted LoRa frequency; attach the correct antenna before enabling
  transmit. TX consent lasts only for the session. GPS detection does not require
  a position fix, and remains independent of the LoRa switch. Maps stays gated
  when GPS is absent; the Maps application is not implemented yet.
- **Radio / Range Test:** use matching POS peers and frequency settings. Received
  messages and outbox files live under Owner/Radio; range CSVs under Owner/RangeTests.
  The Range Test sender requires the receiver's node ID and sends probes at
  60-second intervals while its screen is open. Leaving stops its log/probe run.

## Software Update and battery / recovery changes

After installing POS 0.5.3-dev or newer, open **Settings → System / software
update → Software Update → Check for Updates**. The official HTTPS feed and
trusted public key are built in. Connect Wi-Fi when prompted; POS synchronizes
time when needed for certificate checks. Review the release and approve installation.
Keep the SD inserted and power connected; firmware updates restart and wait for
60 healthy seconds before confirmation.

An older installation needs a one-time USB bootstrap to this release, or manual
configuration of the official URL and public key. Preserve your files using the
backup/reinstall instructions above. See [UPDATES.md](UPDATES.md) for full update,
signing and recovery details.

Every screen shows the battery estimate at the top right. It is voltage-based;
a green full icon indicates sustained near-full voltage, not a verified charger
termination signal. [APPS.md](APPS.md#battery-display) documents thresholds and
controls. Settings includes direct brightness, regional/custom timezones, UTC
clock setting, crash reports and safe-mode requests. [Recovery](RECOVERY.md#crash-records-and-safe-mode)
explains the persistent failed-boot policy and 60-second healthy interval.

Recorder, Files Audio Player and IR/motion controls are described in [APPS.md](APPS.md).
[Developer tooling](DEVELOPMENT.md) adds checksum/backup-checked `posctl flash`,
streamed user file transfer and evidence-gated signed deployment. Do not attempt
wireless firmware deployment before actual USB recovery/OTA/rollback validation.
