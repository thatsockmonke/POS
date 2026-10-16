POS 0.5.4-dev fixes the Linux/Crostini installer's USB backup failure handling.

- A failed fast esptool read now restarts that complete read once using the
  ESP32-S3 ROM's smaller request/response transfers. Later passes stay in ROM
  mode; a second failure stops without restoring or flashing firmware.
- Both full-flash reads must equal the detected physical flash capacity and
  have identical hashes. Partial transfers remain private diagnostic files;
  they are never offered as verified backups. Device identity/security/capacity
  are rechecked before fallback and after each successful read.
- The original app stays stopped between reads, preventing boot-time NVS changes
  during backup. Progress handles esptool backspaces without unreadable question
  marks, and errors retain the actual USB failure plus power-cycle instructions.
- An already prepared 0.5.3-dev card can be kept: run this new pos-setup with
  --release v0.5.3-dev and choose Flash Cardputer only. The executable contains
  the repaired USB helpers; downloaded bundle code is never executed.

The reported failure occurred before any firmware write. Factory firmware booted
again after a power cycle. Host fault-injection tests cover short packets,
failed fallbacks, mismatches, incomplete reads and changed devices. Both firmware
roles are built; successful physical ROM backup/restore/POS installation remains
to be checked on the Cardputer. Experimental prerelease.
