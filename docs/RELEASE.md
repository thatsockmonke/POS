POS 0.5.9-dev adds microphone gain and a visual, animated UI pass.

- Recorder has saved 1x–64x digital gain (8x default), adjustable with Left/Right
  during capture. Gain preferences are written after stopping, outside capture.
  The normal view has a wallpaper-colored microphone and one moving mono bar,
  percentage/time/gain and clipping feedback; raw L/R values are removed.
- Launcher icons spring between selections, zoom/jump and fade quickly into apps,
  then fade back and reverse the zoom with a small bounce on return. Fn + apostrophe
  is now a global close alias alongside short G0 and existing Fn+Space. Held
  recovery shortcuts remain. Motion skips missed frames, retains one foreground
  app and uses streamed scanlines rather than a whole-screen framebuffer.
- Background shows editable stripes directly, pulses the selected stripe white,
  opens its picker with Enter and uses Left/Right for count, S to save, P for the
  pan preset and Escape to cancel. No separate Preview button remains.
- Files has visual cards and folder-content hints for documents, audio, images
  and mixed contents. Decoration inspects at most 24 immediate entries, without
  file-content loading, recursive scans or symlink following. Enter opens folders/audio/text; Space opens the action menu. Protected
  actions, copy/move, Trash, deletion confirmation and pagination remain.
- Counter has a squishy red button and jumping number (Enter/Space/Up +1, Down -1).
  Music/Audio Player have a themed disc/progress view; shared screens carry
  wallpaper accents. Dense technical screens keep readable text.
- Settings format 3 adds gain. Formats 1/2 load with 8x without rewriting; saved
  gain survives backup/restore. Unsupported future preferences remain protected.
- The user confirms the microphone works in 0.5.8-dev. New gain, motion and UI
  performance still need a physical check. Previews use the real portable
  renderer with sample state, not measurements or recordings from hardware.

Both firmware roles build. Host tests cover live gain reaching PCM, clipping,
preferences/migration/backup, folder classification, graphics/control bounds,
fade endpoints, spring settling and input/lifecycle regressions. Signed on-device
update is available through Software Update; no SD format or factory restore is
needed. Experimental prerelease; retain verified recovery backups.
