#include "pos_device.h"
#include "pos_image.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  (void)ctx;
  (void)phase;
  (void)done;
  (void)total;
  pos_event_t e;
  if (pos_device_event(&e, 0) && e.pressed &&
      (e.key == POS_KEY_ESCAPE || e.key == POS_KEY_HOME))
    return 1;
  return pos_home_pending();
}
void pos_ui_image(const char *root, const char *relative) {
  pos_error_t error = {0};
  pos_image_t *i = pos_image_open(root, relative, &error);
  if (!i) {
    pos_ui_notice("Image viewer",
                  *error.message ? error.message : "Not enough memory.");
    return;
  }
  struct {
    pos_view_t view;
    uint16_t pixels[240 * 88];
  } *w = calloc(1, sizeof(*w));
  if (!w) {
    pos_image_close(i);
    pos_ui_notice("Image viewer", "Not enough memory for viewport.");
    return;
  }
  unsigned width, height;
  pos_image_size(i, &width, &height);
  double fit = fmin(240.0 / width, 88.0 / height), scale = fit,
         cx = width / 2.0, cy = height / 2.0;
  uint32_t generation = 0;
  pos_audio_feedback_hold(true);
  while (!pos_home_pending()) {
    if (pos_image_render(i, cx, cy, scale, w->pixels, progress, NULL, &error)) {
      pos_ui_notice("Image viewer", error.message);
      break;
    }
    pos_view_clear(&w->view, "Image viewer");
    pos_ui_style(&w->view);
    w->view.image_pixels = w->pixels;
    w->view.map_generation = ++generation;
    pos_view_line(&w->view, 13, "%ux%u  Zoom %.0f%%", width, height,
                  scale * 100);
    pos_view_line(&w->view, 14, "Arrows:pan +/-:zoom 0:fit");
    pos_view_line(&w->view, 15, "Esc:Files");
    pos_device_show(&w->view);
    pos_event_t e;
    bool redraw = false;
    while (!redraw && !pos_home_pending()) {
      if (!pos_device_event(&e, 100) || !e.pressed)
        continue;
      int k = pos_navigation(&e);
      if (k == POS_KEY_ESCAPE || k == POS_KEY_HOME)
        goto done;
      if (k == POS_KEY_LEFT)
        cx -= 32 / scale;
      if (k == POS_KEY_RIGHT)
        cx += 32 / scale;
      if (k == POS_KEY_UP)
        cy -= 24 / scale;
      if (k == POS_KEY_DOWN)
        cy += 24 / scale;
      if (e.key == '+' || e.key == '=')
        scale = fmin(32, scale * 1.5);
      if (e.key == '-')
        scale = fmax(fit / 2, scale / 1.5);
      if (e.key == '0') {
        scale = fit;
        cx = width / 2.0;
        cy = height / 2.0;
      }
      cx = fmax(0, fmin(width, cx));
      cy = fmax(0, fmin(height, cy));
      redraw = true;
    }
  }
done:
  pos_ui_message("Files", "");
  pos_audio_feedback_hold(false);
  pos_image_close(i);
  free(w);
}
