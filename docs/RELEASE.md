# POS 0.6.11-dev

Adds Calculator, addresses silent PC transfer waits and missing-BlueZ errors,
reduces OSM SD work and suppresses delayed UI beeps during loading.

## Calculator

Open **Calculator** from the launcher. Type `1/3+1/6`, `sqrt(2)`, `2^3`, `a=12`,
then `a/4`, and press Enter. It has a visible keypad, grouped scientific-function
buttons, saved a–z variables/Ans, DEG/RAD, statistics/combinations and fraction/
decimal answers. Tab opens tools; Ctrl+K or Tools switches typing/keypad modes so
punctuation stays usable while typing and bare arrow legends navigate the keypad.

CAS supports polynomial simplification/expansion, derivatives/integrals through
degree 8 and linear/quadratic equation solving, including complex roots. It uses
double precision and bounded rational display, not unrestricted exact algebra.
Unsupported expressions and excessive nesting/output fail explicitly. Esc/global
Home closes normally. See [CALCULATOR.md](CALCULATOR.md).

## PC transfers

Replace the old **pos-transfer** executable with this release's download. Run:

```sh
python3 /path/to/pos-transfer
```

The temporary Wi-Fi key is now visible. The tool checks the device/key first,
shows hashing/preparation/verification waits and progress as soon as download data
arrives. POS itself shows copying/SD verification/completion progress. Keep the
transfer session open until the tool confirms completion.

`org.bluez` missing means Linux has no BlueZ service, commonly because Crostini
cannot access ChromeOS Bluetooth. The tool explains this and offers Wi-Fi; it
cannot provide an unavailable Bluetooth adapter. Wi-Fi needs no Bluetooth helpers.
Use a trusted LAN: this existing session is keyed local HTTP. See [TRANSFER.md](TRANSFER.md).

## Maps and sounds

OSM import avoids discarded-way node lookups, uses a sparse RAM index/4 KiB page
cache, buffers small objects and reuses one scratch file for larger ones. One
identical-output fixture drops from 40,718 to 3 file seeks and 2,020 to 19 file
opens. Actual Cardputer timing remains to measure. Regions and graphs still
stream from SD; existing map files need no redownload.

Map loading/raster work and LAN transfers temporarily pause UI cues, drop delayed
beeps and restore the saved sound settings afterward. Playback/microphone resource
ownership remains independent. See [BUG_REPORT_0.6.11.md](BUG_REPORT_0.6.11.md).

## Update and validation

Use **Settings → System → Software Update**. No SD format or partition migration
is needed. The PC tool must be downloaded separately; device OTA cannot update
files on your computer. Factory recovery and both safe OTA slots remain.

All **431 host tests pass**. Native regressions cover scientific math/CAS, actual keypad/text input and saved
variables, HTTP streaming/progress/integrity and BlueZ fallback, D-Bus GATT
transfer/resume/cleanup, map output/I/O bounds and nested sound holds. Both firmware
roles and matching packages are verified before release. Physical Cardputer,
Crostini/network/BLE, SD throughput, audio timing and power-loss checks remain
unverified in cloud; the exact reported physical Wi-Fi stall is not claimed to
have been reproduced here.
