# POS 0.8.5-dev

The boot intro can now be skipped with a keyboard key or G0. Its target
duration is halved from 1.88 seconds to 0.94 seconds.

- Stripe fills and logo fade use 20 ms frames instead of 40 ms, and the final
  logo holds for 500 ms instead of 1,000 ms. Artwork/order stays the same.
- Input skips the slides, fade or final hold. The skip press and already-queued
  input are consumed, so G0 does not also trigger Home and Enter does not select
  an item on the next screen. Key releases alone do not skip the intro.
- Frame scheduling follows elapsed time and drops overdue frames when display
  work is slow. The first black frame and final logo are retained.
- Skipping ends only the presentation: hardware initialization, SD/system
  validation, setup and recovery still run normally. Held recovery shortcuts
  remain managed by the existing input supervisor.
- Esc without Fn and USB cable file transfer remain included.

Install through **Settings → System → Software Update**. No SD formatting or
reinstallation is needed. Fresh-install downloads include matching installer,
runtime, SD resources, tools and the faster animated preview.

[Implementation, regression checks and limitations](BUG_REPORT_0.8.5.md).
Both firmware roles and the host suite are checked before publication. Physical
Cardputer input/LCD timing testing is still needed; cloud tests do not claim hardware
validation. This is a development prerelease.
