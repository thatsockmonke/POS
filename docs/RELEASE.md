POS 0.3.6-dev adds an SD-backed Text Editor and shared Wi-Fi services.

- Text Editor: New/Open/Recover, ordinary WASD typing, Fn+arrows, Ctrl+S save,
  Ctrl+F find, Ctrl+G go to line and an unsaved-changes prompt on closing.
- Documents stay on SD using snapshots and streamed piece indexes. Verified saves
  keep previous originals; Save As refuses collisions. Working drafts and normal
  kept-draft text files remain recoverable on ordinary computers.
- Wi-Fi: scan/manual/saved connections, live status and SNTP time synchronization.
- Optional saved profiles use a passphrase-encrypted AES-256-GCM NVS vault with
  PBKDF2-HMAC-SHA256 (100,000 iterations). Session-only connections remain available.
- HTTPS downloads and GET/POST API responses stream to SD with certificate
  validation, readback checks and optional expected SHA-256 verification.
- Temporary LAN file transfer supports uploads and one selected export, with a
  fresh session key and automatic shutdown when leaving the transfer screen.
  Local HTTP is unencrypted: use a trusted network.
- Nine-app launcher retains black/white icons, custom stripe backgrounds, A/D to
  move, W to open and one foreground app at a time. Existing boot animation retained.
- Includes full installation and editor/network/recovery instructions.

Download pos-0.3.6-dev.zip and its checksum, extract it, and read INSTALL.md.
Flash the small USB installer at 0x0. Extract the inner pos-install.zip onto a FAT32
microSD; the installer verifies and installs the final runtime itself.

This is experimental development firmware. The complete master roadmap is not
implemented. Actual ADV operation and power-loss resilience still require hardware
validation; preserve factory flash and user data before experimentation.
