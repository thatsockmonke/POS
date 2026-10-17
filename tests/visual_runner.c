#include "pos_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t hash(const pos_view_t *v, int l, int t, int r, int b) {
  uint32_t h = 2166136261u;
  for (int y = t; y < b; ++y)
    for (int x = l; x < r; ++x)
      h = (h ^ pos_view_pixel(v, x, y)) * 16777619u;
  return h;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  pos_view_t v;
  pos_view_clear(&v, "Test");
  v.black_background = true;
  pos_background_defaults(&v.background);
  if (!strcmp(argv[1], "mic")) {
    v.visual = POS_VISUAL_MIC;
    unsigned previous = 0;
    const unsigned levels[] = {0, 300, 800, 1000};
    for (unsigned at = 0; at < 4; ++at) {
      v.visual_level = levels[at];
      unsigned ink = 0;
      for (int y = 39; y < 91; ++y)
        for (int x = 125; x < 211; ++x) {
          uint16_t c = pos_view_pixel(&v, x, y);
          if (c) {
            bool palette = false;
            for (unsigned i = 0; i < v.background.count; ++i)
              palette |= c == pos_color_rgb565(v.background.colors[i]);
            assert(palette);
            ++ink;
          }
        }
      assert(at ? ink > previous : ink == 0);
      previous = ink;
    }
    pos_view_line(&v, 13, "L/R: gain    Space: pause");
    pos_view_t plain = v;
    plain.visual = POS_VISUAL_NONE;
    assert(hash(&v, 0, 104, 240, 112) == hash(&plain, 0, 104, 240, 112));
  } else if (!strcmp(argv[1], "stripes")) {
    v.visual = POS_VISUAL_STRIPES;
    for (unsigned count = 1; count <= 12; ++count) {
      assert(pos_background_resize(&v.background, count));
      for (unsigned i = 0; i < count; ++i)
        v.background.colors[i] = (i + 1) * 0x10101;
      for (unsigned i = 0; i < count; ++i) {
        int y = 26 + (int)((2 * i + 1) * 84 / (2 * count));
        v.visual_selection = i;
        v.visual_flash = false;
        assert(pos_view_pixel(&v, 120, y) ==
               pos_color_rgb565(v.background.colors[i]));
        v.visual_flash = true;
        assert(pos_view_pixel(&v, 120, y) == 0xffff);
      }
    }
  } else if (!strcmp(argv[1], "counter")) {
    v.visual = POS_VISUAL_COUNTER;
    pos_view_line(&v, 4, "42");
    unsigned red[2] = {0};
    int top[2] = {135, 135};
    for (unsigned phase = 0; phase < 2; ++phase) {
      v.visual_motion = phase ? 12 : 0;
      for (int y = 16; y < 112; ++y)
        for (int x = 0; x < 240; ++x) {
          uint16_t c = pos_view_pixel(&v, x, y);
          if (c == 0xf800 || c == 0xb800 || c == 0xfbcf)
            ++red[phase];
          if (c == 0xffff && y < 55 && y < top[phase])
            top[phase] = y;
        }
    }
    assert(red[1] < red[0] && top[1] < top[0]);
  } else if (!strcmp(argv[1], "music")) {
    v.visual = POS_VISUAL_MUSIC;
    pos_view_line(&v, 11, "%11sVol 80%%", " ");
    pos_view_t plain = v;
    plain.visual = POS_VISUAL_NONE;
    assert(hash(&v, 88, 88, 240, 96) == hash(&plain, 88, 88, 240, 96));
    uint32_t empty = hash(&v, 94, 69, 224, 79);
    v.visual_level = 800;
    assert(hash(&v, 94, 69, 224, 79) != empty);
  } else if (!strcmp(argv[1], "fade")) {
    v.visual = POS_VISUAL_MIC;
    v.visual_level = 1000;
    v.battery_overlay = v.battery_available = true;
    v.battery_percent = 75;
    v.battery_bars = 3;
    pos_view_t mid = v;
    mid.fade = 128;
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x) {
        uint16_t a = pos_view_pixel(&v, x, y), b = pos_view_pixel(&mid, x, y);
        assert((b >> 11) <= (a >> 11) && ((b >> 5) & 63) <= ((a >> 5) & 63) &&
               (b & 31) <= (a & 31));
      }
    v.fade = 255;
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x)
        assert(!pos_view_pixel(&v, x, y));
  } else if (!strcmp(argv[1], "ribbon")) {
    pos_ribbon_t m = {0};
    pos_ribbon_nudge(&m, 1);
    assert(pos_ribbon_offset(&m) == 80);
    pos_ribbon_tick(&m, 64);
    assert(pos_ribbon_offset(&m) > 0 && pos_ribbon_offset(&m) < 80);
    int32_t velocity = m.velocity;
    int before = pos_ribbon_offset(&m);
    pos_ribbon_nudge(&m, 1);
    assert(m.velocity == velocity && pos_ribbon_offset(&m) == before + 80);
    bool overshoot = false;
    for (unsigned i = 0; i < 120; ++i) {
      pos_ribbon_tick(&m, 8);
      overshoot |= m.position < 0;
    }
    assert(overshoot && !m.position && !m.velocity);
    pos_ribbon_nudge(&m, -1);
    pos_ribbon_tick(&m, 64);
    velocity = m.velocity;
    pos_ribbon_nudge(&m, 1);
    assert(m.velocity ==
           velocity); /* Reversing input does not erase momentum. */
    for (unsigned i = 0; i < 20; ++i)
      pos_ribbon_tick(&m, 250);
    assert(!m.position && !m.velocity);
    for (unsigned i = 0; i < 1000; ++i)
      pos_ribbon_nudge(&m, 1);
    assert(pos_ribbon_offset(&m) == 160);
    pos_ribbon_tick(&m, 1000000);
    assert(abs(pos_ribbon_offset(&m)) <= 160);
    v.tiles = true;
    v.selected_tile = 1;
    memset(v.icons, 0xff, sizeof(v.icons));
    const int offsets[] = {-160, -80, -5, 0, 5, 80, 160};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      v.ribbon_offset = offsets[i];
      v.tile_scale = i % 2 ? 148 : 100;
      v.tile_lift = i;
      for (int x = 96; x <= 143; ++x) {
        assert(pos_view_pixel(&v, x, 40) == 0xffff);
        assert(pos_view_pixel(&v, x, 87) == 0xffff);
      }
      for (int y = 40; y <= 87; ++y) {
        assert(pos_view_pixel(&v, 96, y) == 0xffff);
        assert(pos_view_pixel(&v, 143, y) == 0xffff);
      }
    }
    v.tile_scale = 100;
    v.tile_lift = 0;
    v.ribbon_offset = 0;
    assert(pos_view_pixel(&v, 24, 50) == 0);
    v.ribbon_offset = 12;
    assert(pos_view_pixel(&v, 24, 50) != 0);
    assert(pos_view_pixel(&v, 36, 50) == 0);
    /* Cached neighbors keep the ribbon populated at both extremes. */
    v.ribbon_offset = 160;
    assert(pos_view_pixel(&v, 24, 50) == 0);
    v.ribbon_offset = -160;
    assert(pos_view_pixel(&v, 184, 50) == 0);
  } else if (!strcmp(argv[1], "spring")) {
    assert(pos_motion_spring(0) == 100 && pos_motion_spring(5) == 0 &&
           pos_motion_spring(99) == 0);
    bool overshoot = false;
    for (unsigned i = 1; i < 5; ++i)
      overshoot |= pos_motion_spring(i) < 0;
    assert(overshoot);
  } else
    return 2;
  puts("visual invariant verified");
  return 0;
}
