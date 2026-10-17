POS 0.6.3-dev - Pan Operating System

Boot starts black. Pink slides in first, yellow starts halfway through pink,
blue starts halfway through yellow, then large POS fades in and holds one second.
ADV arrows: ; (Up), , (Left), . (Down), / (Right), no Fn needed in menus/apps.
Text entry keeps punctuation; Fn+arrows moves the editor cursor.
Launcher: Left/Right moves, Up/Enter opens the centered app. Fn+Space closes the app.
Background: choose 1-12 stripes; arrows pick color, Q/E hue, Shift fine.
Select stripes with Up/Down, Enter opens color picker; S saves, Esc cancels.
Only one foreground app runs; shared system services may continue.
Ctrl+U clears text fields; setup validates names before continuing.
Files shows folder pages and empty folders. Keep SD inserted when restarting.

System files are integrity checked on boot. Put user files outside System.
Update to 0.6.0-dev before later versions; subsequent packages omit the minimum field.
The official source migrates automatically; signatures/hashes remain required.
Text Editor: Fn+arrows move; Ctrl+S save, Ctrl+F find, Ctrl+G line.
Esc opens the editor menu; Home asks about unsaved changes.
Wi-Fi: scan/connect, encrypted saved networks, time sync, HTTPS and LAN transfer.
Sync time before HTTPS. Local HTTP transfer is for trusted networks only.
Files: Copy/Cut, navigate, Folder actions > Paste. Trash is reversible.
Music: MP3 / PCM 16-bit WAV. Space pause, Left/Right tracks, Up/Down volume, R shuffle.
Share: local save, Wi-Fi, or numeric-paired BLE. Accept each incoming file.
GPS/LoRa: Settings > Expansion hardware shows presence and feature enablement.
Radio/Range require a detected, enabled cap; GPS presence separately gates Maps.
Maps downloads small OSM road regions over Wi-Fi, compiles on-device, and works offline. TX consent resets on reboot; location is opt-in.
Radio uses the POS protocol, not Meshtastic. Match frequency/settings on peers.
Range CSVs: POS/Users/Owner/RangeTests. Outbox/messages: POS/Users/Owner/Radio.
Games and the POS terminal are removed from scope. Physical hardware is unverified.

Software Update: Settings > System / software update. The official HTTPS
feed and pinned P-256 key are built in. Connect Wi-Fi and choose Check for Updates.
Assets update without flashing; firmware uses the inactive OTA slot and a journal.
Battery percent/icon remain visible; near-full is a voltage estimate, not charger status.
Editor: Shift+arrows select, Ctrl+A/C/X/V, Ctrl+Z undo, Ctrl+Y redo.
Ctrl+arrows move words/document ends; Ctrl+U/D pages; Ctrl+L/E line start/end.
History and clipboard are on SD and last for the current open document.
Settings: direct profile, brightness, regional/custom timezone, UTC time and reset.
Crash JSON: POS/Users/Owner/Diagnostics/Crashes. Safe mode needs explicit normal boot.
Recorder: standard 16 kHz mono WAV files in /Recordings. Space pauses;
Enter/Esc saves; Fn+Space saves and closes. Recover interrupted .wav.part in Recorder.
Files: select MP3/WAV > Play in Audio Player. No Audio Player launcher icon.
Audio Player stops at EOF; Music remains the folder/playlist library app.
Settings > IR & motion: live BMI270 and explicit 38 kHz NEC transmit (no IR receive).
Versioned app-save metadata uses verified generation slots; legacy Counter saves
migrate without deleting originals. Future schemas are preserved and refused.
Settings > Developer deployment: temporary trusted-LAN session, signed HTTPS
updates only, on-device approval and physical recovery/OTA evidence required.
After firmware reboot reopen the session to verify; no always-on deployment service.
Turn-by-turn/offline routing, map search/names and Meshtastic remain future work.

Computer installation: download pos-setup and follow EASY_INSTALL.md.
Crostini shared folders support copy only; raw formatting/USB need OS access.

0.6.1: strict seven-field update metadata; obsolete updater compatibility removed.
Files uses text lists and Enter action menus again.
Settings > Sounds: master, volume 0-100%, keys, app/notification tones, preview.
Feedback stays silent during recording/playback. Recorder drains batches before
redrawing; two-second PCM buffer preferred, one-second fallback, 4-KiB SD buffer.
Real storage errors keep recoverable partial WAVs. Do not delete your only copy.

0.6.2: fixed speaker/capture handoff on the 100-Hz scheduler; varied UI tones and startup jingle.
Native 32x32 icons, optimized partial/pipelined ribbon rendering and responsive arrow navigation.
Editor nearby-text cache, idle redraw suppression, queued typing batches and confirmed Trash deletion.
Known Networks has Connect, Forget, Auto-connect and explicit old-vault import; no repeated vault password.
Settings > System > Firmware history: up to 15 signed firmware/System pairs in POS/.History.
Updates from this version archive the current pair; Import previous installation can recover a matching prior OTA/System.
Factory recovery and both OTA runtime slots are retained. Flash images never execute from SD.

0.6.3: Maps app, GPS/coordinate regions, Wi-Fi downloads, offline pan/zoom/follow.
Enter sets a session waypoint: straight-line guidance only, not road routing.
Map data: (c) OpenStreetMap contributors / ODbL 1.0; keep original .osm files.
