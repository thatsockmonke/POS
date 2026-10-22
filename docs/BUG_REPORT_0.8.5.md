# POS 0.8.5-dev: shorter, skippable boot presentation

## Change

The previous intro waited without handling input. It scheduled 22 frames at
40 ms plus a one-second final logo hold: 1,880 ms total before ordinary boot
continued. Frames now target 20 ms and the logo holds for 500 ms: 940 ms total.
The renderer and animated preview share these values.

The device presentation controller waits on the existing input queue during
both animation and hold. Any pressed-key event, including the G0 Home event,
ends the intro. It consumes the press and flushes input already queued at exit.
It does not use the normal foreground input handler, so a skip does not trigger
UI sounds, latch Home, or select an item on the following screen. Subsequent
release events remain harmless, and later new presses work normally.

Presentation deadlines follow the intro's start time. If LCD work overruns a
frame, the controller skips overdue samples rather than slowing the full intro.
It always draws black before enabling the backlight, and presents the final
logo unless input skips it or the display fails. Release-only input does not
restart a frame or final hold. The controller adds no heap allocations or SD
access. A compiler check caps its stack frame at 3 KiB.

The controller is a private device module, called at the same point in normal
startup as before. Skipping does not bypass SD validation, first-time setup,
firmware checks, recovery, or the independent held-key recovery supervisor.
Both installer and runtime use it; an ordinary OTA updates the runtime, while
existing factory recovery firmware remains unchanged until deliberately reflashed.

## Verification and limits

Native regression tests compile the actual device presentation controller and
renderer with deterministic input-queue, timer and LCD stubs. They use the
actual 100 Hz scheduler rate and cover completion duration, presses at eight
times across slides/fade/hold with five key types, release-only input, G0 without
a Home latch, queued-input cleanup, new input after exit, missing/failed LCD,
initial scheduling delay and overdue frames under slow rendering. Separate
renderer/pixel and APNG checks validate the artwork, exact frame delays and
halfway stripe stagger.

Results: **538 host regression tests passed** with no skips, including the
renderer and APNG checks. All seven device-controller test methods also passed
with AddressSanitizer and undefined-behavior checks. Both ESP-IDF firmware roles
built successfully. Runtime size is 1,507,056 bytes, leaving 272 bytes in the
existing OTA slot; the installer is 822,768 bytes. Partition sizes and recovery
layout were retained.

Cloud timing is simulated. Physical LCD throughput, key scanning delay and
wall-clock boot duration need Cardputer testing. Skipping takes effect after
an in-progress LCD transfer completes. The 940 ms target describes the intro,
not mandatory hardware/storage startup or the total power-on-to-launcher time.
