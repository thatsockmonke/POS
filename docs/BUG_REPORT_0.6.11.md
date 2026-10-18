# POS 0.6.11-dev investigation and validation

## PC transfer report

**Reported:** correct Wi-Fi session key appears to stall with no progress; wrong
keys fail; Bluetooth both directions reports `org.bluez` has no service.

**Confirmed Linux Bluetooth cause:** that D-Bus message means the Linux instance
has no BlueZ service available. It occurs before POS service discovery or file
transfer. Crostini commonly cannot access ChromeOS's Bluetooth adapter. No firmware
or Python change can supply a missing adapter/virtualization capability.

**Fix:** the PC tool explains this and offers Wi-Fi for the same chosen transfer.
It does not install/start privileged system services, silently downgrade pairing,
or claim to make ChromeOS Bluetooth accessible. A real Linux adapter/BlueZ session
is still required for BLE. Missing local source files keep their own error instead
of being misdiagnosed as a Bluetooth-service failure.

**Confirmed Wi-Fi visibility defects:** source SHA-256 and the Cardputer's export
hash/preparation happen before file progress is shown. The PC read requested 64 KiB
before reporting received bytes, while POS emits smaller chunks. The device's
transfer screen did not display copying/verification progress. These can look
like a dead transfer even while work is happening. The exact reported physical
stall has not been reproduced on a real Cardputer.

**Fix:** validate `/status` and the session key before sending a file; show the
connected POS version, hashing/preparation/verification phases and a two-second
elapsed heartbeat. Receive with `read1(8192)` so available data produces progress
immediately. The device exposes atomic activity counters internally and displays
copy/verify/complete/failure with byte totals. Leave the session screen open.
The temporary session key is entered visibly, at the user's request, and never
saved or placed in a URL. Local Wi-Fi transfer remains authenticated **HTTP on a
trusted LAN**, not encrypted HTTPS. Redirects are refused; files stream, are hashed
and only publish without overwriting a different file.

Tests use a slow HTTP fixture that sends 1 KiB, then waits for the client's first
progress callback before releasing the rest. It now progresses before EOF, which
the old 64 KiB read could not do. Wrong keys fail before upload. Additional checks
cover hashes, truncation, redirects, existing targets and the real Cardputer UI's
copy/verification/completion screens. The private real D-Bus/BlueZ simulation
still passes authenticated GATT flags, agent registration, transfer, disconnect,
resume, integrity and cleanup. These fixtures are not physical network evidence.

## OSM processing

**Confirmed causes:** per-node binary searches issued one file seek/read for each
comparison, including references from building ways that would later be thrown
away. Each way/relation created/deleted its own SD scratch file. On FAT32 this
adds substantial random reads and metadata updates. Increasing the selected area
made that avoidable work more visible; it was not a reason to load a whole map
into RAM.

**Fix:** keep a sparse node-ID directory and one 4 KiB index page in RAM. Resolve
references only after identifying a retained road way. Store small objects in a
fixed 2 KiB buffer and spill longer ones to a single reusable owned scratch file.
Read only the current object's extent, so a shorter object never consumes a
previous object's tail. Cancellation remains available while resolving long roads.
The original XML and installed maps are untouched on failure; new output is
synced and verified before publication. External sorting/graph construction and
all format/count/integrity bounds remain.

Measured on the same deterministic desktop fixture (1,000 nodes, 2,000 discarded
buildings, 302 retained road references):

| Engine | fseek calls | fopen calls | Desktop elapsed |
| --- | ---: | ---: | ---: |
| 0.6.10 | 40,718 | 2,020 | 0.0578 s |
| 0.6.11 | 3 | 19 | 0.0094 s |

Normalized XML is byte-identical. A real previously downloaded 3,148,444-byte U.S.
4 km OSM sample also produces byte-identical normalized roads, then compiles road
and navigation files. Native regression tests bound seek/open counts and exercise
memory-buffer and scratch-spill paths. **These are desktop timings/I/O counts,
not Cardputer SD throughput measurements.** Actual SD speed, remaining graph work,
network/provider limits and live ESP32 heap still require hardware observation.

## Loading audio

**Confirmed interaction:** UI key/notice cues run on a separate task and acquire
shared I2S/codec resources. During heavy SD/UI work they can be delayed, producing
unpleasant late sound feedback.

**Fix:** nestable transient cue holds cover map download/preparation, regional
raster reads and foreground LAN transfer sessions. In-flight cues exit their
worker path; pending cues are discarded rather than replayed later. Completion,
cancel and error paths release the hold, retaining the user's saved sound settings.
Music/microphone ownership is not forcibly stolen or destroyed by a UI hold.
The actual audio worker is tested with nested holds/restoration; native UI fixtures
assert no hold remains after returning, including error/cancel paths. Speaker
quality/timing under real SD load remains a hardware check.

## Calculator checks and limitations

The new scientific Calculator has dependency-free native math tests, independent
arithmetic comparisons and actual text/keypad/UI/save-reopen fixtures. A UI test
caught punctuation being mistaken for arrow navigation; separate typing/keypad
contexts now preserve `/`, `.`, and `,` in expressions. Saves use existing
checksummed redundant app-data slots; damaged/future saves remain untouched.

The engine uses finite double precision, bounded rational display and degree-8
polynomial algebra. Solving is linear/quadratic, including complex roots.
General symbolic functions/rational algebra, multivariate CAS, arbitrary precision
and matrices are not claimed. Non-finite values, invalid domains, too much nesting
and oversized symbolic output fail explicitly, without publishing a truncated
answer or changing a variable after a failed assignment. See [CALCULATOR.md](CALCULATOR.md).

Xtensa stack-usage output reports 672/496/192-byte frames on the recursive parser
path. The eight-level parser bound preserves space within the 16 KiB UI task;
large UI state/history/save metadata are heap-owned. The module also has the
3 KiB single-frame compile guard. Physical minimum-stack, readability and input
comfort still require validation on the ADV.

## Build/recovery scope

Both firmware roles retain the factory installer and the two original OTA slots;
there is no partition migration or requirement to format the SD. To accommodate
Calculator without dropping recovery, unused IPv6 protocol machinery is omitted:
POS already configures IPv4 STA/DHCP and IPv4 LAN sessions, without creating an
IPv6 interface. IPv4 HTTPS keeps certificate/hostname validation, and updates keep
P-256 signature/hash checks. Trial LTO configurations were discarded and are not
shipped. No flash/boot/power-cut or physical radio tests were performed in cloud.

Validation completed: **431 host tests passed**, both ESP-IDF firmware roles built
and fit their existing partitions, private D-Bus transfer checks passed, and the
matching installation bundle passed checksum/image checks plus simulated
0.6.4-to-0.6.11 backup/reinstall/restore with user data preserved. Publication
then verifies the public release and signed feed; none of these are physical
Cardputer flashing, networking or power-cut tests.
