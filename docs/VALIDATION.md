# Collecting actual Panputer evidence

No physical Cardputer ADV is attached to the cloud machine. Hardware flashing,
factory boot recovery, battery calibration and power cuts remain **not run**.
The tooling below prepares and records those tests; it does not substitute host
results for physical observations. Use [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md)
as the full checklist and use a sacrificial card for interruption experiments.

## USB and factory recovery

Activate the pinned ESP-IDF environment on your Framework/Crostini computer.
Choose fresh output filenames and create an evidence directory:

```sh
mkdir -p evidence
python tools/posctl.py probe --port /dev/ttyACM0 > evidence/probe.json
python tools/posctl.py factory-backup --port /dev/ttyACM0 --output evidence/factory.bin
python tools/posctl.py validation-template --output evidence/observations.json
```

Verify the actual board revision, chip, capacity and flash security configuration.
The factory tool requires two matching full reads before publishing a private
backup. Keep a separate copy and the vendor recovery procedure. After reviewing
the matching-device backup, `factory-restore --confirm-restore` writes and verifies
flash; then **observe the original factory application boot**. Its readback JSON
deliberately does not assert that boot happened. Record that observation separately.

The firmware explicitly makes USB input nonblocking so an idle serial connection
cannot stall physical keyboard scanning or the held recovery shortcut. Native pipe
tests check this polling behavior; actual USB connection behavior still needs testing.

Serial capture does not deliberately assert reset or flash a device:

```sh
python tools/posctl.py logs --port /dev/ttyACM0 --seconds 30 --output evidence/usb.log
```

Logs are private files; failures preserve `.part`, and existing outputs are refused.
Opening a USB serial port can still have platform/adapter-specific effects; observe
your actual board. Do not connect a second monitor to the same serial port during
capture. This command is a bounded capture, not a wireless deployment service.

## On-device measurements

Open **Diagnostics** and run:

- SD streaming/integrity: a synchronized 1-MiB write/read with CRC, retaining a
  failed test file for inspection. Timings include UI/filesystem work.
- Display test: examine edges, orientation, colors and readability. Frame metrics
  describe complete rendered transfers, with minimum/mean/maximum since boot.
- Keyboard test: press all 56 raw positions, including modifiers, then exit with
  Fn+backtick. The evidence stores a 56-bit position mask and overflow/drop counts.
  Seeing a position does not prove its decoded character is correct; check that too.
- Save device measurements: write current/minimum/largest heap, main-task stack
  headroom, build/SDK/chip/reset details, flash/firmware/SD bytes, boot/uptime,
  battery millivolts, display/keyboard initialization and network state.
- App cycles: open/close Counter 100 times through the real app manager without
  saving or changing the user's counter. Capture before/after heap measurements.
  Cancellation or failure is labelled separately from a completed run.

Device and SD JSON files are written to `/Diagnostics` on the SD, including in
installer recovery before `/POS` is installed. Older SD test files under
`/POS/Users/Owner/Diagnostics` are also accepted by the report tool. Device JSON is
printed to serial with the `POS_MEASUREMENT` prefix. It contains no Wi-Fi password.
Optional Wi-Fi setup records before/connected-or-failed snapshots; it never enables
a network merely to collect evidence. Samples include a build ID and boot-session
ID so unrelated boots are not paired as a memory comparison.

## Assemble a report

Mount the SD on your computer and collect the measurements:

```sh
python tools/posctl.py hardware-report --root /media/stevie/PANPUTER --output evidence/report-unrun.json
```

Every physical check remains `not_run` by default, even if measurements exist.
In `evidence/observations.json`, record actual checks with `pass` or `fail`, a
specific observation and relative evidence filenames. Keep referenced screenshots,
serial captures, hash comparisons or operator notes inside the evidence directory.
Then create a fresh report:

```sh
python tools/posctl.py hardware-report --root /media/stevie/PANPUTER --observations evidence/observations.json --output evidence/report-observed.json
```

The report hashes every evidence file, groups measurements, and computes a heap
change only for one unambiguous before/after pair from the same build and boot.
It refuses incomplete pass/fail assertions, unsupported observations, duplicated
checks, corrupt measurements and overwritten output names. `physical_validation_complete`
can become true only after every listed check has an explicit recorded pass.
This is an evidence record, not cryptographic attestation of an operator's claim.

## Interrupted-operation checks

Host tests exercise process cuts during copy/finalization, reinstall journaling and
retirement, restore and settings migration. They inject mid-write file failures,
test conservative space refusal and confirm backups/originals survive. Large-file
tests use sparse scans and interrupt a file above 2 GiB after streaming begins;
they do not pretend to have copied a 64-GB card through an ESP32.

On hardware, use the matrix in [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md).
For each deliberate power cut, keep the last serial phase, reboot result, SD/FAT
condition and a comparison against an off-device original. Check missing/corrupt
cards and insufficient space separately. A repaired FAT volume is a recorded
failure, not proof of transaction atomicity. None of these tools operates a power
relay or automatically certifies electrical power-loss behavior.


Recorder tests independently open generated WAVs, compare PCM, recover interrupted
files and check readback corruption/symlink refusal. UI tests verify global close
finalizes recording/releases input and Files playback stops at EOF. IMU tests use
a simulated register bus to check the full 8192-byte configuration, scaled samples,
failed initialization shutdown and exact NEC complement/bit order. App-save tests
cover preserved legacy originals, schema backups, future/corrupt data refusal and
interrupted-slot protection. Developer tests mock USB/LAN/HTTPS to check signatures,
payload/hosted-manifest changes, physical-evidence hashes/identity, transfer hashes,
private session keys and confirmation criteria. These are software checks only.


The guided installer tests use synthetic lsblk/findmnt inventories and mocked
privileged commands. They check internal/system/swap/work-directory exclusion,
changed disk identity before/after partitioning, exact erase/flash confirmation,
unmount failure, FAT32 command targeting, archive traversal/collision/size limits,
verified staged copies, Crostini copy-only behavior, and original backup discovery
and observed-boot requirements. The executable also runs a real read-only doctor
and private GitHub download verification without a checkout. Real Debian sfdisk
and mkfs.fat were exercised against 64 GB sparse regular-file images (MBR type
0x0c, LBA 2048, FAT32 boot sector verified), without sudo or a block device. None
of this establishes actual card hot-plug/ejection, ADV USB recovery or ChromeOS
USB forwarding behavior; those remain physical checks.
