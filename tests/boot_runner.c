#include "pos_boot.h"
#include "pos_device.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct test_queue {
  struct { int64_t at; pos_event_t event; } events[8];
  unsigned count, next, resets;
};
static struct test_queue input;
static int64_t now, drawing;
static unsigned frames, lights, fail_frame, calls;
static bool ready, delayed_start;
static pos_view_t last;

int64_t esp_timer_get_time(void) {
  if (delayed_start && ++calls == 2)
    now += 30000;
  return now;
}
void pos_device_show(const pos_view_t *view) {
  assert(view->boot_splash);
  if (!frames)
    assert(!view->boot_pink_width && !view->boot_yellow_width &&
           !view->boot_blue_width && !view->boot_text_alpha);
  last = *view;
  now += drawing;
  if (++frames == fail_frame)
    ready = false;
}
void pos_device_brightness(unsigned percent) {
  assert(percent == 70 && frames == 1 && ready);
  ++lights;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t wait) {
  int64_t end = now + (int64_t)wait * 10000;
  if (q->next < q->count && q->events[q->next].at <= end) {
    if (q->events[q->next].at > now)
      now = q->events[q->next].at;
    *(pos_event_t *)out = q->events[q->next++].event;
    return pdTRUE;
  }
  now = end;
  return 0;
}
BaseType_t xQueueReset(QueueHandle_t q) {
  while (q->next < q->count && q->events[q->next].at <= now)
    ++q->next;
  ++q->resets;
  return pdTRUE;
}
static void reset(void) {
  memset(&input, 0, sizeof(input));
  memset(&last, 0, sizeof(last));
  now = drawing = frames = lights = fail_frame = calls = 0;
  ready = true;
  delayed_start = false;
  (void)pos_home_take();
}
static void add(unsigned ms, int key, bool pressed) {
  unsigned i = input.count++;
  assert(i < 8);
  input.events[i].at = (int64_t)ms * 1000;
  input.events[i].event = (pos_event_t){key, 0, pressed};
}
int main(int argc, char **argv) {
  assert(argc == 2);
  reset();
  if (!strcmp(argv[1], "complete")) {
    pos_boot_animation(&input, &ready);
    assert(frames == 23 && lights == 1 && now == 940000);
    assert(last.boot_text_alpha == 255 && input.resets == 1);
  } else if (!strcmp(argv[1], "skip")) {
    const unsigned times[] = {0, 20, 100, 250, 430, 440, 600, 939};
    const int keys[] = {'a', POS_KEY_ENTER, POS_KEY_ESCAPE, POS_KEY_HOME,
                        POS_KEY_UP};
    for (unsigned t = 0; t < sizeof(times) / sizeof(*times); ++t)
      for (unsigned k = 0; k < sizeof(keys) / sizeof(*keys); ++k) {
        reset(); add(times[t], keys[k], true);
        pos_boot_animation(&input, &ready);
        assert(now == (int64_t)times[t] * 1000 && lights == 1);
        assert(!pos_home_pending() && input.next == 1 && input.resets == 1);
      }
  } else if (!strcmp(argv[1], "release")) {
    add(99, POS_KEY_ENTER, false); add(501, POS_KEY_HOME, false);
    pos_boot_animation(&input, &ready);
    /* At 100 Hz, allow one tick for frame pacing and one for the final hold.
     * Releases must not restart a frame/hold or truncate the artwork. */
    assert(now >= 940000 && now < 960000 && frames == 23);
    assert(last.boot_text_alpha == 255 && !pos_home_pending());
  } else if (!strcmp(argv[1], "consume")) {
    add(100, POS_KEY_HOME, true); add(100, POS_KEY_ENTER, true);
    add(110, POS_KEY_HOME, false); add(1000, POS_KEY_ENTER, true);
    pos_boot_animation(&input, &ready);
    assert(now == 100000 && input.next == 2 && !pos_home_pending());
    pos_event_t event;
    assert(!xQueueReceive(&input, &event, 0));
    assert(xQueueReceive(&input, &event, 1) && !event.pressed);
    assert(xQueueReceive(&input, &event, 100) && event.pressed &&
           event.key == POS_KEY_ENTER);
  } else if (!strcmp(argv[1], "slow")) {
    drawing = 30000;
    pos_boot_animation(&input, &ready);
    assert(frames < 23 && lights == 1 && last.boot_text_alpha == 255);
    assert(now >= 940000 && now <= 980000);
    reset(); drawing = 1500000;
    pos_boot_animation(&input, &ready);
    assert(frames == 2 && last.boot_text_alpha == 255 && now == 3500000);
  } else if (!strcmp(argv[1], "display")) {
    ready = false; add(0, POS_KEY_HOME, true);
    pos_boot_animation(&input, &ready);
    assert(!frames && !lights && !input.next && !input.resets);
    reset(); pos_boot_animation(NULL, &ready);
    assert(!frames && !lights);
    for (unsigned i = 1; i <= 3; ++i) {
      reset(); fail_frame = i;
      pos_boot_animation(&input, &ready);
      assert(frames == i && !ready && input.resets == 1);
      assert(lights == (i == 1 ? 0u : 1u));
    }
  } else if (!strcmp(argv[1], "first-frame")) {
    delayed_start = true;
    pos_boot_animation(&input, &ready);
    assert(lights == 1 && last.boot_text_alpha == 255);
  } else
    assert(false);
  puts("Boot presentation test passed (timed queue simulation; no physical LCD).");
  return 0;
}
