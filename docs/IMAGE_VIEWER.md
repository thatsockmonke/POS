# Image viewer

In **Files**, choose an image → Open file. The viewer starts fitted to its
240×88 image area, leaving the title, battery indicator and controls visible.

- Arrow keys pan; `+`/`=` zooms in and `-` zooms out.
- `0` fits and recentres the image. Escape returns to Files; global Home exits.

Supported: baseline sequential JPEG (.jpg/.jpeg, using the ESP32-S3 ROM decoder)
and uncompressed 24/32-bit BMP (.bmp, including top-down and padded rows).
Dimensions are bounded to 8192×8192. Progressive JPEG, PNG, GIF, compressed BMP,
16-bit/paletted BMP and EXIF orientation are unsupported; convert to a supported
format on a computer. Unsupported/damaged input produces an error and is kept.

The viewer retains one RGB565 viewport (42,240 bytes), not the full source
image. BMP reads use 1-KiB blocks; JPEG decodes through a 4-KiB work pool into
visible pixels. Zoom/pan decodes the source again, so large JPEGs can take time.
Temporary interface sound holds prevent loading cues from competing for CPU;
saved audio preferences are unchanged. Low-memory failures are reported safely.
ROM JPEG output, large-image timing, colours and navigation need device testing.
