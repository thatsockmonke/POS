#include "pos_background.h"
#include <stdlib.h>
#include <string.h>

void pos_background_defaults(pos_background_t *background) {
  memset(background, 0, sizeof(*background));
  background->count = 3;
  background->colors[0] = 0xff218c;
  background->colors[1] = 0xffd800;
  background->colors[2] = 0x21b1ff;
}
bool pos_background_valid(const pos_background_t *background) {
  if (!background || !background->count ||
      background->count > POS_BACKGROUND_MAX_STRIPES)
    return false;
  for (unsigned i = 0; i < background->count; ++i)
    if (background->colors[i] > 0xffffff)
      return false;
  return true;
}
bool pos_background_resize(pos_background_t *background, unsigned count) {
  if (!pos_background_valid(background) || !count ||
      count > POS_BACKGROUND_MAX_STRIPES)
    return false;
  for (unsigned i = background->count; i < count; ++i)
    background->colors[i] = 0xffffff;
  background->count = count;
  for (unsigned i = count; i < POS_BACKGROUND_MAX_STRIPES; ++i)
    background->colors[i] = 0;
  return true;
}
bool pos_color_parse(const char *text, uint32_t *rgb) {
  if (!text || !rgb)
    return false;
  if (*text == '#')
    ++text;
  if (strlen(text) != 6)
    return false;
  uint32_t value = 0;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned char c = text[i];
    unsigned digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'f')
      digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      digit = c - 'A' + 10;
    else
      return false;
    value = value * 16 + digit;
  }
  *rgb = value;
  return true;
}
uint16_t pos_color_rgb565(uint32_t rgb) {
  return ((rgb >> 19) << 11) | (((rgb >> 10) & 63) << 5) | ((rgb >> 3) & 31);
}
uint32_t pos_color_hsv_rgb(unsigned hue, unsigned saturation, unsigned value) {
  if (hue >= 360 || saturation > 1000 || value > 1000)
    return 0;
  unsigned chroma = value * saturation / 1000;
  unsigned x = chroma * (60 - abs((int)(hue % 120) - 60)) / 60;
  unsigned m = value - chroma, r = 0, g = 0, b = 0;
  switch (hue / 60) {
  case 0:
    r = chroma;
    g = x;
    break;
  case 1:
    r = x;
    g = chroma;
    break;
  case 2:
    g = chroma;
    b = x;
    break;
  case 3:
    g = x;
    b = chroma;
    break;
  case 4:
    r = x;
    b = chroma;
    break;
  default:
    r = chroma;
    b = x;
    break;
  }
  r = ((r + m) * 255 + 500) / 1000;
  g = ((g + m) * 255 + 500) / 1000;
  b = ((b + m) * 255 + 500) / 1000;
  return (r << 16) | (g << 8) | b;
}
void pos_color_rgb_hsv(uint32_t rgb, uint16_t *hue, uint16_t *saturation,
                       uint16_t *value) {
  int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  int maximum = r > g ? r : g;
  if (b > maximum)
    maximum = b;
  int minimum = r < g ? r : g;
  if (b < minimum)
    minimum = b;
  int delta = maximum - minimum, h = 0;
  if (delta) {
    if (maximum == r)
      h = 60 * (g - b) / delta;
    else if (maximum == g)
      h = 120 + 60 * (b - r) / delta;
    else
      h = 240 + 60 * (r - g) / delta;
  }
  *hue = h < 0 ? h + 360 : h;
  *saturation = maximum ? delta * 1000 / maximum : 0;
  *value = maximum * 1000 / 255;
}
