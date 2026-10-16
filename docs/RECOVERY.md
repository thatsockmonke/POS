# Recovery, backup and migration

The factory installer and runtime share the portable recovery library. It does
not format cards, recursively wipe unknown directories, or delete the only backup.
The runtime Recovery app exposes backup, restore, a read-only browser of the
previous installation, a small file-recovery terminal, diagnostics and sleep.
Repair/reinstall restart into the factory installer so the running runtime is
not overwritten underneath its own execution.

## Defined system/user boundary

Bundled `/POS/System/**` is immutable and verified against `installed.pos`.
Installer metadata and work/cache/update directories are system-owned. Mutable
preferences live in `/POS/Users/settings.{0,1}.json`; the stable profile directory
is `/POS/Users/Owner`, independent of the user's display name.

Backup scans all regular SD files except these system/recovery paths:

- `/POS/System`, `/POS/Temp`, `/POS/Cache`, `/POS/Updates`, `/POS/.pos-transaction`.
- `/POS.installing`, `/POS.previous`, `/pos-install`, `/.POS-Recovery-Temp`.
- Both known ZIP/folder backups, their `.part` paths, central scratch and SHA-256 sidecar.
- `/pos-reinstall.pos` and known restore-prompt/completion markers.

This includes ordinary root-level user-created files, documents, downloaded maps,
music, games, app data and user-installed app files outside System. POS.previous
is a separate recovery source; copy it separately before removing it. Unrecognized
directories and files are not silently discarded or treated as old OS assets.

## Streamed ZIP backup

The scan shows files, source bytes, conservative temporary-space requirement and
available space. A fresh backup refuses an existing completed or partial backup
name. Originals stay unchanged. The writer uses an 8 KiB buffer and streams ZIP
central-directory records to a separate temporary SD file, not an in-memory list.

`POS-Filesystem-Backup.zip.part` becomes `POS-Filesystem-Backup.zip` only after
ZIP finalization and a complete independent validation of metadata, entry paths,
offsets, descriptors and every CRC32. A SHA-256 sidecar is also written when
possible. Failure to write the sidecar does not invalidate a CRC-verified ZIP;
the original ZIP remains independently verifiable. Unknown files are never
removed to free space. Incomplete `.part` files remain visibly incomplete.

ZIP uses UTF-8 filenames, stored entries and streamed data descriptors. Ordinary
Linux/macOS/Windows ZIP tools can open it. The archive contains:

```text
POS-Backup.json                  format, migration, source version, time/unknown,
                                 expected source byte count and file count
files/POS/Users/Owner/...         ordinary user paths under files/
files/Music/...
files/Downloads/...
files/<other user paths>
```

The ZIP backend retains its 2048-file/below-2-GiB bounds because ESP/newlib seeks
use signed 32-bit offsets. ZIP64, compressed input and split ZIPs remain unsupported.
The backup command automatically chooses the following folder format when needed,
including when empty directories would otherwise be omitted.

## Large folder backups

`POS-Filesystem-Backup/` is an ordinary directory:

```text
backup.json              completed format-2 metadata and inventory SHA-256
inventory.jsonl          bounded JSON records: path/type/size/SHA-256
files/                   original user folder structure and readable files
.pos-backup-state.json   ownership/version/counts for interrupted-copy recovery
interrupted/             retained partial copies/inventories, if a resume needed them
```

No individual source file is split, encoded or made dependent on POS. Files up to
FAT32's 4-GiB-minus-one-byte limit stream sequentially; aggregate bytes use 64 bits.
There is no 2048-file or 2-GiB total bound. Empty directories are included. Paths
remain limited to 240 bytes and depth 16, with symlinks, unsafe names and ASCII case
collisions refused. Inventory files must fit the device's 2-GiB metadata seek range;
this is far beyond a practical card's file inventory. Metadata uses exact integers
through 2^53-1. A shared name-selection buffer keeps RAM bounded; very large flat
directories take more scan passes than a computer's in-memory directory sort.

Creation uses `POS-Filesystem-Backup.part`. Every copy is read back, hashed and
compared with its source. The entire inventory/tree is independently verified,
including missing/extra entries, and compared against current user files before
rename publishes the completed folder. Copy the **entire folder** to your computer;
opening `files/` recovers documents/music without any POS-specific utility.

`recovery backup-set` explicitly chooses this format for small collections too.
`recovery resume-backup` accepts only a matching owned `.part` folder. Verified
copies are reused; incomplete copies and previous inventories are moved into
`interrupted/`, not erased. Future/unknown state is refused. An interrupted initial
owner-state write cannot be guessed complete and requires preserving the folder
on a computer. Interrupted ZIPs remain preserved `.part` files; they are not
resumed in place. Recovery's on-device backup action offers folder resume.

