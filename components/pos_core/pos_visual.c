#include "font8x8_basic.h"
#include "pos_core.h"
#include <stdlib.h>
#include <string.h>

int pos_motion_spring(unsigned frame) {
  static const int8_t positions[] = {100, 38, -18, -8, 6, 0};
  return frame < sizeof(positions) ? positions[frame] : 0;
}
void pos_ribbon_nudge(pos_ribbon_t *motion, int direction) {
  motion->position += direction > 0 ? 80 * 1024 : -80 * 1024;
  /* Bound rapid key repeat to the two cached offscreen neighbors. */
  if (motion->position > 160 * 1024)
    motion->position = 160 * 1024;
  if (motion->position < -160 * 1024)
    motion->position = -160 * 1024;
}
bool pos_ribbon_tick(pos_ribbon_t *motion, unsigned elapsed_ms) {
  /* Integrate in small steps so LCD/SD stalls cannot destabilize the spring. */
  if (elapsed_ms > 250)
    elapsed_ms = 250;
  while (elapsed_ms) {
    unsigned step = elapsed_ms > 8 ? 8 : elapsed_ms;
    int64_t acceleration = -180LL * motion->position - 18LL * motion->velocity;
    motion->velocity += (int32_t)(acceleration * step / 1000);
    motion->position += (int32_t)((int64_t)motion->velocity * step / 1000);
    elapsed_ms -= step;
  }
  if (abs(motion->position) < 256 && abs(motion->velocity) < 4096)
    motion->position = motion->velocity = 0;
  return motion->position || motion->velocity;
}
int pos_ribbon_offset(const pos_ribbon_t *motion) {
  return motion->position / 1024;
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
