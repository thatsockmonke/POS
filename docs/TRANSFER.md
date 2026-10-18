# Simple Linux file transfer — Bluetooth and Wi-Fi

Download **pos-transfer** from the [latest POS release](https://github.com/thatsockmonke/POS/releases).
It is also inside the installation ZIP and its source is `tools/pos_transfer.py`.
The existing POS BLE protocol remains compatible with 0.6.9-dev and later.
0.6.11-dev adds Calculator, faster OSM processing and on-device transfer progress. The program needs Python 3.10+. Bluetooth additionally needs Linux, BlueZ and
Bluetooth enabled; Wi-Fi needs no extra Python packages.

Run it from your Downloads folder:

```sh
python3 ~/Downloads/pos-transfer
```

After choosing **Bluetooth** for the first time, agree to install its Bluetooth helpers into your home folder.
This uses a private Python environment, without modifying system Python or needing
sudo. Later runs reuse it automatically. If Python says `venv`/`ensurepip` is
missing, install `python3-venv` using your distribution's package manager first.
Dependencies are Bleak 0.22.3 and dbus-fast 2.44.5, fetched with normal verified
HTTPS. The device/transfer never requires Internet; the one-time helper download does.

```text
POS Transfer
1. Send to Cardputer
2. Receive from Cardputer
Choose a number (Ctrl+C cancels):
```

After Send/Receive, choose **Wi-Fi** or **Bluetooth**.

## Wi-Fi (recommended for large files and Crostini)

1. Put the computer and Cardputer on the same trusted Wi-Fi network.
2. For **Send to Cardputer**, open POS **Share → Receive Wi-Fi files / PC**.
   For **Receive from Cardputer**, choose a file in POS Files, then
   **Share → Wi-Fi file transfer**. Start the trusted-LAN session.
3. In POS Transfer choose Send or Receive, then **Wi-Fi**. Type the Cardputer
   IP and temporary 16-character session key shown on screen. The key is entered
   visibly, as requested. It is never saved or put in a URL.
4. Sent files arrive in Cardputer **Downloads**. Received files go into
   **~/Downloads/POS**, with their original names on 0.6.10-dev and later. Older POS versions
   ask you for a filename instead. Both directions stream and verify SHA-256.
5. Close the session on POS when finished.

This uses the existing **local HTTP** service: the temporary key authorizes access
but does not encrypt the file or key. Use a trusted LAN, not public/untrusted Wi-Fi.
No router ports, Internet server or always-on endpoint is needed. Network client
isolation/firewall rules may prevent computers on the same network from connecting.
Crostini can normally initiate this LAN connection without Bluetooth/USB access.
Wi-Fi retries start afresh, not at a partial offset. Failed uploads may leave a
POS partial that needs inspecting/moving in Files before retrying the same name.
Linux downloads keep failed `.pos-wifi-*.part` files; these are not complete files.

## Bluetooth: Send to Cardputer

1. On POS open **Share → Receive BLE file**.
2. Choose **1**, then **Bluetooth**, paste the file path, and select your Cardputer.
3. Compare the six-digit pairing codes on both screens. Accept only if they match.
4. Accept the incoming file on POS. It arrives in **Files → Downloads** after
   SHA-256 verification. Your computer's source stays intact.

## Bluetooth: Receive from Cardputer

1. Choose **2**, then **Bluetooth**. Leave the terminal open.
2. On POS use **Files → select file → Share → Send over BLE**, or **Share → Share
   a file → Send over BLE**. Select **POS Linux**.
3. Confirm matching pairing codes on both devices, then accept the named file
   in the Linux terminal.
4. Verified files appear in **~/Downloads/POS**. Ctrl+C closes the receiver.
   It can accept another file while open; each offer needs confirmation.

Receiving requires the Linux adapter to support BLE peripheral advertising,
not just discovery. The tool advertises only during Receive, requires LE Secure
Connections authenticated access, and unregisters its session on exit. It does
not expose other services, automatically trust peers, or send files without consent.
The BlueZ default pairing agent is held only while the program is running.

## Chromebook / Crostini

**Crostini often cannot access ChromeOS's built-in Bluetooth.** Installing Python
helpers cannot change this. Use a native Linux computer, or a USB BLE adapter
that ChromeOS actually allows you to pass through and BlueZ can use in Linux.
Receiving also needs peripheral support on that adapter. The tool reports missing
BlueZ/adapter access; it does not claim ChromeOS pairing alone provides access.
Wi-Fi file transfer and removable microSD remain alternatives.

## Optional commands and Bluetooth recovery

```sh
python3 ~/Downloads/pos-transfer send ~/Documents/note.txt --wifi
python3 ~/Downloads/pos-transfer receive --bluetooth --folder ~/Downloads/Panputer
```

Use ordinary files with FAT-compatible names of at most 80 UTF-8 bytes and below
2 GiB. BLE is suited to notes and small files; use Wi-Fi or SD for large music/maps.
The sender streams rather than loading the file into RAM. Transfer throughput
also depends on per-chunk durable writes and negotiated BLE capacity.

To resume, run Receive again and send the **same unchanged file with the same
name**. Hidden `.pos-share-*.part` and `.part.offer` files are kept on interruption;
metadata must match. No different existing completed file is overwritten: rename
the source or move the conflicting destination yourself. A hash failure retains
the partial for recovery; move it elsewhere before starting that file afresh.
Linux completion uses an atomic no-overwrite hard link, so choose a local Linux
filesystem supporting hard links (the default home folder works), not a FAT32 or
ChromeOS-shared destination. Failed publication preserves the verified partial.

Enable Bluetooth and make sure BlueZ's `bluetooth` service is running if the
adapter is missing. If stale pairing keys prevent reconnection, remove that
Cardputer from Linux Bluetooth settings and retry with matching-code confirmation.

Protocol details: [PROTOCOLS.md](PROTOCOLS.md). Tests cover the transfer engine,
resume, duplicates, malformed input, hashes, symlinks and conflicting files, plus
GATT/agent/advertisement registration over a private real D-Bus with simulated
BlueZ. **Physical Linux/Cardputer pairing, throughput, adapter compatibility and
Crostini USB access are not validated by these cloud tests.**

## If transfers appear stuck

Replace your old executable with the new release's **pos-transfer**. `--version`
shows its version. The new tool first checks the device/session key, then labels
source hashing, Cardputer file preparation and upload verification, with elapsed
wait messages every two seconds. Receive progress uses available socket data
rather than waiting for a 64 KiB read. POS 0.6.11 also shows transfer, SD verification
and completion progress on the device; leave that screen open until finished.

The message **“The name org.bluez was not provided by any .service files”** means
Linux cannot find its BlueZ Bluetooth service. It does not indicate a POS file
protocol or pairing-code failure. The tool explains this and offers to use Wi-Fi
for the same transfer. Crostini normally cannot use ChromeOS's built-in Bluetooth.
Installing the Python helpers or pairing through ChromeOS does not expose it.
Use Wi-Fi in Crostini, or native Linux with a supported adapter. On a native
Debian/Ubuntu system where an adapter exists but BlueZ is absent, install the
`bluez` package and enable the `bluetooth` service through your normal system tools.
The transfer tool does not install/start system services or bypass adapter access.

If Wi-Fi still fails, preserve the displayed phase and error, direction and file
size. Wrong session keys fail before file upload. Public/client-isolated Wi-Fi,
firewalls, SD errors and actual adapter/firmware behavior still need checking on
the real computer and Cardputer; cloud HTTP/D-Bus fixtures cannot prove those.
