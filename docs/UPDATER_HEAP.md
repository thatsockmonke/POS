# Installing the 0.6.6-dev low-memory bridge

The menu numbers below describe the **0.6.6-dev installer**. Current 0.6.9-dev
POS Setup uses **1 — Prepare the microSD**, **2 — Flash Cardputer**, and
**3 — Restore backup**; see [the short current guide](EASY_INSTALL.md).

This focused release is based on **0.6.4-dev**, without the new world map or
numeric microphone gain. Those belong in the following full release.

The affected updater reserves 32 KB for any manifest, plus about 15 KB for 64
file records, and repeats those allocations during firmware-history verification.
Even a tiny update still goes through that old code before its firmware can run.
A smaller ZIP therefore cannot guarantee an OTA escape from this problem.

## Try the existing updater once after restarting

Before restarting, turn **Settings → Sounds → master Off** and **Wi-Fi → Known
Networks → Auto-connect Off**. Save both. Then power-cycle POS, manually connect
your saved Wi-Fi network, and go directly to Settings → System → Software Update.
Do not open Share/Recorder/Maps first. Both old service implementations only skip
creating their 4 KB/6 KB background stacks at boot, so toggling them without
restarting is insufficient. Re-enable preferences after the bridge boots.

This frees roughly 10 KB of task stacks and can reduce fragmentation, but it is
**not a guaranteed fix**. If the same heap
error returns, stop retrying and use the USB/recovery route below. Your current
installation remains usable; do not format the card or erase NVS to clear the error.

## File-preserving USB/recovery route

1. Power off and copy the SD card's user files onto your computer as an extra
   backup. Keep your original full-flash `.bin` and `.json` computer backup too.
2. Download **pos-setup from the 0.6.6-dev release**, then run it with the command
   below. Choose **2. Prepare the microSD only**. Folder mode copies installation
   files without formatting. Eject the card using ChromeOS Files.
3. Run the same command again with the Cardputer connected over USB and choose
   **3. Flash Cardputer only**. Follow the existing original-backup/recovery
   checks and confirm the displayed **0.6.6-dev** installer before flashing.
   Only one USB device needs to be connected at a time. USB recovery can reset
   internal Wi-Fi credentials; your SD files are separate.
4. Reinsert the card and boot the installer. Select **Back up & reinstall**.
   Let it create and verify the backup. At **Backup verified**, power off and
   copy the ZIP (or the entire POS-Filesystem-Backup folder) onto your computer
   before continuing. Reinsert the card, resume Back up & reinstall and type
   **REINSTALL** at its confirmation prompt. No formatting is required.
5. Power-cycle after installation. Accept the detected backup's **Restore user
   files** prompt. Check your documents/recordings before removing any backup.
   The former POS tree is retained as `POS.previous`; if it already exists, copy
   it onto your computer rather than deleting it blindly to bypass the guard.
6. Confirm System Information shows **0.6.6-dev**, reconnect Wi-Fi if needed, and
   allow the healthy-boot confirmation to finish. Future updates use the fixed
   manifest reader. Keep the backups until you have checked the full update too.

For the user's Crostini paths (after making the new executable runnable):

```sh
"/mnt/shared/MyFiles/Downloads/pos-setup" --release v0.6.6-dev --sd-folder "/mnt/shared/removable/POS Drive"
```

This uses the existing verified backup/reinstall workflow. USB flashing, real
FAT32 behavior, heap with Wi-Fi and power-loss recovery still need physical
validation; host tests do not claim to prove device behavior.

## Software fix and tests

Package records are now allocated for their real count (still up to 64). Manifest
text is read into an exact-size owned buffer, with bounds/truncation checks and
no second file-load copy. Signed-update JSON trees and history-discovery metadata
are released before the next phase. Signature/hash validation, completion markers,
transaction order and both OTA slots remain. Error messages include requested
text/record allocation sizes, and USB logs report free/minimum/largest heap.

Tests exercise a five-record manifest in 12 KB POS/cJSON heap, an 8 KB largest
allocation limit, the maximum 64-record/32 KB input, every manifest allocation
failure, and history+installation while target metadata remains held in 32 KB.
OpenSSL/libc internal allocations are outside this synthetic budget; it is not a
measurement of ESP32 free heap or hardware TLS use.
