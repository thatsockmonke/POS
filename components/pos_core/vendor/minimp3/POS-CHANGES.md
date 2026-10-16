# POS adapter changes

Upstream commit: ea99364f61c14656440e8d77e9c233ccf3124633 (CC0).

The decoder implementation takes an explicit `mp3dec_scratch_t *` workspace.
POS stores this workspace in its bounded, per-player heap context and frees the
entire context on close. The original entry point remains as a heap-backed wrapper.
No decoding arithmetic or bitstream behavior is changed. This avoids the stock
decoder's approximately 16-KiB local scratch frame on the ESP32 UI task stack.
The tone fixture and decoded PCM are compared with the unmodified upstream decoder.
