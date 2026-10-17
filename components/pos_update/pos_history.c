#include "pos_history.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int fail(pos_error_t *e, const char *s) {
  if (e && s != e->message)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static bool id_valid(const char *s) {
  return s && strlen(s) == 64 && strspn(s, "0123456789abcdef") == 64;
}
static int regular(const char *p) {
  struct stat st;
  return pos_fs_lstat(p, &st) || !S_ISREG(st.st_mode) ? -1 : 0;
}
static int checked_path(char out[POS_FS_PATH], const char *root,
                        const char *relative) {
  char p[POS_FS_PATH];
  if (pos_fs_join(p, root, relative))
    return -1;
  strcpy(out, p);
  for (char *s = p + 1; *s; ++s)
    if (*s == '/') {
      *s = 0;
      struct stat st;
      int bad = pos_fs_lstat(p, &st) || !S_ISDIR(st.st_mode);
      *s = '/';
      if (bad)
        return -1;
    }
  return 0;
}
static int verify(const char *p, const pos_file_t *f) {
  char hash[65];
  return regular(p) || pos_hash_file(p, f->size, hash) ||
                 strcmp(hash, f->sha256)
             ? -1
             : 0;
}
static int copy_stream(const char *from, const char *to,
                       const pos_file_t *record, pos_history_read_t read,
                       void *ctx, const pos_platform_t *platform,
                       pos_error_t *e) {
  if (!verify(to, record))
    return 0;
  struct stat st;
  if (!pos_fs_lstat(to, &st))
    return fail(
        e, "Changed history file preserved; current installation untouched.");
  char temp[POS_FS_PATH];
  if (snprintf(temp, sizeof(temp), "%s.part", to) >= (int)sizeof(temp))
    return -1;
  /* An interrupted file is ours only under the matching, signed staging
   * directory. Never replace it: use another bounded attempt filename. */
  FILE *out = NULL;
  for (unsigned i = 0; i < 100; ++i) {
    snprintf(temp, sizeof(temp), "%s.%u.part", to, i);
    out = fopen(temp, "wbx");
    if (out || errno != EEXIST)
      break;
  }
  FILE *in = from ? fopen(from, "rb") : NULL;
  unsigned char *buffer = malloc(4096);
  int bad = !out || !buffer || (!read && !in);
  uint32_t done = 0;
  while (!bad && done < record->size) {
    size_t n = record->size - done;
    if (n > 4096)
      n = 4096;
    bad = read ? read(ctx, done, buffer, n) : (fread(buffer, 1, n, in) != n);
    if (!bad && fwrite(buffer, 1, n, out) != n)
      bad = 1;
    done += n;
    if (!bad && platform && platform->progress)
      bad = platform->progress(platform->ctx, "history-copy", record->path,
                               done, record->size);
  }
  if (in) {
    if (ferror(in))
      bad = 1;
    fclose(in);
  }
  if (out) {
    if (fflush(out) || fsync(fileno(out)))
      bad = 1;
    if (fclose(out))
      bad = 1;
  }
  free(buffer);
  if (bad || verify(temp, record) || pos_fs_publish_new(temp, to))
    return fail(e, "History copy incomplete. Current firmware, System and "
                   "earlier history preserved.");
  return 0;
}
static int load_entry(const char *directory, const char *key,
                      pos_update_t **out, bool full, pos_error_t *e) {
  char path[POS_FS_PATH], marker[65];
  FILE *f;
  *out = NULL;
  if (checked_path(path, directory, "complete") || regular(path) ||
      (f = fopen(path, "rb")) == NULL)
    return -1;
  size_t n = fread(marker, 1, 65, f);
  fclose(f);
  if (n != 64)
    return -1;
  marker[64] = 0;
  if (checked_path(path, directory, "manifest.json") || regular(path) ||
      pos_update_authenticate(path, key, out, e))
    return -1;
  pos_update_t *u = *out;
  int bad = strcmp(marker, u->package->id) != 0;
  if (full && !bad) {
    bad = checked_path(path, directory, "firmware.bin") ||
          verify(path, &u->package->firmware);
    for (size_t i = 0; !bad && i < u->package->count; ++i) {
      char relative[POS_PATH_MAX + 9];
      snprintf(relative, sizeof(relative), "payload/%s",
               u->package->files[i].path);
      bad = checked_path(path, directory, relative) ||
            verify(path, &u->package->files[i]);
    }
  }
  if (bad) {
    pos_update_free(u);
    *out = NULL;
    return fail(
        e, "History signature, completion marker or file verification failed.");
  }
  return 0;
}
static int compare(const void *a, const void *b) {
  const pos_history_entry_t *x = a, *y = b;
  return x->sequence == y->sequence  ? strcmp(x->id, y->id)
         : x->sequence > y->sequence ? -1
                                     : 1;
}
int pos_history_list(const char *root, const char *key,
                     pos_history_entry_t *entries, unsigned capacity,
                     unsigned *count, pos_error_t *e) {
  *count = 0;
  char folder[POS_FS_PATH];
  if (checked_path(folder, root, POS_HISTORY_ROOT))
    return errno == ENOENT ? 0 : -1;
  DIR *d = opendir(folder);
  if (!d)
    return errno == ENOENT ? 0 : fail(e, "History directory is unavailable.");
  struct dirent *entry;
  while ((entry = readdir(d)))
    if (id_valid(entry->d_name)) {
      char full[POS_FS_PATH];
      pos_update_t *u = NULL;
      if (checked_path(full, folder, entry->d_name) ||
          load_entry(full, key, &u, false, NULL))
        continue;
      if (*count == capacity) {
        pos_update_free(u);
        closedir(d);
        return fail(e, "History inventory exceeds its safe scan bound; "
                       "preserve the directory.");
      }
      if (strcmp(entry->d_name, u->package->id)) {
        pos_update_free(u);
        continue;
      }
      pos_history_entry_t *r = &entries[(*count)++];
      strcpy(r->id, entry->d_name);
      strcpy(r->release, u->release);
      strcpy(r->runtime, u->package->version);
      r->sequence = u->sequence;
      pos_update_free(u);
    }
  closedir(d);
  qsort(entries, *count, sizeof(*entries), compare);
  return 0;
}
/* Remove only exact paths authenticated by this entry. Unknown files, partial
 * directories and symlinks are retained. No generic recursive erase is used. */
