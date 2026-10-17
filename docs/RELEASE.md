POS 0.5.10-dev updates the boot sequence and removes Fn from everyday navigation.

- Boot starts on a completely black screen. Pink slides into the top third
  from the left; yellow starts in the middle when pink is halfway, then blue
  starts at the bottom when yellow is halfway. After the flag completes,
  large centered POS fades in and holds for one second. No color flashes.
- The first black frame is drawn before the backlight turns on. Boot artwork
  takes priority over the battery overlay, which returns on normal screens.
- Printed arrow keys work directly in the launcher, menus, Files, Background,
  color picker, Music and Recorder without Fn. Shift still gives fine picker
  adjustment. Text fields/editor keep punctuation; Fn+arrows still moves the
  text cursor. USB arrows and existing Fn navigation remain supported.
- Saved 1x–64x microphone gain, live gain changes, spring/fade app transitions,
  graphical app views and protected user-data/update behavior are retained.
- Updated installation instructions, controls and animated boot preview.

Both firmware roles build. Native checks cover the entire black/stripe sequence,
exact half-slide staggering, logo fade/hold, preview timings, physical TCA8418
navigation with/without Fn, punctuation entry and live microphone gain.
Physical animation timing still needs an on-device check. Previews use the real
portable renderer with sample state, not a recording of hardware.

Install through Settings → System → Software Update → Check for Updates over
Wi-Fi. No SD format or factory restore is needed. Experimental prerelease.
