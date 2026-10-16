POS 0.5.3-dev makes Settings Software Update use the official signed POS feed.

- The official HTTPS manifest URL and P-256 public key are built into firmware.
  Fresh installs and previously empty update settings need no manual configuration.
  Custom sources and their anti-replay sequence history are preserved.
- Check for Updates guides disconnected users into Wi-Fi settings and synchronizes
  the clock when needed for TLS. Release details and install confirmation remain explicit.
- A publisher signs the exact firmware/System files from the verified release ZIP,
  retains immutable version directories and advances a signed sequence/channel.
  Repeated publication preserves signatures and produces identical update archives.
- GitHub Actions publishes signed OTA downloads/feed after a successful release.
  Automatic signing needs the maintainer's POS_UPDATE_SIGNING_KEY repository secret;
  verified offline signing is supported without putting a private key in Git.
- Signed update ZIPs include distribution licenses. Installer, documentation and
  on-device help describe the one-time bootstrap and subsequent on-device updates.

Firmware is streamed to the inactive OTA slot and read back before activation.
Previous System and user files are retained; confirmation waits for a healthy
60-second boot. Host tests exercise signatures, downloads, UI, install transactions,
rollback and publication. Actual Cardputer reboot/rollback and electrical power-loss
behavior still require physical testing. Experimental prerelease.
