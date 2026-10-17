# POS 0.6.8-dev

Both map views already supported arrow-key pan and +/- zoom. This release adds
adjustable pan distance and makes the controls easier to discover.

- **Arrows:** pan without Fn. Moving manually pauses GPS follow.
- **P:** cycle **Normal → Large → Fine → Normal**. The current movement per
  press is shown on screen and scales with zoom. The choice is shared between
  both map views during this boot; reboot resets Normal.
- **+** or **=** zooms in; **−** zooms out.
- **C:** recenter and follow GPS. World recentering now handles offsets smaller
  than one pixel, too. A current fix is required for a truthful live position.
- **0:** fit the whole planet (world view only). **Enter:** map menu.

Regional Fine/Normal/Large steps are 1/16, 1/4 and 1/2 of visible map width
(62.5/250/500 m at a 1 km view). World steps are 7.5/30/60 degrees divided by
zoom. Changing the step updates labels without another SD raster read.

## Installation and updates

Use **Settings → System → Software Update** for the signed 0.6.8-dev update.
No SD formatting, map redownload, settings migration or partition change is
required. The matching USB installer and SD package are also included for a
fresh installation; use this release's **pos-setup** and [EASY_INSTALL.md](EASY_INSTALL.md).
If an older updater still cannot load its manifest, use the existing
[USB/recovery instructions](UPDATER_HEAP.md). All 0.6.7 features and its memory fix
remain. Firmware and installed SD System versions are verified together.

## Validation

Regression tests exercise both real map UIs: all four arrow directions, pan-step
cycling/wrap, scaled movement, +/=/- zoom, world reset, GPS recenter/follow,
metadata-only redraw when pressing P and cleanup on Home. Both firmware roles
build successfully and all **384 host tests pass**. A simulated package
backup/reinstall/restore preserves user data and validates the new System hashes.
Signed release
files, versions and payload hashes are verified. Physical Cardputer/GPS/SD
performance and on-device key feel still need hardware validation.
