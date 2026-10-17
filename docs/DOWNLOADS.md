# Download POS

The complete package contains the matching USB installer, SD installation files,
USB helper scripts, checksums, feature status and full installation instructions.
You do not need to compile the OS to use a prebuilt package.

## Current development package

**POS 0.6.2-dev — experimental; audio handoff fix, faster rendering/editor, Known Networks and verified firmware history. Upgrade through 0.6.0-dev first.**

- **Beginner installer:** [download pos-setup](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/pos-setup) · [checksum](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/pos-setup.sha256) · [easy instructions](EASY_INSTALL.md)
- [POS 0.6.2-dev release and installation downloads](https://github.com/thatsockmonke/POS/releases/tag/v0.6.2-dev)
- [Download pos-0.6.2-dev.zip](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/pos-0.6.2-dev.zip)
- [Download its SHA-256 checksum](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/pos-0.6.2-dev.zip.sha256)
- [Signed OTA package](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/pos-update-0.6.2-dev.zip) · [checksum](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/pos-update-0.6.2-dev.zip.sha256) · [on-device update instructions](UPDATES.md)
- [Boot animation preview](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/boot-animation.png)
- [Animated launcher/app transitions](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/ui-animation.png)
- App previews: [Recorder](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/recorder.png), [Files](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/files.png), [Background](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/background.png), [Counter](https://github.com/thatsockmonke/POS/releases/download/v0.6.2-dev/counter.png)
- [Browse versioned downloads](https://github.com/thatsockmonke/POS/tree/main/downloads)
- [Full installation guide](INSTALL.md)
- [Implemented / partial / planned features](STATUS.md)

The 0.3.6-dev candidate was withdrawn before release after an ESP32 stack audit.
Use 0.6.2-dev, which includes the bounded-buffer fix and a compiler frame-size guard.

Every new POS version is published as a versioned GitHub Release with its matching
installation ZIP and checksum. Development versions are marked **prerelease**.
For a private repository, sign into the GitHub account with access before
downloading with the USB wizard. The official on-device update feed uses public downloads. The versioned files
committed under `downloads/` remain available as a fallback.

Extract the named POS ZIP, **not GitHub's automatic Source code ZIP**, and follow
`INSTALL.md` inside it. Flash only `pos-installer-usb.bin` at 0x0; extract the inner
`pos-install.zip` onto FAT32 microSD. Do not flash its runtime binary manually at 0x0.

## GitHub Actions and Releases

The **Build and package POS** workflow builds both roles, runs host tests, then
exports the same complete installation bundle. After a successful run, open
**Actions → Build and package POS → the run → Artifacts → pos-firmware-VERSION**.
GitHub may wrap the artifact in another ZIP; extract that wrapper, then use the
named `pos-VERSION.zip` and `.sha256` file inside.

Pushing a matching `vVERSION` tag runs the checks and creates an **experimental
prerelease** with the ZIP/checksum and static/animated splash previews attached. The tag must match `PROJECT_VER` in
CMakeLists.txt. The main branch build uploads artifacts; it does not automatically
claim hardware validation or mark development firmware stable.

For each new version, update `PROJECT_VER`, the SD payload version and release
notes together, then push the matching `vVERSION` tag after validation. Published
release assets are retained on workflow reruns; corrections need a new version.

## Build a package yourself

From the source checkout with ESP-IDF v5.5.5 activated:

```sh
python tools/posctl.py build --build-root build-v050
python -m unittest discover -s tests -v
python tools/posctl.py bundle --build-root build-v050 --output dist/pos-0.6.2-dev --zip-output dist/pos-0.6.2-dev.zip
```

Choose a fresh output name if one already exists. `bundle` verifies images and
includes checksums; the outer ZIP adds a `.sha256` sidecar. The downloaded bundle's
USB helper scripts work with pip-installed esptool. Building firmware or running
native recovery helpers requires the **full source checkout** and SDK, not just
the two copied USB scripts in the binary download.
