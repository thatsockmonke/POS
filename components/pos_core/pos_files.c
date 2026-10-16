#include "pos_files.h"
#include "cJSON.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int fail(pos_error_t *e, const char *text) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return -1;
}
bool pos_files_mutable(const char *root, const char *relative) {
  if (!root || !pos_fs_archive_path(relative) || !pos_fs_user_path(relative) ||
      !strncasecmp(relative, "POS/Users/settings.", 19) ||
      !strcasecmp(relative, "POS") || !strcasecmp(relative, "POS/Users") ||
      !strcasecmp(relative, "POS/Users/Owner") ||
      !strcasecmp(relative, POS_TRASH))
    return false;
  char full[POS_FS_PATH];
  if (pos_fs_join(full, root, relative))
    return false;
  size_t start = strlen(root) + 1;
  for (char *p = full + start;; ++p)
    if (*p == '/' || !*p) {
      char saved = *p;
      *p = 0;
      struct stat st;
      int result = pos_fs_lstat(full, &st);
      *p = saved;
      if (!result && (!S_ISDIR(st.st_mode) && (saved || !S_ISREG(st.st_mode))))
        return false;
      if (result && errno != ENOENT)
        return false;
      if (!saved)
        break;
    }
  return true;
}
static int paths(const char *root, const char *a, const char *b,
                 char x[POS_FS_PATH], char y[POS_FS_PATH], pos_error_t *e) {
  size_t n = strlen(a);
  if (!pos_files_mutable(root, a) || !pos_files_mutable(root, b) ||
      !strcasecmp(a, b) || (!strncasecmp(a, b, n) && b[n] == '/') ||
      pos_fs_join(x, root, a) || pos_fs_join(y, root, b))
    return fail(e, "Choose different user paths; system data and nested "
                   "destinations are protected.");
  struct stat st;
  if (!pos_fs_lstat(y, &st) || errno != ENOENT)
    return fail(e, "Destination exists or is unavailable. Choose a new name.");
  return 0;
}
typedef struct {
  char x[POS_FS_PATH], y[POS_FS_PATH], staged[POS_FS_PATH], ra[241], rb[241];
} copy_work_t;
typedef struct {
  pos_io_progress_t callback;
  void *ctx;
} copy_progress_t;
static int checking_copy(void *opaque, uint64_t done, uint64_t total) {
  copy_progress_t *p = opaque;
  return p->callback ? p->callback(p->ctx, "Verifying copied file", done, total)
                     : 0;
}
static int copy_tree(const char *root, const char *a, const char *b,
                     unsigned depth, pos_io_progress_t progress, void *ctx,
                     pos_error_t *e) {
  if (depth > 16)
    return fail(e, "Folder depth exceeds 16; partial copy retained.");
  copy_work_t *w = calloc(1, sizeof(*w));
  if (!w)
    return fail(e, "Not enough memory to copy; source retained.");
  int result = -1;
  if (paths(root, a, b, w->x, w->y, e))
    goto done;
  struct stat st;
  if (pos_fs_lstat(w->x, &st)) {
    fail(e, "Source unavailable; nothing was removed.");
    goto done;
  }
  if (S_ISDIR(st.st_mode)) {
    if (mkdir(w->y, 0775)) {
      fail(e, "Cannot create destination folder.");
      goto done;
    }
    DIR *dir = opendir(w->x);
    if (!dir) {
      fail(e, "Cannot read source folder.");
      goto done;
    }
    struct dirent *entry;
    result = 0;
    errno = 0;
    while ((entry = readdir(dir))) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
        continue;
      if (snprintf(w->ra, sizeof(w->ra), "%s/%s", a, entry->d_name) >=
              (int)sizeof(w->ra) ||
          snprintf(w->rb, sizeof(w->rb), "%s/%s", b, entry->d_name) >=
              (int)sizeof(w->rb) ||
          copy_tree(root, w->ra, w->rb, depth + 1, progress, ctx, e)) {
        result = -1;
        break;
      }
      errno = 0;
    }
    if (errno && !entry)
      result = fail(e, "Directory read failed. Partial destination retained.");
    closedir(dir);
    goto done;
  }
  uint32_t size;
  if (!S_ISREG(st.st_mode) || pos_fs_file_size(w->x, &size)) {
    fail(e, "Choose a regular file or folder.");
    goto done;
  }
  if (snprintf(w->staged, sizeof(w->staged), "%s.pos-copy.part", w->y) >=
      (int)sizeof(w->staged))
    goto done;
  FILE *in = fopen(w->x, "rb"), *out = fopen(w->staged, "wbx");
  unsigned char *buffer = malloc(4096);
  int bad = !in || !out || !buffer;
  uint64_t total = 0;
  uint32_t crc = 0;
  while (!bad) {
    size_t n = fread(buffer, 1, 4096, in);
    if (!n) {
      bad = ferror(in);
      break;
    }
    total += n;
    crc = pos_crc32(crc, buffer, n);
    if (total > size || fwrite(buffer, 1, n, out) != n ||
        (progress && progress(ctx, "Copying files", total, size)))
      bad = 1;
  }
  free(buffer);
  if (in)
    fclose(in);
  if (out) {
    if (fflush(out) || fsync(fileno(out)))
      bad = 1;
    if (fclose(out))
      bad = 1;
  }
  uint32_t check;
  copy_progress_t verification = {progress, ctx};
  if (total != size ||
      (!bad && (pos_fs_crc_file_progress(w->staged, size, &check, checking_copy,
                                         &verification) ||
                check != crc)))
    bad = 1;
  if (bad || pos_fs_publish_new(w->staged, w->y)) {
    fail(e, "Copy incomplete. Source untouched; partial destination retained.");
    goto done;
  }
  result = 0;
