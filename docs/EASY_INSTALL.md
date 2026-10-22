# Install POS on Linux or Chromebook Linux

You need a Cardputer ADV, USB data cable, FAT32 microSD and a card reader.
Firmware goes over USB; the microSD holds the installation files and system drive.

## Download and run

Download **[pos-setup](https://github.com/thatsockmonke/POS/releases/download/v0.8.4-dev/pos-setup)**
from the [0.8.4-dev release](https://github.com/thatsockmonke/POS/releases/tag/v0.8.4-dev).
Replace an older copy. You need Python 3.10 or newer; no compiler is required.

For the Chromebook paths used in this project:

```sh
python3 "/mnt/shared/MyFiles/Downloads/pos-setup" --sd-folder "/mnt/shared/removable/POS Drive"
```

On another Linux computer, use `python3 /path/to/pos-setup`. Do not run it with sudo.

```text
What would you like to do?
1. Prepare the microSD
2. Flash Cardputer
3. Restore backup
Choose a number (Ctrl+C to cancel):
```

## 1. Prepare the microSD

Connect the card and choose **1**. Select its top-level folder or removable drive.
POS Setup downloads the latest complete release and checks the files before copying.

- Already FAT32? Keep it; formatting is unnecessary.
- Need FAT32? Direct disk mode offers formatting with an exact erase confirmation.
  **Formatting erases the selected card. Copy wanted files onto your computer first.**
- Chromebook: share the card with Linux through ChromeOS Files. Shared-folder mode
  copies only; confirm the card is already FAT32. It cannot format through a share.

Eject the card in your computer's Files app after copying. One USB port is enough.

## 2. Flash Cardputer

Connect Panputer with a USB **data** cable, run the same command and choose **2**.
On ChromeOS, share the Cardputer through Settings → Developers → Linux → USB devices
if required. POS Setup can install its small USB helper once, then continues the
selected action without showing the main menu again.

Select the original pre-POS factory backup when offered. Keep its **.bin and .json**
files on the computer. The wizard checks device identity, backup integrity and
factory recovery before writing, then asks you to type **FLASH**.
A new snapshot saves whatever software is installed now; it is not a substitute
for an earlier original factory backup.

When USB flashing and readback verification finish:

1. Restart Panputer with the prepared SD inserted.
2. Select **Install POS / resume** and type **INSTALL**.
3. Leave power/card connected. Restart when prompted and complete setup.

Menu arrows work without Fn; Enter selects. Keep the SD inserted during normal use.
The installer installs and verifies matching runtime firmware and SD System files.
Internal Wi-Fi/settings can reset during USB flashing.

**Keeping an installed POS system and its files?** Back up the card first and use
**Back up & reinstall** on Panputer instead. Follow its backup/REINSTALL/restore
prompts. See [the detailed installation guide](INSTALL.md) and [recovery guide](RECOVERY.md).

## 3. Restore backup

Choose **3**, select the connected Cardputer and a matching saved backup, and
confirm restoration. This restores saved software; it does not install a new POS
release. Restored bytes are verified. Keep the original backup afterward.

## Updates and connection help

Already on 0.6.7-dev or later? Use **Settings → System → Software Update**.
No card formatting or map redownload is required for 0.8.4-dev.

- **Ctrl+C** cancels. Completed backups/downloads and partial files are kept.
- `--doctor`: read-only USB/card connection help; no writes or downloads.
- `--verbose`: detailed USB diagnostics. Normal output shows concise progress.
- `--release v0.8.4-dev`: select this version explicitly.
- `--bundle /path/pos-0.8.4-dev.zip`: use a downloaded ZIP and its checksum offline.

For the old manifest-heap error, see [UPDATER_HEAP.md](UPDATER_HEAP.md).
For map controls/deletion, see [MAPS.md](MAPS.md); for apps, see [APPS.md](APPS.md).

After upgrading, USB cable file transfer uses the separate `pos-transfer` tool;
keep the SD inserted and boot POS normally. See [the USB transfer guide](TRANSFER.md#usb-cable--linux-and-chromebookcrostini).
It does not flash firmware or require a fresh installation.
