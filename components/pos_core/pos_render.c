#include "font8x8_basic.h"
#include "pos_core.h"
#include <string.h>

uint16_t pos_view_pixel(const pos_view_t *v, int x, int y) {
  if (!v || x < 0 || x >= 240 || y < 0 || y >= 135)
    return 0;
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
