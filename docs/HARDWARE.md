# Cardputer ADV development profile

Target: `m5stack-cardputer-adv`, ESP32-S3. This is an explicit profile, not automatic
revision detection. The constants below are verified against M5Stack source;
physical signals, orientation, timing and calibration remain unverified here.

| Function | GPIO / configuration | Source |
| --- | --- | --- |
| SD SCK / MISO / MOSI / CS | 40 / 39 / 14 / 12, SPI3 at 10 MHz | M5Unified ADV pin table |
| LCD SCK / MOSI / CS / DC / reset / backlight | 36 / 35 / 37 / 34 / 33 / 38, SPI2 at 20 MHz | M5GFX Cardputer/ADV ST7789 profile |
| LCD geometry | 240x135 landscape; swapped axes, gap 40/52, inversion | M5GFX profile; orientation needs bench confirmation |
| ADV keyboard | TCA8418 0x34, SDA 8, SCL 9, 400 kHz, 7x8 matrix | M5Unified + M5Cardputer ADV reader |
| Keyboard IRQ | GPIO 11 published; POS polls FIFO/status instead | M5Cardputer ADV reader |
| System button | G0, active low | M5Unified ADV button configuration |
| Battery | ADC1 GPIO 10, divider ratio 2; curve-fit calibration when available | M5Unified power profile |
| USB | ESP32-S3 native USB Serial/JTAG console | ESP-IDF |

Pinned references:

- [M5Unified pin tables](https://github.com/m5stack/M5Unified/blob/e125e73f28b6b83e621e48df432af426bb2ff935/src/M5Unified.inl)
- [Battery profile](https://github.com/m5stack/M5Unified/blob/e125e73f28b6b83e621e48df432af426bb2ff935/src/utility/Power_Class.inl)
- [M5GFX display profile](https://github.com/m5stack/M5GFX/blob/347a23274081a105cb9fc0fc3bbf0e15c78e79c3/src/M5GFX.cpp)
- [ADV keyboard/remap](https://github.com/m5stack/M5Cardputer/blob/f1392858b9994c3547120e602a57d3553d16ab01/src/utility/Keyboard/KeyboardReader/TCA8418.cpp)
- [Key layout](https://github.com/m5stack/M5Cardputer/blob/f1392858b9994c3547120e602a57d3553d16ab01/src/utility/Keyboard/Keyboard.h)

Only IDF drivers are linked, not Arduino/M5Unified/M5GFX implementations. A failed
TCA probe leaves USB input available. LCD failure leaves text output available.
The display streams eight scanlines through a 3840-byte DMA buffer, rather than
retaining a whole color framebuffer. The background input task owns hardware keys
and reserves Home before applications; a held system shortcut can request restart.

The boot/About screens report heap, minimum heap, largest internal block, physical
flash, packaged runtime size, boot/frame timing, SD capacity/free space, keyboard
status/count and calibrated battery millivolts (0 means unavailable). Battery
percentage and charging state are not inferred from an uncalibrated reading.
Diagnostics measure actual SD read/write/CRC integrity and store successful results
in `POS/Users/Owner/Diagnostics`. Interactive write times include UI/fsync overhead.

The LoRa/GNSS cap, audio, IR and IMU are not initialized by this milestone. Start
with the bare ADV and verify revision/cap routing before adding those drivers.

## Flash and memory

`partitions.csv` defines a new layout in the first 4 MiB, not the factory layout
or the entire physical chip. PSRAM is not enabled or assumed. DIO/80 MHz are the
current IDF flash settings; validate them against the actual module before flashing.

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | 0x9000 | 0x4000 |
| OTA metadata | 0xd000 | 0x2000 |
| PHY data | 0xf000 | 0x1000 |
| Factory installer | 0x10000 | 0x100000 |
| Runtime OTA 0 | 0x110000 | 0x170000 |
| Runtime OTA 1 | 0x280000 | 0x170000 |

Both images are checked against these partitions during builds. Main task stack is
16 KiB, FAT locks 32, file streaming buffers 8 KiB; metadata and app allocations are
bounded. Available runtime heap still requires an actual device measurement.
Optional Wi-Fi does not start until requested, uses RAM credential storage and
may consume significant heap. Password persistence, general network services and
wireless update endpoints are not implemented.

Use [the bring-up record](HARDWARE_VALIDATION.md) to distinguish compiled/simulated
behavior from observed hardware evidence. No physical result has been filled in.


## Boot flag palette

The boot sequence follows the pansexual flag from top to bottom: pink `#FF218C`,
yellow `#FFD800`, blue `#21B1FF`, each shown once for 180 ms. The reference is
[this published flag SVG](https://github.com/JoeHart/pride-flag-api/blob/20320fccb7441847f60a66cdfb7c5ac6f3c7ad06/data/flags/svg/pansexual_2010.svg).
The LCD uses RGB565 values `0xF911`, `0xFEC0`, `0x259F`;
actual panel color appearance still requires on-device observation.

After the blue flash, yellow slides in from the left into the middle third.
When yellow is halfway across, pink starts sliding into the top third; blue
remains underneath. Each slide takes a nominal 400 ms with a 200 ms stagger.
Black POS lettering then fades in over 400 ms. Its visible 198x91-pixel bounds
are centered, reaching halfway into the pink and blue stripes. The completed
flag holds for 1000 ms after presentation, then validated startup continues.
Animation frames target 40 ms including transfer time; physical panel timing
still needs measurement. Rendering reuses the stripe buffer without SD assets
or a full-screen framebuffer.
