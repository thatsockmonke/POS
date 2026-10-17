#include "ui.h"
#include "esp_timer.h"
#include "pos_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pos_background_t theme;
void pos_ui_set_theme(const pos_background_t *background) {
  if (pos_background_valid(background))
    theme = *background;
}
void pos_ui_style(pos_view_t *view) {
  view->black_background = true;
  if (!pos_background_valid(&theme))
    pos_background_defaults(&theme);
  view->background = theme;
}
void pos_ui_spring(pos_view_t *view, bool opening, bool returning) {
  int offset = view->ribbon_offset;
  int64_t start = esp_timer_get_time();
  for (unsigned frame = 0; frame < 6;) {
    int spring = pos_motion_spring(frame);
    view->ribbon_offset = offset * spring / 100;
    view->tile_lift = frame < 5 ? (100 - abs(spring)) * 8 / 100 : 0;
    view->tile_scale = opening     ? 100 + (100 - spring) * 48 / 100
                       : returning ? 100 + spring * 48 / 100
                                   : 100;
    view->visual_motion = frame < 5 ? (100 - abs(spring)) * 6 / 100 : 0;
    view->fade = opening && frame >= 3    ? (frame - 2) * 85
                 : returning && frame < 2 ? 180 - frame * 90
                                          : 0;
    pos_device_show(view);
    unsigned elapsed = (unsigned)((esp_timer_get_time() - start) / 32000);
    unsigned next = frame + 1;
    if (elapsed > next)
      next = elapsed;
    frame = next > 5 && frame < 5 ? 5 : next;
  }
  view->ribbon_offset = 0;
  view->tile_scale = 100;
  view->tile_lift = view->visual_motion = 0;
  view->fade = 0;
}
bool pos_ui_take_home(void) { return pos_home_take(); }
static bool cancelled(const pos_event_t *event) {
  if (!event->pressed)
    return false;
  if (event->key == POS_KEY_HOME)
    pos_home_request();
  return event->key == POS_KEY_HOME || event->key == POS_KEY_ESCAPE;
}

/* Shared line wrapping and pagination for notices on the 30-column display. */
static unsigned message_page(const char *title, const char *body, unsigned page,
                             bool notice) {
  pos_view_t v;
  pos_view_clear(&v, title);
  pos_ui_style(&v);
  unsigned line = 0;
  while (body && *body) {
    size_t n = 0;
    while (body[n] && body[n] != '\n' && n < POS_COLS)
      ++n;
    if (n == POS_COLS && body[n] && body[n] != '\n') {
      size_t word = n;
      while (word && body[word] != ' ')
        --word;
      if (word)
        n = word;
    }
    if (line / 12 == page) {
      memcpy(v.lines[2 + line % 12], body, n);
      v.lines[2 + line % 12][n] = 0;
    }
    body += n;
    if (*body == '\n' || *body == ' ')
      ++body;
    ++line;
  }
  unsigned pages = line ? (line + 11) / 12 : 1;
  if (pages > 1)
    pos_view_line(&v, 14, "Page %u/%u   Up/Down:scroll", page + 1, pages);
  pos_view_line(&v, 15,
                notice && page + 1 < pages ? "Enter: next   Fn+Space: home"
                                           : "Enter: continue Fn+Space: home");
  pos_device_show(&v);
  return pages;
}
void pos_ui_message(const char *title, const char *body) {
  if (!pos_home_pending())
    (void)message_page(title, body, 0, false);
}
void pos_ui_notice(const char *title, const char *body) {
  if (pos_home_pending())
    return;
  unsigned page = 0, pages = message_page(title, body, 0, true);
  pos_event_t e;
  for (;;) {
    if (!pos_device_event(&e, 100) || !e.pressed)
      continue;
    if (cancelled(&e))
      return;
    int key = pos_navigation(&e);
    if (key == POS_KEY_ENTER) {
      if (page + 1 == pages)
        return;
      ++page;
    } else if ((key == POS_KEY_DOWN || key == POS_KEY_RIGHT) &&
               page + 1 < pages)
      ++page;
    else if ((key == POS_KEY_UP || key == POS_KEY_LEFT) && page)
      --page;
    else
      continue;
    pages = message_page(title, body, page, true);
  }
}

