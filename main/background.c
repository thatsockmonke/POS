#include "esp_timer.h"
#include "pos_device.h"
#include "pos_fs.h"
#include "pos_launcher.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned clamp(int value, unsigned maximum) {
  return value < 0 ? 0 : (unsigned)value > maximum ? maximum : (unsigned)value;
}

bool pos_ui_color(const char *title, uint32_t *rgb) {
  if (!rgb || *rgb > 0xffffff)
    return false;
  uint16_t hue, saturation, value;
  pos_color_rgb_hsv(*rgb, &hue, &saturation, &value);
  uint32_t picked = *rgb;
  bool redraw = true;
  while (!pos_home_pending()) {
    if (redraw) {
      pos_view_t view;
      pos_color_picker_view(title, picked, hue, saturation, value, &view);
      pos_device_show(&view);
      redraw = false;
    }
    pos_event_t event;
    if (!pos_device_event(&event, 100) || !event.pressed)
      continue;
    if (event.key == POS_KEY_HOME) {
      pos_home_request();
      return false;
    }
    if (event.key == POS_KEY_ESCAPE)
      return false;
    if (event.modifiers & (POS_MOD_CTRL | POS_MOD_ALT))
      continue;
    if (event.key == POS_KEY_ENTER) {
      *rgb = picked;
      return true;
    }
    int step = event.modifiers & POS_MOD_SHIFT ? 5 : 25;
    int hstep = event.modifiers & POS_MOD_SHIFT ? 1 : 10;
    int key = pos_navigation(&event);
    uint16_t old_hue = hue, old_saturation = saturation, old_value = value;
    if (key == POS_KEY_LEFT)
      saturation = clamp((int)saturation - step, 1000);
    if (key == POS_KEY_RIGHT)
      saturation = clamp(saturation + step, 1000);
    if (key == POS_KEY_UP)
      value = clamp(value + step, 1000);
    if (key == POS_KEY_DOWN)
      value = clamp((int)value - step, 1000);
    if (event.key == 'q' || event.key == 'Q')
      hue = (hue + 360 - hstep) % 360;
    if (event.key == 'e' || event.key == 'E')
      hue = (hue + hstep) % 360;
    if (hue != old_hue || saturation != old_saturation || value != old_value) {
      picked = pos_color_hsv_rgb(hue, saturation, value);
      redraw = true;
    }
  }
  return false;
}

bool pos_ui_background(const char *root, pos_settings_t *settings) {
  pos_settings_t draft = *settings;
  unsigned selected = 0;
  bool redraw = true;
  uint64_t last = 0;
  while (!pos_home_pending()) {
    uint64_t now = esp_timer_get_time() / 1000;
    if (redraw || now - last >= 160) {
      pos_view_t view;
      pos_view_clear(&view, "Background");
      pos_ui_style(&view);
      view.visual = POS_VISUAL_STRIPES;
      view.background = draft.background;
      view.visual_selection = selected;
      view.visual_flash = (now % 1400) < 180;
      pos_view_line(&view, 1, "Draft - S saves / Esc cancels");
      pos_view_line(&view, 2, "Stripe %u/%u  #%06X", selected + 1,
                    draft.background.count,
                    (unsigned)draft.background.colors[selected]);
      pos_view_line(&view, 14, "Up/Down:stripe Enter:color");
      pos_view_line(&view, 15, "L/R:count S:save P:presets");
      pos_device_show(&view);
      last = now;
      redraw = false;
    }
    pos_event_t e;
    if (!pos_device_event(&e, 40) || !e.pressed)
      continue;
    if (e.key == POS_KEY_HOME || e.key == POS_KEY_ESCAPE)
      return false;
    int key = pos_navigation(&e);
    unsigned count = draft.background.count;
    if (key == POS_KEY_UP)
      selected = (selected + count - 1) % count;
    if (key == POS_KEY_DOWN)
      selected = (selected + 1) % count;
    if (key == POS_KEY_LEFT && count > 1)
      pos_background_resize(&draft.background, count - 1);
    if (key == POS_KEY_RIGHT && count < 12)
      pos_background_resize(&draft.background, count + 1);
    if (selected >= draft.background.count)
      selected = draft.background.count - 1;
    if (key == POS_KEY_ENTER) {
      char title[32];
      snprintf(title, sizeof(title), "Stripe %u color", selected + 1);
      pos_ui_color(title, &draft.background.colors[selected]);
    }
    if (e.key == 'p' || e.key == 'P') {
      const char *presets[] = {"Pansexual flag", "Nonbinary flag", "Cancel"};
      int preset = pos_ui_menu("Background presets", presets, 3);
      if (preset == 0 || preset == 1) {
        pos_background_preset(&draft.background,
                              (pos_background_preset_t)preset);
        selected = 0;
      }
    }
    if (e.key == 's' || e.key == 'S') {
      if (pos_settings_save(root, &draft))
        pos_ui_notice("Background not saved",
                      "Previous background retained. Check SD space; Esc "
                      "discards the draft.");
      else {
        *settings = draft;
        pos_ui_set_theme(&draft.background);
        return true;
      }
    }
    redraw = true;
  }
  return false;
}
