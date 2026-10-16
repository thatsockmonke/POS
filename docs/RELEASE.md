POS 0.3.2-dev is an experimental Cardputer ADV operating-environment foundation.

- Corrects boot colors/order to the pansexual flag: pink #FF218C → yellow #FFD800
  → blue #21B1FF, once per boot at 180 ms per color.
- Adds Ctrl+U clear and character-limit counters while keeping password masking.
- Validates setup names in place and preserves previous settings on cancellation.
- Shows folder page counts and empty folders, with fixed Previous/Next actions.
- Clarifies SD preparation, file verification, firmware writing/readback and the
  final instruction to power-cycle with the SD still inserted.

Download the named pos-VERSION.zip and its checksum, extract it, and read INSTALL.md.
Flash the small USB installer at 0x0. Extract the inner pos-install.zip onto a FAT32
microSD; the installer verifies and installs the final runtime itself.

The full master roadmap is not implemented. Physical ADV flashing, display/key
mapping, factory recovery, SD behavior, sleep and electrical power-loss resilience
still require validation. Preserve factory flash and user data before experimentation.
