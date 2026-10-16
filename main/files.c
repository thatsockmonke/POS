#include "pos_device.h"
#include "pos_files.h"
#include "pos_music.h"
#include "ui.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char folder[241], clipboard[241], selected[241], full[POS_FS_PATH];
  char names[12][256];
  unsigned page, pages, count, total;
  bool moving;
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
        snprintf(f->names[f->count++], 256, "%s", entry->d_name);
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
static void actions(const char *root, files_ui_t *f, bool folder) {
  const char *items[] = {folder ? "Open folder" : "Open file",
                         "Copy",
                         "Cut / move",
                         "Rename",
                         "Move to Trash",
                         "Share file",
                         "Delete permanently",
                         "Details",
                         "Back"};
  int choice = pos_ui_menu(basename_of(f->selected), items, 9);
  pos_error_t e = {0};
  if (choice == 0) {
    if (folder) {
      strcpy(f->folder, f->selected);
      f->page = 0;
    } else if (pos_music_supported(f->selected))
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
  if (choice == 1 || choice == 2) {
    if (!pos_files_mutable(root, f->selected))
      pos_ui_notice("Protected data", "Choose a user file or folder.");
    else {
      strcpy(f->clipboard, f->selected);
      f->moving = choice == 2;
      pos_ui_notice("Clipboard ready",
                    "Go to the destination folder, then Folder actions > "
                    "Paste. Existing names will not be overwritten.");
    }
  }
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
}
static void folder_actions(const char *root, files_ui_t *f) {
  const char *items[] = {
      "New folder", "Paste clipboard",         "This folder actions",
      "Open Trash", "Restore this Trash item", "SD root",
      "Back"};
  int choice = pos_ui_menu("Folder actions", items, 7);
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
  if (choice == 1) {
    if (!*f->clipboard)
      pos_ui_notice("Clipboard empty",
                    "Choose Copy or Cut on a file/folder first.");
    else {
      char name[241];
      snprintf(name, sizeof(name), "%s", basename_of(f->clipboard));
      if (!pos_ui_text(f->moving ? "Move into this folder"
                                 : "Copy into this folder",
                       "Destination filename", name, sizeof(name), false))
        return;
      if (strchr(name, '/')) {
        pos_ui_notice("Invalid filename",
                      "Use a filename without directories.");
        return;
      }
      char destination[241];
      if (snprintf(destination, sizeof(destination), "%s%s%s", f->folder,
                   *f->folder ? "/" : "", name) >= (int)sizeof(destination))
        return;
      int status = f->moving
                       ? pos_files_move(root, f->clipboard, destination, &e)
                       : pos_files_copy(root, f->clipboard, destination,
                                        progress, NULL, &e);
      if (!status && f->moving)
        *f->clipboard = 0;
      result(status, &e);
    }
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
  if (choice == 5) {
    *f->folder = 0;
    f->page = 0;
  }
}
void pos_ui_files(const char *root) {
  files_ui_t *f = calloc(1, sizeof(*f));
  if (!f)
    return;
  strcpy(f->folder, "POS/Users/Owner");
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
    for (unsigned i = 0; i < f->count; ++i)
      items[i + 4] = f->names[i];
    int choice = pos_ui_menu_page(*f->folder ? f->folder : "SD root", items,
                                  f->count + 4, f->page, f->pages,
                                  f->total ? NULL : "Empty folder");
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
                 items[choice]) >= (int)sizeof(f->selected))
      continue;
    struct stat st;
    if (pos_fs_join(f->full, root, f->selected) || pos_fs_lstat(f->full, &st) ||
        (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)))
      continue;
    actions(root, f, S_ISDIR(st.st_mode));
  }
  free(f);
}
