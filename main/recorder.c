#include "esp_timer.h"
#include "pos_device.h"
#include "pos_peripherals.h"
#include "pos_recorder.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>
static int save_capture_tail(pos_recorder_t *r, pos_error_t *e) {
  pos_microphone_end();
  int result = 0;
  for (;;) {
    int16_t samples[256];
    size_t n = 0;
    if (pos_microphone_read(samples, 256, &n, e) ||
        (n && pos_recorder_write(r, samples, n, e))) {
      result = -1;
      break;
    }
    if (!n)
      break;
  }
  pos_microphone_stop();
  return result;
}
static void save_gain(const char *root, pos_settings_t *settings,
                      unsigned gain) {
  if (gain == settings->microphone_gain)
    return;
  pos_settings_t next = *settings;
  next.microphone_gain = gain;
  if (pos_settings_save(root, &next))
    pos_ui_notice("Gain not saved",
                  "Recording is preserved. Check SD space; the previous gain "
                  "preference is retained.");
  else
    *settings = next;
}
static void record(const char *root, pos_settings_t *settings) {
  pos_error_t e = {0};
  struct {
    pos_view_t view;
    int16_t samples[1024];
  } *work = calloc(1, sizeof(*work));
  pos_view_t *v = work ? &work->view : NULL;
  if (!work) {
    pos_ui_notice("Recorder unavailable", "Not enough heap.");
    return;
  }
  pos_view_clear(v, "Recorder");
  pos_view_line(v, 4, "Starting microphone...");
  pos_view_line(v, 6, "16 kHz mono PCM WAV");
  pos_device_show(v);
  /* Directory scans, file creation and header fsync happen before I2S starts.
   */
  pos_recorder_t *r = pos_recorder_open(root, &e);
  if (!r) {
    pos_ui_notice("Cannot record", e.message);
    free(work);
    return;
  }
  pos_microphone_set_gain(settings->microphone_gain);
  if (pos_microphone_start(&e)) {
    pos_recorder_close(r);
    pos_ui_notice("Microphone unavailable", e.message);
    free(work);
    return;
  }
  bool paused = false, failed = false, done = false;
  uint32_t displayed = UINT32_MAX;
  unsigned peak = 0, silent_readouts = 0, level = 0;
  unsigned gain = settings->microphone_gain;
  uint64_t last_draw = 0;
  bool redraw = true;
  while (!done && !pos_home_pending()) {
    pos_event_t event;
    if (pos_device_event(&event, paused ? 50 : 0) && event.pressed) {
      int key = pos_navigation(&event);
      if (key == POS_KEY_HOME || key == POS_KEY_ESCAPE || key == POS_KEY_ENTER)
        done = true;
      if (key == POS_KEY_LEFT || key == POS_KEY_RIGHT) {
        if (key == POS_KEY_LEFT && gain > 1)
          gain /= 2;
        if (key == POS_KEY_RIGHT && gain < 4096)
          gain = gain > 2048 ? 4096 : gain * 2;
        pos_microphone_set_gain(gain);
        redraw = true;
      }
      if (key == ' ') {
        paused = !paused;
        if (paused) {
          if (save_capture_tail(r, &e)) {
            failed = true;
            break;
          }
        } else if (pos_microphone_start(&e)) {
          failed = true;
          break;
        }
        displayed = UINT32_MAX;
        redraw = true;
      }
    }
    if (done || pos_home_pending())
      break;
    if (!paused) {
      unsigned drained = 0;
      do {
        int16_t *samples = work->samples;
        size_t n = 0;
        if (pos_microphone_read(samples, 1024, &n, &e) ||
            pos_recorder_write(r, samples, n, &e)) {
          failed = true;
          break;
        }
        for (size_t i = 0; i < n; ++i) {
          unsigned amplitude = samples[i] < 0 ? -(int)samples[i] : samples[i];
          if (amplitude > peak)
            peak = amplitude;
        }
        drained += (unsigned)n;
      } while (pos_microphone_pending() >= 256 && drained < 16000);
      if (failed)
        break;
    }
    uint32_t seconds = pos_recorder_frames(r) / POS_RECORD_RATE;
    uint64_t now = esp_timer_get_time() / 1000;
    if ((paused || pos_microphone_pending() < 4000) &&
        (redraw || seconds != displayed || now - last_draw >= 200)) {
      bool silent = !paused && !peak;
      silent_readouts = silent ? silent_readouts + 1 : 0;
      unsigned target = peak * 1000 / 32768;
      level = target >= level ? target : (level * 3 + target) / 4;
      pos_view_clear(v, "Recorder");
      pos_ui_style(v);
      v->background = settings->background;
      v->visual = POS_VISUAL_MIC;
      v->visual_level = level;
      pos_view_line(v, 1, "%s",
                    paused                  ? "Paused"
                    : peak >= 32700         ? "Clipping - lower gain"
                    : silent_readouts >= 20 ? "No input detected"
                                            : "Recording");
      const char *name = strrchr(pos_recorder_path(r), '/');
      pos_view_line(v, 2, "%.30s", name ? name + 1 : pos_recorder_path(r));
      pos_view_line(v, 3, "%lu:%02lu", (unsigned long)seconds / 60,
                    (unsigned long)seconds % 60);
      unsigned tenths = peak * 1000 / 32768;
      pos_view_line(v, 12, "            %u.%u%% Gain%ux", tenths / 10,
                    tenths % 10, gain);
      pos_view_line(v, 13, "L/R: gain    Space: pause");
      pos_view_line(v, 14, "Enter/Esc: stop & save");
      pos_view_line(v, 15, "Fn+': close     G0: home");
      pos_device_show(v);
      displayed = seconds;
      last_draw = esp_timer_get_time() / 1000;
      redraw = false;
      peak = 0;
    }
  }
  if (!failed && !paused && save_capture_tail(r, &e))
    failed = true;
  pos_microphone_stop();
  save_gain(root, settings, gain);
  pos_view_clear(v, "Saving recording");
  pos_view_line(v, 5, "Verifying WAV on SD...");
  pos_device_show(v);
  if (!failed && pos_recorder_finish(r, &e))
    failed = true;
  if (failed)
    pos_ui_notice("Partial recording retained", e.message);
  else
    pos_ui_notice("Recording saved", pos_recorder_path(r));
  pos_recorder_close(r);
  free(work);
}
void pos_ui_recorder(const char *root, pos_settings_t *settings) {
  const char *items[] = {
      "Start recording",         "Microphone gain", "Open Recordings in Files",
      "Recover interrupted WAV", "Controls",        "Back"};
  while (!pos_home_pending()) {
    int choice = pos_ui_menu("Recorder", items, 6);
    if (choice < 0 || choice == 5)
      return;
    if (choice == 0)
      record(root, settings);
    if (choice == 1) {
      char text[16];
      snprintf(text, sizeof(text), "%u", settings->microphone_gain);
      while (pos_ui_text("Microphone gain", "Gain: whole number 1-4096", text,
                         sizeof(text), false)) {
        char *end;
        unsigned long gain = strtoul(text, &end, 10);
        bool digits = *text != 0;
        for (const char *p = text; *p; ++p)
          if (*p < '0' || *p > '9')
            digits = false;
        if (digits && !*end && gain >= 1 && gain <= 4096) {
          save_gain(root, settings, (unsigned)gain);
          break;
        }
        pos_ui_notice("Invalid gain", "Enter a whole number from 1 to 4096.");
      }
    }
    if (choice == 2)
      pos_ui_files_from(root, POS_RECORDINGS);
    if (choice == 3) {
      char relative[241];
      pos_error_t e = {0};
      if (pos_ui_pick_file_from(root, "Interrupted .wav.part", POS_RECORDINGS,
                                relative)) {
        const char *confirm[] = {"Cancel", "Recover and verify WAV"};
        if (pos_ui_menu("Keep whole recorded PCM frames?", confirm, 2) == 1)
          pos_ui_notice("Recording recovery",
                        pos_recorder_recover(root, relative, &e)
                            ? e.message
                            : "WAV verified and saved beside the partial file. "
                              "Original audio preserved.");
      }
    }
    if (choice == 4)
      pos_ui_help(root, "Recorder controls", "recorder-controls.txt");
  }
}
