# Files, search and clipboard

Open **Files**. Arrow keys choose an item; Enter opens its actions. Files stays a
text list, with a checkbox before each filename. Escape goes back; the global
Home shortcut closes the app.

- **Space:** toggle the highlighted checkbox. Select up to 32 user files/folders,
  including across pages and folders. **Ctrl+A** selects up to 32 items in the
  current folder. Folder actions → Selection offers Copy, Cut and Clear.
- **Ctrl+C / Ctrl+X:** copy/cut the checked items, or the highlighted item when
  nothing is checked. A cut keeps the originals until Paste succeeds.
- **Ctrl+V:** paste into the current folder. Folder actions → Paste also works.
  All names and destinations are checked first; existing files are never
  overwritten. Duplicate basenames and parent/child selections are refused.
  Copies stream to staging files/folders and verify before publication; moves
  use FAT rename. A failed batch keeps completed items and reports its count;
  only uncompleted cut items remain in the clipboard. A batch is not an atomic
  all-or-nothing transaction. Clipboard and selections last until Files closes.
- **Ctrl+F**, or Folder actions → Search: search this folder recursively. Choose
  filenames alone or filenames plus text contents. Matching ignores ASCII case.
  Text extensions: .txt, .md, .csv, .log, .json, .calc and extensionless documents.
  Contents stream through a small buffer; matches can cross read boundaries.
  Up to 64 results and 16 directory levels are shown. System/staging files,
  dot-prefixed entries and symlinks are skipped. Cancel safely with Escape/Home.
  Choose a result to open its containing folder, then choose the file normally.

**Open file** recognizes Calculator worksheets, audio, JPEG and BMP images.
**Edit PCM recording** opens the non-destructive Recorder editor. Existing
rename, Trash/restore, Share, details and confirmed permanent deletion remain.

Protected system files and POS settings cannot be moved, copied into or deleted
through these commands. Ordinary user files remain readable by other computers.
