#include "pos_device.h"
#include "pos_peripherals.h"
#include "pos_wave.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  (void)ctx;
  return pos_ui_progress(NULL, phase, "", done, total);
}
static bool seconds(const char *title, double *value) {
  char text[32];
  snprintf(text, sizeof(text), "%.4f", *value);
  if (!pos_ui_text(title, "Seconds", text, sizeof(text), false))
    return false;
  char *end;
  double x = strtod(text, &end);
  if (!*text || *end || !isfinite(x) || x < 0) {
    pos_ui_notice("Invalid time", "Enter nonnegative seconds.");
    return false;
  }
  *value = x;
  return true;
}
void pos_ui_recording_edit(const char *root, const char *relative) {
  struct {
    pos_wave_info_t info;
    pos_view_t view;
    uint8_t pixels[2640];
  } *w = calloc(1, sizeof(*w));
  if (!w)
    return;
  pos_error_t e = {0};
  pos_audio_feedback_hold(true);
  if (pos_wave_scan(root, relative, &w->info, progress, NULL, &e)) {
    pos_ui_notice("Recording editor", e.message);
    goto done;
  }
  double start = 0, end = (double)w->info.frames / w->info.rate;
  bool normalize = false;
  while (!pos_home_pending()) {
    char a[64], b[64], c[48];
    snprintf(a, sizeof(a), "Start: %.3f s", start);
    snprintf(b, sizeof(b), "End: %.3f s", end);
    snprintf(c, sizeof(c), "Normalize: %s",
             normalize ? "on (95% peak)" : "off");
    const char *items[] = {"Waveform",         a,     b, c, "Play original",
                           "Save edited copy", "Back"};
    int n = pos_ui_menu("Recording editor", items, 7);
    if (n < 0 || n == 6)
      break;
    if (n == 1)
      seconds("Trim start", &start);
    if (n == 2)
      seconds("Trim end", &end);
    if (n == 3)
      normalize = !normalize;
    if (n == 4)
      pos_ui_music_file(root, relative);
    if (n == 0) {
      memset(w->pixels, 0, sizeof(w->pixels));
      for (int x = 0; x < 240; ++x) {
        unsigned h = w->info.peaks[x] * 40u / 32768;
        for (int y = 43 - (int)h; y <= 43 + (int)h; ++y) {
          unsigned p = y * 240 + x;
          w->pixels[p / 8] |= 1u << (p % 8);
        }
      }
      pos_view_clear(&w->view, "Recording waveform");
      pos_ui_style(&w->view);
      w->view.map_pixels = w->pixels;
      w->view.map_generation++;
      pos_view_line(&w->view, 13, "%.3f s | %u Hz %s",
                    (double)w->info.frames / w->info.rate, w->info.rate,
                    w->info.channels == 1 ? "mono" : "stereo");
      pos_view_line(&w->view, 14, "Trim %.3f to %.3f s", start, end);
      pos_view_line(&w->view, 15, "Any key: editing controls");
      pos_device_show(&w->view);
      pos_event_t ev;
      while (!pos_device_event(&ev, 100) && !pos_home_pending()) {
      }
      pos_ui_message("Recording editor", "");
    }
    if (n == 5) {
      double first = start * w->info.rate, last = end * w->info.rate;
      if (first >= last || last > w->info.frames + 0.5 || first > UINT32_MAX) {
        pos_ui_notice("Invalid trim", "Keep an interval inside the recording.");
        continue;
      }
      char saved[241];
      if (pos_wave_edit(root, relative, (uint32_t)llround(first),
                        (uint32_t)llround(last), normalize, saved, progress,
                        NULL, &e))
        pos_ui_notice("Edit incomplete", e.message);
      else
        pos_ui_notice("Edited copy saved", saved);
    }
  }
done:
  pos_ui_message("Recorder", "");
  pos_audio_feedback_hold(false);
  free(w);
}
