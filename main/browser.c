#include "pos_fs.h"
#include "ui.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

enum { PAGE_FILES = 12 };
typedef struct {
  uint32_t page, pages, total, count;
  char names[PAGE_FILES][256];
} directory_page_t;

/* Two bounded passes give an accurate page count without retaining all names.
 */
static int read_page(const char *path, directory_page_t *p) {
  DIR *dir = opendir(path);
  if (!dir)
    return -1;
  p->total = p->count = 0;
  struct dirent *entry;
  errno = 0;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    if (p->total == UINT32_MAX) {
      closedir(dir);
      return -1;
    }
    ++p->total;
    errno = 0;
  }
  int bad = errno;
  closedir(dir);
  if (bad)
    return -1;
  p->pages = p->total ? (p->total - 1) / PAGE_FILES + 1 : 1;
  if (p->page >= p->pages)
    p->page = p->pages - 1;
  dir = opendir(path);
  if (!dir)
    return -1;
  uint32_t skipped = 0;
  errno = 0;
  while (p->count < PAGE_FILES && (entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    if (skipped++ < p->page * PAGE_FILES)
      continue;
    snprintf(p->names[p->count++], sizeof(p->names[0]), "%s", entry->d_name);
    errno = 0;
  }
  bad = errno;
  closedir(dir);
  return bad ? -1 : 0;
}

void pos_ui_browse(const char *root, const char *initial) {
  char relative[256];
  snprintf(relative, sizeof(relative), "%s", initial);
  directory_page_t page = {0};
  for (;;) {
    char path[POS_FS_PATH];
    if (pos_fs_join(path, root, relative))
      return;
    if (read_page(path, &page)) {
      pos_ui_notice("Files", "Cannot open directory. Files were not changed.");
      return;
    }
    const char *items[3 + PAGE_FILES] = {
        ".. (parent)", page.page ? "< Previous page" : "Previous page (first)",
        page.page + 1 < page.pages ? "Next page >" : "Next page (last)"};
    for (unsigned i = 0; i < page.count; ++i)
      items[i + 3] = page.names[i];
    int selected = pos_ui_menu_page(*relative ? relative : "SD files", items,
                                    3 + page.count, page.page, page.pages,
                                    page.total ? NULL : "Empty folder");
    if (selected < 0)
      return;
    if (!selected) {
      page.page = 0;
      char *slash = strrchr(relative, '/');
      if (slash)
        *slash = 0;
      else
        *relative = 0;
      continue;
    }
    if (selected == 1) {
      if (page.page)
        --page.page;
      continue;
    }
    if (selected == 2) {
      if (page.page + 1 < page.pages)
        ++page.page;
      continue;
    }
    char next[256];
    if (snprintf(next, sizeof(next), "%s%s%s", relative, *relative ? "/" : "",
                 items[selected]) >= (int)sizeof(next))
      continue;
    if (pos_fs_join(path, root, next))
      continue;
    struct stat st;
    if (pos_fs_lstat(path, &st))
      continue;
    if (S_ISDIR(st.st_mode)) {
      strcpy(relative, next);
      page.page = 0;
      continue;
    }
    if (!S_ISREG(st.st_mode))
      continue;
    FILE *f = fopen(path, "rb");
    if (!f)
      continue;
    char text[361];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    int bad = ferror(f);
    if (fclose(f))
      bad = 1;
    if (bad) {
      pos_ui_notice("Files", "Could not read this file.");
      continue;
    }
    for (size_t i = 0; i < n; ++i)
      if ((unsigned char)text[i] < 32 && text[i] != '\n')
        text[i] = '.';
    text[n] = 0;
    pos_ui_notice(items[selected], *text ? text : "Empty file");
  }
}
