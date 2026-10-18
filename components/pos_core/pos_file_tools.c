#include "pos_files.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int error(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static bool inside(const char *a, const char *b) {
  size_t n = strlen(a);
  return !strncasecmp(a, b, n) && (!b[n] || b[n] == '/');
}
int pos_files_paste(const char *root, pos_file_clipboard_t *c,
                    const char *folder, pos_io_progress_t progress, void *ctx,
                    pos_error_t *e) {
  if (!c || !c->count || c->count > POS_FILE_SELECTION_MAX ||
      (*folder && (!pos_fs_archive_path(folder) || !pos_fs_user_path(folder))))
    return error(e, "Clipboard or destination is invalid.");
  char target[241], full[POS_FS_PATH], source[POS_FS_PATH];
  struct stat st;
  /* Preflight the whole batch before changing anything. */
  for (unsigned i = 0; i < c->count; ++i) {
    const char *a = c->paths[i], *name = strrchr(a, '/');
    name = name ? name + 1 : a;
    if (!pos_files_mutable(root, a) || pos_fs_join(source, root, a) ||
        pos_fs_lstat(source, &st) ||
        (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) ||
        snprintf(target, sizeof(target), "%s%s%s", folder, *folder ? "/" : "",
                 name) >= (int)sizeof(target) ||
        !pos_files_mutable(root, target) || inside(a, target) ||
        pos_fs_join(full, root, target))
      return error(
          e, "Protected, missing or nested source/destination. Nothing moved.");
    if (!pos_fs_lstat(full, &st) || errno != ENOENT)
      return error(e, "A destination already exists. Nothing overwritten.");
    for (unsigned j = 0; j < i; ++j) {
      const char *other = strrchr(c->paths[j], '/');
      other = other ? other + 1 : c->paths[j];
      if (!strcasecmp(name, other) || inside(a, c->paths[j]) ||
          inside(c->paths[j], a))
        return error(e,
                     "Selection contains duplicate names or nested folders.");
    }
  }
  unsigned completed = 0, original = c->count;
  while (completed < original) {
    const char *a = c->paths[c->moving ? 0 : completed],
               *name = strrchr(a, '/');
    name = name ? name + 1 : a;
    snprintf(target, sizeof(target), "%s%s%s", folder, *folder ? "/" : "",
             name);
    int bad = c->moving ? pos_files_move(root, a, target, e)
                        : pos_files_copy(root, a, target, progress, ctx, e);
    if (bad) {
      char reason[sizeof(e->message)];
      snprintf(reason, sizeof(reason), "%s", e ? e->message : "");
      if (e)
        snprintf(e->message, sizeof(e->message), "%u/%u completed. %.160s",
                 completed, original, reason);
      return -1;
    }
    ++completed;
    if (c->moving) {
      --c->count;
      memmove(c->paths, c->paths + 1, c->count * sizeof(c->paths[0]));
    }
  }
  return 0;
}
static bool contains(const unsigned char *p, size_t n, const char *q) {
  size_t k = strlen(q);
  for (size_t i = 0; i + k <= n; ++i) {
    size_t j = 0;
    while (j < k && tolower(p[i + j]) == tolower((unsigned char)q[j]))
      ++j;
    if (j == k)
      return true;
  }
  return false;
}
static bool text_file(const char *p) {
  const char *s = strrchr(p, '.');
  return !s || !strcasecmp(s, ".txt") || !strcasecmp(s, ".md") ||
         !strcasecmp(s, ".csv") || !strcasecmp(s, ".log") ||
         !strcasecmp(s, ".json") || !strcasecmp(s, ".calc");
}
typedef struct {
  const char *root, *query;
  bool contents;
  pos_file_match_t match;
  void *ctx;
  pos_io_progress_t progress;
  uint64_t scanned;
  unsigned found;
  pos_error_t *error;
  unsigned char buffer[1024 + 80];
} search_t;
static int scan(search_t *s, const char *relative, unsigned depth) {
  if (depth > 16)
    return 0;
  char *path = malloc(POS_FS_PATH), *child = malloc(241);
  if (!path || !child) {
    free(path);
    free(child);
    return error(s->error, "Not enough memory for search.");
  }
  int result = 0;
  if (pos_fs_join(path, s->root, relative)) {
    result = -1;
    goto done;
  }
  DIR *d = opendir(path);
  if (!d) {
    result = error(s->error, "A searched folder is unreadable.");
    goto done;
  }
  struct dirent *entry;
  errno = 0;
  while ((entry = readdir(d))) {
    if (entry->d_name[0] == '.')
      continue;
    if (snprintf(child, 241, "%s%s%s", relative, *relative ? "/" : "",
                 entry->d_name) >= 241)
      continue;
    bool container = !strcasecmp(child, "POS") ||
                     !strcasecmp(child, "POS/Users") ||
                     !strcasecmp(child, "POS/Users/Owner");
    if (!container && !pos_files_mutable(s->root, child))
      continue;
    if (pos_fs_join(path, s->root, child)) {
      result = -1;
      break;
    }
    struct stat st;
    if (pos_fs_lstat(path, &st)) {
      result = error(s->error, "File disappeared during search.");
      break;
    }
    if (S_ISDIR(st.st_mode)) {
      if (!container && contains((const unsigned char *)entry->d_name,
                                 strlen(entry->d_name), s->query)) {
        ++s->found;
        if (s->match(s->ctx, child, false)) {
          result = 1;
          break;
        }
      }
      result = scan(s, child, depth + 1);
      if (result)
        break;
    } else if (S_ISREG(st.st_mode)) {
      bool matched = contains((const unsigned char *)entry->d_name,
                              strlen(entry->d_name), s->query),
           body = false;
      if (!matched && s->contents && text_file(entry->d_name)) {
        FILE *f = fopen(path, "rb");
        if (!f) {
          result = error(s->error, "Cannot read searched document.");
          break;
        }
        size_t tail = 0, n;
        while ((n = fread(s->buffer + tail, 1, 1024, f))) {
          s->scanned += n;
          if (contains(s->buffer, n + tail, s->query)) {
            body = matched = true;
            break;
          }
          size_t keep = strlen(s->query) - 1;
          if (keep > n + tail)
            keep = n + tail;
          memmove(s->buffer, s->buffer + n + tail - keep, keep);
          tail = keep;
          if (s->progress && s->progress(s->ctx, "Searching document contents",
                                         s->scanned, 0)) {
            result = 1;
            break;
          }
        }
        if (ferror(f))
          result = error(s->error, "Document read failed.");
        fclose(f);
        if (result)
          break;
      }
      if (matched) {
        ++s->found;
        if (s->match(s->ctx, child, body)) {
          result = 1;
          break;
        }
      }
    }
    if (s->progress && s->progress(s->ctx, "Searching files", s->scanned, 0)) {
      result = 1;
      break;
    }
    errno = 0;
  }
  if (!entry && errno)
    result = error(s->error, "Directory read failed during search.");
  closedir(d);
done:
  free(path);
  free(child);
  return result;
}
int pos_files_search(const char *root, const char *folder, const char *query,
                     bool contents, pos_file_match_t match,
                     pos_io_progress_t progress, void *ctx, pos_error_t *e) {
  if (!root || !folder || !query || !*query || strlen(query) > 80 || !match ||
      (*folder && (!pos_fs_archive_path(folder) || !pos_fs_user_path(folder))))
    return error(e, "Search needs a user folder and 1-80 characters.");
  search_t *s = calloc(1, sizeof(*s));
  if (!s)
    return error(e, "Not enough memory for search.");
  *s = (search_t){.root = root,
                  .query = query,
                  .contents = contents,
                  .match = match,
                  .ctx = ctx,
                  .progress = progress,
                  .error = e};
  int result = scan(s, folder, 0);
  free(s);
  return result;
}
