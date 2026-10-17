#include "pos_device.h"
#include "pos_editor.h"
#include "ui.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t top, positions[12][31];
  unsigned row, col;
  bool visible, selecting;
  uint32_t anchor;
  unsigned char bytes[2048];
  pos_view_t view;
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
static int render(pos_editor_t *e, screen_t *screen, bool show) {
  if (screen->top > e->cursor)
    screen->top = previous_row(e, e->cursor);
  for (unsigned retry = 0; retry < 2; ++retry) {
    unsigned char *bytes = screen->bytes;
    size_t n;
    if (pos_editor_read(e, screen->top, bytes, sizeof(screen->bytes), &n))
      return -1;
    memset(screen->positions, 0xff, sizeof(screen->positions));
    screen->visible = false;
    pos_view_t *view = &screen->view;
    const char *name = strrchr(e->source, '/');
    name = name ? name + 1 : e->source;
    pos_view_clear(view, name);
    pos_ui_style(view);
    pos_view_line(view, 1, "%s  %u KB  %u%%", e->dirty ? "* edited" : "Saved",
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
      uint32_t low = screen->anchor < e->cursor ? screen->anchor : e->cursor;
      uint32_t high = screen->anchor > e->cursor ? screen->anchor : e->cursor;
      if (screen->selecting && offset >= low && offset < high)
        view->editor_selected[row] |= 1u << col;
      char c;
      unsigned used = glyph(bytes + i, n - i, &c);
      i += used;
      if (c == '\n') {
        ++row;
        col = 0;
        continue;
      }
      view->lines[row + 2][col] = c;
      if (++col == 30) {
        view->lines[row + 2][30] = 0;
        ++row;
        col = 0;
      }
    }
    if (!screen->visible) {
      screen->top = e->cursor;
      continue;
    }
    view->text_editor = true;
    view->editor_cursor_row = screen->row + 2;
    view->editor_cursor_col = screen->col;
    pos_view_line(view, 14,
                  screen->selecting ? "Selecting  Ctrl+C/X:copy/cut"
                                    : "Ctrl+S:save Ctrl+Z:undo");
    pos_view_line(view, 15, "Shift+arrows:select Esc:menu");
    if (show)
      pos_device_show(view);
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
    if (render(e, screen, false)) {
      e->cursor = cursor;
      return;
    }
    row = 0;
  } else if (direction > 0 && row == 11) {
    uint32_t top = screen->positions[1][0];
    if (top == UINT32_MAX || top == screen->top)
      return;
    screen->top = top;
    if (render(e, screen, false))
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
static void selection(const pos_editor_t *e, const screen_t *s, uint32_t *start,
                      uint32_t *count) {
  *start = e->cursor;
  *count = 0;
  if (s->selecting) {
    *start = s->anchor < e->cursor ? s->anchor : e->cursor;
    *count = (s->anchor > e->cursor ? s->anchor : e->cursor) - *start;
  }
}
static void clipboard(pos_editor_t *e, screen_t *s, int action) {
  uint32_t start, count;
  selection(e, s, &start, &count);
  pos_error_t error = {0};
  int result = 0;
  if (action == 'c' || action == 'x')
    result = pos_editor_copy_selection(e, start, start + count, progress, NULL,
                                       &error);
  if (!result && (action == 'x' || action == 'v')) {
    uint32_t cursor = e->cursor;
    e->cursor = start;
    result = action == 'x' ? pos_editor_replace(e, count, "", 0, &error)
                           : pos_editor_paste(e, count, progress, NULL, &error);
    if (result)
      e->cursor = cursor;
    else {
      s->selecting = false;
      s->top = previous_row(e, e->cursor);
    }
  }
  if (result)
    pos_ui_notice("Clipboard", error.message);
}
static bool word_byte(const pos_editor_t *e, uint32_t at) {
  unsigned char c;
  size_t n;
  return !pos_editor_read(e, at, &c, 1, &n) && n &&
         (isalnum(c) || c == '_' || c >= 128);
}
static void move_cursor(pos_editor_t *e, screen_t *s, int key, bool ctrl) {
  if (key == POS_KEY_LEFT) {
    uint32_t at = previous(e, e->cursor);
    if (ctrl) {
      while (at && !word_byte(e, at))
        at = previous(e, at);
      while (at && word_byte(e, previous(e, at)))
        at = previous(e, at);
    }
    e->cursor = at;
  } else if (key == POS_KEY_RIGHT) {
    uint32_t at = next(e, e->cursor);
    if (ctrl) {
      while (at < e->length && word_byte(e, at))
        at = next(e, at);
      while (at < e->length && !word_byte(e, at))
        at = next(e, at);
    }
    e->cursor = at;
  } else if (ctrl) {
    e->cursor = key == POS_KEY_UP ? 0 : e->length;
    s->top = previous_row(e, e->cursor);
  } else
    vertical(e, s, key == POS_KEY_UP ? -1 : 1);
}
static void page_move(pos_editor_t *e, screen_t *s, int direction) {
  for (unsigned i = 0; i < 11; ++i) {
    vertical(e, s, direction);
    if (render(e, s, false))
      break;
  }
}
static bool edit(pos_editor_t *e) {
  screen_t *screen = calloc(1, sizeof(*screen));
  if (!screen)
    return false;
  screen->top = e->cursor;
  bool leave = false, home = false;
  bool redraw = true, queued = false;
  pos_event_t pending = {0};
  while (!leave) {
    if (redraw && render(e, screen, true)) {
      pos_ui_notice("Read failed", "Original and working files remain on SD.");
      break;
    }
    redraw = false;
    pos_event_t event;
    if (queued) {
      event = pending;
      queued = false;
    } else if (!pos_device_event(&event, 100) || !event.pressed)
      continue;
    redraw = true;
    if (event.key == POS_KEY_HOME) {
      leave = close_document(e);
      home = leave;
      continue;
    }
    if (event.key == POS_KEY_ESCAPE) {
      const char *items[] = {"Continue editing",
                             "Save",
                             "Save As",
                             "Share saved file",
                             "Find text",
                             "Go to line",
                             "Start of document",
                             "End of document",
                             "Close document",
                             "Undo",
                             "Redo",
                             "Select all",
                             "Copy selection",
                             "Cut selection",
                             "Paste",
                             "Page up",
                             "Page down",
                             "Controls help",
                             "Delete document"};
      int choice = pos_ui_menu("Text editor", items, 19);
      if (choice == 18) {
        const char *confirm[] = {"Cancel", "Move document to Trash"};
        pos_ui_notice(
            "Delete document",
            "The saved document moves to Trash. Unsaved edits will be "
            "discarded only after the move succeeds. Restore it using Files.");
        if (pos_ui_menu("Delete this document?", confirm, 2) == 1) {
          pos_error_t error = {0};
          if (pos_editor_delete(e, &error))
            pos_ui_notice("Document kept", error.message);
          else {
            leave = true;
            continue;
          }
        }
      }
      if (choice == 1)
        save_as(e, false);
      if (choice == 2)
        save_as(e, true);
      if (choice == 3) {
        if ((!e->dirty && e->original_exists) || save_as(e, false))
          pos_ui_share(e->root, e->source);
      }
      if (choice == 4 || choice == 5)
        find_text(e, screen, choice == 5);
      if (choice == 6) {
        e->cursor = 0;
        screen->top = 0;
      }
      if (choice == 7) {
        e->cursor = e->length;
        screen->top = previous_row(e, e->cursor);
      }
      if (choice == 9 || choice == 10) {
        pos_error_t error = {0};
        if (pos_editor_undo(e, choice == 10, &error))
          pos_ui_notice("History", error.message);
        screen->selecting = false;
        screen->top = previous_row(e, e->cursor);
      }
      if (choice == 11) {
        screen->selecting = true;
        screen->anchor = 0;
        e->cursor = e->length;
        screen->top = previous_row(e, e->cursor);
      }
      if (choice >= 12 && choice <= 14)
        clipboard(e, screen, choice == 12 ? 'c' : choice == 13 ? 'x' : 'v');
      if (choice == 15 || choice == 16)
        page_move(e, screen, choice == 15 ? -1 : 1);
      if (choice == 17)
        pos_ui_notice("Editor controls",
                      "Shift + arrows: select. Ctrl+A: select all. Ctrl+C/X/V: "
                      "copy/cut/paste. Ctrl+Z: undo; Ctrl+Y or Ctrl+Shift+Z: "
                      "redo (32 edits each). Ctrl+arrows: words/document ends. "
                      "Ctrl+U/D: page up/down. Ctrl+L/E: line start/end. "
                      "Ctrl+F/G: find/go to line. Ctrl+S: save. Clipboard and "
                      "history are SD-backed and last for this open document.");
      if (choice == 8 || pos_home_pending()) {
        bool requested = pos_home_pending();
        leave = close_document(e);
        if (leave && requested)
          home = true;
      }
      continue;
    }
    if (event.key >= POS_KEY_UP && event.key <= POS_KEY_RIGHT) {
      if (event.modifiers & POS_MOD_SHIFT) {
        if (!screen->selecting)
          screen->anchor = e->cursor;
        screen->selecting = true;
      } else
        screen->selecting = false;
      move_cursor(e, screen, event.key, event.modifiers & POS_MOD_CTRL);
      continue;
    }
    if (event.modifiers & POS_MOD_CTRL) {
      int key = tolower((unsigned char)event.key);
      if (key == 's')
        save_as(e, false);
      if (key == 'f' || key == 'g')
        find_text(e, screen, key == 'g');
      if (key == 'a') {
        screen->anchor = 0;
        screen->selecting = true;
        e->cursor = e->length;
        screen->top = previous_row(e, e->cursor);
      }
      if (key == 'z' || key == 'y') {
        pos_error_t error = {0};
        if (pos_editor_undo(e, key == 'y' || (event.modifiers & POS_MOD_SHIFT),
                            &error))
          pos_ui_notice("History", error.message);
        screen->selecting = false;
        screen->top = previous_row(e, e->cursor);
      }
      if (key == 'c' || key == 'x' || key == 'v')
        clipboard(e, screen, key);
      if (key == 'u' || key == 'd') {
        screen->selecting = false;
        page_move(e, screen, key == 'u' ? -1 : 1);
      }
      if (key == 'l' || key == 'e') {
        screen->selecting = false;
        while (key == 'l' ? e->cursor > 0 : e->cursor < e->length) {
          uint32_t at = key == 'l' ? previous(e, e->cursor) : e->cursor;
          char c;
          size_t n;
          if (pos_editor_read(e, at, &c, 1, &n) || !n || c == '\n' || c == '\r')
            break;
          e->cursor = key == 'l' ? at : next(e, e->cursor);
        }
        screen->top = previous_row(e, e->cursor);
      }
      if (pos_home_pending()) {
        leave = close_document(e);
        home = leave;
      }
      continue;
    }
    pos_error_t error = {0};
    uint32_t start, count;
    selection(e, screen, &start, &count);
    uint32_t old_cursor = e->cursor;
    bool editing = false;
    int result = 0;
    if (event.key == POS_KEY_BACKSPACE && (count || e->cursor)) {
      if (!count) {
        start = previous(e, e->cursor);
        count = e->cursor - start;
      }
      e->cursor = start;
      editing = true;
      result = pos_editor_replace(e, count, "", 0, &error);
    } else if (event.key == POS_KEY_DELETE &&
               (count || e->cursor < e->length)) {
      if (!count)
        count = next(e, e->cursor) - e->cursor;
      e->cursor = start;
      editing = true;
      result = pos_editor_replace(e, count, "", 0, &error);
    } else if (event.key == POS_KEY_ENTER || event.key == '\t' ||
               (event.key >= 32 && event.key <= 126 &&
                !(event.modifiers & POS_MOD_ALT))) {
      e->cursor = start;
      editing = true;
      char c = event.key;
      const char *text = event.key == POS_KEY_ENTER ? "\n"
                         : event.key == '\t'        ? "    "
                                                    : &c;
      char burst[64];
      size_t used = event.key == '\t' ? 4 : 1;
      memcpy(burst, text, used);
      /* Commit already-queued typing in one durable transaction. Retain the
       * first command/navigation event instead of dropping it or reordering. */
      while (used < sizeof(burst)) {
        pos_event_t next_event;
        if (!pos_device_event(&next_event, 0))
          break;
        if (!next_event.pressed)
          continue;
        if (next_event.key >= 32 && next_event.key <= 126 &&
            !(next_event.modifiers & (POS_MOD_CTRL | POS_MOD_ALT)))
          burst[used++] = (char)next_event.key;
        else {
          pending = next_event;
          queued = true;
          break;
        }
      }
      result = pos_editor_replace(e, count, burst, used, &error);
    }
    if (editing) {
      if (result)
        e->cursor = old_cursor;
      else
        screen->selecting = false;
    }
    if (result)
      pos_ui_notice("Edit not committed", error.message);
  }
  free(screen);
  return home;
}
void pos_ui_editor_open(const char *root, const char *relative) {
  pos_editor_t *editor = calloc(1, sizeof(*editor));
  if (!editor)
    return;
  pos_error_t error = {0};
  if (pos_editor_open(editor, root, relative, progress, NULL, &error))
    pos_ui_notice("Cannot open document", error.message);
  else if (edit(editor))
    pos_home_request();
  free(editor);
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
