POS is an experimental Cardputer ADV operating-environment foundation.

This build adds a one-time blue → yellow → pink boot sequence and a complete
ready-to-install ZIP: USB installer, SD package, helper tools, checksums, full
installation guide and an explicit implemented/partial/planned feature table.

Download the named pos-VERSION.zip and its checksum, extract it, and read INSTALL.md.
The small USB installer is flashed at 0x0. Extract the inner pos-install.zip onto
a FAT32 microSD; the installer verifies and installs the final runtime itself.

The full master roadmap is not implemented. Display/key mapping, factory recovery,
SD behavior, sleep and electrical power-loss resilience still require physical ADV
validation. Back up the original factory flash and user data before experimentation.
