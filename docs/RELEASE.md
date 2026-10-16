POS 0.5.2-dev switches navigation from WASD to arrow keys throughout POS.

- Launcher: Left/Right moves between apps; Up or Enter opens the centered app.
- Menus, Files, installer and setup: arrows move; Enter selects.
- Background color picker: arrows move the color cursor; Q/E changes hue;
  Shift makes fine adjustments; Enter accepts and Escape cancels.
- Music and Files Audio Player: Left/Right changes tracks; Up/Down adjusts volume.
- USB fallback understands ANSI and application-mode arrow sequences without
  blocking physical keyboard scanning. A lone Escape has a 100 ms timeout.
- Help text, installation instructions and UI previews show the new controls.
  WASD remain normal letters in text fields and the editor.

On the Cardputer ADV, hold Fn with the printed arrows: semicolon for Up,
comma for Left, period for Down and slash for Right. Fn+Space still returns home.

Includes the guided pos-setup Linux/Crostini installer and complete matching
firmware/SD package. See EASY_INSTALL.md for installation. On-device maps and
Meshtastic compatibility remain on the software roadmap.

Experimental prerelease. Native regression tests and both firmware builds are
checked; physical Cardputer/SD/USB validation remains outstanding.
