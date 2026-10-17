#include "font8x8_basic.h"
#include "pos_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pansexual flag: #FF218C, #FFD800, #21B1FF, quantized to LCD RGB565. */
const uint16_t pos_boot_colors[POS_BOOT_COLOR_COUNT] = {0xf911, 0xfec0, 0x259f};

bool pos_boot_frame(unsigned step, pos_view_t *view, uint32_t *hold_ms) {
  if (step > POS_BOOT_HOLD_FRAME || !view || !hold_ms)
    return false;
  pos_view_clear(view, "POS");
  view->boot_splash = true;
  unsigned widths[POS_BOOT_COLOR_COUNT];
  for (unsigned stripe = 0; stripe < POS_BOOT_COLOR_COUNT; ++stripe) {
    unsigned start = stripe * POS_BOOT_STAGGER_FRAMES;
    unsigned frame = step > start ? step - start : 0;
    widths[stripe] = frame < POS_BOOT_SLIDE_FRAMES
                         ? 240 * frame / POS_BOOT_SLIDE_FRAMES
                         : 240;
  }
  view->boot_pink_width = widths[0];
  view->boot_yellow_width = widths[1];
  view->boot_blue_width = widths[2];
  unsigned slides = POS_BOOT_SLIDE_FRAMES + 2 * POS_BOOT_STAGGER_FRAMES;
  unsigned fade = step > slides ? step - slides : 0;
  view->boot_text_alpha =
      fade < POS_BOOT_FADE_FRAMES ? 255 * fade / POS_BOOT_FADE_FRAMES : 255;
  *hold_ms =
      step == POS_BOOT_HOLD_FRAME ? POS_BOOT_SPLASH_MS : POS_BOOT_FRAME_MS;
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
  pos_view_line(view, 1, "Arrows:color Q/E:hue");
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
         ((v->icons[tile + 2][y] >> x) & 1);
}

