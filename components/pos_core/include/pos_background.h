#pragma once
#include <stdbool.h>
#include <stdint.h>

#define POS_BACKGROUND_MAX_STRIPES 12
typedef struct {
  uint8_t count;
  uint32_t colors[POS_BACKGROUND_MAX_STRIPES]; /* Ordinary 24-bit RGB. */
} pos_background_t;

void pos_background_defaults(pos_background_t *background);
typedef enum {
  POS_BACKGROUND_PANSEXUAL,
  POS_BACKGROUND_NONBINARY
} pos_background_preset_t;
bool pos_background_preset(pos_background_t *, pos_background_preset_t);
bool pos_background_valid(const pos_background_t *background);
bool pos_background_resize(pos_background_t *background, unsigned count);
bool pos_color_parse(const char *text, uint32_t *rgb);
uint16_t pos_color_rgb565(uint32_t rgb);
/* Hue is 0..359 degrees; saturation/value are 0..1000. */
uint32_t pos_color_hsv_rgb(unsigned hue, unsigned saturation, unsigned value);
void pos_color_rgb_hsv(uint32_t rgb, uint16_t *hue, uint16_t *saturation,
                       uint16_t *value);
