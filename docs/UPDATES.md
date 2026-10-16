# Signed online software updates

Open **Settings → System / software update → Software Update**. POS 0.4 adds a
signed update client; a publisher must supply an HTTPS update directory and a
signing public key. **No public feed or embedded publisher key is configured by
default.** A private GitHub repository's release links require authentication and
are not an anonymous update feed. Host the signed directory on an HTTPS server
Panputer can access; do not place credentials in the URL or disable certificate
validation.

## Configure and use on Panputer

1. Copy the publisher's **public** PEM key into Downloads. Obtain its SHA-256
   fingerprint through a separate trusted channel. Never copy its private key to SD.
2. Connect Wi-Fi and synchronize time (or set UTC in Settings → Date & time).
3. In Software Update → Configure source / trusted key, enter the HTTPS URL of
   `manifest.json`. Import the public key, compare the displayed **DER SPKI SHA-256**
   fingerprint, and explicitly choose **Fingerprint verified: trust key**.
4. Choose **Check for Updates**. POS verifies the P-256/SHA-256 signature before
   trusting metadata, then checks model, package format, firmware target, semantic
   version, minimum directly upgradeable version and increasing signed sequence.
5. Review release/type/size and explicitly choose Download and install. Downloads
   and file readback stream in bounded buffers. Escape/Home can cancel preparation;
   the short commit step completes without interruption from UI cancellation.
6. An **asset update** swaps only verified SD System files and does not write flash.
   A **firmware update** verifies the inactive OTA slot before switching System and
   boot selection, then restarts. Keep SD inserted. The new runtime must validate
   its files/flash and service the UI for 60 seconds before confirming OTA.

Changing only the URL retains the highest accepted sequence. Importing a different
trusted key explicitly resets that key's sequence history. Source, public key and
accepted sequence live together in internal NVS; an NVS error never triggers an
erase. This configuration is not included in SD filesystem backups.

If the installed version is below the signed minimum, POS requires a clean
installation and directs you to back up the filesystem. Downgrade firmware and
already accepted sequences are refused. This is application-level update
verification, not ESP secure boot, eFuse anti-rollback or encrypted SD storage.
The USB bootstrap installer remains unsigned development firmware.

## Publisher tools

Use the source checkout with Python and OpenSSL installed. Generate your own key
once, outside the checkout, and keep a separately backed-up private copy:

```sh
python tools/posctl.py update-keygen \
  --private-key /your/private/location/pos-signing.pem \
  --public-key /your/public/location/pos-public.pem
```

The tool refuses existing paths, creates the private file with mode 0600, and
prints only the public fingerprint. Never commit or upload the private key.

Build a new runtime first, then make a firmware update directory:

```sh
python tools/posctl.py update-package --kind firmware \
  --firmware build-v040/runtime/pos_runtime.bin --payload sd-payload \
  --release 0.4.0-dev --minimum-version 0.4.0-dev --sequence 1 \
  --base-url https://your-server.example/pos/1/ \
  --private-key /your/private/location/pos-signing.pem --output dist/update-1
```

This example packages the current build. A device already on that same firmware
correctly reports no newer firmware; use a genuinely newer build/version when
publishing an upgrade. The source firmware must already contain this updater
(0.4 or newer). Bootstrap older POS releases through the USB installer/recovery
workflow first. Version labels follow `X.Y.Z` with optional SemVer prerelease
identifiers, such as `0.4.1-dev.2`.

For assets, copy the **exact installed manifest** from the target device or its
matching installation bundle. Supply a directory containing only changed System
resources; the tool merges their records into a complete target manifest:

```sh
python tools/posctl.py update-package --kind assets \
  --installed-manifest /path/to/target-installed.pos --payload /path/to/changed-assets \
  --release 0.4.1-dev --minimum-version 0.4.0-dev --sequence 2 \
  --base-url https://your-server.example/pos/2/ \
  --private-key /your/private/location/pos-signing.pem --output dist/update-2
```

Asset packages target the same resident firmware version/hash; changing firmware
metadata is refused on-device. Unchanged files are verified and copied from the
current System. Changed files come from `payload/System/...`. Upload the complete
output directory at its signed base URL. Supply a stable HTTPS manifest URL that
serves the latest envelope; never republish different content under an accepted
sequence. The displayed size is an upper bound because unchanged resources can
be reused locally. No user-installed app/package manager is implied by the term
“update package.”

## Format and interruption recovery

The UTF-8 envelope has exactly `payload` (the exact signed JSON string) and
`signature` (lowercase hex DER ECDSA signature). SHA-256 covers every payload byte.
The payload has exactly `format` (1), `model`, `release`, `minimum_version`,
`sequence` (1..4294967295), `kind` (`firmware`/`assets`), `base_url` (HTTPS) and
`package` (the ordinary complete installer-manifest object). Signed manifests are
limited to 32 KiB. All System paths and per-file sizes/hashes use the installer's
strict parser. Each HTTP response is bounded before publication, including
unknown-length/chunked responses.

Prepared and previous data live under `/POS/Updates/<signed-payload-sha256>/`.
`new-System` is fully written and verified first. Firmware is written and hashed
from the inactive flash slot before any boot selection. The root `pos-update.pos`
journal records both manifest identities, versions, update kind and sequence with
a checksum before System is renamed. User folders are never moved or replaced.

Before ordinary runtime validation, POS resolves an interrupted journal. An old
runtime after a failed update restores the previous System; a new runtime keeps
only a fully verified matching System. Damaged new firmware/System requests IDF
rollback. Unknown/torn journals stop automatic changes and offer recovery.
Successful confirmation archives the journal as `complete.json`; rollback archives
it as `rolled-back.json`, preserving the failed System when present.

Previous System folders, downloaded files, partials and logs are retained. Keep
these while testing a release; copy them to a computer before manually reclaiming
space. Do not delete `pos-update.pos`, rename active staging folders, remove the SD
or switch firmware while an update is pending. If automatic recovery cannot
resolve a state, preserve the entire card and use the factory installer/read-only
browser. Filesystem rename/journal tests do not establish electrical power-cut
safety on a real SD card. The factory installer partition and USB recovery remain
unchanged.
