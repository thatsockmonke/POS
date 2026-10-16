# Easy installation: Linux or Chromebook Linux

You need a **Cardputer ADV**, a USB **data** cable, a microSD card (64 GB is fine)
and a reader for your computer. Back up anything important on the card first.
The firmware goes into the Cardputer over USB. The microSD holds its installation
files and becomes the POS system drive. You do not need ESP-IDF, a compiler,
manual flash addresses or an understanding of partitions.

## Start here

1. Open the [current POS release](https://github.com/thatsockmonke/POS/releases/tag/v0.5.6-dev).
2. Download **`pos-setup`**. This is the executable wizard, not the Source code ZIP.
   Its optional `pos-setup.sha256` checksum is beside it. The complete POS ZIP
   also includes a copy of the wizard.
3. Put it in your Linux home folder. On a Chromebook, open **Files** and move the
   download into **Linux files**. Enable the Linux development environment first
   if you have not already done so.
4. Open the Linux Terminal and run:

   ```sh
   chmod +x ~/pos-setup
   ~/pos-setup
   ```

The tool requires Python 3.10 or newer, normally already provided by current
Ubuntu/Debian and Crostini. If you saved it elsewhere, run it from that folder
instead. **Do not run the wizard with `sudo`.** It asks for admin access only
when installing missing support packages or formatting the selected card.

Choose **1. Install POS: prepare microSD + flash Cardputer**, then follow the
prompts. Everything is a numbered menu or a plain-English confirmation.

## What the wizard does

- Downloads the newest published POS version, including experimental development
  releases. It skips drafts/incomplete releases and checks version numbers, not
  just release dates. You see the selected version before installing.
- Downloads the matching USB firmware and SD package together, verifies the ZIP
  checksum, every bundled file and the SD manifest, and checks firmware versions.
  It never executes Python/shell files from a downloaded bundle. This executable
  contains the USB helpers it uses.
- For a private repository, helps install GitHub CLI if needed, then offers its
  normal browser sign-in. Sign in to the account that can open the POS repository.
  POS Setup never asks you to paste a GitHub password/token. Public releases work
  without GitHub sign-in. There is no need to make your repository public.
- Offers a small private USB-helper installation when needed. This downloads
  pinned esptool from PyPI into its own Python environment. It may restart the
  menu once after that first-time setup; choose Install POS again.
- Finds USB serial connections, asks you to select the Cardputer and checks its
  chip/security state. Only the **ADV** hardware profile is supported.
- Reads its complete existing flash **twice**, verifies the copies match, and
  keeps a private backup on the computer. If a matching earlier verified backup
  exists, you can choose it instead, including after an interrupted attempt.
  A failed fast read restarts from byte zero with the ROM's smaller transfers;
  the remaining pass uses ROM mode too. This can take several minutes. Two
  complete, matching reads of the detected flash capacity are still mandatory.
- Restores that verified backup and asks you to look at the Cardputer and confirm
  its previous software boots. It cannot observe the screen for you. If that
  recovery check fails, it stops before installing POS.
- Detects candidate removable SD/USB disks and displays their device name,
  capacity, reader model and filesystem. **It cannot distinguish every SD reader
  from every USB drive, so select your microSD carefully.** Internal/system disks,
  swap, active mapped storage and the disk holding the wizard/downloads/backups
  are excluded. If active virtual-disk storage cannot be traced safely, direct
  formatting is disabled. It rechecks device identity and mounts before disk writes.
- Keeps an existing FAT32 card if you choose that option. Otherwise it offers
  one FAT32 partition. Formatting **erases every file/partition on that card**;
  you must type the exact displayed phrase, such as `ERASE /dev/sdb`. There is
  no unattended `--yes` or automatic erase merely because a card was detected.
- Copies the correctly nested `pos-install/` folder, syncs it, reads every file
  back and checks its hash before marking it ready. Existing `/POS`, different
  installer folders and interrupted copies are preserved rather than overwritten.
- After you put the card into the powered-off ADV and reconnect USB, asks you to
  type `FLASH`. It checks the installer images/layout and matching backup, writes
  the USB installer at the correct address and verifies flash readback. Progress
  is visible. Verification finishes while the device stays in the ROM downloader;
  only then does the wizard reset it. This prevents first-boot settings writes
  from interfering with the full-image comparison. It never uses a force flag, burns eFuses or erases the whole flash.

Backups contain whatever software was on the Cardputer before that backup was
made. A new backup is the manufacturer's factory software only if that software
is still installed. Keep an earlier known-good factory backup; the wizard never
deletes one to make room for a newer attempt.

## Finish on the Cardputer

After the wizard says the USB flash/readback succeeded:

1. Power the Cardputer off and on with the prepared SD inserted.
2. On its screen use **Up/Down** and **Enter** to choose **Install POS / resume**.
   On the ADV, hold **Fn** with **;** for Up or **.** for Down.
3. Type **INSTALL** and press Enter. Keep power and SD connected while it works.
4. When installation is complete, power it off and on again.
5. Set your name and preferences. Wi-Fi can be skipped.

Leave the card inserted during normal use. You do not flash the runtime binary
yourself; the on-device installer installs it after verifying the SD system.

## Chromebook / Crostini: what works and what can be blocked

ChromeOS controls which devices Linux can access. Sharing a folder is different
from passing through the physical storage device.

- **SD already FAT32:** in ChromeOS Files, right-click the card → **Share with
  Linux**. Run the wizard and choose **Prepare the microSD only**. It finds shared
  cards under `/mnt/chromeos/removable/`; select your card's top-level folder.
  Folder mode copies/verifies files, preserves other files and cannot format.
- **SD is exFAT or its format is unknown:** Crostini generally cannot repartition
  a card exposed only as a shared folder. In ChromeOS Files, use its format dialog
  **only if it explicitly offers FAT32**. If it does not, run this wizard on a
  normal Linux computer with a card reader and let it format the card there.
  A shared folder may report `9p`/`virtiofs`; that does not prove the card is FAT32.
  The wizard explicitly asks you to confirm FAT32 in this case.
- **USB flashing:** connect the ADV, then check ChromeOS **Settings → Developers →
  Linux development environment → Manage USB devices**. Share it with Linux if
  listed. Use a USB data cable. If ChromeOS does not allow that device through,
  the CLI cannot override ChromeOS; flash on another Linux computer instead.
  A shared Cardputer folder cannot replace USB serial access.

Run `~/pos-setup --doctor` for a **read-only** report: Linux/Crostini detection,
visible serial paths, candidate cards and shared folders. It downloads nothing,
installs nothing, mounts nothing and writes no firmware or card data.

Google documents [Linux USB support limits](https://support.google.com/chromebook/answer/9145439)
and [supported ChromeOS external filesystems](https://support.google.com/chromebook/answer/183093).
Exact USB support varies by ChromeOS version and device policy; Crostini is not
promised to support the entire format-and-flash path.

## If something goes wrong

The private working folder is **`~/.local/share/pos-setup`**:

| Folder | Contents |
| --- | --- |
| `backups/` | Original `.bin`, matching `.json`, restore-readback and observed-boot records. Keep these! |
| `downloads/` | Verified release ZIP and checksum; incomplete downloads keep a partial name. |
| `unpacked/` | Matching firmware, SD files and full instructions. |
| `usb-helper/` | Private Python environment for esptool. |

The main menu includes **Restore a saved original/factory backup**. It discovers
matching verified backups and checks device identity/hash again before restoring.
It also accepts a saved `.bin` from another folder with its `.json` beside it.
Use the ADV's documented G0/BOOT + power/reset procedure if normal USB download
entry stops working; close serial monitors and check Linux permissions.

If Linux reports serial permission denied, on Debian/Ubuntu run
`sudo usermod -aG dialout "$USER"`, then log out/in (or restart Linux on Chromebook).
This is a one-time Linux permission fix; it is not a reason to run the wizard as root.

An interrupted SD copy stays **`pos-install.copying`** and is never offered as a
completed install. Preserve/move that folder before retrying. A different existing
`pos-install` or `/POS` installation is not overwritten. For an existing POS
system, use [RECOVERY.md](RECOVERY.md) to back up/reinstall or the signed updater.

### Backup stops with “Corrupt data” or the screen stays black

This can happen during a fast esptool USB read through ChromeOS forwarding.
Version 0.5.4-dev added one automatic ROM compatibility retry, using smaller
request/response transfers instead of the fast streaming stub protocol. Progress
is displayed as readable, rate-limited lines. The retry is slower; leave USB
connected while it is making progress. Do not start another serial program.

If the backup still stops, **no restore or POS firmware write has been attempted**.
A `.part` / `.stub-failed` file is incomplete and cannot be used to restore.
After the wizard has stopped, unplug USB, switch OFF, release G0/all keys,
wait 10 seconds and switch ON. The previous firmware should boot; reconnect
USB and share it with Linux again before retrying. If ROM reads also fail,
try another data cable/port or a regular Linux computer: the tool cannot repair
ChromeOS USB forwarding. Keep all completed backups.

### New SD files prepared, but the old upside-down installer still boots

Preparing the SD card does not reflash the software on the ESP32. The wizard
first restores a saved recovery backup, asks you to confirm the original
manufacturer software boots, and **only then** asks you to type **FLASH** to
write the downloaded POS USB installer. If the wizard stops before that step,
the new installer has not been flashed.

In the reported 0.5.5-dev attempt, a new snapshot of an older POS installer was
created and restored. The wizard then received `t` at its Y/N boot question and
stopped. That explains why the old installer and its old input behavior remained.
This is not evidence that the 0.5.5-dev driver changes were tested on the device.

Use the **earliest original pre-POS factory backup**, not a fresh snapshot of
POS. The updated wizard identifies POS snapshots by their application descriptor
and refuses to treat them as factory recovery proof. Snapshots remain available
in the separate restore menu. Previously observed original recovery backups are
labelled clearly; the operator must still confirm the factory software is visible.
Invalid Y/N input is now re-prompted instead of silently declining.

For the reported device, the known original filename ends in
**1792184685107168701.bin**. Keep it and its `.json`. When that original software
has been restored and is visible, answer **Y** and continue to the **FLASH**
prompt. Look for **NEW POS VERSION installer USB flash and readback completed**.
New firmware also shows its version in the on-device installer title and prints
`POS_BUILD` on USB at startup.

A card already prepared with 0.5.5-dev can be kept: use the new pos-setup with
`--release v0.5.5-dev`, choose **Flash Cardputer only**, and select that original
backup. The repaired host prompts are embedded in the new executable. This
installs the already-prepared 0.5.5-dev software; formatting/re-copying is unnecessary.

### Upgrading the prepared installer after the upside-down-screen fix

The display/keyboard fix was introduced in **both 0.5.5-dev firmware roles**
and is included in 0.5.6-dev. Refresh the
SD package as well as the USB installer; using `--release v0.5.3-dev` would keep
installing the old firmware.

If the card has only the old installer files and you have **not installed POS**:

1. Power off the Cardputer and connect the card reader to your Chromebook.
2. In ChromeOS Files, rename the card's top-level **pos-install** folder to
   **pos-install-old** (or another unused name). Keep the old files; formatting
   is unnecessary. Preserve any `pos-install.copying` folder under another name too.
3. Download this release's **pos-setup**, replace the old executable and run it
   with `--release v0.5.6-dev`. Choose **2. Prepare the microSD only** and select
   the card's shared top-level folder. It copies and verifies the matching package.
4. Eject the card, put it into the powered-off ADV, connect the ADV and share its
   USB connection with Linux. Run the same command and choose **3. Flash Cardputer
   only**. Select the earlier verified factory backup, rather than making a new
   backup of the POS installer. Follow the restore/observed-factory-boot prompts,
   then type FLASH when asked.
5. On the Cardputer choose **Install POS / resume**, type **INSTALL**, and
   power-cycle when prompted. Setup uses black screens. Navigate with Fn held
   plus the printed arrows; Enter also works while Fn remains held. Fn+backtick
   goes back, and the printed Del key erases the previous character.

With the paths used in the reported Crostini session, run:

```sh
chmod +x "/mnt/shared/MyFiles/Downloads/pos-setup"
"/mnt/shared/MyFiles/Downloads/pos-setup" --release v0.5.6-dev --sd-folder "/mnt/shared/removable/POS Drive"
```

If the card already contains an installed **/POS** tree, keep it and your files.
Use the signed Software Update from the installed OS, or the documented backup
and reinstall path. The card-preparation wizard intentionally refuses to overwrite
an installed system. Keep the private factory `.bin` and its `.json` on Linux.

Optional commands for troubleshooting:

```sh
~/pos-setup --download-only                 # Download/verify without device changes
~/pos-setup --release v0.5.6-dev             # Use a specific published version
~/pos-setup --bundle /path/pos-0.5.6-dev.zip # Use a ZIP and matching .sha256 offline
~/pos-setup --sd-folder '/mnt/chromeos/removable/YOUR CARD'
```

Published development bundles use checksums and GitHub HTTPS, not a signed USB
bootstrap. Physical ADV USB recovery, SD removal/power-loss and actual Chromebook
USB forwarding still require hardware testing. The wizard does not turn a software
test into a claim that real hardware was validated. The longer [INSTALL.md](INSTALL.md)
explains each underlying step if you need it.

## Future updates from Panputer

Once 0.5.3-dev or newer is installed, use **Settings → System / software update →
Software Update → Check for Updates**. Connect to Wi-Fi, review the new version,
and choose **Download and install verified update**. The official feed and trusted
key are included. Firmware updates restart; keep power and the SD connected and
let POS run normally for at least 60 seconds. Asset updates do not reflash firmware.
You do not need the Linux installer again for compatible future updates.
