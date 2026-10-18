# POS 0.8.3-dev: USB cable file transfer

## Scope and separation from firmware

The reported Chromebook mesh-Wi-Fi transfer problem was not diagnosed as a POS
protocol defect: current error output was unavailable, and physical LAN access
cannot be established in cloud tests. Crostini BlueZ access is a separate
platform limitation. This release provides the requested direct USB alternative;
it does not claim to repair the router, Wi-Fi isolation or ChromeOS Bluetooth.

The device must run the new POS firmware once, installed through the existing
signed updater. Thereafter USB file sessions use normal USB serial/JTAG console
I/O. No bootloader command, esptool, flash/partition write, raw SD export, RTS/DTR
control or physical storage removal is involved. Physical keyboard and G0 remain
under the independent input supervisor.

## Protocol and resource ownership

An explicit on-device USB Share screen claims console input atomically, then
reads bounded nonblocking frames in the foreground. The PC waits for a valid
protocol/model greeting before transmitting; old firmware is not probed with
bytes that could become keyboard commands. Framed packets arriving after exit
are swallowed by the console parser, including embedded Home/Escape controls.
On release the console quarantines the remainder of a packet line even if its
prefix was already consumed before cancellation. A successful checked PC session
sends a final newline to clear that quarantine; after an interrupted serial
terminal session, press Enter once before using USB keyboard fallback. Physical
keyboard/G0 are unaffected. A regression covers prefix-less remaining bytes.
CRC framing ignores logs/noise, oversized lines and damaged packets. Completed
responses are cached for safe retry after a lost acknowledgment.

Only the file selected locally can be exported. Uploads require filename/size
confirmation and go through the existing protected Downloads staging, resumable
offer metadata, durable chunk acknowledgment and SHA-256 publication path. The
PC uses its existing secure receiver with resumable staging and exclusive
publication. Existing different files, failed hashes and incomplete transfers
are preserved. Transfers stream 512-byte file blocks; no whole-file RAM buffer.
Both sides retain progress and cancellation. USB needs only standard Python.

Source offers also now use the existing nonblocking/no-follow regular-file
check before hashing, so symlinks are refused and a FIFO cannot hang the tool.

## USB serial/JTAG line-ending regression

The pinned ESP-IDF console uses CRLF for stdout and CR for stdin. Its USB VFS
adds a carriage return to each outgoing frame terminator. The initial PC client
counted that extra CR against the payload bound, rejecting a maximum-sized
512-byte download block even though the CRC was correct. Smaller replies still
worked, hiding the edge case in plain-LF pseudo-terminal tests. The PC decoder
now removes the optional terminator CR before checking frame size and CRC.
A maximum-frame CRLF regression failed before the correction and passes after
it; firmware console configuration remains unchanged. The unpublished release
candidate's workflow was stopped and rebuilt with this correction.

## Flash and SD resources

The previous release had little runtime-slot headroom. Existing explanatory
notices (32 entries, 4,749 text bytes) move unchanged into one signed System/Help
catalog. The shared help reader selects bounded sections with its existing
2 KiB workspace and rejects missing, oversized, NUL-containing or unfinished
sections. No service, cipher, TLS validation, app, OTA slot or recovery
capability was removed. SDK compilation settings and partition sizes stay the
same. The new catalog adds one manifest record, avoiding dozens of extra records.

## Verification

- **528 full regression tests passed**, including native firmware crypto/service
  tests with the pinned Mbed TLS host libraries; no ordinary-suite skips.
- **198 sanitizer tests passed** (199 discovered, one documented ASan/virtual
  address-space skip); no address, leak or undefined-behavior findings. The final
  18 cable tests were also rerun under sanitizers after the offer-screen changes.
- USB tests exercise the actual C service/UI and actual Linux tty client over
  pseudo-terminals: both directions, empty files, same-file completion retry,
  upload/download resume, declined offers, collisions, bad hashes/CRC, noise,
  oversized frames, lost ACKs, old/wrong-mode devices and modem-control safety.
  The 256 KiB round-trip uses a 16 KiB POS allocation budget; libc/SDK allocations
  and allocator metadata are outside that budget, not claimed measured hardware.
- Existing physical-key/ANSI-console tests cover exclusive input ownership and
  swallowing late file frames containing Home/control bytes. SD help section
  tests cover exact selection, missing entries, oversized/NUL/unfinished text,
  and matching every reference with exactly one catalog entry.
- GCC static analysis of the new codec/service, console and help reader found
  no warnings. Post-LTO USB/UI/core stack reports remain below the 3 KiB guard.
- Both firmware roles build: runtime **1,506,816 bytes**, leaving **512 bytes** in
  each unchanged 1,507,328-byte OTA slot; installer **822,576 bytes**, below its
  1,048,576-byte factory slot. The flash margin is small, separately from RAM.
- Linux private-D-Bus Bluetooth transfer/cleanup smoke passed; no physical radio.

Reproduce USB sanitizer coverage with the pinned environment active:

```sh
POS_TEST_ASAN=1 ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1:halt_on_error=1 \
  PYTHONPATH=tests python -m unittest test_cable -v
```

Installation bundles are checked for versions, hashes, ZIP CRCs, embedded tools
and matching SD resources. The tag release/signing workflows and independent
anonymous pinned-key/download verification are required before announcing the
on-device update ready. These software checks do not simulate a passing hardware
result.

## Physical checks

Confirm ChromeOS exposes the serial device, normal boot survives opening/closing
it, large transfers are responsive, G0/Esc cancel reliably, and cable removal
retains partials for retry. Test SD stalls and radio coexistence, minimum heap
and stack high-water marks. No physical USB, actual Cardputer or ChromeOS USB
sharing was available to the cloud environment. Do not hold G0 while connecting
for file copying; boot POS normally and open its USB Share screen.
