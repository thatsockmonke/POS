#include "pos_device.h"
#include "pos_peripherals.h"
#include "pos_recorder.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>
static void record(const char *root) {
  pos_error_t e = {0};
  pos_view_t *v = calloc(1, sizeof(*v));
  if (!v) {
    pos_ui_notice("Recorder unavailable", "Not enough heap.");
    return;
  }
  pos_view_clear(v, "Recorder");
  pos_view_line(v, 4, "Starting microphone...");
  pos_view_line(v, 6, "16 kHz mono PCM WAV");
  pos_device_show(v);
  if (pos_microphone_start(&e)) {
    pos_ui_notice("Microphone unavailable", e.message);
    free(v);
    return;
  }
  pos_recorder_t *r = pos_recorder_open(root, &e);
  if (!r) {
    pos_microphone_stop();
    pos_ui_notice("Cannot record", e.message);
    free(v);
    return;
  }
  bool paused = false, failed = false, done = false;
  uint32_t displayed = UINT32_MAX;
  unsigned peak = 0;
  while (!done && !pos_home_pending()) {
    pos_event_t event;
    if (pos_device_event(&event, paused ? 50 : 0) && event.pressed) {
      if (event.key == POS_KEY_HOME || event.key == POS_KEY_ESCAPE ||
          event.key == POS_KEY_ENTER)
        done = true;
      if (event.key == ' ') {
        paused = !paused;
        if (paused)
          pos_microphone_stop();
        else if (pos_microphone_start(&e)) {
          failed = true;
          break;
        }
        displayed = UINT32_MAX;
      }
    }
    if (done || pos_home_pending())
      break;
    if (!paused) {
      int16_t samples[256];
      size_t n = 0;
      if (pos_microphone_read(samples, 256, &n, &e) ||
          pos_recorder_write(r, samples, n, &e)) {
        failed = true;
        break;
      }
      for (size_t i = 0; i < n; ++i) {
        unsigned amplitude = samples[i] < 0 ? -(int)samples[i] : samples[i];
        if (amplitude > peak)
          peak = amplitude;
      }
    }
    uint32_t seconds = pos_recorder_frames(r) / POS_RECORD_RATE;
    if (seconds != displayed) {
      pos_view_clear(v, "Recorder");
      pos_view_line(v, 2, "%s", paused ? "Paused" : "RECORDING");
      const char *name = strrchr(pos_recorder_path(r), '/');
      pos_view_line(v, 4, "%s", name ? name + 1 : pos_recorder_path(r));
      pos_view_line(v, 6, "%lu:%02lu  %lu KiB", (unsigned long)seconds / 60,
                    (unsigned long)seconds % 60,
                    (unsigned long)pos_recorder_frames(r) * 2 / 1024);
      pos_view_line(v, 8, "Mic peak: %u%%", peak * 100 / 32768);
      pos_view_line(v, 10, "Space: pause / resume");
      pos_view_line(v, 12, "Enter / Esc: stop & save");
      pos_view_line(v, 14, "Fn+Space: save & close");
      pos_device_show(v);
      displayed = seconds;
      peak = 0;
    }
  }
  pos_microphone_stop();
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
  free(v);
}
void pos_ui_recorder(const char *root) {
  const char *items[] = {"Start recording", "Open Recordings in Files",
                         "Recover interrupted WAV", "Controls", "Back"};
  while (!pos_home_pending()) {
    int choice = pos_ui_menu("Recorder", items, 5);
    if (choice < 0 || choice == 4)
      return;
    if (choice == 0)
      record(root);
    if (choice == 1)
      pos_ui_files_from(root, POS_RECORDINGS);
    if (choice == 2) {
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
    if (choice == 3)
      pos_ui_notice(
          "Recorder controls",
          "Enter starts recording. Space pauses/resumes. Enter or Esc stops "
          "and saves. Fn+Space saves and closes. Recordings are standard 16 "
          "kHz mono WAV files. Interrupted .wav.part files remain in "
          "Recordings for recovery. Audio output is exclusive with recording.");
  }
}
