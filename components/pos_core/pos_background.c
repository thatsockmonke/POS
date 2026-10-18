#include "pos_background.h"
#include <stdlib.h>
#include <string.h>

void pos_background_defaults(pos_background_t *background) {
  (void)pos_background_preset(background, POS_BACKGROUND_PANSEXUAL);
}
const char *const pos_background_preset_names[POS_BACKGROUND_PRESET_COUNT] = {
    "Pansexual", "Nonbinary",   "Rainbow",     "Lesbian",
    "Gay men",   "Bisexual",    "Transgender", "Asexual",
    "Aromantic", "Genderqueer", "Genderfluid", "Agender"};
bool pos_background_preset(pos_background_t *background,
                           pos_background_preset_t preset) {
  static const pos_background_t flags[] = {
      {3, {0xff218c, 0xffd800, 0x21b1ff}},
      {4, {0xfff430, 0xffffff, 0x9c59d1, 0x000000}},
      {6, {0xe40303, 0xff8c00, 0xffed00, 0x008026, 0x24408e, 0x732982}},
      {5, {0xd52d00, 0xff9a56, 0xffffff, 0xd362a4, 0xa30262}},
      {7,
       {0x078d70, 0x26ceaa, 0x98e8c1, 0xffffff, 0x7bade2, 0x5049cc, 0x3d1a78}},
      {3, {0xd60270, 0x9b4f96, 0x0038a8}},
      {5, {0x5bcefa, 0xf5a9b8, 0xffffff, 0xf5a9b8, 0x5bcefa}},
      {4, {0x000000, 0xa3a3a3, 0xffffff, 0x800080}},
      {5, {0x3da542, 0xa7d379, 0xffffff, 0xa9a9a9, 0x000000}},
      {3, {0xb57edc, 0xffffff, 0x4a8123}},
      {5, {0xff75a2, 0xffffff, 0xbe18d6, 0x000000, 0x333ebd}},
      {7,
       {0x000000, 0xb9b9b9, 0xffffff, 0xb8f483, 0xffffff, 0xb9b9b9, 0x000000}}};
  if (!background || (unsigned)preset >= POS_BACKGROUND_PRESET_COUNT)
    return false;
  *background = flags[preset];
  return true;
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
