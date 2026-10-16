POS 0.3.5-dev is an experimental Cardputer ADV operating-environment foundation.

- Adds the Background app: choose 1-12 horizontal stripes and each stripe color.
- Visual picker: WASD moves the cursor, Q/E changes hue, Shift makes finer
  adjustments, Enter accepts and Escape cancels. Includes a live swatch.
- Preview a draft, then Save & Apply. Cancel/Home and failed saves keep the
  previous background. Preferences survive restarts and backup/restore.
- Replaces launcher pages with a seven-app ribbon: A/D moves left/right;
  W opens the centered app. Icons are black with white outlines.
- Fn+Space closes the foreground app from any screen; Escape goes back and
  closes apps from their main screen. Only one foreground app runs at a time.
- Settings format 2 stores readable RGB colors. Format 1 remains readable and
  gains the default pan-flag background without losing identity/preferences.
- Includes launcher, color-picker and boot-animation previews. The existing
  animated boot sequence is retained.

Download the named pos-VERSION.zip and its checksum, extract it, and read INSTALL.md.
Flash the small USB installer at 0x0. Extract the inner pos-install.zip onto a FAT32
microSD; the installer verifies and installs the final runtime itself.

The full master roadmap is not implemented. Physical ADV flashing, display/key
mapping, factory recovery, SD behavior, sleep and electrical power-loss resilience
still require validation. Preserve factory flash and user data before experimentation.
