#include "font8x8_basic.h"
#include "pos_core.h"
#include <string.h>

/* Pansexual flag: #FF218C, #FFD800, #21B1FF, quantized to LCD RGB565. */
const uint16_t pos_boot_colors[POS_BOOT_COLOR_COUNT] = {0xf911, 0xfec0, 0x259f};

bool pos_boot_frame(unsigned step, pos_view_t *view, uint32_t *hold_ms) {
  if (step > POS_BOOT_HOLD_FRAME || !view || !hold_ms)
    return false;
  pos_view_clear(view, "POS");
  if (step < POS_BOOT_COLOR_COUNT) {
    view->solid_color = true;
    view->color_rgb565 = pos_boot_colors[step];
    *hold_ms = POS_BOOT_COLOR_MS;
  } else {
    view->boot_splash = true;
    unsigned frame = step - POS_BOOT_COLOR_COUNT + 1;
    unsigned slides = POS_BOOT_SLIDE_FRAMES + POS_BOOT_STAGGER_FRAMES;
    view->boot_yellow_width = frame < POS_BOOT_SLIDE_FRAMES
                                  ? 240 * frame / POS_BOOT_SLIDE_FRAMES
                                  : 240;
    unsigned pink =
        frame > POS_BOOT_STAGGER_FRAMES ? frame - POS_BOOT_STAGGER_FRAMES : 0;
    view->boot_pink_width =
        pink < POS_BOOT_SLIDE_FRAMES ? 240 * pink / POS_BOOT_SLIDE_FRAMES : 240;
    unsigned fade = frame > slides ? frame - slides : 0;
    view->boot_text_alpha =
        fade < POS_BOOT_FADE_FRAMES ? 255 * fade / POS_BOOT_FADE_FRAMES : 255;
    *hold_ms =
        step == POS_BOOT_HOLD_FRAME ? POS_BOOT_SPLASH_MS : POS_BOOT_FRAME_MS;
  }
  return true;
}

uint16_t pos_view_pixel(const pos_view_t *v, int x, int y) {
  if (!v || x < 0 || x >= 240 || y < 0 || y >= 135)
    return 0;
  if (v->boot_splash) {
    uint16_t background = pos_boot_colors[2];
    if (y < 45 && x < v->boot_pink_width)
      background = pos_boot_colors[0];
    else if (y >= 45 && y < 90 && x < v->boot_yellow_width)
      background = pos_boot_colors[1];
    /* Center the visible 198x91 ink, including the font's trailing blank
     * columns/row. Letters reach halfway into both outer stripes. */
    int tx = x - 21, ty = y - 22;
    if (v->boot_text_alpha && tx >= 0 && tx < 3 * 8 * 9 && ty >= 0 &&
        ty < 7 * 13) {
      static const char title[] = "POS";
      if ((font8x8_basic[(unsigned char)title[tx / 72]][ty / 13] >>
           ((tx / 9) % 8)) &
          1) {
        unsigned remaining = 255 - v->boot_text_alpha;
        unsigned red = (((background >> 11) & 31) * remaining + 127) / 255;
        unsigned green = (((background >> 5) & 63) * remaining + 127) / 255;
        unsigned blue = ((background & 31) * remaining + 127) / 255;
        return (red << 11) | (green << 5) | blue;
      }
    }
    return background;
  }
  if (v->solid_color)
    return v->color_rgb565;
  int row = y / 8, column = x / 8;
  uint16_t bg = 0x0842, fg = 0xffff;
  if (row == 0) {
    bg = 0x034c;
    fg = 0xffff;
  }
  if (row == v->selected_row) {
    bg = 0x07ff;
    fg = 0;
  }
  if (v->tiles && y >= 40 && y < 104) {
    int tile = x / 80, local = x % 80;
    if ((local == 3 || local == 76 || y == 40 || y == 103) && local >= 3 &&
        local <= 76)
      return tile == v->selected_tile ? 0x07ff : 0x4228;
    if (local >= 24 && local < 56 && y >= 48 && y < 80 &&
        ((v->icons[tile][(y - 48) / 4] >> ((local - 24) / 4)) & 1))
      return tile == v->selected_tile ? 0x07ff : 0xffff;
  }
  if (row < POS_ROWS && column < POS_COLS) {
    size_t n = strnlen(v->lines[row], POS_COLS);
    unsigned char c =
        column < (int)n ? (unsigned char)v->lines[row][column] : ' ';
    if (c >= 128)
      c = '?';
    if ((font8x8_basic[c][y % 8] >> (x % 8)) & 1)
      return fg;
  }
  return bg;
}
