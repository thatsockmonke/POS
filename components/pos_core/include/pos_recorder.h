#pragma once
#include "pos_files.h"
#define POS_RECORDINGS "Recordings"
#define POS_RECORD_RATE 16000
typedef struct pos_recorder pos_recorder_t;
/* Mono signed-16 PCM WAV. A .wav.part remains recoverable until readback and
 * publication succeed; existing files are never replaced. */
pos_recorder_t *pos_recorder_open(const char *root, pos_error_t *error);
int pos_recorder_write(pos_recorder_t *, const int16_t *, size_t,
                       pos_error_t *);
uint32_t pos_recorder_frames(const pos_recorder_t *);
const char *pos_recorder_path(const pos_recorder_t *);
int pos_recorder_finish(pos_recorder_t *, pos_error_t *);
void pos_recorder_close(pos_recorder_t *);
/* Repair a selected interrupted WAV using its actual whole PCM frames. */
int pos_recorder_recover(const char *root, const char *relative, pos_error_t *);
