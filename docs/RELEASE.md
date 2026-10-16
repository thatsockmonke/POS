POS 0.3.4-dev is an experimental Cardputer ADV operating-environment foundation.

- Keeps the pansexual-flag color sequence: pink #FF218C → yellow #FFD800
  → blue #21B1FF, once per boot at 180 ms per color.
- After blue, yellow slides into the middle from the left. Pink begins sliding
  into the top when yellow is halfway across; blue remains at the bottom.
- Fades in a larger centered POS logo over 400 ms. Its visible 198x91-pixel
  lettering reaches halfway into the pink and blue stripes.
- Holds the completed flag and logo for one second before validated startup.
- Includes a looping animated PNG preview rendered by the actual portable code;
  the device plays the animation once per boot. No SD image assets are needed.

Download the named pos-VERSION.zip and its checksum, extract it, and read INSTALL.md.
Flash the small USB installer at 0x0. Extract the inner pos-install.zip onto a FAT32
microSD; the installer verifies and installs the final runtime itself.

The full master roadmap is not implemented. Physical ADV flashing, display/key
mapping, factory recovery, SD behavior, sleep and electrical power-loss resilience
still require validation. Preserve factory flash and user data before experimentation.
