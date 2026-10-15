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
not linked into POS. The bundled six 8x8 app icons are project-created bitmap
data. No new license for the user's POS project is selected by this file.