Space estimates include the source size, directory clusters, inventory and report
overhead. They remain conservative even for a resume. A 64-GB card holding more
than about half its capacity may need an off-device copy because another full copy
will not fit; this software does not pretend extra storage exists. Keep files quiet
while backing up. No FAT operation can provide a concurrent filesystem snapshot.
If ZIP and folder backups both exist, automatic selection refuses ambiguity;
select an explicit path on Linux or move one backup aside on your computer.

## Reinstall transaction

1. Validate the new package, hardware/version headers, payload hashes and space.
2. Scan, create and verify a user backup, or verify an existing backup. Before
   first retirement, compare the backup with the **current** user files so an old
   valid archive cannot be mistaken for a current backup.
3. Prompt the user to power off and copy the ZIP or entire folder onto their computer. Continuing
   requires the explicit `REINSTALL` word. No automatic erase option is offered.
4. Write `/pos-reinstall.pos` binding the new package ID to the verified backup
   identity (ZIP SHA-256 or completed folder metadata SHA-256). Only then rename old `/POS` to `/POS.previous`.
5. Run the normal install transaction. A restart after retirement can resume from
   the matching journal/backup; mismatches stop without deleting either tree.
6. On verified new runtime boot, offer restore before setup writes preferences.
   Source backup and POS.previous stay available, including after restore failure.
7. After successful restore, write its completion marker and clean only known
   unchanged installer media. A deferred or failed restore retains that media.

A torn initial journal is preserved rather than guessed complete. Unknown
POS.previous or staging trees block destructive reuse. After validating the new
installation and copying all recovery material to your computer, move previous
trees/journals/backup files off the card before starting another transaction.
The implementation intentionally does not automatically garbage-collect old user data.

## Restore and migration

Restore validates the entire archive **before writes** and checks available space.
It rejects traversal, system paths, ASCII case collisions, file/directory collisions,
symlink entries, encrypted/compressed or unsupported archives, bad sizes and CRCs.
Copies go through a verified temporary file before publication. Existing identical
files are reused. Existing different files, directory conflicts and unsupported
preferences are preserved at `POS/Recovered/<archive-id>/<original-path>`.
Original destination files and the backup are never overwritten to make restore pass.
An interrupted restore can be rerun; verified completed files are reused.

Current settings format 1 uses checksummed generation slots. Both restore backends
validate the complete envelope, SHA-256, schema and values. Raw settings originals
are preserved under Recovered; the newest valid archived generation becomes a
candidate for a fresh current settings slot only after all file copies finish.
Existing valid or future destination preferences take priority, including on retries.
Known corrupt or future settings are preserved rather than activated. Known legacy format 0
at `POS/Users/settings.json` maps `user_name`, `device_name`, `timezone` and optional
`brightness` into format 1 only when no current valid preferences exist. The old
JSON remains under Recovered. Unsupported newer preferences are never activated
or silently replaced; first-time setup uses defaults where possible and preserves
the unknown files. Data/apps requiring unknown migrations stay accessible as files;
this version does not execute restored user-installed app binaries.

## File recovery terminal

Commands are `ls [PATH]`, `pwd`, `cat PATH`, `cp SRC DST`, `free`, `reboot`,
`shutdown`. Paths are relative to the SD root. `cp` streams and verifies a new
destination, refuses system/reserved paths, preserves an existing destination and
never removes its source. `cat` shows a small preview rather than loading a whole
document. The browser pages through directories in batches of 12 entries with folder page
counts, fixed Previous/Next actions, first/last labels and an explicit empty state;
notices scroll across pages. Ctrl+U clears recovery terminal input and all text fields. This is not Bash or a general text editor.

Linux can run the same portable recovery engine without hardware:

```sh
python tools/posctl.py recovery scan --root /path/to/mounted-card
python tools/posctl.py recovery backup --root /path/to/mounted-card
python tools/posctl.py recovery backup-set --root /path/to/another-card
python tools/posctl.py recovery resume-backup --root /path/to/interrupted-card
python tools/posctl.py recovery verify --root /path/to/mounted-card
python tools/posctl.py recovery restore --root /path/to/destination --archive /path/to/POS-Filesystem-Backup.zip --confirm-restore
```

Native services use the same limits and conflict rules as firmware. The helper
compiles from repository sources with the pinned SDK cJSON and system OpenSSL.
Each restore writes an append-only JSONL report beneath
`POS/Users/Owner/Recovery/restore-<backup-id>.jsonl`, recording original paths,
destinations, reused files, preserved preferences and a final completion record.
A cut before completion leaves a partial report; retry may append repeated
observations. Settings migration is delayed until file restoration succeeds.
Unknown app formats remain accessible as user data; there is no invented migration
for app schemas that do not yet exist. ZIP CRC/folder SHA checks detect corruption; they do not authenticate a malicious author.
FAT is not transactional: file flushes and logical resume tests do not guarantee
survival of torn sectors, card-controller faults or power loss. Keep off-device
copies and perform the hardware test matrix before trusting real data.
