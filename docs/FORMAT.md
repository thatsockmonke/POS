# Package format 1 and installation transaction

All paths below are relative to the SD mount. The `manifest.pos` file is UTF-8
JSON, not a proprietary binary format. A representative manifest is:

```json
{
  "format": 1,
  "model": "m5stack-cardputer-adv",
  "version": "0.1.0-dev",
  "firmware": {"path": "firmware.bin", "size": 123456, "sha256": "64 lowercase hex digits"},
  "files": [
    {"path": "System/Config/defaults.json", "size": 200, "sha256": "64 lowercase hex digits"}
  ]
}
```

The sizes and hashes above are placeholders; `posctl package` writes real values.
Unknown/duplicate object keys, unsupported format/model, bad numeric bounds and
ambiguous paths are rejected. The initial format supports at most 64 payload
files and a 32 KiB manifest. JSON nesting is capped before cJSON parsing. Version
strings are 1–31 ASCII characters starting with a digit. Payload files are limited
by FAT32's 32-bit file size; firmware must fit the 0x170000-byte OTA slot.

Only `System/` payload paths are allowed. Each path is under 160 bytes, with at
most 8 components of at most 48 ASCII characters. Dot-prefixed names, trailing
dots, spaces, Windows device names, traversal, backslashes, case-insensitive
collisions, file/directory conflicts, `.part` names and internal metadata are
rejected. Bundled System files are immutable for boot validation. Future mutable
preferences and user documents must live outside these bundled paths.

## Ordering

1. Parse the bounded manifest, derive its SHA-256 transaction ID, and inspect
   existing destination ownership without writes.
2. Verify every source file's exact size/hash, firmware chip/project/version
   header, target partition capacity, and conservative SD free-space requirement.
3. Ask for explicit `INSTALL` on the console. Repeat preflight after confirmation.
4. Create `/POS.installing/.pos-transaction` containing the 64-byte transaction
   ID. An existing staging tree is reusable only when its marker matches.
5. Stream each file through an 8 KiB reusable buffer to a `.part` file. Flush,
   fsync, close and independently rehash the destination before publishing it.
   Already correct files are reused. Known corrupted files in this unconfirmed
   transaction can be replaced only after a complete verified `.part` exists.
6. Save the exact manifest at `System/installed.pos`, verify all staged files,
   then rename `/POS.installing` to `/POS`. The transaction marker remains.
7. Stream the runtime into an inactive OTA slot, checking the source hash again.
   ESP-IDF validates the image at `esp_ota_end`. Rehash the entire packaged byte
   range from flash and compare it with the manifest.
8. Set the OTA boot partition, retain the package and request a cold restart.
9. Runtime validates its manifest, transaction ID, payload files, version and
   running flash bytes. Only then confirm the pending firmware with IDF.
10. Persist `System/.boot-ok`, then clean up only listed unchanged installer files.
    Remove the installer manifest last, allowing interrupted cleanup to resume.

The installer remains in the factory partition. IDF boot rollback is enabled.
An existing `/POS` is repairable by this initial installer only when it belongs
to the exact same unconfirmed transaction and has no boot-success marker.
Fresh installation refuses confirmed systems. The recovery Repair action accepts
only the exact same transaction package; Back Up & Reinstall verifies a current
backup and retains the old tree before invoking this install transaction. See
[RECOVERY.md](RECOVERY.md) for its journal, ZIP/folder formats and preference migration.

## Filesystem limits and interruption

FAT is not a transactional filesystem. File flushes, readback verification and
renames reduce risk but cannot guarantee safety against torn directory sectors,
card controller failures or power loss. FatFs cannot rename over an existing
file. For an owned, unconfirmed damaged file, POS first verifies its replacement
`.part`, removes that damaged destination and renames the replacement. A power
cut in this gap leaves the original installer payload and retryable transaction.
It never uses this replacement path for a confirmed installation or user file.

If power fails before the initial ownership marker is published, the resulting
unknown staging tree is preserved for inspection rather than guessed safe to
delete. A damaged marker similarly requires inspection on an ordinary computer.

The free-space estimate includes the full payload, standard system directories,
a fresh temporary copy and
conservative directory/allocation overhead (up to 64 KiB per estimated FAT
allocation unit). A resumed install may therefore be refused even though some
correct files can be reused; no existing data is deleted to make room. This
initial estimate targets normal FAT32 cards with clusters no larger than 64 KiB.

The manifest and parsed metadata are bounded heap allocations. File data and
firmware are always streamed; assets never need to fit in RAM. Boot reports
current/minimum heap, but real heap and SD speed remain hardware measurements.

## Test boundary

The native runners compile the exact C parser/copy/transaction/cleanup code and
uses OpenSSL SHA-256 in place of ESP-IDF's mbedTLS backend. Firmware-write and
boot-selection platform hooks are simulated; real ESP-IDF OTA and FAT drivers
are cross-compiled, not executed by those tests. Process interruption is tested
at meaningful transaction boundaries. This does not substitute for hardware
power-cut tests, USB recovery tests or publisher authentication. Separate native
tests also exercise app/input/UI callbacks, settings slots/migration and streamed
ZIP/folder backup/restore, more than 2048 files, over-64-GiB sparse scans, resumed
owned backups, mid-write failures, journal-before-retirement changes, delayed settings
migration, paginated notices, and hardware evidence validation. USB/serial command
orchestration is mocked; no physical serial device is opened.
