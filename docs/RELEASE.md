POS 0.5.1-dev adds pos-setup, a beginner-friendly executable CLI for Linux and
Chromebook Linux/Crostini. No ESP-IDF, compiler or source checkout is required.

Download **pos-setup**, move it into Linux home (ChromeOS: Linux files), run
`chmod +x ~/pos-setup` then `~/pos-setup`, and choose Install POS. See EASY_INSTALL.md.

- Downloads the latest complete GitHub version, including prereleases, with private
  repo browser sign-in through GitHub CLI when needed. Verifies the ZIP, all bundle
  hashes, firmware versions/images and SD manifest; never executes downloaded scripts.
- Detects candidate removable media; excludes system/swap/mapped/internal storage
  and the disk holding the wizard/downloads/backups. Offers FAT32 or keep-files mode,
  rechecks device identity/mounts and requires an exact ERASE /dev/... confirmation.
- Copies a staged SD package and verifies every file before completion; preserves
  existing POS systems, different installers and interrupted copies.
- Guides USB detection, a double-read private full-flash backup, checked restore
  and an actual user-observed prior boot before installing POS. Earlier matching
  verified backups are available for recovery after interrupted attempts.
- Requires FLASH before the checked USB installer write/readback. Shows progress
  and the final on-device INSTALL/power-cycle/setup steps. Includes a restore menu.
- Crostini shared-card mode copies to an already FAT32 card without formatting.
  Raw formatting and USB flashing depend on ChromeOS forwarding; the CLI cannot
  override ChromeOS restrictions. A read-only --doctor reports available access.
- Keeps firmware features/partition layout from 0.5.0-dev. Maps/navigation and
  Meshtastic compatibility remain the software roadmap.

Release assets include pos-setup plus its checksum, the complete matching firmware
ZIP/checksum and four UI previews. The full ZIP also includes the executable and
beginner instructions. Download from this release, not the automatic Source code ZIP.

Experimental prerelease. Disk-selection/mutation safeguards and the wizard are
covered by host tests; partition/FAT commands were also exercised on sparse 64 GB
regular-file images without root or a block device. Actual SD formatting/ejection,
ADV flashing/recovery and Chromebook USB forwarding have not been tested here.
Keep your card data and original factory backups safe.
