#include "font8x8_basic.h"
#include "pos_core.h"
#include <stdlib.h>
#include <string.h>

int pos_motion_spring(unsigned frame) {
  static const int8_t positions[] = {100, 38, -18, -8, 6, 0};
  return frame < sizeof(positions) ? positions[frame] : 0;
}
static bool box(int x, int y, int l, int t, int w, int h) {
  return x >= l && x < l + w && y >= t && y < t + h;
}
static bool rim(int x, int y, int l, int t, int w, int h) {
  return box(x, y, l, t, w, h) && !box(x, y, l + 2, t + 2, w - 4, h - 4);
}
static bool disk(int x, int y, int cx, int cy, int radius) {
  int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= radius * radius;
}
static bool capsule(int x, int y, int l, int t, int w, int h, int r) {
  if (!box(x, y, l, t, w, h))
    return false;
  int cx = x < l + r ? l + r : x >= l + w - r ? l + w - r - 1 : x;
  int cy = y < t + r ? t + r : y >= t + h - r ? t + h - r - 1 : y;
  return disk(x, y, cx, cy, r);
}
static uint16_t accent(const pos_view_t *v, int y) {
  unsigned count = v->background.count;
  return count && count <= POS_BACKGROUND_MAX_STRIPES
             ? pos_color_rgb565(v->background.colors[(unsigned)y * count / 135])
             : pos_boot_colors[(unsigned)y / 45];
}
static bool symbol(unsigned kind, int x, int y) {
  if (kind == POS_ICON_DOCUMENT || kind == POS_ICON_FILE) {
    return rim(x, y, 23, 9, 20, 34) ||
           (kind == POS_ICON_DOCUMENT && x >= 27 && x < 39 &&
            (y == 19 || y == 25 || y == 31));
  }
  if (kind == POS_ICON_AUDIO) {
    return box(x, y, 34, 10, 3, 26) || box(x, y, 34, 10, 12, 4) ||
           disk(x, y, 29, 37, 6);
  }
  if (kind == POS_ICON_IMAGE) {
    return rim(x, y, 18, 12, 30, 27) || disk(x, y, 39, 20, 3) ||
           (x >= 22 && x <= 43 && y >= 25 && y <= 35 && abs(x - 28) < y - 23);
  }
  if (kind == POS_ICON_MIXED)
    return rim(x, y, 21, 10, 18, 28) || rim(x, y, 29, 18, 18, 28);
  if (kind == POS_ICON_PARENT)
    return (abs(x - 32) <= 2 && y > 16 && y < 48) ||
           (y >= 13 && y < 29 && abs(abs(x - 32) - (28 - y)) < 2);
  if (kind == POS_ICON_PREV || kind == POS_ICON_NEXT) {
    if (kind == POS_ICON_NEXT)
      x = 64 - x;
    return y >= 17 && y <= 45 && abs(x - (20 + abs(y - 31))) < 3;
  }
  if (kind == POS_ICON_ACTIONS)
    return disk(x, y, 18, 31, 4) || disk(x, y, 32, 31, 4) ||
           disk(x, y, 46, 31, 4);
  return false;
}
static bool folder(unsigned kind, int x, int y) {
  bool content = symbol(kind, x, y);
  bool back = rim(x, y, 12, 23, 43, 28) || rim(x, y, 15, 17, 20, 9);
  /* Sloping front panel leaves the content visible above the open folder. */
  int left = 8 + (y - 33) / 5, right = 59 - (y - 33) / 5;
  bool front =
      y >= 33 && y <= 54 &&
      (y <= 35 || y >= 52 || abs(x - left) <= 1 || abs(x - right) <= 1) &&
      x >= left && x <= right;
  return content || back || front;
}
bool pos_visual_pixel(const pos_view_t *v, int x, int y, uint16_t *color) {
  *color = 0;
  if (v->visual == POS_VISUAL_STRIPES && y >= 24 && y < 112) {
    unsigned count = v->background.count;
    if (count && count <= POS_BACKGROUND_MAX_STRIPES && x >= 8 && x < 232 &&
        y >= 26 && y < 110) {
      unsigned index = (y - 26) * count / 84;
      *color = pos_color_rgb565(v->background.colors[index]);
      if (index == v->visual_selection && v->visual_flash)
        *color = 0xffff;
      if (index == v->visual_selection && (x < 11 || x >= 229))
        *color = 0xffff;
    }
    return true;
  }
  if (v->visual == POS_VISUAL_MIC && y >= 32 && y < 104) {
    if (y >= 96 && x >= 96)
      return false;
    if (capsule(x, y, 54, 37, 28, 43, 13))
      *color = capsule(x, y, 57, 40, 22, 37, 10)
                   ? accent(v, (y - 40) * 135 / 37)
                   : 0xffff;
    if ((disk(x, y, 68, 72, 22) && !disk(x, y, 68, 72, 19) && y >= 64) ||
        box(x, y, 46, 57, 3, 16) || box(x, y, 88, 57, 3, 16) ||
        box(x, y, 67, 94, 3, 9) || box(x, y, 54, 102, 31, 3))
      *color = 0xffff;
    if (rim(x, y, 121, 35, 94, 60))
      *color = 0xffff;
    unsigned level = v->visual_level > 1000 ? 1000 : v->visual_level;
    if (box(x, y, 125, 39, 86, 52) && y >= 91 - (int)(52 * level / 1000) &&
        (y - 39) % 7 < 5)
      *color = accent(v, (y - 39) * 135 / 52);
    if (level >= 985 && box(x, y, 125, 36, 86, 3))
      *color = 0xf800;
    return true;
  }
  if (v->visual == POS_VISUAL_COUNTER && y >= 16 && y < 112) {
    int press = v->visual_motion;
    if (press < 0)
      press = 0;
    if (press > 12)
      press = 12;
    const char *number = v->lines[4];
    size_t n = strnlen(number, POS_COLS);
    int tx = x - (240 - (int)n * 16) / 2, ty = y - (30 - press);
    if (tx >= 0 && tx < (int)n * 16 && ty >= 0 && ty < 16 &&
        ((font8x8_basic[(unsigned char)number[tx / 16]][ty / 2] >>
          (tx / 2 % 8)) &
         1))
      *color = 0xffff;
    int dx = x - 120, dy = y - (87 + press / 2), r = 18 - press / 2;
    if (abs(dx) <= 42 && abs(dy - 7) <= r &&
        dx * dx * r * r + (dy - 7) * (dy - 7) * 42 * 42 <= 42 * 42 * r * r)
      *color = 0x5000;
    if (abs(dx) <= 42 && abs(dy) <= r &&
        dx * dx * r * r + dy * dy * 42 * 42 <= 42 * 42 * r * r) {
      *color = dy < -r + 3 ? 0xfbcf : dy < 0 ? 0xf800 : 0xb800;
      if ((abs(dx) <= 2 && abs(dy) <= 7) || (abs(dx) <= 8 && abs(dy) <= 2))
        *color = 0xffff;
    }
    return true;
  }
  if (v->visual == POS_VISUAL_FILES && y >= 26 && y < 96) {
    int tile = x / 80, local = x % 80;
    int lift = tile == 1 ? v->visual_motion : 0;
    int iy = y - 28 + lift;
    unsigned kind = v->visual_icons[tile];
    if (kind == 255)
      return true;
    if (tile == 1 && rim(local, y, 3, 26, 74, 69))
      *color = 0xffff;
    bool ink = (kind & 128) || kind == POS_ICON_FOLDER
                   ? folder(kind & 127, local - 8, iy)
                   : symbol(kind, local - 8, iy);
    if (ink)
      *color = 0xffff;
    if (tile == 1 && box(local, y, 7, 91, 66, 2))
      *color = accent(v, y);
    return true;
  }
  if (v->visual == POS_VISUAL_MUSIC && y >= 40 && y < 104) {
    if (y >= 88 && x >= 88)
      return false;
    if (disk(x, y, 48, 72, 29))
      *color = disk(x, y, 48, 72, 26) ? 0 : 0xffff;
    if (disk(x, y, 48, 72, 13))
      *color = accent(v, y);
    if (disk(x, y, 48, 72, 4))
      *color = 0;
    static const int8_t dx[] = {20, 14, 0, -14, -20, -14, 0, 14};
    static const int8_t dy[] = {0, 14, 20, 14, 0, -14, -20, -14};
    if (disk(x, y, 48 + dx[v->visual_motion & 7], 72 + dy[v->visual_motion & 7],
             3))
      *color = 0xffff;
    if (rim(x, y, 91, 66, 136, 16))
      *color = 0xffff;
    if (box(x, y, 94, 69, 130, 10) &&
        x < 94 + (int)(130 * v->visual_level / 1000))
      *color = accent(v, y);
    return true;
  }
  return false;
}
