# Easy installation: Linux or Chromebook Linux

You need a **Cardputer ADV**, a USB **data** cable, a microSD card (64 GB is fine)
and a reader for your computer. Back up anything important on the card first.
The firmware goes into the Cardputer over USB. The microSD holds its installation
files and becomes the POS system drive. You do not need ESP-IDF, a compiler,
manual flash addresses or an understanding of partitions.

## Start here

1. Open the [current POS release](https://github.com/thatsockmonke/POS/releases/tag/v0.5.1-dev).
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
  is visible. It never uses a force flag, burns eFuses or erases the whole flash.

Backups contain whatever software was on the Cardputer before that backup was
made. A new backup is the manufacturer's factory software only if that software
is still installed. Keep an earlier known-good factory backup; the wizard never
deletes one to make room for a newer attempt.

## Finish on the Cardputer

After the wizard says the USB flash/readback succeeded:

1. Power the Cardputer off and on with the prepared SD inserted.
2. On its screen use **W/S** and **Enter** to choose **Install POS / resume**.
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

Optional commands for troubleshooting:

```sh
~/pos-setup --download-only                 # Download/verify without device changes
~/pos-setup --release v0.5.1-dev             # Use a specific published version
~/pos-setup --bundle /path/pos-0.5.1-dev.zip # Use a ZIP and matching .sha256 offline
~/pos-setup --sd-folder '/mnt/chromeos/removable/YOUR CARD'
```

Published development bundles use checksums and GitHub HTTPS, not a signed USB
bootstrap. Physical ADV USB recovery, SD removal/power-loss and actual Chromebook
USB forwarding still require hardware testing. The wizard does not turn a software
test into a claim that real hardware was validated. The longer [INSTALL.md](INSTALL.md)
explains each underlying step if you need it.
