# POS developer tools

Use the pinned ESP-IDF 5.5.5 environment for builds; prebuilt bundle USB commands
need Python 3.10+ and esptool 4.12.0. Signing/deployment also requires OpenSSL.
See INSTALL.md for the complete first-install/factory-recovery process.

## Build and USB flash

```sh
python tools/posctl.py build --role all --build-root build
python tools/posctl.py bundle --output dist/bundle --zip-output dist/pos-0.5.0-dev.zip
python tools/posctl.py probe --port /dev/ttyACM0
python tools/posctl.py factory-backup --port /dev/ttyACM0 --output /private/factory.bin
```

First prove factory restore and observe its boot as described in INSTALL.md.
Back up the SD separately. Verify the download ZIP/checksum and the extracted
bundle's `SHA256SUMS`. The `pos-installer-usb.bin` checksum is listed there.

```sh
python tools/posctl.py flash --port /dev/ttyACM0 \
  --image dist/bundle/pos-installer-usb.bin --sha256 COPY_THE_64_HEX_DIGIT_CHECKSUM \
  --factory-backup /private/factory.bin --confirm-flash
```

`flash` validates the checksum, both ESP images, existing POS partition layout,
factory-backup size/hash, device identity and unencrypted development security
state before writing, then runs flash readback verification. It replaces the
bootloader, partition table, NVS/OTA metadata and factory installer within the
merged image. It does not format SD or erase all physical flash. Finish the
on-device installer using the matching `pos-install/` SD package. This is the USB
installer path; normal signed OTA writes the inactive runtime slot instead.

## User file transfer

Connect Wi-Fi. Open **Share → Receive → Wi-Fi**, or select a file in **Files →
Share → Wi-Fi** to export it. Use the exact trusted-LAN IP shown, port 8080.
Store its 16-digit session key in a local file with mode 0600; do not put it in
shell history, command-line arguments or a Git repository. A browser session's
existing key also works with the tools. File sharing is HTTP on your trusted LAN;
it is explicitly opened, authenticated and stopped on exit, not an Internet service.

```sh
python tools/posctl.py files status --device http://192.168.1.8:8080 --key-file /private/panputer-session.key
python tools/posctl.py files push --device http://192.168.1.8:8080 \
  --key-file /private/panputer-session.key --source /path/song.mp3
python tools/posctl.py files pull --device http://192.168.1.8:8080 \
  --key-file /private/panputer-session.key --output /path/new-recording.wav
```

Push writes a new file in Downloads, with client SHA-256 checked before device
publication. Pull downloads only the export selected on Panputer, verifies its
SHA-256 and publishes a fresh local filename. Interrupted downloads remain `.part`;
collisions never overwrite an existing file. Directory browsing/arbitrary SD
access is not exposed through this API. Status reports model, device/build/version,
health, OTA state, accepted sequence and signing-key fingerprint; it omits secrets.

## Evidence-gated wireless updates

The implementation exists, but **no physical recovery or OTA result has been
recorded in this repository**. Do these actual tests before using `deploy`:
USB/factory backup and restore with observed boot, SD streaming, signed update and
healthy confirmation, forced rollback and the interruption checks named below.
Host simulations cannot substitute for those results.

