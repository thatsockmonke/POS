# Versioned app saves and migrations

User audio/documents remain ordinary files. Versioned app metadata uses the
portable `pos_appdata` API; it does not require a package manager. An app owns
`POS/Users/Owner/Saves/<app-id>/`, where app IDs contain 1–32 lowercase letters,
digits or hyphens. Large data stays in streamed companion files. A save envelope
contains bounded JSON metadata/references (at most 1024 bytes), not whole tracks,
documents or maps.

Two `slot-0.json` / `slot-1.json` envelopes contain exactly:

```json
{"format":1,"app":"counter","schema":1,"generation":1,"payload":"{\"count\":7}","crc32":"xxxxxxxx"}
```

`payload` is the exact serialized JSON string. CRC-32 covers UTF-8
`<app>\n<schema>\n<generation>\n<payload>`; `crc32` is eight lowercase hex digits.
Generations start at 1; generation modulo 2 chooses the slot. Load verifies the
envelope, CRC, JSON and bounded integers, selecting the latest valid generation.
An unsupported future envelope/schema in either valid slot blocks writes.
The loader does not rewrite files while inspecting them.

`pos_appdata_load(root, app, current_schema, migrate_step, ctx, out, error)` returns
0 for current, 1 for absent, 2 for a successful in-memory migration, -1 for damage
and -2 for unsupported/future data. A converter handles **one schema step** at a
time into a separate JSON buffer; each output is validated. Failed/unknown steps
leave the original SD files intact. Migration chains are bounded to 16 steps.
The caller must refuse saving after negative results; Counter does this explicitly.

`pos_appdata_save` checks the current generation/schema again, refuses stale/future
writes, writes a separate exclusive `.part`, syncs and reads it back, then replaces
only the older target slot. The other known-good slot remains available. A stale
partial blocks retry until inspected/moved through Files; it is not automatically
erased. Before committing a schema change, the **exact original** envelope is
copied and verified as `migration-<schema>-<generation>.json`. Conflicting migration
backups block writing. Originals and preserved migration snapshots remain readable
on a normal computer. CRC detects accidental corruption; it is not authentication.

## Concrete Counter migrations

- Previous `counter.txt` / `counter.previous.txt` plain integers are validated and
  imported to schema 1 without deleting or modifying either original.
- Versioned Counter schema 0 `{"value":N}` becomes schema 1 `{"count":N}`.
  The old envelope is retained permanently before commit.
- Values must be integral and between -1,000,000 and 1,000,000. Future schemas,
  malformed payloads, checksum damage without a valid fallback or symlinked paths
  are refused; opening the app cannot silently reset and overwrite them.

ZIP and folder restores already preserve user saves and conflicts as ordinary
files. App conversion happens through the known app's loader after restore,
without overwriting the new operating system. Existing Settings migrations remain
separate, as documented in RECOVERY.md. Future app/save schemas require explicit
converters when those schemas are defined; unsupported data stays accessible.
No framework can safely guess the meaning of arbitrary future save formats.

## Settings format 3: microphone gain

0.5.9-dev writes checksummed settings format 3 with `microphone_gain` (a power of
two from 1 to 64). Formats 1 and 2 load without rewriting user files, defaulting
gain to 8x and retaining supported identity, timezone, brightness and background.
The first intentional save writes format 3 to the alternate slot. Backup/restore
uses the same decoder/migration path and preserves the gain. Unknown future
formats remain protected. Older firmware treats format 3 as future preferences
and preserves it rather than silently downgrading it; user documents and audio
remain ordinary files regardless of preferences compatibility.

## Settings format 4: UI sounds

0.6.1-dev saves `sounds_enabled`, `sounds_volume` (0–100), `sounds_keys` and
`sounds_alerts` in the same checked generation slots. Formats 1–3 load without
rewriting; missing sound preferences default to enabled, 20% volume and both
categories on. Identity, wallpaper, timezone, brightness and microphone gain are
preserved. Backup/restore uses the same validated decoder. Unknown future formats
remain protected. Firmware older than 0.6.1 treats format 4 as future preferences
rather than rewriting it; user documents/audio remain ordinary recoverable files.