done:
  free(w);
  return result;
}
int pos_files_copy(const char *root, const char *a, const char *b,
                   pos_io_progress_t p, void *ctx, pos_error_t *e) {
  char source[POS_FS_PATH], target[POS_FS_PATH];
  if (paths(root, a, b, source, target, e))
    return -1;
  struct stat st;
  if (pos_fs_lstat(source, &st))
    return fail(e, "Source unavailable.");
  if (!S_ISDIR(st.st_mode))
    return copy_tree(root, a, b, 0, p, ctx, e);
  char relative[241], staging[POS_FS_PATH];
  bool chosen = false;
  for (unsigned i = 0; i < 100; ++i) {
    if (snprintf(relative, sizeof(relative), "%s.pos-copy-%u.part", b, i) >=
            (int)sizeof(relative) ||
        pos_fs_join(staging, root, relative))
      break;
    if (pos_fs_lstat(staging, &st) && errno == ENOENT) {
      chosen = true;
      break;
    }
  }
  if (!chosen)
    return fail(
        e, "No free folder-copy staging name; choose a shorter destination.");
  if (copy_tree(root, a, relative, 0, p, ctx, e))
    return -1;
  return pos_files_move(root, relative, b, e);
}
int pos_files_move(const char *root, const char *a, const char *b,
                   pos_error_t *e) {
  char x[POS_FS_PATH], y[POS_FS_PATH];
  if (paths(root, a, b, x, y, e))
    return -1;
  return rename(x, y) ? fail(e, "Move failed. Original path retained.") : 0;
}
int pos_files_folder(const char *root, const char *relative, pos_error_t *e) {
  char full[POS_FS_PATH];
  if (!pos_files_mutable(root, relative) || pos_fs_join(full, root, relative) ||
      mkdir(full, 0775))
    return fail(e, "Cannot create folder; choose a new user path.");
  return 0;
}
int pos_files_trash(const char *root, const char *relative, char entry[241],
                    pos_error_t *e) {
  if (!pos_files_mutable(root, relative) ||
      !strncasecmp(relative, POS_TRASH "/", sizeof(POS_TRASH)))
    return fail(e, "This item cannot be moved to Trash.");
  char full[POS_FS_PATH];
  if (pos_fs_join(full, root, POS_TRASH) || pos_fs_mkdirs(full))
    return -1;
  for (unsigned i = 0; i < 100000; ++i) {
    snprintf(entry, 241, POS_TRASH "/item-%u", i);
    pos_fs_join(full, root, entry);
    if (!mkdir(full, 0775))
      break;
    if (errno != EEXIST || i == 99999)
      return fail(e, "No free Trash entry.");
  }
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddStringToObject(j, "original", relative);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!text)
    return -1;
  char metadata[POS_FS_PATH], data[241];
  if (pos_fs_join(metadata, full, "restore.json")) {
    free(text);
    return fail(e, "Trash path is too long; original retained.");
  }
  int result = pos_fs_write_new(metadata, text, strlen(text));
  free(text);
  if (snprintf(data, sizeof(data), "%s/data", entry) >= (int)sizeof(data))
    return -1;
  return result
             ? fail(e,
                    "Trash metadata could not be written; original retained.")
             : pos_files_move(root, relative, data, e);
}
int pos_files_restore(const char *root, const char *entry, pos_error_t *e) {
  if (strncmp(entry, POS_TRASH "/item-", sizeof(POS_TRASH "/item-") - 1) ||
      strchr(entry + sizeof(POS_TRASH "/item-") - 1, '/'))
    return fail(e, "Choose a Trash entry.");
  char path[POS_FS_PATH], relative[241], text[513];
  snprintf(relative, sizeof(relative), "%s/restore.json", entry);
  if (pos_fs_join(path, root, relative))
    return -1;
  FILE *f = fopen(path, "rb");
  if (!f)
    return fail(e, "Trash metadata unavailable.");
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  int bad = ferror(f) || !feof(f);
  fclose(f);
  text[n] = 0;
  if (bad || !pos_fs_json_safe(text, n))
    return fail(e, "Invalid Trash metadata; data preserved.");
  cJSON *j = cJSON_Parse(text),
        *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *original = cJSON_GetObjectItemCaseSensitive(j, "original");
  int result = -1;
  if (cJSON_IsNumber(format) && format->valuedouble == 1 &&
      cJSON_IsString(original)) {
    snprintf(relative, sizeof(relative), "%s/data", entry);
    result = pos_files_move(root, relative, original->valuestring, e);
  } else
    fail(e, "Invalid Trash metadata; data preserved.");
  cJSON_Delete(j);
  return result;
}
typedef struct {
  char full[POS_FS_PATH], child[241];
} remove_work_t;
static int remove_tree(const char *root, const char *relative, unsigned depth,
                       pos_io_progress_t progress, void *ctx, pos_error_t *e) {
  if (depth > 16 || !pos_files_mutable(root, relative))
    return fail(
        e, "Protected or invalid deletion path. Remaining files preserved.");
  remove_work_t *w = malloc(sizeof(*w));
  if (!w)
    return fail(e, "Not enough memory; remaining files preserved.");
  int result = -1;
  struct stat st;
  if (pos_fs_join(w->full, root, relative) || pos_fs_lstat(w->full, &st))
    goto done;
  if (progress && progress(ctx, "Deleting permanently", 0, 0))
    goto done;
  if (S_ISREG(st.st_mode)) {
    result = unlink(w->full);
    goto done;
  }
  if (!S_ISDIR(st.st_mode))
    goto done;
  DIR *d = opendir(w->full);
  if (!d)
    goto done;
  struct dirent *entry;
  bool bad = false;
  errno = 0;
  while ((entry = readdir(d))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    if (snprintf(w->child, sizeof(w->child), "%s/%s", relative,
                 entry->d_name) >= (int)sizeof(w->child) ||
        remove_tree(root, w->child, depth + 1, progress, ctx, e)) {
      bad = true;
      break;
    }
    errno = 0;
  }
  if (!entry && errno)
    bad = true;
  closedir(d);
  if (!bad)
    result = rmdir(w->full);
done:
  free(w);
  if (result && e && !*e->message)
    fail(e, "Deletion incomplete or cancelled. Remaining files preserved.");
  return result;
}
int pos_files_remove(const char *root, const char *relative,
                     pos_io_progress_t progress, void *ctx, pos_error_t *e) {
  return remove_tree(root, relative, 0, progress, ctx, e);
}