static int prune(const char *root, const char *key, const char *keep,
                 const char *pin, pos_error_t *e) {
  pos_history_entry_t *items = calloc(64, sizeof(*items));
  unsigned count = 0;
  if (!items)
    return -1;
  int bad = pos_history_list(root, key, items, 64, &count, e);
  for (unsigned i = count, remaining = count;
       !bad && i && remaining > POS_HISTORY_LIMIT; --i) {
    pos_history_entry_t *r = &items[i - 1];
    if (!strcmp(r->id, keep) || (pin && !strcmp(r->id, pin)))
      continue;
    char folder[POS_FS_PATH], relative[100], file[POS_FS_PATH];
    snprintf(relative, sizeof(relative), POS_HISTORY_ROOT "/%s", r->id);
    pos_update_t *u = NULL;
    if (checked_path(folder, root, relative) ||
        load_entry(folder, key, &u, true, e)) {
      bad = 1;
      break;
    }
    /* Completion is removed first: interruption can never present a partial
     * deleted entry as a verified rollback candidate. */
    if (checked_path(file, folder, "complete") || unlink(file))
      bad = 1;
    for (size_t j = 0; !bad && j < u->package->count; ++j) {
      char part[POS_PATH_MAX + 9];
      snprintf(part, sizeof(part), "payload/%s", u->package->files[j].path);
      if (checked_path(file, folder, part) || unlink(file))
        bad = 1;
      char *slash = strrchr(file, '/');
      while (slash && strlen(file) > strlen(folder)) {
        *slash = 0;
        rmdir(file);
        slash = strrchr(file, '/');
      }
    }
    for (unsigned j = 0; !bad && j < 2; ++j)
      if (checked_path(file, folder, j ? "manifest.json" : "firmware.bin") ||
          unlink(file))
        bad = 1;
    if (!bad) {
      rmdir(folder);
      --remaining;
    }
    pos_update_free(u);
  }
  free(items);
  return bad ? -1 : 0;
}
int pos_history_capture_from(const char *root, const char *system,
                             const char *signed_file, const char *key,
                             pos_history_read_t read, void *ctx,
                             const pos_platform_t *platform,
                             const char *preserve_id, pos_error_t *e) {
  pos_update_t *u = NULL;
  pos_package_t *installed = NULL;
  char *paths = calloc(4, POS_FS_PATH);
  if (!paths)
    return -1;
  char *stage = paths, *final = paths + POS_FS_PATH,
       *file = paths + 2 * POS_FS_PATH, *from = paths + 3 * POS_FS_PATH;
  int bad = checked_path(file, system, "installed.pos") ||
            pos_load_manifest(file, &installed, e) ||
            pos_update_authenticate(signed_file, key, &u, e);
  if (bad)
    goto done;
  if (strcmp(installed->id, u->package->id)) {
    bad = fail(e, "Signed history metadata does not match the running System.");
    goto done;
  }
  char relative[100];
  snprintf(relative, sizeof(relative), POS_HISTORY_ROOT "/%s", installed->id);
  if (pos_fs_join(final, root, relative)) {
    bad = 1;
    goto done;
  }
  pos_update_t *existing = NULL;
  if (!load_entry(final, key, &existing, true, NULL)) {
    pos_update_free(existing);
    bad = prune(root, key, installed->id, preserve_id, e);
    goto done;
  }
  snprintf(relative, sizeof(relative), POS_HISTORY_ROOT "/.%s.part",
           installed->id);
  if (pos_fs_join(stage, root, relative) || pos_fs_mkdirs(stage)) {
    bad = 1;
    goto done;
  }
  if (pos_fs_join(file, stage, "manifest.json")) {
    bad = 1;
    goto done;
  }
  pos_file_t manifest = {.size = strlen(u->signed_manifest)};
  strcpy(manifest.path, "manifest.json");
  pos_hash_data(u->signed_manifest, manifest.size, manifest.sha256);
  /* Copy the original envelope from a normalized, signature-preserving file. */
  char normalized[POS_FS_PATH];
  pos_fs_join(normalized, stage, "signed.part");
  struct stat st;
  if (pos_fs_lstat(normalized, &st)) {
    if (pos_fs_write_new(normalized, u->signed_manifest, manifest.size)) {
      bad = 1;
      goto done;
    }
  }
  if (verify(normalized, &manifest) ||
      copy_stream(normalized, file, &manifest, NULL, NULL, platform, e)) {
    bad = 1;
    goto done;
  }
  unlink(normalized);
  if (pos_fs_join(file, stage, "firmware.bin") ||
      copy_stream(NULL, file, &installed->firmware, read, ctx, platform, e)) {
    bad = 1;
    goto done;
  }
  for (size_t i = 0; !bad && i < installed->count; ++i) {
    char part[POS_PATH_MAX + 9];
    snprintf(part, sizeof(part), "payload/%s", installed->files[i].path);
    if (pos_fs_join(file, stage, part) ||
        checked_path(from, system, installed->files[i].path + 7)) {
      bad = 1;
      break;
    }
    char *slash = strrchr(file, '/');
    *slash = 0;
    bad = pos_fs_mkdirs(file);
    *slash = '/';
    if (!bad)
      bad = copy_stream(from, file, &installed->files[i], NULL, NULL, platform,
                        e);
  }
  if (!bad) {
    pos_fs_join(file, stage, "complete");
    if (pos_fs_lstat(file, &st))
      bad = pos_fs_write_new(file, installed->id, 64);
    else {
      char hash[65], expected[65];
      bad = pos_hash_data(installed->id, 64, expected) ||
            pos_hash_file(file, 64, hash) || strcmp(hash, expected);
    }
    pos_update_t *verified = NULL;
    if (!bad)
      bad = load_entry(stage, key, &verified, true, e);
    pos_update_free(verified);
    if (!bad)
      bad = rename(stage, final);
    if (!bad)
      bad = prune(root, key, installed->id, preserve_id, e);
  }
done:
  pos_package_free(installed);
  pos_update_free(u);
  free(paths);
  return bad ? fail(e, e && *e->message
                           ? e->message
                           : "Firmware history could not be verified. Current "
                             "installation retained.")
             : 0;
}
typedef struct {
  const pos_update_platform_t *hooks;
  char folder[POS_FS_PATH], base[512];
} rollback_t;
static int fetch_local(void *ctx, const char *url, const char *directory,
                       const char *name, uint32_t size, const char *sha,
                       pos_error_t *e) {
  rollback_t *r = ctx;
  size_t n = strlen(r->base);
  if (strncmp(url, r->base, n))
    return -1;
  const char *part = url + n;
  if (*part == '/')
    ++part;
  if (!pos_fs_archive_path(part))
    return -1;
  char from[POS_FS_PATH], to[POS_FS_PATH];
  pos_file_t record = {.size = size};
  snprintf(record.path, sizeof(record.path), "%s", name);
  snprintf(record.sha256, sizeof(record.sha256), "%s", sha);
  return checked_path(from, r->folder, part) ||
                 pos_fs_join(to, directory, name) ||
                 copy_stream(from, to, &record, NULL, NULL, &r->hooks->platform,
                             e)
             ? -1
             : 0;
}
int pos_history_rollback(const char *root, const char *id, const char *key,
                         const pos_update_platform_t *hooks, pos_error_t *e) {
  if (!id_valid(id))
    return fail(e, "Invalid history selection.");
  rollback_t *r = calloc(1, sizeof(*r));
  if (!r)
    return -1;
  r->hooks = hooks;
  char relative[100];
  snprintf(relative, sizeof(relative), POS_HISTORY_ROOT "/%s", id);
  pos_update_t *u = NULL;
  int bad = checked_path(r->folder, root, relative) ||
            load_entry(r->folder, key, &u, true, e);
  if (!bad) {
    if (strcmp(id, u->package->id))
      bad = fail(e, "History directory does not match its signed package.");
    strcpy(r->base, u->base_url);
    char current[POS_FS_PATH];
    pos_package_t *before = NULL;
    if (!bad)
      bad = checked_path(current, root, "POS/System/installed.pos") ||
            pos_load_manifest(current, &before, e);
    if (!bad && !strcmp(before->id, u->package->id))
      bad = fail(e, "This firmware/System pair is already installed.");
    if (!bad) {
      u->firmware = strcmp(before->version, u->package->version) != 0;
      if (!u->firmware &&
          (before->firmware.size != u->package->firmware.size ||
           strcmp(before->firmware.sha256, u->package->firmware.sha256)))
        bad = fail(e, "Different firmware with the same version is ambiguous. "
                      "Use verified USB recovery.");
    }
    pos_package_free(before);
    /* A fresh journal ID lets repeated deliberate rollbacks use the same
     * signed release without reusing an earlier transaction directory. */
    for (unsigned i = 0; i < 100; ++i) {
      char seed[100], stage[POS_FS_PATH];
      snprintf(seed, sizeof(seed), "%s:rollback:%u", id, i);
      pos_hash_data(seed, strlen(seed), u->id);
      snprintf(relative, sizeof(relative), "POS/Updates/%s", u->id);
      pos_fs_join(stage, root, relative);
      struct stat st;
      if (pos_fs_lstat(stage, &st) && errno == ENOENT)
        break;
      if (i == 99)
        bad = 1;
    }
    if (!bad) {
      pos_update_platform_t local = *hooks;
      local.ctx = r;
      local.fetch = fetch_local;
      bad = pos_update_apply(root, u, &local, e);
    }
  }
  pos_update_free(u);
  free(r);
  return bad ? -1 : 0;
}

int pos_history_capture(const char *root, const char *signed_file,
                        const char *key, pos_history_read_t read, void *ctx,
                        const pos_platform_t *platform, const char *pin,
                        pos_error_t *e) {
  char system[POS_FS_PATH];
  if (checked_path(system, root, "POS/System"))
    return -1;
  return pos_history_capture_from(root, system, signed_file, key, read, ctx,
                                  platform, pin, e);
}
