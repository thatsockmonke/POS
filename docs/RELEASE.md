# POS 0.8.3-dev

Adds USB cable file transfer for Linux and Chromebook/Crostini, avoiding local
Wi-Fi/BlueZ dependence and keeping the microSD inserted.

- PC tool: Send/Receive → **3. USB cable**; detect/select the serial port. Standard
  Python only, with no additional USB/Bluetooth package installation.
- On POS: **Share → Receive USB cable / PC**, or **Files → file → Share → USB
  cable to PC**. Incoming filenames/sizes require on-device acceptance.
- Streamed files, progress on both sides, CRC framing, SHA-256 verification,
  interruption/resume, duplicate-ACK handling and no overwrite of existing data.
- File transfer uses the running OS, without esptool, modem-control resets,
  firmware/partition writes or raw USB disk access. Console input is reserved
  during the explicit session; physical keyboard/G0 still work. Late packets
  cannot act as serial keyboard shortcuts.
- Existing explanatory notices are retained in a signed SD help catalog to
  preserve both OTA slots and independent recovery within the flash limit.

Update POS once through Settings → System → Software Update, then download the
new `pos-transfer`. No SD formatting or repartition is required. On ChromeOS,
share the Cardputer USB device with Linux and close flashing/serial monitors.

[Step-by-step instructions](TRANSFER.md) · [verification and limits](BUG_REPORT_0.8.3.md).
Native tests exercise actual C/PC code over pseudo-terminals. Physical Cardputer
USB, ChromeOS sharing, actual throughput and unplug timing are not claimed tested.
This is a development prerelease. Audio memory fixes remain included.
