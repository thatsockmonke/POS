#pragma once
#include "pos_files.h"
#include <stdio.h>
typedef struct pos_music pos_music_t;
typedef struct {
  unsigned rate, channels;
  uint32_t bytes, total_bytes;
  uint64_t frames;
  bool ended;
} pos_music_status_t;
bool pos_music_supported(const char *name);
pos_music_t *pos_music_open(const char *root, const char *relative,
                            pos_error_t *e);
/* Decodes one bounded frame into stereo signed-16-bit PCM. */
int pos_music_read(pos_music_t *music, const int16_t **pcm, size_t *frames,
                   unsigned volume, pos_error_t *e);
pos_music_status_t pos_music_status(const pos_music_t *music);
void pos_music_close(pos_music_t *music);
int pos_music_track(const char *root, const char *folder, unsigned index,
                    char relative[241], unsigned *total);
int pos_music_playlist_add(const char *root, const char *playlist,
                           const char *track, pos_error_t *e);
int pos_music_playlist_track(const char *root, const char *playlist,
                             unsigned index, char relative[241],
                             unsigned *total);
