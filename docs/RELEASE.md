POS 0.5.5-dev fixes the ADV display, setup screens, keyboard behavior and USB
verification order found during the first real Cardputer installation.

- Display uses the official M5GFX landscape orientation (swap XY, mirror X)
  and matching 40/53 viewport offset. This corrects the reported 180-degree
  rotation in both the installer and normal runtime.
- Installer/first-time setup and shared dialogs use black backgrounds, white
  text and a white selection highlight. Hints identify Fn+arrows, Fn+backtick,
  Enter and the printed top-right Del key.
- Keyboard behavior follows pinned stock M5Cardputer-UserDemo ADV firmware:
  Fn+arrows/Escape/Delete, Fn+letters for capitals, and ordinary Enter/Tab/
  numbers/punctuation while Fn is held. Aa/Shift, Ctrl, Alt and Opt/Meta are
  independent; key-up events retain the pressed key when modifiers change.
  POS keeps Fn+Space as global Home. Fn+Del deletes ahead in Text Editor.
- USB writes and readback verification now both keep the application stopped.
  Only a successful comparison permits reset, preventing first-boot NVS writes
  from changing a merged image before verification. Factory restore uses the
  same sequence. A real verification failure still stops; it is never ignored.
- Reusing a verified backup now retains earlier observed-boot records and saves
  a fresh record, instead of stopping because its old record already exists.
- Keep the known-good private factory backup. For an uninstalled prepared card,
  rename the old pos-install folder, prepare the new SD package, then flash the
  matching 0.5.5-dev installer. EASY_INSTALL.md includes the one-port Crostini
  process. The old --release v0.5.3-dev retry command cannot deliver these fixes.

The user confirmed two matching 8 MiB ROM backup reads, verified factory restore
and observed boot, then installer boot and SD detection. A post-reset full-image
verification failed; POS had already booted and can write NVS. Host tests cover
verify-before-boot, stop-on-mismatch, all physical keys/layers, Fn-held menu
selection, forward Delete and black setup screens. Both firmware roles are built.
New orientation/keyboard behavior and the corrected USB sequence still require
confirmation on the physical ADV. Experimental prerelease.
