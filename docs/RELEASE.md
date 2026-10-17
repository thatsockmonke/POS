# POS 0.6.9-dev

POS Setup now has a short three-choice menu, avoids duplicate startup screens
and shows concise progress. Maps can delete a downloaded region with confirmation
and recovery through Trash. Includes the 0.6.8 pan-step improvements.

## Installation tool

```text
What would you like to do?
1. Prepare the microSD
2. Flash Cardputer
3. Restore backup
Choose a number (Ctrl+C to cancel):
```

Preparing the card and flashing are separate steps, convenient with one USB port.
If the USB helper needs installing, POS Setup resumes the chosen action without
repeating the menu/banner. Routine USB output shows progress; --verbose enables
technical details and --doctor retains read-only connection help. Backup hashes,
matching-device checks, factory recovery/boot confirmation, explicit format/flash
consent, firmware verification and flash readback remain.

Download this release's **pos-setup** to get the simpler desktop tool. An on-device
OTA update does not replace the executable already downloaded on your computer.
See [EASY_INSTALL.md](EASY_INSTALL.md) for the short guide.

## Maps

**Saved regions → choose a region → Enter → Delete region**. Keep region is the
default. Confirmation moves its map, sources, graph and saved routes to
**Files → Trash**. Other regions, similarly named notes, attribution and the
world overview are retained. Restore through Trash, or permanently delete there
to reclaim SD space. Active routes close before their files move. The catalog
file moves last; failures retain remaining originals and recoverable Trash files.

## Update and validation

Use Settings → System → Software Update for the signed device update. No format,
map redownload, settings conversion or partition migration is required. Both
firmware roles, host regression tests and matching installer/SD packages are
checked before publication. All **396 host tests pass**, both firmware builds
fit the existing partitions, and simulated backup/reinstall/restore passes.
Tests cover the three action paths, cancellation,
helper resume, quiet/verbose diagnostics, region deletion, restoration, canceled
and blocked deletion, unrelated-file protection and deletion with an active route.
Physical USB/Cardputer/FAT32/card-removal/power-loss behavior remains unverified
in the cloud.
