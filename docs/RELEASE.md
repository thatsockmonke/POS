# POS 0.8.4-dev

Fixes the physical Esc key across apps and dialogs. Previously the top-left
key produced a backtick unless Fn was held, so screens advertising Esc could
ignore it or insert a character instead of cancelling.

- **Esc alone** now goes back/cancels. **Fn+Esc** continues to work.
- **Shift+Esc** types `~`; **Alt+Esc** types a backtick in text inputs.
- Shared menu/text prompts and hardware diagnostics now show the correct key.
- Cancellation remains local: Esc dismisses a dialog or goes back one screen;
  in the Text Editor it opens the existing document menu. Global Home and
  unsaved-document protections remain intact.
- Regression tests feed actual TCA8418 press/release codes into 16 app/dialog
  paths, with and without Fn, and check modifier/release identity, literal text,
  preserved settings and application cleanup.

Install through **Settings → System → Software Update**. No SD formatting or
reinstallation is needed. USB cable file transfer remains supported with the
0.8.3-dev PC tool; the new release includes its matching standalone tools too.

[Cause, regression checks and limitations](BUG_REPORT_0.8.4.md).
Both firmware roles and the host suite are checked before publication. Physical
Cardputer keyboard testing is still needed; cloud tests do not claim hardware
validation. This is a development prerelease.
