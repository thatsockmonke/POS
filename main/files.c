#include "pos_device.h"
#include "pos_files.h"
#include "pos_image.h"
#include "pos_music.h"
#include "ui.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char folder[241], selected[241], full[POS_FS_PATH];
  unsigned page, pages, count, total;
  pos_file_clipboard_t *batch;
  char (*checked)[241], labels[12][260];
  unsigned checked_count;
  int cursor;
} files_ui_t;
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  (void)ctx;
  return pos_ui_progress(NULL, phase, "", done, total);
}
static const char *basename_of(const char *p) {
  const char *slash = strrchr(p, '/');
  return slash ? slash + 1 : p;
}
static int listing(const char *root, files_ui_t *f) {
  if (pos_fs_join(f->full, root, f->folder))
    return -1;
  DIR *d = opendir(f->full);
  if (!d)
    return -1;
  f->total = f->count = 0;
  struct dirent *entry;
  errno = 0;
  while ((entry = readdir(d)))
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
      ++f->total;
  int bad = errno;
  closedir(d);
  if (bad)
    return -1;
  f->pages = f->total ? (f->total - 1) / 12 + 1 : 1;
  if (f->page >= f->pages)
    f->page = f->pages - 1;
  d = opendir(f->full);
  if (!d)
    return -1;
  unsigned index = 0;
  errno = 0;
  while (f->count < 12 && (entry = readdir(d)))
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
      if (index++ >= f->page * 12)
        snprintf(f->labels[f->count++] + 4, 256, "%s", entry->d_name);
      errno = 0;
    }
  bad = errno;
  closedir(d);
  return bad ? -1 : 0;
}
static void result(int status, const pos_error_t *e) {
  pos_ui_notice(
      status ? "Operation incomplete" : "Done",
      status
          ? (*e->message ? e->message
                         : "Files were preserved; check paths and free space.")
          : "Operation complete. Existing unrelated files were preserved.");
}
static void details(const char *root, const char *relative) {
  char path[POS_FS_PATH], text[400];
  struct stat st;
  uint32_t size = 0;
  if (pos_fs_join(path, root, relative) || pos_fs_lstat(path, &st))
    return;
  bool folder = S_ISDIR(st.st_mode);
  char size_text[64] = "folder (not scanned)";
  if (!folder) {
    if (pos_fs_file_size(path, &size))
      snprintf(size_text, sizeof(size_text), "unavailable");
    else
      snprintf(size_text, sizeof(size_text), "%u bytes", (unsigned)size);
  }
  snprintf(text, sizeof(text), "%s\n%s\nSize: %s\n%s", relative,
           folder ? "Folder" : "File", size_text,
           pos_files_mutable(root, relative) ? "User-owned data"
                                             : "Protected system/container");
  pos_ui_notice("Details", text);
}
static int checked(files_ui_t *f, const char *p) {
  for (unsigned i = 0; i < f->checked_count; ++i)
    if (!strcmp(f->checked[i], p))
      return (int)i;
  return -1;
}
static void toggle(files_ui_t *f, const char *p) {
  int i = checked(f, p);
  if (i >= 0) {
    --f->checked_count;
    memmove(f->checked + i, f->checked + i + 1,
            (f->checked_count - (unsigned)i) * 241);
    if (!f->checked_count) {
      free(f->checked);
      f->checked = NULL;
    }
  } else if (f->checked_count < POS_FILE_SELECTION_MAX) {
    if (!f->checked)
      f->checked = malloc(POS_FILE_SELECTION_MAX * sizeof(*f->checked));
    if (!f->checked) {
      pos_ui_notice("Selection unavailable", "Not enough memory.");
      return;
    }
    strcpy(f->checked[f->checked_count++], p);
  } else
    pos_ui_notice("Selection full", "Select up to 32 items per batch.");
}
static void clipboard(const char *root, files_ui_t *f, bool moving) {
  unsigned n = f->checked_count ? f->checked_count : 1;
  for (unsigned i = 0; i < n; ++i)
    if (!pos_files_mutable(root,
                           f->checked_count ? f->checked[i] : f->selected)) {
      pos_ui_notice("Protected data", "Choose user files or folders.");
      return;
    }
  if (!f->batch)
    f->batch = malloc(sizeof(*f->batch));
  if (!f->batch) {
    pos_ui_notice("Clipboard unavailable",
                  "Not enough memory; files unchanged.");
    return;
  }
  f->batch->count = n;
  f->batch->moving = moving;
  for (unsigned i = 0; i < n; ++i)
    strcpy(f->batch->paths[i], f->checked_count ? f->checked[i] : f->selected);
  f->checked_count = 0;
  free(f->checked);
  f->checked = NULL;
  pos_ui_notice(moving ? "Cut ready" : "Copy ready",
                "Go to the destination, then Ctrl+V or Folder actions > Paste. "
                "Originals stay until pasted. Existing names are protected.");
}
static void paste(const char *root, files_ui_t *f) {
  pos_error_t e = {0};
  result(pos_files_paste(root, f->batch, f->folder, progress, NULL, &e), &e);
  if (f->batch && !f->batch->count) {
    free(f->batch);
    f->batch = NULL;
  }
}
typedef struct {
  char paths[64][241];
  unsigned count;
} results_t;
static int match(void *ctx, const char *p, bool body) {
  (void)body;
  results_t *r = ctx;
  strcpy(r->paths[r->count++], p);
  return r->count == 64;
}
static void search(const char *root, files_ui_t *f) {
  char query[81] = "";
  if (!pos_ui_text("Search files", "Name or document text", query,
                   sizeof(query), false) ||
      !*query)
    return;
  const char *modes[] = {"Filenames", "Filenames + text contents", "Cancel"};
  int mode = pos_ui_menu("Search this folder recursively", modes, 3);
  if (mode < 0 || mode == 2)
    return;
  results_t *r = calloc(1, sizeof(*r));
  if (!r)
    return;
  pos_error_t e = {0};
  int status = pos_files_search(root, f->folder, query, mode == 1, match,
                                progress, r, &e);
  if (status < 0)
    pos_ui_notice("Search incomplete", e.message);
  if (r->count) {
    const char *items[64];
    for (unsigned i = 0; i < r->count; ++i)
      items[i] = r->paths[i];
    int n = pos_ui_menu(status == 1 ? "Matches (first 64 / stopped)"
                                    : "Search results",
                        items, (int)r->count);
    if (n >= 0) {
      strcpy(f->folder, r->paths[n]);
      char *p = strrchr(f->folder, '/');
      if (p)
        *p = 0;
      else
        *f->folder = 0;
      f->page = 0;
      f->cursor = 0;
    }
  } else
    pos_ui_notice("Search",
                  status ? "Search stopped; no matches yet." : "No matches.");
  free(r);
}
static void actions(const char *root, files_ui_t *f, bool folder) {
  const char *items[] = {folder ? "Open folder"
                         : pos_music_supported(f->selected)
                             ? "Play in Audio Player"
                             : "Open file",
                         "Copy",
                         "Cut / move",
                         "Rename",
                         "Move to Trash",
                         "Share file",
                         "Delete permanently",
                         "Details",
                         "Edit PCM recording",
                         "Back"};
  int choice = pos_ui_menu(basename_of(f->selected), items, 10);
  pos_error_t e = {0};
  if (choice == 0) {
    if (folder) {
      strcpy(f->folder, f->selected);
      f->page = 0;
      f->cursor = 0;
    } else if (strrchr(f->selected, '.') &&
               !strcmp(strrchr(f->selected, '.'), ".calc"))
      pos_ui_calculator_open(root, f->selected);
    else if (pos_image_supported(f->selected))
      pos_ui_image(root, f->selected);
    else if (pos_music_supported(f->selected))
      pos_ui_music_file(root, f->selected);
    else {
      const char *open[] = {"Text Editor", "Preview", "Share", "Back"};
      int selected = pos_ui_menu("Open file", open, 4);
      if (selected == 0)
        pos_ui_editor_open(root, f->selected);
      if (selected == 1) {
        char text[361];
        pos_fs_join(f->full, root, f->selected);
        FILE *in = fopen(f->full, "rb");
        if (in) {
          size_t n = fread(text, 1, sizeof(text) - 1, in);
          fclose(in);
          for (size_t i = 0; i < n; ++i)
            if ((unsigned char)text[i] < 32 && text[i] != '\n')
              text[i] = '.';
          text[n] = 0;
          pos_ui_notice(basename_of(f->selected), n ? text : "Empty file");
        }
      }
      if (selected == 2)
        pos_ui_share(root, f->selected);
    }
  }
  if (choice == 1 || choice == 2)
    clipboard(root, f, choice == 2);
  if (choice == 3) {
    char name[121];
    snprintf(name, sizeof(name), "%.120s", basename_of(f->selected));
    if (pos_ui_text("Rename", "New filename", name, sizeof(name), false)) {
      char dest[241], parent[241];
      strcpy(parent, f->selected);
      char *slash = strrchr(parent, '/');
      if (slash)
        *slash = 0;
      else
        *parent = 0;
      if (strchr(name, '/') ||
          snprintf(dest, sizeof(dest), "%s%s%s", parent, *parent ? "/" : "",
                   name) >= (int)sizeof(dest))
        pos_ui_notice("Invalid name", "Use a simple filename.");
      else
        result(pos_files_move(root, f->selected, dest, &e), &e);
    }
  }
  if (choice == 4) {
    const char *yes[] = {"Cancel", "Move to Trash"};
    if (pos_ui_menu("Keep a recoverable copy?", yes, 2) == 1) {
      char entry[241];
      int status = pos_files_trash(root, f->selected, entry, &e);
      if (!status)
        pos_ui_notice("Moved to Trash", entry);
      else
        result(status, &e);
    }
  }
  if (choice == 5) {
    if (folder)
      pos_ui_notice("Choose a file", "Share transfers one file. Copy a folder "
                                     "locally, or choose a file inside it.");
    else
      pos_ui_share(root, f->selected);
  }
  if (choice == 6) {
    char confirm[7] = "";
    if (pos_ui_text("Permanent deletion", "Type DELETE; cannot undo", confirm,
                    sizeof(confirm), false) &&
        !strcmp(confirm, "DELETE"))
      result(pos_files_remove(root, f->selected, progress, NULL, &e), &e);
  }
  if (choice == 7)
    details(root, f->selected);
  if (choice == 8 && !folder)
    pos_ui_recording_edit(root, f->selected);
}
static void folder_actions(const char *root, files_ui_t *f) {
  const char *items[] = {"New folder",
                         "Paste clipboard",
                         "This folder actions",
                         "Open Trash",
                         "Restore this Trash item",
                         "SD root",
                         "Recordings",
                         "Search",
                         "Selection / shortcuts",
                         "Back"};
  int choice = pos_ui_menu("Folder actions", items, 10);
  pos_error_t e = {0};
  if (choice == 0) {
    char name[121] = "New folder";
    if (pos_ui_text("Create folder", "Folder name", name, sizeof(name),
                    false)) {
      char relative[241];
      if (strchr(name, '/') ||
          snprintf(relative, sizeof(relative), "%s%s%s", f->folder,
                   *f->folder ? "/" : "", name) >= (int)sizeof(relative))
        pos_ui_notice("Invalid name", "Use a simple folder name.");
      else
        result(pos_files_folder(root, relative, &e), &e);
    }
  }
  if (choice == 1)
    paste(root, f);
  if (choice == 7)
    search(root, f);
  if (choice == 8) {
    const char *ops[] = {"Copy selected", "Cut selected", "Clear selection",
                         "Controls", "Back"};
    int op = pos_ui_menu("Selection", ops, 5);
    if (op < 2 && op >= 0) {
      if (f->checked_count)
        clipboard(root, f, op == 1);
      else
        pos_ui_notice("No selection", "Space toggles a checkbox; Ctrl+A "
                                      "selects this folder (up to 32). ");
    }
    if (op == 2) {
      f->checked_count = 0;
      free(f->checked);
      f->checked = NULL;
    }
    if (op == 3)
      pos_ui_notice("Files controls",
                    "Space: checkbox. Ctrl+A: select folder. Ctrl+C: copy. "
                    "Ctrl+X: cut. Ctrl+V: paste. Ctrl+F: search. Enter: file "
                    "actions. Cut/paste never overwrites existing names.");
  }
  if (choice == 2) {
    strcpy(f->selected, f->folder);
    actions(root, f, true);
  }
  if (choice == 3) {
    char path[POS_FS_PATH];
    pos_fs_join(path, root, POS_TRASH);
    if (pos_fs_mkdirs(path))
      pos_ui_notice("Trash unavailable", "Check storage.");
    else {
      strcpy(f->folder, POS_TRASH);
      f->page = 0;
    }
  }
  if (choice == 4) {
    int status = pos_files_restore(root, f->folder, &e);
    result(status, &e);
    if (!status) {
      strcpy(f->folder, POS_TRASH);
      f->page = 0;
    }
  }
  if (choice == 6) {
    strcpy(f->folder, "Recordings");
    f->page = 0;
  }
  if (choice == 5) {
    *f->folder = 0;
    f->page = 0;
  }
}
void pos_ui_files_from(const char *root, const char *initial) {
  files_ui_t *f = calloc(1, sizeof(*f));
  if (!f)
    return;
  if (!initial || strlen(initial) >= sizeof(f->folder) ||
      (*initial &&
       (!pos_fs_archive_path(initial) || !pos_fs_user_path(initial)))) {
    free(f);
    return;
  }
  strcpy(f->folder, initial);
  while (!pos_home_pending()) {
    if (listing(root, f)) {
      pos_ui_notice("Cannot open folder", "Files were not changed.");
      *f->folder = 0;
      if (listing(root, f))
        break;
    }
    const char *items[16] = {
        ".. (parent)", f->page ? "< Previous" : "Previous (first)",
        f->page + 1 < f->pages ? "Next >" : "Next (last)", "Folder actions"};
    for (unsigned i = 0; i < f->count; ++i) {
      char p[241];
      int n = snprintf(p, sizeof(p), "%s%s%s", f->folder, *f->folder ? "/" : "",
                       f->labels[i] + 4);
      memcpy(f->labels[i], n < 241 && checked(f, p) >= 0 ? "[x] " : "[ ] ", 4);
      items[i + 4] = f->labels[i];
    }
    char status[64];
    snprintf(status, sizeof(status), "%u checked | Space Ctrl+C/X/V",
             f->checked_count);
    pos_event_t shortcut = {0};
    int choice = pos_ui_files_menu(*f->folder ? f->folder : "SD root", items,
                                   (int)f->count + 4, f->page, f->pages, status,
                                   &f->cursor, &shortcut);
    if (choice == -2) {
      bool valid =
          f->cursor >= 4 && (unsigned)f->cursor < f->count + 4 &&
          snprintf(f->selected, sizeof(f->selected), "%s%s%s", f->folder,
                   *f->folder ? "/" : "", f->labels[f->cursor - 4] + 4) < 241;
      int key = tolower(shortcut.key);
      if (key == ' ' && valid && pos_files_mutable(root, f->selected))
        toggle(f, f->selected);
      if (key == 'f')
        search(root, f);
      if (key == 'v')
        paste(root, f);
      if ((key == 'c' || key == 'x') && (valid || f->checked_count))
        clipboard(root, f, key == 'x');
      if (key == 'a') {
        DIR *d = opendir(f->full);
        struct dirent *ent;
        f->checked_count = 0;
        if (d) {
          while ((ent = readdir(d)) &&
                 f->checked_count < POS_FILE_SELECTION_MAX) {
            char p[241];
            if (ent->d_name[0] == '.')
              continue;
            if (snprintf(p, sizeof(p), "%s%s%s", f->folder,
                         *f->folder ? "/" : "", ent->d_name) < 241 &&
                pos_files_mutable(root, p))
              toggle(f, p);
          }
          closedir(d);
        }
      }
      continue;
    }
    if (choice < 0)
      break;
    if (choice == 0) {
      char *slash = strrchr(f->folder, '/');
      if (slash)
        *slash = 0;
      else
        *f->folder = 0;
      f->page = 0;
      continue;
    }
    if (choice == 1) {
      if (f->page)
        --f->page;
      continue;
    }
    if (choice == 2) {
      if (f->page + 1 < f->pages)
        ++f->page;
      continue;
    }
    if (choice == 3) {
      folder_actions(root, f);
      continue;
    }
    if (snprintf(f->selected, sizeof(f->selected), "%s%s%s", f->folder,
                 *f->folder ? "/" : "",
                 f->labels[choice - 4] + 4) >= (int)sizeof(f->selected))
      continue;
    struct stat st;
    if (pos_fs_join(f->full, root, f->selected) || pos_fs_lstat(f->full, &st) ||
        (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)))
      continue;
    actions(root, f, S_ISDIR(st.st_mode));
  }
  free(f->checked);
  free(f->batch);
  free(f);
}

void pos_ui_files(const char *root) {
  pos_ui_files_from(root, "POS/Users/Owner");
}