Create observations with `validation-template`, then fill actual outcomes/notes
and evidence paths. Put `device_id` (the probe's MAC) and `validated_build_id`
(the device status/diagnostic 64-digit build ID) in that observations JSON. Keep
serial captures, copied diagnostics, backup verification and test notes in the
same evidence folder. Do not mark a check passed because firmware compiled.

```sh
python tools/posctl.py validation-template --output /evidence/observations.json
# Perform the tests and fill observations.json with their real results.
python tools/posctl.py hardware-report --root /media/YOU/POS-SD \
  --observations /evidence/observations.json --output /evidence/report.json
```

Deployment requires explicit evidence-backed passes for `revision`, `usb_download`,
`factory_backup`, `factory_restore_boot`, `sd_streaming`, `cut_sd_commit`,
`cut_firmware_write`, `cut_boot_select`, `cut_boot_confirm`, `cut_restore_migrate`,
`cut_cleanup`, `ota_update` and `ota_rollback`. The tool rehashes those evidence
files and binds the report to the target device/running build. This is an operator
record, not cryptographic attestation. There is no `--skip-validation` option.

Build and create a **signed** update directory using UPDATES.md. Publish it at
its signed HTTPS base URL and serve its exact `manifest.json` over HTTPS. Configure
the matching separately verified public key in Software Update on Panputer.
Settings → **Developer deployment** asks whether physical testing passed before
opening a five-minute session; it displays its IP and fresh key. Keep the key in
your private session file. Then:

```sh
python tools/posctl.py deploy --device http://192.168.1.8:8080 \
  --key-file /private/panputer-session.key --update-dir dist/update-1 \
  --manifest-url https://YOUR-HOST/pos/latest.json --public-key /private/pos-public.pem \
  --validation-report /evidence/report.json --evidence-root /evidence --wait 600
```

The tool verifies the local P-256 signature/payload hashes, target trust/identity,
health and physical evidence, and the hosted manifest's exact bytes through normal
TLS verification. It requests that URL through the short-lived authenticated LAN
session. **Approve the displayed signed update on Panputer.** The device downloads
its firmware/resources over certificate-validated HTTPS and uses the existing
transactional inactive-slot updater. No raw firmware-flash network endpoint exists.

Asset updates keep the session. Firmware updates reboot and close it: after boot,
reconnect Wi-Fi if necessary, reopen Developer deployment and replace your private
session-key file with the new key. The tool keeps polling without persisting a
secret or enabling a startup server. It confirms only the matching device, trusted
publisher, exact build/version, accepted sequence, no pending update, healthy UI
and valid OTA state. Allow the healthy 60-second boot interval. A timeout is an
unconfirmed result, never a success. This version requires these on-device steps;
fully unattended deployment and automatic discovery are not enabled.

The certificate bundle uses ESP-IDF's common root set to fit the retained OTA
partitions. A host trusted by your computer may still use a root absent on POS;
choose a supported chain or build a verified custom/full bundle. Never disable TLS
validation to make a deployment pass. Keep exact release installers and known-good
backups available while testing.

For a guided first installation, [pos-setup](EASY_INSTALL.md) downloads current
releases and automates safe SD preparation/USB backup and flashing. Build its
standalone executable with `python tools/build_setup.py --output dist/pos-setup`.
It embeds the verified USB helper modules and runs without a checkout.

## Linux PC transfer validation

The standard host suite includes the dependency-free transfer engine, Wi-Fi
HTTP fixture and interoperability with native POS C offer/receive code. To run
the real private D-Bus GATT/agent smoke test without a physical Bluetooth radio:

```sh
python3 -m venv /tmp/pos-transfer-test
/tmp/pos-transfer-test/bin/pip --isolated install bleak==0.22.3 dbus-fast==2.44.5
/tmp/pos-transfer-test/bin/python tests/ble_dbus_smoke.py
```

Requires `dbus-daemon` (CI Ubuntu includes it). The test uses a private daemon and
simulated BlueZ to check actual D-Bus serialization/export, secure flags, numeric
comparison callbacks, file transfer, disconnect/resume and unregister cleanup.
It does not operate a Bluetooth radio or validate physical pairing/encryption.
Use native Linux and Cardputer for that final interoperability check.

The Calculator core has dependency-free native math tests and actual UI/SD-save
fixtures. Its eight-level parser bound is intentional: the Xtensa `.su` reports
show the recursive expression path's frame sizes, so nesting is capped for the
16 KiB UI task. Watch physical minimum stack/heap as part of device validation.
Navigation tests wrap `fseek`/`fopen` to guard the sparse-cache and scratch reuse
optimization, including spill-to-disk and a shorter object following a long one.

## Meshtastic native interoperability checks

With the pinned ESP-IDF environment active:

```sh
export POS_MESH_MBEDTLS_HOST="/tmp/pos-mesh-mbedtls"
cmake -S "$IDF_PATH/components/mbedtls/mbedtls" -B "$POS_MESH_MBEDTLS_HOST" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF
cmake --build "$POS_MESH_MBEDTLS_HOST" -j 4
python -m unittest discover -s tests -v
```

This runs the independent vectors through both OpenSSL and the actual Mbed TLS
firmware crypto path, plus the real device service with simulated NVS/clock/RTOS
and a native filesystem. CI requires it. Without that variable those specific
Mbed TLS/service checks are explicitly skipped; the OpenSSL/mesh tests still run.
No physical SX1262, FAT32 timing, mesh router or Meshtastic radio is simulated as
hardware evidence. See MESHTASTIC.md and BUG_REPORT_0.7.0.md.


## 0.8.1 release-size build

The pinned GCC 14.2 toolchain uses `-Oz` and POS-component-only LTO, with one
partition for cross-file size optimization. SDK/prebuilt hardware libraries stay
outside LTO. CMake removes ESP-IDF's default `-fno-lto` only from the final POS
link and locates the matching GCC plugin. `POS_OPTIMIZE_LINK=OFF` is available for
diagnostics, but an unoptimized runtime can exceed the unchanged OTA slot.
Both runtime and installer must pass ordinary IDF size checks before packaging.
Assertions, panic/backtraces, certificate validation and signed updates remain.
