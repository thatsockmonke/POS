#include "pos_device.h"
#include "pos_editor.h"
#include "ui.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t top, positions[12][31];
  unsigned row, col;
  bool visible;
} screen_t;
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  (void)ctx;
  return pos_ui_progress(NULL, phase, "", done, total);
}
static uint32_t previous(const pos_editor_t *e, uint32_t at) {
  if (!at)
    return 0;
  unsigned char bytes[4];
  uint32_t start = at > 4 ? at - 4 : 0;
  size_t n;
  if (pos_editor_read(e, start, bytes, at - start, &n) || !n)
    return at;
  unsigned i = n - 1;
  if (bytes[i] == '\n' && i && bytes[i - 1] == '\r')
    return start + i - 1;
  while (i && (bytes[i] & 0xc0) == 0x80)
    --i;
  return start + i;
}
static unsigned glyph(const unsigned char *bytes, size_t n, char *c) {
  *c = '?';
  if (!n)
    return 0;
  if (*bytes == '\r' || *bytes == '\n') {
    *c = '\n';
    return *bytes == '\r' && n > 1 && bytes[1] == '\n' ? 2 : 1;
  }
  if (*bytes < 128) {
    *c = *bytes >= 32 ? *bytes : ' ';
    return 1;
  }
  unsigned length = (*bytes & 0xe0) == 0xc0   ? 2
                    : (*bytes & 0xf0) == 0xe0 ? 3
                    : (*bytes & 0xf8) == 0xf0 ? 4
                                              : 1;
  if (length > n)
    return 1;
  for (unsigned i = 1; i < length; ++i)
    if ((bytes[i] & 0xc0) != 0x80)
      return 1;
  return length;
}
static uint32_t next(const pos_editor_t *e, uint32_t at) {
  unsigned char bytes[4];
  size_t n;
  char c;
  if (pos_editor_read(e, at, bytes, sizeof(bytes), &n) || !n)
    return at;
  return at + glyph(bytes, n, &c);
}
static uint32_t previous_row(const pos_editor_t *e, uint32_t top) {
  uint32_t at = top;
  if (at) {
    uint32_t p = previous(e, at);
    unsigned char c;
    size_t n;
    if (!pos_editor_read(e, p, &c, 1, &n) && n && (c == '\r' || c == '\n'))
      at = p;
  }
  for (unsigned column = 0; column < 30 && at; ++column) {
    uint32_t p = previous(e, at);
    unsigned char c;
    size_t n;
    if (p == at || pos_editor_read(e, p, &c, 1, &n) || !n || c == '\r' ||
        c == '\n')
      break;
    at = p;
  }
  return at;
}
static int render(pos_editor_t *e, screen_t *screen) {
  if (screen->top > e->cursor)
    screen->top = previous_row(e, e->cursor);
  for (unsigned retry = 0; retry < 2; ++retry) {
    unsigned char bytes[2048];
    size_t n;
    if (pos_editor_read(e, screen->top, bytes, sizeof(bytes), &n))
      return -1;
    memset(screen->positions, 0xff, sizeof(screen->positions));
    screen->visible = false;
    pos_view_t view;
    const char *name = strrchr(e->source, '/');
    name = name ? name + 1 : e->source;
    pos_view_clear(&view, name);
    pos_view_line(&view, 1, "%s  %u KB  %u%%", e->dirty ? "* edited" : "Saved",
                  (unsigned)((e->length + 1023ULL) / 1024),
                  e->length ? (unsigned)((uint64_t)e->cursor * 100 / e->length)
                            : 0);
    unsigned row = 0, col = 0;
    size_t i = 0;
    while (row < 12) {
      uint32_t offset = screen->top + i;
      screen->positions[row][col] = offset;
      if (offset == e->cursor) {
        screen->visible = true;
        screen->row = row;
        screen->col = col;
      }
      if (i >= n)
        break;
      char c;
      unsigned used = glyph(bytes + i, n - i, &c);
      i += used;
      if (c == '\n') {
        ++row;
        col = 0;
        continue;
      }
      view.lines[row + 2][col] = c;
      if (++col == 30) {
        view.lines[row + 2][30] = 0;
        ++row;
        col = 0;
      }
    }
    if (!screen->visible) {
      screen->top = e->cursor;
      continue;
    }
    view.text_editor = true;
    view.editor_cursor_row = screen->row + 2;
    view.editor_cursor_col = screen->col;
    pos_view_line(&view, 14, "Ctrl+S:save Ctrl+F:find");
    pos_view_line(&view, 15, "Fn+arrows:move Esc:menu");
    pos_device_show(&view);
    return 0;
  }
  return -1;
}
static void vertical(pos_editor_t *e, screen_t *screen, int direction) {
  unsigned col = screen->col, row = screen->row;
  if (direction < 0 && !row) {
    uint32_t top = previous_row(e, screen->top);
    if (top == screen->top)
      return;
    screen->top = top;
    uint32_t cursor = e->cursor;
    e->cursor = top;
    if (render(e, screen)) {
      e->cursor = cursor;
      return;
    }
    row = 0;
  } else if (direction > 0 && row == 11) {
    uint32_t top = screen->positions[1][0];
    if (top == UINT32_MAX || top == screen->top)
      return;
    screen->top = top;
    if (render(e, screen))
      return;
    row = 11;
  } else
    row = direction < 0 ? row - 1 : row + 1;
  while (col && screen->positions[row][col] == UINT32_MAX)
    --col;
  if (screen->positions[row][col] != UINT32_MAX)
    e->cursor = screen->positions[row][col];
}
static bool save_as(pos_editor_t *e, bool as_new) {
  char relative[241];
  strcpy(relative, e->source);
  if (as_new) {
    char name[121] = "Copy.txt";
    if (!pos_ui_text("Save As", "Filename in Documents", name, sizeof(name),
                     false))
      return false;
    if (!pos_fs_archive_path(name) || strchr(name, '/') || *name == '.') {
      pos_ui_notice("Invalid filename", "Use a filename without directories.");
      return false;
    }
    snprintf(relative, sizeof(relative), "POS/Users/Owner/Documents/%s", name);
  }
  pos_error_t error = {0};
  if (pos_editor_save(e, relative, !as_new, progress, NULL, &error)) {
    pos_ui_notice("Save incomplete", error.message);
    return false;
  }
  return true;
}
static bool close_document(pos_editor_t *e) {
  (void)pos_ui_take_home();
  if (!e->dirty) {
    pos_editor_discard(e);
    return true;
  }
  const char *items[] = {"Save & close", "Keep draft & close", "Discard edits",
                         "Continue editing"};
  int choice = pos_ui_menu("Unsaved changes", items, 4);
  if (choice == 0 && save_as(e, false)) {
    pos_editor_discard(e);
    return true;
  }
  if (choice == 1) {
    char relative[241], full[POS_FS_PATH];
    struct stat st;
    bool chosen = false;
    for (unsigned i = 0; i < 10000; ++i) {
      snprintf(relative, sizeof(relative),
               "POS/Users/Owner/Documents/Draft-%u.txt", i);
      if (!pos_fs_join(full, e->root, relative) && pos_fs_lstat(full, &st) &&
          errno == ENOENT) {
        chosen = true;
        break;
      }
    }
    pos_error_t error = {0};
    if (chosen &&
        !pos_editor_save(e, relative, false, progress, NULL, &error)) {
      pos_ui_notice("Draft kept", relative);
      pos_editor_discard(e);
      return true;
    }
    pos_ui_notice("Draft not closed",
                  *error.message ? error.message : "No free draft filename.");
  }
  if (choice == 2) {
    pos_editor_discard(e);
    return true;
  }
  return false;
}
static void find_text(pos_editor_t *e, screen_t *screen, bool line) {
  char text[129] = "";
  if (!pos_ui_text(line ? "Go to line" : "Find text",
                   line ? "Line number (starts at 1)"
                        : "Search from cursor; wraps once",
                   text, sizeof(text), false) ||
      !*text)
    return;
  uint32_t at;
  int result;
  if (line) {
    char *end;
    unsigned long number = strtoul(text, &end, 10);
    result = *end || !number || number > UINT32_MAX
                 ? -1
                 : pos_editor_line(e, number, &at, progress, NULL);
  } else {
    result = pos_editor_find(
        e, text, e->cursor < e->length ? next(e, e->cursor) : e->length, &at,
        progress, NULL);
    if (result == 1)
      result = pos_editor_find(e, text, 0, &at, progress, NULL);
  }
  if (!result) {
    e->cursor = at;
    screen->top = previous_row(e, at);
  } else if (!pos_home_pending())
    pos_ui_notice("Editor", result == 1 ? "Not found."
                                        : "Search cancelled or unavailable.");
}
static bool edit(pos_editor_t *e) {
  screen_t *screen = calloc(1, sizeof(*screen));
  if (!screen)
    return false;
  screen->top = e->cursor;
  bool leave = false, home = false;
  while (!leave) {
    if (render(e, screen)) {
      pos_ui_notice("Read failed", "Original and working files remain on SD.");
      break;
    }
    pos_event_t event;
    if (!pos_device_event(&event, 100) || !event.pressed)
      continue;
    if (event.key == POS_KEY_HOME) {
      leave = close_document(e);
      home = leave;
      continue;
    }
    if (event.key == POS_KEY_ESCAPE) {
      const char *items[] = {"Continue editing", "Save",
                             "Save As",          "Find text",
                             "Go to line",       "Start of document",
                             "End of document",  "Close document"};
      int choice = pos_ui_menu("Text editor", items, 8);
      if (choice == 1)
        save_as(e, false);
      if (choice == 2)
        save_as(e, true);
      if (choice == 3 || choice == 4)
        find_text(e, screen, choice == 4);
      if (choice == 5) {
        e->cursor = 0;
        screen->top = 0;
      }
      if (choice == 6) {
        e->cursor = e->length;
        screen->top = previous_row(e, e->cursor);
      }
      if (choice == 7 || pos_home_pending()) {
        bool requested = pos_home_pending();
        leave = close_document(e);
        if (leave && requested)
          home = true;
      }
      continue;
    }
    if (event.modifiers & POS_MOD_CTRL) {
      if (event.key == 's' || event.key == 'S')
        save_as(e, false);
      if (event.key == 'f' || event.key == 'F')
        find_text(e, screen, false);
      if (event.key == 'g' || event.key == 'G')
        find_text(e, screen, true);
      if (pos_home_pending()) {
        leave = close_document(e);
        home = leave;
      }
      continue;
    }
    if (event.key == POS_KEY_LEFT) {
      e->cursor = previous(e, e->cursor);
      continue;
    }
    if (event.key == POS_KEY_RIGHT) {
      e->cursor = next(e, e->cursor);
      continue;
    }
    if (event.key == POS_KEY_UP || event.key == POS_KEY_DOWN) {
      vertical(e, screen, event.key == POS_KEY_UP ? -1 : 1);
      continue;
    }
    pos_error_t error = {0};
    int result = 0;
    if (event.key == POS_KEY_BACKSPACE && e->cursor) {
      uint32_t end = e->cursor;
      e->cursor = previous(e, e->cursor);
      result = pos_editor_replace(e, end - e->cursor, "", 0, &error);
    } else if (event.key == POS_KEY_ENTER)
      result = pos_editor_replace(e, 0, "\n", 1, &error);
    else if (event.key == '\t')
      result = pos_editor_replace(e, 0, "    ", 4, &error);
    else if (event.key >= 32 && event.key <= 126 &&
             !(event.modifiers & POS_MOD_ALT)) {
      char c = event.key;
      result = pos_editor_replace(e, 0, &c, 1, &error);
    }
    if (result)
      pos_ui_notice("Edit not committed", error.message);
  }
  free(screen);
  return home;
}
static bool recover(pos_editor_t *e, const char *root) {
  char folder[POS_FS_PATH];
  if (pos_fs_join(folder, root, POS_EDITOR_DRAFT_ROOT))
    return false;
  DIR *dir = opendir(folder);
  if (!dir)
    return false;
  struct dirent *entry;
  pos_error_t error = {0};
  while ((entry = readdir(dir)))
    if (!strncmp(entry->d_name, "editor-", 7)) {
      char relative[241];
      snprintf(relative, sizeof(relative), POS_EDITOR_DRAFT_ROOT "/%.180s",
               entry->d_name);
      if (!pos_editor_recover(e, root, relative, &error)) {
        const char *items[] = {"Open this draft", "Next draft", "Back"};
        int choice = pos_ui_menu(e->source, items, 3);
        if (choice == 0) {
          closedir(dir);
          return true;
        }
        if (choice < 0 || choice == 2) {
          closedir(dir);
          return false;
        }
      }
    }
  closedir(dir);
  pos_ui_notice(
      "Recover drafts",
      "No more verified editor drafts. Unknown working files were preserved.");
  return false;
}
void pos_ui_editor(const char *root) {
  pos_editor_t *e = calloc(1, sizeof(*e));
  if (!e)
    return;
  for (;;) {
    const char *items[] = {"New document", "Open document",
                           "Recover working draft", "Back"};
    int choice = pos_ui_menu("Text editor", items, 4);
    if (choice < 0 || choice == 3)
      break;
    if (choice == 2) {
      if (recover(e, root) && edit(e))
        break;
      if (pos_home_pending())
        break;
      continue;
    }
    char relative[241];
    if (choice == 1) {
      if (!pos_ui_pick_file(root, "Open text document", relative)) {
        if (pos_home_pending())
          break;
        continue;
      }
    } else {
      char name[121] = "Untitled.txt";
      if (!pos_ui_text("New document", "Filename in Documents", name,
                       sizeof(name), false)) {
        if (pos_home_pending())
          break;
        continue;
      }
      if (!pos_fs_archive_path(name) || strchr(name, '/') || *name == '.') {
        pos_ui_notice("Invalid filename",
                      "Choose a simple filename, such as Notes.txt.");
        continue;
      }
      snprintf(relative, sizeof(relative), "POS/Users/Owner/Documents/%s",
               name);
      char full[POS_FS_PATH];
      struct stat st;
      if (pos_fs_join(full, root, relative) || !pos_fs_lstat(full, &st)) {
        pos_ui_notice("Document exists",
                      "Choose Open document or use another filename.");
        continue;
      }
    }
    pos_error_t error = {0};
    if (pos_editor_open(e, root, relative, progress, NULL, &error))
      pos_ui_notice("Editor unavailable", error.message);
    else if (edit(e))
      break;
    if (pos_home_pending())
      break;
  }
  free(e);
}
