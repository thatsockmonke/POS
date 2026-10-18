#pragma once
#include "pos_files.h"
#define POS_IMAGE_WIDTH 240
#define POS_IMAGE_HEIGHT 88
typedef struct pos_image pos_image_t;
bool pos_image_supported(const char *);
/* Baseline JPEG (ROM decoder on ESP32-S3) and uncompressed 24/32-bit BMP.
 * Pixels are streamed into a viewport; dimensions are bounded to 8192. */
pos_image_t *pos_image_open(const char *, const char *, pos_error_t *);
void pos_image_size(const pos_image_t *, unsigned *, unsigned *);
int pos_image_render(pos_image_t *, double cx, double cy,
                     double pixels_per_source, uint16_t viewport[240 * 88],
                     pos_io_progress_t, void *, pos_error_t *);
void pos_image_close(pos_image_t *);
