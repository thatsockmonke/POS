# Firmware history and rollback

POS 0.6.2-dev keeps up to 15 completed firmware/System pairs under the hidden,
system-owned directory `/POS/.History`. Each entry contains `manifest.json`
(the publisher's signed envelope), `firmware.bin`, matching `payload/System/...`
files, and a `complete` marker. These are ordinary files readable on a computer.
Files does not treat this protected directory as user-editable storage.

Updates installed by 0.6.2 or newer archive the currently running image and its
matching System **before** altering the installation. The running image is read
from internal flash, streamed, hashed against trusted signed metadata and copied
with bounded buffers. Assets are verified against the same package. Completion
is published only after full verification. Interrupted copies stay incomplete;
they never appear as valid history. Failure to preserve/verify history stops the
update while retaining the current installation. Allow several MiB of free space.

The update that first installs 0.6.2 runs through the older updater, so it cannot
retroactively archive every previously installed release. After its healthy boot
has completed, open **Settings → System → Firmware history → Import previous
installation**. This can archive a retained previous System only when its exact
firmware still exists in the inactive OTA partition and trusted signed metadata
is available. Cached metadata is used first; Wi-Fi may be required to retrieve
matching official metadata. Missing, ambiguous or untrusted pairs are refused.
Older releases without compatible signed metadata remain preserved in existing
recovery files; they are not invented or marked valid. A custom source must retain
its matching signed envelope. The archive also keeps `signed-update.json` with
new Systems so future captures can work offline.

To roll back:

1. Open **Settings → System → Firmware history**, select a release, read the
   explanation and choose **Verify and roll back**.
2. POS authenticates the envelope with the currently trusted key, verifies all
   archive hashes, and preserves the current firmware/System pair too. Retention
   protects the selected rollback target while keeping the newest 15 completed
   pairs possible. Incomplete/unknown/corrupt work is preserved for inspection.
3. The verified image is written to the **inactive internal OTA slot**, checked
   by ESP-IDF and independently read back. Only then is the journal written and
   the matching SD System swapped. An identical resident image needs only its
   System restored. Same-version images with different firmware hashes are
   refused because boot-version recovery would be ambiguous.
4. Keep the card inserted during reboot. Existing healthy-boot confirmation and
   ESP-IDF automatic rollback remain active. Interrupted writes leave the current
   slot bootable; interrupted System swaps use the existing transaction journal.

Documents, recordings and other user files stay in place. Their settings/save
formats are **not** rolled back; older apps may not understand newer formats.
Unknown formats remain preserved. Rollback does not lower the accepted signed
sequence. Return to an archived newer release through History, or wait for a
higher-sequence online update. Signing-key changes can make older archives
untrusted; POS does not silently trust the key stored beside an archive.

Only authenticated paths owned by a completed entry are pruned, after a new
replacement has been fully verified. Unknown files and symlinks are preserved;
corruption can stop automatic pruning rather than risking data. At most 15
completed pairs are normally retained; unknown/incomplete preserved material
may occupy additional space and requires deliberate recovery inspection.

## Internal flash decision

The existing independently bootable factory recovery image plus **two OTA
runtime slots** are retained. No partition table migration or eFuse change is
performed. A single-runtime layout could use factory recovery to rewrite that
slot from SD, but it loses automatic runtime rollback, depends on accessible
SD media after a failed write and requires a risky partition-layout migration
on installed devices. It therefore does not meet the same failure guarantees.
Historical releases belong on SD; the second runtime slot remains transactional
working space, not an archive. Firmware never boots directly from microSD.

Cloud tests simulate flash writes, boot selection, interruption and filesystem
recovery. They do not establish electrical power-loss, SD-controller durability
or bootloader behavior on a physical Cardputer. Keep USB recovery backups.
