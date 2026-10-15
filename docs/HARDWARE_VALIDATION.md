# Bring-up evidence record

All physical tests below are **not run**. The cloud machine has no attached ADV.
Cross-compilation, host UI rendering, native fault injection and mocked USB command
tests do not establish physical behavior. Fill this record with observations from
the actual Framework/Crostini + Panputer setup, keeping the factory backup private.

| Check | Tool / expected evidence | Physical result |
| --- | --- | --- |
| Actual board revision/module | Photograph/label and vendor revision documentation | Not run |
| ESP32-S3 identity and flash/security state | `posctl probe --port ...` JSON; verify profile/capacity/security | Not run |
| USB download-mode entry | Vendor sequence on actual revision; repeat connection | Not run |
| Factory full-flash backup | `factory-backup`, two matching full reads and SHA-256 | Not run |
| Factory recovery | Explicit `factory-restore`; flash readback, then observe factory boot | Not run |
| LCD geometry and color | Diagnostics display test; all edges/text readable, correct orientation/color | Not run |
| Keyboard mapping | Diagnostics events; test all 56 positions/modifiers/chords | Not run |
| Global escape | Home from every menu/app; held shortcut during a stalled app | Not run |
| FAT32 SD mount/removal | Readable card, missing card, corrupt/non-FAT card, removal during read/write | Not run |
| SD streaming | Diagnostics 1 MiB write/read/CRC; JSON measurements in /Diagnostics | Not run |
| Heap | About current/minimum/largest block after boot, apps, and optional Wi-Fi | Not run |
| Boot and firmware footprint | About boot time/image bytes; compare compiled partition checks | Not run |
| Frame timing | Diagnostics latest frame time; observe actual refresh and UI latency | Not run |
| Battery | Compare calibrated ADC millivolts with independent measurement | Not run |
| Wi-Fi skip/connect | Offline setup works; connect if requested, no credential files appear | Not run |
| Sleep/wake | Display/Wi-Fi stop; G0 wake and physical power switch | Not run |
| Other peripherals/cap conflicts | Validate audio, IR, IMU, LoRa/GNSS wiring before adding their drivers | Not run |

The SD benchmark reports interactive timings including filesystem synchronization
and UI work. Do not label them raw SPI throughput. ADC 0 mV means unavailable,
not an empty battery. A successful driver initialization is not proof of every key
or peripheral signal. Observe a real keypress/display pattern and independently
verify the stored test file/CRC.

See [VALIDATION.md](VALIDATION.md) for serial capture, JSON measurements, an
unrun observation template and hash-linked reports. The tools keep physical checks
unrun until actual observations are explicitly recorded.

## Power-loss matrix

Use a sacrificial card and copies of the trusted package/backup. Record the exact
last USB phase (`validated`, `copy`, `staged`, `sd_committed`, `firmware_written`,
`boot_selected`) and the physical interruption point. Native tests already cover
logical process interruption; the following specifically require real power cuts:

| Interruption | Required observations after restart | Result |
| --- | --- | --- |
| Before / during initial marker write | Unknown staging retained; no guessed deletion | Not run |
| During `.part` copy | Original payload retained; no incomplete file treated complete | Not run |
| During staged directory commit | Boot manager/recovery handles actual FAT state; user files remain recoverable | Not run |
| During runtime flash write | Factory installer still usable; runtime not selected prematurely | Not run |
| During boot selection | Valid factory or pending runtime boots; no unverified runtime confirmation | Not run |
| Before runtime boot confirmation | IDF rollback/factory path works; installer media retained | Not run |
| During backup finalization | `.part` never appears as a completed ZIP/folder; original user data intact | Not run |
| During reinstall journal/retirement | Journal/old tree/backup retained; correct resume or safe refusal | Not run |
| During restore/migration | Original backup retained; verified files reusable and conflicts accessible | Not run |
| During cleanup | Unknown files and backups retained; partial known-file cleanup can resume | Not run |

After each cut, examine the FAT volume on a computer and verify user data against
an off-device copy. If the card requires repair, record that as a filesystem
failure; do not count an automatic repair as proof that installation was atomic.
No program in this repository controls an external power relay or has performed
these tests. Factory USB recovery remains the prerequisite and fallback.
