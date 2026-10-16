#include "font8x8_basic.h"
#include "pos_core.h"
#include <stdlib.h>
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

void pos_color_picker_view(const char *title, uint32_t rgb, uint16_t hue,
                           uint16_t saturation, uint16_t value,
                           pos_view_t *view) {
  pos_view_clear(view, title);
  view->color_picker = true;
  view->picker_hue = hue;
  view->picker_saturation = saturation;
  view->picker_value = value;
  view->picker_rgb = rgb;
  pos_view_line(view, 1, "WASD: color   Q/E: hue");
  pos_view_line(view, 2, "Shift: fine adjustment");
  pos_view_line(view, 9, "%23s#%06X", "", (unsigned)rgb);
  pos_view_line(view, 10, "%23sH %u", "", (unsigned)hue);
  pos_view_line(view, 11, "%23sS %u%%", "", (unsigned)saturation / 10);
  pos_view_line(view, 12, "%23sV %u%%", "", (unsigned)value / 10);
  pos_view_line(view, 14, "Enter: use   Esc: cancel");
  pos_view_line(view, 15, "Fn+Space: close app");
}

static bool picker_pixel(const pos_view_t *v, int x, int y, uint16_t *color) {
  if (x >= 8 && x < 168 && y >= 28 && y < 100) {
    unsigned saturation = (x - 8) * 1000 / 159;
    unsigned value = (99 - y) * 1000 / 71;
    *color =
        pos_color_rgb565(pos_color_hsv_rgb(v->picker_hue, saturation, value));
    int cx = 8 + v->picker_saturation * 159 / 1000;
    int cy = 99 - v->picker_value * 71 / 1000;
    int dx = abs(x - cx), dy = abs(y - cy), distance = dx > dy ? dx : dy;
    if (distance == 4)
      *color = 0;
    if (distance == 3)
      *color = 0xffff;
    return true;
  }
  if (x >= 7 && x <= 168 && y >= 27 && y <= 100 &&
      (x == 7 || x == 168 || y == 27 || y == 100)) {
    *color = 0xffff;
    return true;
  }
  if (x >= 8 && x < 168 && y >= 104 && y < 112) {
    *color =
        pos_color_rgb565(pos_color_hsv_rgb((x - 8) * 360 / 160, 1000, 1000));
    int marker = 8 + v->picker_hue * 159 / 359;
    if (abs(x - marker) == 1)
      *color = 0;
    if (x == marker)
      *color = 0xffff;
    return true;
  }
  if (x >= 183 && x <= 232 && y >= 27 && y <= 68) {
    *color = x == 183 || x == 232 || y == 27 || y == 68
                 ? 0xffff
                 : pos_color_rgb565(v->picker_rgb);
    return true;
  }
  return false;
}
static bool icon_ink(const pos_view_t *v, int tile, int x, int y) {
  return x >= 0 && x < 32 && y >= 0 && y < 32 &&
         ((v->icons[tile][y / 4] >> (x / 4)) & 1);
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
  uint16_t picker;
  if (v->color_picker && picker_pixel(v, x, y, &picker))
    return picker;
  int row = y / 8, column = x / 8;
  uint16_t bg = v->color_picker ? 0 : 0x0842, fg = 0xffff;
  if (v->tiles) {
    unsigned count = v->background.count;
    bg = count && count <= POS_BACKGROUND_MAX_STRIPES
             ? pos_color_rgb565(v->background.colors[y * count / 135])
             : pos_boot_colors[y / 45];
  }
  if (row == 0) {
    bg = v->tiles || v->color_picker ? 0 : 0x034c;
    fg = 0xffff;
  }
  if (row == v->selected_row) {
    bg = 0x07ff;
    fg = 0;
  }
  if (v->tiles) {
    int tile = x / 80, local = x % 80;
    if (tile == v->selected_tile && local >= 14 && local <= 65 && y >= 38 &&
        y <= 89) {
      if (local < 16 || local > 63 || y < 40 || y > 87)
        return 0;
      if (local == 16 || local == 63 || y == 40 || y == 87)
        return 0xffff;
    }
    int ix = local - 24, iy = y - 48;
    if (icon_ink(v, tile, ix, iy))
      return 0;
    if (icon_ink(v, tile, ix - 1, iy) || icon_ink(v, tile, ix + 1, iy) ||
        icon_ink(v, tile, ix, iy - 1) || icon_ink(v, tile, ix, iy + 1))
      return 0xffff;
    if (row >= 14)
      bg = 0;
  }
  if (v->text_editor && row == v->editor_cursor_row && column == v->editor_cursor_col) {
    bg = 0xffff;
    fg = 0;
  }
  if (row < POS_ROWS && column < POS_COLS) {
    size_t n = strnlen(v->lines[row], POS_COLS);
    unsigned char c =
        column < (int)n ? (unsigned char)v->lines[row][column] : ' ';
    if (v->tiles && row > 0 && row < 14) {
      size_t first = 0, last = n;
      while (first < last && v->lines[row][first] == ' ')
        ++first;
      while (last > first && v->lines[row][last - 1] == ' ')
        --last;
      if (column >= (int)first && column < (int)last)
        bg = 0;
    }
    if (c >= 128)
      c = '?';
    if ((font8x8_basic[c][y % 8] >> (x % 8)) & 1)
      return fg;
  }
  return bg;
}
