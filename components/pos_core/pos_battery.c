#include "pos_battery.h"
#include <stddef.h>
static int percent(int mv) {
  static const struct {
    int mv, percent;
  } curve[] = {{3300, 0},  {3500, 5},  {3600, 10}, {3680, 20},
               {3740, 30}, {3790, 40}, {3830, 50}, {3870, 60},
               {3920, 70}, {3980, 80}, {4060, 90}, {4200, 100}};
  if (mv <= curve[0].mv)
    return 0;
  for (unsigned i = 1; i < sizeof(curve) / sizeof(*curve); ++i)
    if (mv <= curve[i].mv)
      return curve[i - 1].percent +
             (mv - curve[i - 1].mv) *
                 (curve[i].percent - curve[i - 1].percent) /
                 (curve[i].mv - curve[i - 1].mv);
  return 100;
}
void pos_battery_sample(pos_battery_t *s, int mv, uint64_t now) {
  if (!s)
    return;
  if (mv < 2800 || mv > 4400) {
    s->available = false;
    s->full = false;
    s->high_since_ms = 0;
    return;
  }
  if (!s->available || now < s->last_ms) {
    s->filtered_mv = mv;
    s->percent = percent(mv);
    s->bars = (s->percent + 24) / 25;
    s->high_since_ms = 0;
    s->full = false;
  } else {
    s->filtered_mv = (s->filtered_mv * 3 + mv + 2) / 4;
    int p = percent(s->filtered_mv);
    if (p >= s->percent + 2 || p <= s->percent - 2 || p == 0 || p == 100)
      s->percent = p;
  }
  s->available = true;
  s->last_ms = now;
  if (s->filtered_mv >= 4180) {
    if (!s->high_since_ms)
      s->high_since_ms = now ? now : 1;
    if (now >= s->high_since_ms && now - s->high_since_ms >= 60000)
      s->full = true;
  } else if (s->filtered_mv < 4120) {
    s->full = false;
    s->high_since_ms = 0;
  }
  if (s->full)
    s->percent = 100;
  while (s->bars < 4 && s->percent >= (int)s->bars * 25 + 3)
    ++s->bars;
  while (s->bars && s->percent <= (int)(s->bars - 1) * 25 - 3)
    --s->bars;
  if (s->percent == 0)
    s->bars = 0;
  if (s->full)
    s->bars = 4;
}
