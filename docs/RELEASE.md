POS 0.5.6-dev clarifies the installer recovery step that prevented the new
0.5.5-dev firmware from being flashed in the reported Crostini attempt.

- Backup menus distinguish POS snapshots from original saved software. Previously
  observed original recovery backups are labelled clearly. A recognized POS
  snapshot is preserved, but cannot be restored as proof of factory recovery.
  The separate backup-restore menu can still restore those POS snapshots.
- Invalid Y/N input, including the reported `t`, is re-prompted. Explicit No and
  cancellation still stop; a typo cannot silently end a successful recovery test.
- Prompts distinguish preparing the SD, restoring saved software, and actually
  flashing the NEW POS installer. The firmware version is printed before writing
  and after verified completion. The on-device installer now displays its own
  version; POS_BUILD prints role/version on USB at startup.
- The display rotation, black setup screens and stock ADV Fn-key behavior from
  0.5.5-dev are retained. The new user log shows an old POS snapshot was restored
  and the wizard stopped before its FLASH step; it did not test the 0.5.5-dev
  firmware on the device. No additional speculative driver change is included.
- Existing 0.5.5-dev SD media can be used with this new executable by selecting
  --release v0.5.5-dev and Flash Cardputer only. Use the original pre-POS factory
  backup, not another snapshot of the old POS installer. EASY_INSTALL.md explains
  the exact distinction and the known original backup from the reported session.

Both firmware roles build and host tests cover POS snapshot recognition,
confirmation typos, recovery evidence, USB ordering, physical key mapping and
black setup screens. New physical display/input confirmation still requires
completing the USB flash. Experimental prerelease.
