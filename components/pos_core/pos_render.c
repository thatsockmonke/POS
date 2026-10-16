#include "font8x8_basic.h"
#include "pos_core.h"
#include <string.h>

/* Pansexual flag: #FF218C, #FFD800, #21B1FF, quantized to LCD RGB565. */
const uint16_t pos_boot_colors[POS_BOOT_COLOR_COUNT] = {0xf911, 0xfec0, 0x259f};

bool pos_boot_frame(unsigned step, pos_view_t *view, uint32_t *hold_ms) {
  if (step > POS_BOOT_COLOR_COUNT || !view || !hold_ms)
    return false;
  pos_view_clear(view, "POS");
  if (step < POS_BOOT_COLOR_COUNT) {
    view->solid_color = true;
    view->color_rgb565 = pos_boot_colors[step];
    *hold_ms = POS_BOOT_COLOR_MS;
  } else {
    view->boot_splash = true;
    *hold_ms = POS_BOOT_SPLASH_MS;
  }
  return true;
}

uint16_t pos_view_pixel(const pos_view_t *v, int x, int y) {
  if (!v || x < 0 || x >= 240 || y < 0 || y >= 135)
    return 0;
  if (v->boot_splash) {
    /* Three equal 45-pixel horizontal stripes; 4x font centered on yellow. */
    int tx = x - (240 - 3 * 8 * 4) / 2, ty = y - (135 - 8 * 4) / 2;
    if (tx >= 0 && tx < 3 * 8 * 4 && ty >= 0 && ty < 8 * 4) {
      static const char title[] = "POS";
      if ((font8x8_basic[(unsigned char)title[tx / 32]][ty / 4] >>
           ((tx / 4) % 8)) &
          1)
        return 0; /* Black letters stay legible against the yellow stripe. */
    }
    return pos_boot_colors[y / 45];
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
