# POS 0.8.4-dev: physical Esc

## Cause

The stock ADV keyboard mapping treats the top-left key as a backtick and maps
Fn plus that key to Escape. POS copied this mapping even though most app
screens advertise Esc without Fn. The physical key therefore never produced
the `POS_KEY_ESCAPE` event those screens handle. Text fields could insert a
backtick instead. The shared cancellation handlers themselves already accepted
Escape; changing individual app handlers would leave the same input mismatch.

Previous UI tests generally injected `POS_KEY_ESCAPE` directly. They tested
the cancellation branches but did not exercise the physical top-left key first.
The full keyboard-layout test also expected the stock bare backtick behavior.

## Fix

The centralized TCA8418 decoder now makes the Esc legend primary. Bare Esc
and Fn+Esc both emit `POS_KEY_ESCAPE`; Shift+Esc emits a tilde, and Alt+Esc
emits a backtick. The last two combinations preserve text entry. The pressed
key identity remains latched until release, including when Fn is released first.
Other stock keys, arrow punctuation in text, modifier tracking and protected
Home shortcuts keep their existing behavior.

Menu/text hints, installation instructions and hardware key-test guidance
describe Esc without Fn. Press Esc last when collecting all raw key positions.
Esc is local back/cancel, not a global Home request. The editor's Esc menu and
unsaved-document confirmation remain in place. Cancellation does not approve
destructive confirmations or save draft settings.

## Verification

The new integration regression failed on the old decoder in all 16 bare-Esc
paths, while Fn+Esc already passed. It now runs both layers through shared
menus, text fields, notices, progress cancellation, the color picker,
Background, first-time setup, Settings, Files, the file picker, Text Editor,
Wi-Fi, Recorder, Music, Share and Calculator. It checks release identity,
unchanged settings, unchanged cancelled text and the absence of a global Home
latch. Further tests cover tilde/backtick text entry and Fn-release ordering.
The app lifecycle test alternates global Home and physical bare Esc for 100
open/save/close cycles, verifying resource cleanup.

Results: all **530** host regression tests passed with no skips. All four
Escape-focused test methods also passed with AddressSanitizer and undefined
behavior checks enabled. Both ESP-IDF firmware roles built successfully: the
runtime is 1,506,816 bytes (512 bytes below its existing OTA-slot limit), and
the installer is 822,576 bytes. No partition change was needed.

These are native tests of the real decoder/core/UI with mocked device services,
not physical key presses on a Cardputer. Both installer and runtime builds must
fit the existing partitions; signed update/package verification remains required.
Physical ADV key timing, USB/SD/audio behavior and power-loss recovery have not
been tested by this cloud task.
