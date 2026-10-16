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

static void preview(const char *root, const pos_settings_t *draft) {
  char system[POS_FS_PATH];
  if (pos_fs_join(system, root, "POS"))
    return;
  pos_view_t view;
  pos_launcher_view(system, draft->device_name, &draft->background, 6, &view);
  pos_view_line(&view, 14, "PREVIEW - not saved");
  pos_view_line(&view, 15, "Enter/Esc: back to editor");
  pos_device_show(&view);
  pos_event_t event;
  while (!pos_home_pending()) {
    if (!pos_device_event(&event, 100) || !event.pressed)
      continue;
    if (event.key == POS_KEY_HOME) {
      pos_home_request();
      return;
    }
    if (event.key == POS_KEY_ENTER || event.key == POS_KEY_ESCAPE)
      return;
  }
}

bool pos_ui_background(const char *root, pos_settings_t *settings) {
  pos_settings_t draft = *settings;
  int selected = 0;
  while (!pos_home_pending()) {
    char labels[POS_BACKGROUND_MAX_STRIPES + 5][32];
    const char *items[POS_BACKGROUND_MAX_STRIPES + 5];
    unsigned count = draft.background.count;
    snprintf(labels[0], sizeof(labels[0]), "Stripe count: %u", count);
    for (unsigned i = 0; i < count; ++i)
      snprintf(labels[i + 1], sizeof(labels[i + 1]), "Stripe %u: #%06X", i + 1,
               (unsigned)draft.background.colors[i]);
    snprintf(labels[count + 1], sizeof(labels[0]), "Preview launcher");
    snprintf(labels[count + 2], sizeof(labels[0]), "Save & Apply");
    snprintf(labels[count + 3], sizeof(labels[0]), "Pan flag preset");
    snprintf(labels[count + 4], sizeof(labels[0]), "Cancel");
    for (unsigned i = 0; i < count + 5; ++i)
      items[i] = labels[i];
    selected =
        pos_ui_menu_from("Background (draft)", items, count + 5, selected);
    if (selected < 0 || selected == (int)count + 4)
      return false;
    if (!selected) {
      char amount[4];
      snprintf(amount, sizeof(amount), "%u", count);
      if (pos_ui_text("Stripe count", "Horizontal stripes: 1-12", amount,
                      sizeof(amount), false)) {
        char *end;
        unsigned long next = strtoul(amount, &end, 10);
        if (!*amount || *end || !pos_background_resize(&draft.background, next))
          pos_ui_notice("Invalid stripe count",
                        "Choose a number from 1 to 12.");
      }
    } else if (selected <= (int)count) {
      char title[32];
      snprintf(title, sizeof(title), "Stripe %d color", selected);
      (void)pos_ui_color(title, &draft.background.colors[selected - 1]);
    } else if (selected == (int)count + 1) {
      preview(root, &draft);
    } else if (selected == (int)count + 2) {
      if (pos_settings_save(root, &draft)) {
        pos_ui_notice(
            "Background not saved",
            "Your previous background is still active. Check SD space "
            "and try again, or press Esc to discard this draft.");
      } else {
        *settings = draft;
        return true;
      }
    } else if (selected == (int)count + 3) {
      pos_background_defaults(&draft.background);
      selected = 0;
    }
  }
  return false;
}
