#include "ui.h"
#include "esp_timer.h"
#include "pos_device.h"
#include <stdio.h>
#include <string.h>

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
    pos_view_line(&v, 14, "Page %u/%u   W/S: scroll", page + 1, pages);
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

int pos_ui_menu(const char *title, const char *const *items, int count) {
  if (count < 1 || pos_home_pending())
    return -1;
  int selected = 0;
  bool redraw = true;
  for (;;) {
    if (redraw) {
      pos_view_t view;
      pos_view_clear(&view, title);
      int first = selected / 10 * 10;
      for (int i = first; i < count && i < first + 10; ++i)
        pos_view_line(&view, 3 + i - first, "%s", items[i]);
      view.selected_row = 3 + selected - first;
      pos_view_line(&view, 15, "W/S: move Enter: select Esc:back");
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
      pos_view_line(&v, 3, "%s", label);
      size_t start = n > 28 ? n - 28 : 0;
      if (secret) {
        memset(v.lines[6], '*', n - start);
        v.lines[6][n - start] = 0;
      } else
        pos_view_line(&v, 6, "%s_", text + start);
      pos_view_line(&v, 12, "Enter: accept  Backspace: erase");
      pos_view_line(&v, 15, "Fn+`: back  Fn+Space: home");
      pos_device_show(&v);
      redraw = false;
    }
    pos_event_t e;
    if (!pos_device_event(&e, 100) || !e.pressed)
      continue;
    if (cancelled(&e))
      return false;
    if (e.key == POS_KEY_ENTER)
      return true;
    if (e.key == POS_KEY_BACKSPACE && n) {
      text[--n] = 0;
      redraw = true;
    } else if (e.key >= 32 && e.key <= 126 && n + 1 < capacity) {
      text[n++] = (char)e.key;
      text[n] = 0;
      redraw = true;
    }
  }
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
    pos_view_clear(&v, phase);
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
