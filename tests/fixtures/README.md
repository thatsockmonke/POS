# Generated audio fixtures

`tone.mp3` is an original 440-Hz sine tone, generated without third-party audio:

```
ffmpeg -f lavfi -i 'sine=frequency=440:duration=0.15:sample_rate=22050' \
  -ac 1 -c:a libmp3lame -b:a 32k -write_xing 0 -map_metadata -1 -f mp3 tone.mp3
```

`tone-reference.raw` is the stereo signed-16-bit PCM decoded using unmodified
minimp3 commit `ea99364f61c14656440e8d77e9c233ccf3124633`, scalar mode,
`MINIMP3_ONLY_MP3`, with mono duplicated into left/right channels. The POS
heap-workspace adapter must produce exactly the same bytes. FFmpeg is not a
runtime/test dependency; these small generated fixtures are committed.
