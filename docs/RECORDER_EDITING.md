# Edit a recording

Open **Recorder → Edit recording**, or **Files → a WAV → Edit PCM recording**.
The editor scans a waveform without loading the recording into RAM.

1. **Waveform** shows the whole recording's peak envelope. Any key returns.
2. Enter **Start** and **End** in seconds. The retained interval starts at Start
   and ends just before End, rounded to the nearest PCM frame.
3. Optionally enable **Normalize** to scale the retained audio to a 95% peak.
   Silence stays silent. Normalization does not remove noise or repair clipping.
4. **Play original** uses the existing Audio Player. **Save edited copy** writes
   `Recordings/Edit-NNNN.wav`, never replacing the source or an existing edit.

Input/output supports 16-bit PCM WAV, mono or stereo, 8–48 kHz, with standard RIFF
chunks. Unsupported/compressed WAVs are kept untouched. Scanning, trimming,
normalization and verification use small reusable buffers. A `.wav.part` is
synced, parsed and compared against the source transformation before it becomes
an ordinary `.wav`. Cancel/error leaves the original and any partial output;
no incomplete file is marked finished. Keep enough SD space for the new copy.
