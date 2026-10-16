# Third-party source and notices

The firmware builds against ESP-IDF v5.5.5, commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`, including its FreeRTOS, FatFs,
mbedTLS, cJSON, display/peripheral and Espressif components. Their individual
licenses and copyright notices remain in that SDK checkout; ESP-IDF's root
LICENSE describes its Apache-2.0 and third-party licensing arrangements.
POS does not vendor or modify these components. Native Linux services link the
system OpenSSL libcrypto and compile the same pinned SDK cJSON sources.

`components/pos_core/font8x8_basic.h` comes from Daniel Hepper's public-domain
font8x8 project, commit `8e279d2d864e79128e96188a6b9526cfa3fbfef9`:

https://github.com/dhepper/font8x8/blob/8e279d2d864e79128e96188a6b9526cfa3fbfef9/font8x8_basic.h

Its complete public-domain author/provenance notice is retained in the header.
Only the declaration was changed to `static const unsigned char` for flash
storage. Vendored SHA-256:
`77f7eecdf7bc8144a318b94d3c3b04512356814d41cd86b505d1b360437d752f`.

Hardware pin/register/layout facts were checked against M5Stack's M5Unified,
M5GFX and M5Cardputer sources linked in docs/HARDWARE.md. Those frameworks are
not linked into POS. The bundled fifteen 8x8 app icons are project-created bitmap
data. No new license for the user's POS project is selected by this file.


## Added audio and radio dependencies

- **minimp3**, commit `ea99364f61c14656440e8d77e9c233ccf3124633`, CC0:
  https://github.com/lieff/minimp3/tree/ea99364f61c14656440e8d77e9c233ccf3124633
  The header, license and commit are vendored under `components/pos_core/vendor/minimp3`.
  POS changes only workspace ownership: a caller-supplied heap workspace replaces
  the decoder's large local scratch frame; the original API remains as a heap-backed
  wrapper. `POS-CHANGES.md` records the adaptation. Generated tone/PCM fixtures verify
  unchanged output against the unmodified upstream decoder.
- **Semtech SX126x driver**, commit `a10c5dfdf89788c6ac805e9fe98889de44175aa2`,
  Clear BSD: https://github.com/Lora-net/sx126x_driver/tree/a10c5dfdf89788c6ac805e9fe98889de44175aa2
  Source, headers and the full license are vendored under
  `components/pos_device/vendor/sx126x`. POS supplies the ESP-IDF SPI/busy/reset HAL;
  upstream driver source is unmodified.
- **Apache NimBLE**, including its SDK dependencies, is supplied by the pinned
  ESP-IDF checkout. POS uses its native GATT, GAP and Secure Connections APIs.

Binary bundles include minimp3's CC0 text, Semtech's full Clear BSD notice and
SDK NimBLE/TinyCrypt license/notice files under `licenses/`, alongside the SDK
root license and this provenance document. No Bluetooth Classic headphone stack
is added. The audio fixture is an originally generated sine tone, with reproduction
instructions in `tests/fixtures/README.md`.

Update verification uses the ESP-IDF-provided mbedTLS P-256/SHA-256 implementation.
Host signing/key-generation tools invoke the system OpenSSL command; private keys
are user-managed and no signing key is bundled in firmware or the source tree.


**BMI270 configuration:** `components/pos_core/vendor/bmi270/config.h` contains
M5Stack's unchanged 8192-byte configuration array from M5Unified commit
`e125e73f28b6b83e621e48df432af426bb2ff935`, `src/utility/imu/BMI270_config.inl`.
Only the C declaration/header guard was adapted. The upstream MIT LICENSE is
retained alongside it and shipped in bundles as `licenses/M5Stack-MIT.txt`.
The POS C initialization/scaling driver is separate from the M5Unified C++ classes.