static uint16_t content_pixel(const pos_view_t *v, int x, int y) {
  if (!v || x < 0 || x >= 240 || y < 0 || y >= 135)
    return 0;
  if (v->boot_splash) {
    uint16_t background = 0;
    if (y < 45 && x < v->boot_pink_width)
      background = pos_boot_colors[0];
    else if (y >= 45 && y < 90 && x < v->boot_yellow_width)
      background = pos_boot_colors[1];
    else if (y >= 90 && x < v->boot_blue_width)
      background = pos_boot_colors[2];
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
  if (v->battery_overlay && x >= 184 && y < 9) {
    if (y == 8)
      return 0;
    if (x >= 223 && x <= 237 && y >= 1 && y <= 6) {
      if (x == 223 || x == 237 || y == 1 || y == 6)
        return 0xffff;
      return v->battery_available && x < 225 + v->battery_bars * 3 &&
                     x >= 225 && y >= 3 && y <= 4
                 ? (v->battery_full ? 0x07e0 : 0xffff)
                 : 0;
    }
    if (x == 238 && y >= 3 && y <= 4)
      return 0xffff;
    char label[5];
    if (v->battery_available)
      snprintf(label, sizeof(label), "%3u%%", v->battery_percent);
    else
      memcpy(label, " --%", 5);
    if (x < 216 && ((font8x8_basic[(unsigned char)label[(x - 184) / 8]][y] >>
                     ((x - 184) % 8)) &
                    1))
      return 0xffff;
    return 0;
  }
  if (v->solid_color)
    return v->color_rgb565;
  uint16_t picker;
  if (v->color_picker && picker_pixel(v, x, y, &picker))
    return picker;
  if (v->map_pixels && y >= 16 && y < 104) {
    int my = y - 16;
    if (v->map_fix && abs(x - v->map_gps_x) + abs(my - v->map_gps_y) <= 4)
      return 0x259f;
    if (v->map_waypoint &&
        abs(x - v->map_target_x) + abs(my - v->map_target_y) == 5)
      return 0xf911;
    if ((x == 120 && abs(my - 44) <= 3) || (my == 44 && abs(x - 120) <= 3))
      return 0xfec0;
    unsigned p = (unsigned)(my * 240 + x);
    return (v->map_pixels[p / 8] >> (p % 8)) & 1 ? 0xce79 : 0x0842;
  }
  uint16_t visual;
  if (pos_visual_pixel(v, x, y, &visual))
    return visual;
  if (!v->tiles && y >= 132 && pos_background_valid(&v->background))
    return pos_color_rgb565(
        v->background.colors[x * v->background.count / 240]);
  int row = y / 8, column = x / 8;
  uint16_t bg = v->color_picker || v->black_background ? 0 : 0x0842,
           fg = 0xffff;
  if (v->tiles) {
    unsigned count = v->background.count;
    bg = count && count <= POS_BACKGROUND_MAX_STRIPES
             ? pos_color_rgb565(v->background.colors[y * count / 135])
             : pos_boot_colors[y / 45];
  }
  if (row == 0) {
    bg = v->tiles || v->color_picker || v->black_background ? 0 : 0x034c;
    fg = 0xffff;
  }
  if (row == v->selected_row) {
    bg = v->black_background ? 0xffff : 0x07ff;
    fg = 0;
  }
  if (v->tiles) {
    /* The focus belongs to the screen; only the ribbon moves underneath. */
    int focus_x = x - v->selected_tile * 80;
    if (focus_x >= 14 && focus_x <= 65 && y >= 38 && y <= 89) {
      if (focus_x < 16 || focus_x > 63 || y < 40 || y > 87)
        return 0;
      if (focus_x == 16 || focus_x == 63 || y == 40 || y == 87)
        return 0xffff;
    }
    int rx = x - v->ribbon_offset + 160;
    int tile = rx >= 0 ? rx / 80 - 2 : -3;
    int local = rx >= 0 ? rx % 80 : 0;
    if (tile >= -2 && tile < 5) {
      int lift = tile == v->selected_tile ? v->tile_lift : 0;
      unsigned scale =
          tile == v->selected_tile && v->tile_scale ? v->tile_scale : 100;
      int lx = (local - 40) * 100 / (int)scale + 40;
      int ly = (y + lift - 64) * 100 / (int)scale + 64;
      int ix = lx - 24, iy = ly - 48;
      if (icon_ink(v, tile, ix, iy))
        return 0;
      if (icon_ink(v, tile, ix - 1, iy) || icon_ink(v, tile, ix + 1, iy) ||
          icon_ink(v, tile, ix, iy - 1) || icon_ink(v, tile, ix, iy + 1))
        return 0xffff;
    }
    if (row >= 14)
      bg = 0;
  }
  if (v->text_editor && row >= 2 && row < 14 &&
      (v->editor_selected[row - 2] & (1u << column))) {
    bg = 0x034c;
    fg = 0xffff;
  }
  if (v->text_editor && row == v->editor_cursor_row &&
      column == v->editor_cursor_col) {
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

uint16_t pos_view_pixel(const pos_view_t *view, int x, int y) {
  if (!view || view->fade == 255)
    return 0;
  uint16_t color = content_pixel(view, x, y);
  if (!view || !view->fade)
    return color;
  unsigned remain = 255 - view->fade;
  return ((((color >> 11) & 31) * remain / 255) << 11) |
         ((((color >> 5) & 63) * remain / 255) << 5) |
         ((color & 31) * remain / 255);
}

/* Fast raster path for the moving ribbon. No per-pixel string scans, theme
 * validation, coordinate division, or generic visual dispatch in this band. */
void pos_view_scanline(const pos_view_t *v, int y, uint16_t pixels[240]) {
  if (!v->tiles || v->fade || v->tile_lift ||
      (v->tile_scale && v->tile_scale != 100) || y < 32 || y >= 96) {
    for (int x = 0; x < 240; ++x)
      pixels[x] = pos_view_pixel(v, x, y);
    return;
  }
  unsigned count = v->background.count;
  uint16_t bg = count && count <= POS_BACKGROUND_MAX_STRIPES
                    ? pos_color_rgb565(v->background.colors[y * count / 135])
                    : pos_boot_colors[y / 45];
  for (int x = 0; x < 240; ++x)
    pixels[x] = bg;
  int iy = y - 48;
  if (iy >= -1 && iy <= 32)
    for (int tile = -2; tile < 5; ++tile) {
      int start = tile * 80 + 24 + v->ribbon_offset;
      if (start > 239 || start + 32 < 0)
        continue;
      uint32_t row = iy >= 0 && iy < 32 ? v->icons[tile + 2][iy] : 0;
      uint32_t above = iy > 0 && iy <= 32 ? v->icons[tile + 2][iy - 1] : 0;
      uint32_t below = iy >= -1 && iy < 31 ? v->icons[tile + 2][iy + 1] : 0;
      for (int ix = -1; ix <= 32; ++ix) {
        int x = start + ix;
        if (x < 0 || x >= 240)
          continue;
        if (ix >= 0 && ix < 32 && ((row >> ix) & 1))
          pixels[x] = 0;
        else if ((ix > 0 && ((row >> (ix - 1)) & 1)) ||
                 (ix < 31 && ((row >> (ix + 1)) & 1)) ||
                 (ix >= 0 && ix < 32 && (((above | below) >> ix) & 1)))
          pixels[x] = 0xffff;
      }
    }
  int left = v->selected_tile * 80;
  if (y >= 38 && y <= 89)
    for (int x = left + 14; x <= left + 65 && x < 240; ++x) {
      if (x < 0)
        continue;
      int local = x - left;
      if (local < 16 || local > 63 || y < 40 || y > 87)
        pixels[x] = 0;
      else if (local == 16 || local == 63 || y == 40 || y == 87)
        pixels[x] = 0xffff;
    }
}
