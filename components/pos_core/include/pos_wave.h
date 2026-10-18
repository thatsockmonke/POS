#pragma once
#include "pos_files.h"
typedef struct {
  unsigned rate, channels;
  uint32_t frames;
  uint16_t peaks[240];
} pos_wave_info_t;
int pos_wave_scan(const char *, const char *, pos_wave_info_t *,
                  pos_io_progress_t, void *, pos_error_t *);
/* Frame-exact half-open interval; always a new verified WAV in Recordings. */
int pos_wave_edit(const char *, const char *, uint32_t first, uint32_t end,
                  bool normalize, char saved[241], pos_io_progress_t, void *,
                  pos_error_t *);
