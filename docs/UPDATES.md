# Signed online software updates

Open **Settings → System / software update → Software Update → Check for Updates**.
POS **0.5.3-dev and newer** include the official feed and trusted P-256 public key.
There is no GitHub login or manual key import for a fresh/default installation.

1. Connect Wi-Fi when prompted. POS synchronizes time when the clock has not been
   set; a correct date is required for HTTPS certificate validation.
2. Choose **Check for Updates**. POS verifies the signature, hardware, package
   format, semantic version and increasing signed sequence.
3. Review the new release/type/size, then approve **Download and install verified
   update**. Escape/Home cancels preparation; the short commit step finishes safely.
4. A firmware update writes and verifies the inactive OTA slot, switches the verified
   SD System and restarts. Keep power and SD connected. Leave POS running normally
   for at least **60 seconds** so the new boot can be confirmed. A failed boot uses
   IDF rollback and the retained previous System. Asset-only updates do not flash.
5. The current version reports **POS is up to date**. From **0.6.0-dev**, there is
   no minimum-version cutoff or displayed minimum. Existing working updaters can
   install this release normally; installations without an updater still need USB
   installation. Signature/hash, model/format, sequence and downgrade checks remain.

**Required update order: install 0.6.0-dev before any later update.** It is the
last transition release that carries the obsolete minimum field for older
updaters. Future update packages omit it completely.

The official channel for 0.6.0-dev and newer is:

```text
https://raw.githubusercontent.com/thatsockmonke/POS/main/updates/latest.json
```

The old `updates/manifest.json` address stays pinned to signed **0.6.0-dev**.
Older devices therefore discover that required release first, even after newer
versions ship. Installing 0.6.0 migrates only the exact old official URL/key to
`latest.json`, preserving the trusted key and accepted sequence. Custom/partial
sources remain unchanged. Custom servers should likewise offer 0.6.0 before
serving later metadata. Fresh USB installations can use their matching bundle.

The built-in public key is [public-key.pem](../updates/public-key.pem), with DER
SPKI SHA-256 fingerprint:

```text
ce79ef57358d4714d7b5748e2f0c0140f4d916426a0d82ace21dce5a7b0d8561
```

The POS repository must remain public for anonymous device downloads. A network
failure, captive portal, missing feed, invalid date, bad signature or hash stops
installation; it never disables certificate checks or replaces the trusted key.
Downloads, previous System and diagnostics are retained for recovery.

## Older installs and custom sources

Older POS firmware does not contain this default feed/key. Bootstrap once using
the matching USB installer, following [INSTALL.md](INSTALL.md) and its backup/
reinstall instructions. Alternatively, POS 0.4+ can use its existing update-source
screen: copy the public PEM to Downloads, set the official manifest URL, compare
the fingerprint above and explicitly trust the key before checking for updates.
The ordinary full installation ZIP is not itself an OTA package.

Existing custom sources and their accepted sequences are preserved. **Configure
source / trusted key → Use official POS updates** restores the built-in settings
with confirmation. Keeping the same signing key retains its sequence floor;
explicitly changing keys starts a new key's history. URL, public key and sequence
live in internal NVS, outside SD backups. An NVS error never triggers an erase.
This is application-level verification, not ESP secure boot or eFuse anti-rollback.
The USB bootstrap installer remains unsigned development firmware.

## Publishing official releases

The normal firmware workflow builds/tests/publishes a tagged release. **Publish
signed POS update feed** then downloads that exact release ZIP/checksum, verifies
all contents, signs the firmware/System records and publishes:

- `updates/releases/<version>/`: immutable signed manifest, firmware, System
  payload, public key, distribution licenses and hashes.
- `updates/latest.json`: the current signed channel; `updates/manifest.json`
  remains pinned to the 0.6.0-dev transition for older clients. Versions and sequences
  must increase; interrupted/repeated jobs never replace existing version bytes.
- `pos-update-<version>.zip` and its checksum on the same GitHub release. This is
  the signed OTA download, distinct from the full installer ZIP.

For unattended signing the maintainer must configure the encrypted repository
secret **POS_UPDATE_SIGNING_KEY**, containing the matching private PEM. This is a
maintainer task only, never a step for ordinary device owners. The private key
stays outside Git, SD, public downloads and logs. Preserve a separate private
backup; losing it requires a deliberately trusted key change or USB bootstrap.

On a trusted Linux machine with GitHub CLI signed in, the helper validates the
key against the firmware's public fingerprint before securely setting the secret:

```sh
chmod 600 /private/path/pos-update-p256.pem
python tools/pos_release_updates.py --configure-signing-key /private/path/pos-update-p256.pem
```

Or use repository **Settings → Secrets and variables → Actions → New repository
secret**, name `POS_UPDATE_SIGNING_KEY`, and enter the private PEM there. Never
paste it into chat, an issue, a workflow input or a committed file. An integration
without Secrets permission cannot create this secret even if it can push code.
Missing signing configuration fails publication explicitly; it never publishes
an unsigned update or silently replaces the key.

Offline signing is also supported. After the exact release ZIP/checksum has
been published/downloaded, run from the POS checkout with esptool 4.12 and OpenSSL:

```sh
python tools/pos_release_updates.py --bundle /downloads/pos-0.5.3-dev.zip \
  --private-key /private/path/pos-update-p256.pem
python tools/pos_release_updates.py --verify
```

Commit/push only the generated `updates/latest.json`, the bridge `updates/manifest.json` when publishing 0.6.0, and the version directory.
The push triggers verification against the exact released bundle and attaches
its deterministic signed update ZIP. It requires no CI private key because the
signature already exists. Do not regenerate or overwrite a published version.
The GitHub workflow uses its scoped repository token to push the feed; it does
not need an additional publication token. Keep Actions repository writes enabled.

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
  --firmware build-v050/runtime/pos_runtime.bin --payload sd-payload \
  --release 0.6.0-dev --sequence 1 \
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
  --release 0.6.1-dev --sequence 2 \
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
The payload has `format` (1), `model`, `release`, `sequence` (1..4294967295),
`kind` (`firmware`/`assets`), `base_url` (HTTPS) and `package` (the ordinary complete
installer-manifest object). No other fields are accepted except the optional
legacy `minimum_version`. The 0.6.0-dev parser validates that field's syntax when
present but never uses it to block upgrades or show a minimum. The publisher
emits the neutral value `0.0.0` only for 0.6.0-dev and older packages, because
already-installed readers require eight fields to download the transition.
Packages newer than 0.6.0-dev contain exactly seven fields and never include
`minimum_version`. There is no minimum-version CLI option or publisher setting.
The official publisher requires the 0.6.0 bridge before publishing a later release
and never advances the old feed beyond it. Published older manifests/signatures
remain unchanged. Runtime legacy-field parsing and old-source migration can be
removed in a later release once the required 0.6.0 transition is established;
keep the frozen bridge available for devices that have not upgraded yet.
Signed manifests are
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