static int menu(const char *title, const char *const *items, int count,
                unsigned page, unsigned pages, const char *status,
                int initial) {
  if (count < 1 || pos_home_pending())
    return -1;
  int selected = initial >= 0 && initial < count ? initial : 0;
  bool redraw = true;
  for (;;) {
    if (redraw) {
      pos_view_t view;
      pos_view_clear(&view, title);
      pos_ui_style(&view);
      if (pages)
        pos_view_line(&view, 1, "Folder page %u/%u", page + 1, pages);
      if (status)
        pos_view_line(&view, 2, "%s", status);
      int first = selected / 10 * 10;
      for (int i = first; i < count && i < first + 10; ++i)
        pos_view_line(&view, 3 + i - first, "%s", items[i]);
      view.selected_row = 3 + selected - first;
      if (count > 10)
        pos_view_line(&view, 14, "Menu %d/%d", selected / 10 + 1,
                      (count + 9) / 10);
      pos_view_line(&view, 15, "Fn+arrows Enter:OK Fn+`:back");
      pos_device_show(&view);
      redraw = false;
    }
    pos_event_t event;
    if (!pos_device_event(&event, 100))
      continue;
    int key = pos_navigation(&event);
    if (cancelled(&event))
      return -1;
    if (key == POS_KEY_UP || key == POS_KEY_LEFT) {
      selected = (selected + count - 1) % count;
      redraw = true;
    }
    if (key == POS_KEY_DOWN || key == POS_KEY_RIGHT) {
      selected = (selected + 1) % count;
      redraw = true;
    }
    if (key == POS_KEY_ENTER)
      return selected;
  }
}
int pos_ui_menu(const char *title, const char *const *items, int count) {
  return menu(title, items, count, 0, 0, NULL, 0);
}
int pos_ui_menu_from(const char *title, const char *const *items, int count,
                     int selected) {
  return menu(title, items, count, 0, 0, NULL, selected);
}
int pos_ui_menu_page(const char *title, const char *const *items, int count,
                     unsigned page, unsigned pages, const char *status) {
  return menu(title, items, count, page, pages, status, 0);
}
bool pos_ui_text(const char *title, const char *label, char *text,
                 size_t capacity, bool secret) {
  if (!capacity || pos_home_pending())
    return false;
  size_t n = strnlen(text, capacity);
  if (n == capacity)
    return false;
  bool redraw = true;
  for (;;) {
    if (redraw) {
      pos_view_t v;
      pos_view_clear(&v, title);
      pos_ui_style(&v);
      pos_view_line(&v, 3, "%s", label);
      size_t start = n > 28 ? n - 28 : 0;
      if (secret) {
        memset(v.lines[6], '*', n - start);
        v.lines[6][n - start] = 0;
      } else
        pos_view_line(&v, 6, "%s_", text + start);
      pos_view_line(&v, 10, "%zu/%zu characters", n, capacity - 1);
      pos_view_line(&v, 12, "Enter: accept  Ctrl+U: clear");
      pos_view_line(&v, 13, "Del: erase (top-right key)");
      pos_view_line(&v, 15, "Fn+`: back  Fn+Space: home");
      pos_device_show(&v);
      redraw = false;
    }
    pos_event_t e;
    if (!pos_device_event(&e, 100) || !e.pressed)
      continue;
    if (cancelled(&e))
      return false;
    if (e.key == 21 ||
        ((e.modifiers & POS_MOD_CTRL) && (e.key == 'u' || e.key == 'U'))) {
      memset(text, 0, capacity);
      n = 0;
      redraw = true;
      continue;
    }
    if (e.key == POS_KEY_ENTER)
      return true;
    if (e.key == POS_KEY_BACKSPACE && n) {
      text[--n] = 0;
      redraw = true;
    } else if (!(e.modifiers & POS_MOD_CTRL) && e.key >= 32 && e.key <= 126 &&
               n + 1 < capacity) {
      text[n++] = (char)e.key;
      text[n] = 0;
      redraw = true;
    }
  }
}
const char *pos_ui_phase_title(const char *phase) {
  static const struct {
    const char *phase, *title;
  } names[] = {{"verify", "Verify files"},
               {"copy", "Prepare SD system"},
               {"validated", "Package verified"},
               {"staged", "SD files verified"},
               {"sd_committed", "SD system ready"},
               {"firmware_write", "Write runtime firmware"},
               {"firmware_verify", "Verify runtime firmware"},
               {"firmware_written", "Firmware verified"},
               {"boot_selected", "Ready to restart"}};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    if (!strcmp(phase, names[i].phase))
      return names[i].title;
  return phase;
}
void pos_ui_install_complete(void) {
  pos_ui_notice("Installation complete",
                "Power off and back on.\nKeep the SD card inserted.\n"
                "Let POS validate its first boot before removing installer "
                "files. Your backups and previous system are retained.");
}
int pos_ui_progress(void *ctx, const char *phase, const char *name,
                    uint64_t done, uint64_t total) {
  (void)ctx;
  if (pos_home_pending())
    return 1;
  static int64_t last;
  static char previous[24];
  int64_t now = esp_timer_get_time();
  if (now - last > 100000 || strcmp(previous, phase)) {
    snprintf(previous, sizeof(previous), "%s", phase);
    last = now;
    pos_view_t v;
    pos_view_clear(&v, pos_ui_phase_title(phase));
    pos_ui_style(&v);
    pos_view_line(&v, 3, "%s", name);
    if (strlen(name) > 30)
      pos_view_line(&v, 4, "%.30s", name + 30);
    if (!strcmp(phase, "scan")) {
      pos_view_line(&v, 7, "Files scanned: %llu", (unsigned long long)total);
      pos_view_line(&v, 8, "Source bytes: %llu", (unsigned long long)done);
    } else
      pos_view_line(&v, 7, "Processed: %llu", (unsigned long long)done);
    if (total && strcmp(phase, "scan"))
      pos_view_line(&v, 8, "Total:     %llu", (unsigned long long)total);
    if (total && strcmp(phase, "scan")) {
      unsigned filled = done >= total ? 28 : (unsigned)(done * 28 / total);
      memset(v.lines[10], '=', filled);
      v.lines[10][filled] = 0;
    }
    pos_view_line(&v, 15, "Fn+Space / Esc: cancel safely");
    pos_device_show(&v);
  }
  pos_event_t e;
  while (pos_device_event(&e, 0))
    if (cancelled(&e))
      return 1;
  return 0;
}

