POS 0.6.5-dev — numeric microphone gain and a GPS-centered world map.

- Recorder → Microphone gain: enter any whole number from 1 to 4096, including
  values above 64. Ctrl+U clears the field; Enter saves. Left/Right still halves
  or doubles gain during capture, capped at 4096. Gain affects the mono WAV and
  meter; clipping safely saturates PCM. Existing preferences migrate on save.
- Maps highlights World map. Enter opens an offline, north-up Natural Earth
  coastline overview centered on the current GPS fix with a pink position pin.
  Arrows pan, +/− zoom, C resumes GPS follow, 0 fits the whole planet, and
  Enter/Escape returns to regional maps/downloads/navigation. No pin is shown
  without a fresh fix. The overview covers the planet, including poles/date line;
  detailed roads and routing still use downloaded regions.
- Known Networks no longer has the legacy import or old-passphrase option.
  Saved SSIDs, Connect, Forget and Auto-connect remain. Credentials remain
  authenticated encrypted device storage; old vault bytes are preserved.
- World coastlines are streamed from a 39,612-byte SD asset using a fixed read
  buffer. The asset is checksummed and included in signed updates; source and
  public-domain provenance ship beside it. No world road database is loaded.

Update from 0.6.4 through Settings → System → Software Update → Check for Updates.
No SD formatting, repartition or USB reflash is needed. Keep the card inserted
through reboot. Older-than-0.6.0 installations must pass through 0.6.0 first.
The independent factory recovery and both OTA slots remain unchanged.

Validation: all 367 host tests passed, including the regression suite, real audio-capture driver with simulated I2S
(including arbitrary and high gains, signed saturation and shared ownership),
world projection/asset corruption tests, menu/lifecycle tests and both pinned
ESP-IDF builds. No physical hardware tests are claimed: microphone loudness,
SD latency, GPS reception/follow and screen usability require device validation.
