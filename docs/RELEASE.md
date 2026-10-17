POS 0.6.0-dev — required transition before any future update.

**Update to 0.6.0-dev first before installing any later release.** This is the
last release that carries the obsolete minimum_version field so older installed
updaters can read it. All later update packages omit that field entirely.

- Software Update no longer shows Minimum version or blocks direct upgrades
  based on that value. There is no --minimum-version option or publisher setting.
- The old official updates/manifest.json feed stays pinned to signed 0.6.0-dev.
  Older devices will therefore discover 0.6.0 first even when newer versions exist.
  0.6.0 automatically migrates the exact old official source/key to updates/latest.json,
  preserving trust and the accepted signed sequence. Custom sources stay untouched.
- This transition package retains a neutral 0.0.0 legacy value. An older updater
  may display it once while reviewing 0.6.0; the line is gone after upgrading.
  Future packages contain no minimum_version key. Legacy runtime parsing/source
  migration can be removed later while the frozen 0.6.0 bridge remains available.
- Signatures, HTTPS certificates, file hashes, hardware/package compatibility,
  signed sequences, firmware downgrade prevention, rollback and user-data
  preservation remain. Older releases/signatures remain immutable.
- Includes the black-start staggered flag/logo boot, navigation without Fn in
  menus/apps, microphone gain, spring transitions and graphical app views.

Builds/tests cover new metadata, the transition feed and migration, future packages
without the field, existing-updater compatibility and protected update behavior.
Physical USB/OTA/power-loss behavior remains subject to on-device validation.

Install over Wi-Fi through Settings → System → Software Update → Check for Updates.
No SD formatting or USB reflash is needed for a working installation with Software
Update. Experimental prerelease.
