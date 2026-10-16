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
| Microphone | ES8311 ADC data-in46, BCLK41/LRCK43; I2S1 16-bit stereo slots, left channel recorded as 16 kHz mono | Official ADV pin map + pinned M5Unified ADV mic profile |
| IR | GPIO44, emitter only; RMT 38 kHz carrier, standard 8-bit NEC TX | Official ADV pin map |
| IMU | BMI270 I2C0x69 (0x68 fallback), SDA8/SCL9; 8 KiB configuration, 100 Hz, +/-4 g, +/-1000 degrees/s | Pinned M5Unified + Bosch register definitions |
| Audio codec / I2S | ES8311 0x18 on SDA8/SCL9; BCLK41, LRCK43, data-out42; I2S1, 16-bit stereo slots | M5Unified ADV speaker profile |
| Cap LoRa SPI | SPI3 shared with SD: SCK40, MOSI14, MISO39, cap NSS5 (SD CS12) | M5Stack cap docs + UserDemo |
| Cap reset / IRQ / busy | GPIO3 / GPIO4 / GPIO6 | M5Stack cap pin map |
| Cap antenna switch | PI4IOE5V6408 0x43, P0 output high; high-impedance disabled | M5Stack cap docs |
| SX1262 clock | DIO3 TCXO 3.0 V, DIO2 RF switching | Pinned M5Stack UserDemo |
| Cap GNSS | ATGM336H, UART1 TX13/RX15, 115200 8N1 | Cap docs + UserDemo |

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

Audio initializes only for playback; BLE only for an explicit sharing session.
The location/cap service listens for GNSS data and probes cap hardware at runtime.
LoRa features default off; TX consent is session-only. GPS presence independently
gates Maps. IR and IMU initialize on demand; BMI270 configuration/readback must succeed before samples are exposed. Microphone and speaker are exclusive. Probe/initialization results are not
physical performance measurements.

Additional references verified for this release:

- [Official Cap LoRa-1262 pin map and antenna-switch requirement](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
- [Official Cardputer ADV audio/pin map](https://docs.m5stack.com/en/core/Cardputer-Adv)
- [Pinned cap HAL and 3.0-V TCXO configuration](https://github.com/m5stack/M5Cardputer-UserDemo/blob/b549eac0a3c65bc108186c276b8fac0a214aaa4e/main/hal/cap_lora868/cap_lora868.cpp)
- [Pinned ES8311 speaker register profile](https://github.com/m5stack/M5Unified/blob/e125e73f28b6b83e621e48df432af426bb2ff935/src/M5Unified.inl#L1045)

Semtech's pinned SX126x C driver handles radio register commands and chip
workarounds, with an ESP-IDF SPI HAL. SPI-device transactions arbitrate access to
the shared SD bus; radio NSS is held high during SD initialization. This still
requires actual coexistence tests on the correct cap revision. Do not hot-plug it.

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
may consume significant heap. Saved profiles use a passphrase-encrypted NVS
vault; HTTPS, time sync and file transfers are implemented. Wireless deployment is implemented through temporary developer sessions and the
signed updater, gated on physical recovery/OTA evidence. Reopen a session after
reboot for confirmation; no always-on service is enabled. New firmware still fits the
existing slots, with limited runtime flash headroom; future growth must be
measured against the actual device's verified flash configuration.

Compiled stack-usage reports are emitted for POS components. MP3 scratch memory
is in the bounded player heap context; the stock approximately 16-KiB local scratch
frame is not used. Audio decoder frames and editor/network frames have compiler
size guards. These static checks do not replace on-device minimum-stack/heap
measurements while services coexist.

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

Battery status is a smoothed voltage estimate, with icon hysteresis and a
sustained-voltage full estimate. No GPIO is invented for charger status. The panic
wrapper uses the pinned SDK's Xtensa panic frame, retains only a bounded RTC
snapshot and invokes the original handler; flash/SD export happens after reboot.
The partition offsets are unchanged. New code uses compiler stack guards; launcher
UI/path workspaces are heap-owned to keep nested update/TLS calls off a large
resident task frame. Actual stack/heap and electrical fault behavior need device
measurement.


## Recorder / IR / IMU driver references and validation

The microphone register profile is taken from `_microphone_enabled_cb_cardputer_adv`
and its GPIO46/43/41 table in the pinned M5Unified source above. I2S clocks run
before codec setup, one second of analog warm-up is discarded, and DMA receive
queue overflow stops recording with an explicit error. Actual gain, channel,
noise, clock ratio and coexistence need bench verification. DAC output is muted
while recording; recording/playback cannot own I2S1 simultaneously.

BMI270 configuration bytes are vendored unchanged from the pinned
[M5Unified configuration](https://github.com/m5stack/M5Unified/blob/e125e73f28b6b83e621e48df432af426bb2ff935/src/utility/imu/BMI270_config.inl).
The portable driver uploads 32-byte chunks, checks internal initialization status,
sets accel/gyro ranges/rates and checks sensor error status. Samples expose sensor
axes, not a calibrated board orientation or heading. No magnetometer is assumed.
IR uses an ESP-IDF RMT carrier/envelope and one explicit bounded NEC frame; it does
not claim a receiver acknowledged it. The ADV has an emitter, no IR receiver.

The official ADV documentation lists 8 MiB physical flash. POS continues using
its existing first-4-MiB layout pending a per-device probe. HTTPS now uses the SDK's
common-root bundle (approximately 99% coverage per ESP-IDF), retaining certificate
validation and failing closed for other roots. Builds may enable the full/custom
bundle if they fit; see `sdkconfig.defaults`. SDK log level is WARN, while POS
measurement/USB reports and panic output remain available. No PSRAM is assumed.