int pos_ui_file_cards(const char *title, const char *const *names,
                      const uint8_t *icons, int count, unsigned page,
                      unsigned pages) {
  if (count < 1 || pos_home_pending())
    return -1;
  int selected = count > 4 ? 4 : 3;
  if (selected >= count)
    selected = 0;
  bool redraw = true, animate = false;
  while (!pos_home_pending()) {
    if (redraw) {
      pos_view_t view;
      pos_view_clear(&view, title);
      pos_ui_style(&view);
      view.visual = POS_VISUAL_FILES;
      if (count == 4)
        pos_view_line(&view, 1, "Empty folder - use Actions");
      else
        pos_view_line(&view, 1, "Page %u/%u    Item %d/%d", page + 1, pages,
                      selected + 1, count);
      pos_view_line(&view, 2, "%.30s", names[selected]);
      char labels[3][11] = {{0}};
      for (int tile = 0; tile < 3; ++tile) {
        int at = selected + tile - 1;
        view.visual_icons[tile] = at >= 0 && at < count ? icons[at] : 255;
        if (at >= 0 && at < count)
          snprintf(labels[tile], sizeof(labels[tile]), "%.10s", names[at]);
      }
      pos_view_line(&view, 12, "%-10.10s%-10.10s%-10.10s", labels[0], labels[1],
                    labels[2]);
      pos_view_line(&view, 14, "Enter:open Space:actions");
      pos_view_line(&view, 15, "Esc:back  Fn+':close G0:home");
      if (animate)
        pos_ui_spring(&view, false, false);
      else
        pos_device_show(&view);
      redraw = false;
      animate = false;
    }
    pos_event_t e;
    if (!pos_device_event(&e, 100) || !e.pressed)
      continue;
    if (e.key == POS_KEY_HOME || e.key == POS_KEY_ESCAPE)
      return -1;
    int key = pos_navigation(&e);
    if (key == ' ')
      return selected + count;
    if (key == POS_KEY_ENTER)
      return selected;
    if (key == POS_KEY_LEFT || key == POS_KEY_UP) {
      selected = (selected + count - 1) % count;
      redraw = animate = true;
    }
    if (key == POS_KEY_RIGHT || key == POS_KEY_DOWN) {
      selected = (selected + 1) % count;
      redraw = animate = true;
    }
  }
  return -1;
}
