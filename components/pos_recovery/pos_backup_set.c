/* Large FAT32 backups: ordinary folders, sequential copies, bounded RAM.
 * The JSONL inventory is traversed in the same portable sorted order as files/;
 * verification rejects missing, extra, duplicated, unsafe or changed entries.
 */
#include "cJSON.h"
#include "pos_fs.h"
#include "pos_migrate.h"
#include "pos_recovery.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef int (*visit_t)(void *, const char *, const char *, bool, uint32_t);
typedef struct {
  char names[16][256]; /* One shared selection batch; never a full file list. */
  char last[17][256], full[POS_FS_PATH], key[256];
} batch_t;

static int failure(pos_error_t *e, const char *format, ...) {
  if (e) {
    va_list args;
    va_start(args, format);
    vsnprintf(e->message, sizeof(e->message), format, args);
    va_end(args);
  }
  return -1;
}
static int tick(pos_recovery_progress_t progress, void *ctx, const char *phase,
                const char *name, uint64_t done, uint64_t total,
                pos_error_t *e) {
  return progress && progress(ctx, phase, name, done, total)
             ? failure(e, "Cancelled; originals and incomplete backup retained")
             : 0;
}
static int sync_close(FILE *f) {
  int bad = fflush(f) || fsync(fileno(f));
  if (fclose(f))
    bad = 1;
  return bad ? -1 : 0;
}
static int missing(const char *path) {
  struct stat st;
  return pos_fs_lstat(path, &st) && errno == ENOENT;
}
static int parent(const char *path) {
  char copy[POS_FS_PATH];
  if (strlen(path) >= sizeof(copy))
    return -1;
  strcpy(copy, path);
  char *slash = strrchr(copy, '/');
  if (!slash)
    return -1;
  *slash = 0;
  return pos_fs_mkdirs(copy);
}

/* A directory's next 16 names are selected in one scan. Child recursion reuses
 * this buffer and the parent resumes from its last name. Files never require
 * heap proportional to their number. Empty directories are visited too. */
static int ordered_walk(const char *root, char relative[256], unsigned depth,
                        bool source, batch_t *batch, visit_t visit, void *ctx,
                        pos_error_t *error) {
  if (depth > 16)
    return failure(error, "Directory depth exceeds 16");
  size_t base = strlen(relative);
  char *last = batch->last[depth], *full = batch->full;
  *last = 0;
  for (;;) {
    if (pos_fs_join(full, root, relative))
      return -1;
    DIR *dir = opendir(full);
    if (!dir)
      return failure(error, "Cannot scan %s", relative);
    unsigned count = 0;
    int bad = 0;
    struct dirent *item;
    errno = 0;
    while ((item = readdir(dir))) {
      if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
        continue;
      int n = snprintf(relative + base, 256 - base, "%s%s", base ? "/" : "",
                       item->d_name);
      if (n < 0 || (size_t)n >= 256 - base || strlen(relative) > 240) {
        bad = 1;
        break;
      }
      if (source && !pos_fs_user_path(relative)) {
        errno = 0;
        continue;
      }
      struct stat st;
      if (!pos_fs_archive_path(relative) || !pos_fs_user_path(relative) ||
          pos_fs_join(full, root, relative) || pos_fs_lstat(full, &st) ||
          (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) {
        bad = 1;
        break;
      }
      char *key = batch->key;
      size_t key_length = strlen(item->d_name);
      if (key_length > 240) {
        bad = 1;
        break;
      }
      memcpy(key, item->d_name, key_length);
      key[key_length] = 0;
      if (strcasecmp(key, last) <= 0) {
        errno = 0;
        continue;
      }
      unsigned at = 0;
      while (at < count && strcasecmp(batch->names[at], key) < 0)
        ++at;
      if (at < count && !strcasecmp(batch->names[at], key)) {
        bad = 1;
        break;
      }
      if (at < 16) {
        unsigned end = count < 16 ? count++ : 15;
        for (unsigned i = end; i > at; --i)
          strcpy(batch->names[i], batch->names[i - 1]);
        strcpy(batch->names[at], key);
      }
      errno = 0;
    }
    if (errno)
      bad = 1;
    closedir(dir);
    relative[base] = 0;
    if (bad)
      return failure(error, "Unsafe, ambiguous or unreadable path below %s",
                     relative);
    if (!count)
      return 0;
    bool recursed = false;
    for (unsigned i = 0; i < count; ++i) {
      strcpy(last, batch->names[i]);
      snprintf(relative + base, 256 - base, "%s%s", base ? "/" : "", last);
      struct stat entry;
      if (pos_fs_join(full, root, relative) || pos_fs_lstat(full, &entry) ||
          (!S_ISDIR(entry.st_mode) && !S_ISREG(entry.st_mode)))
        return -1;
      bool isdir = S_ISDIR(entry.st_mode);
      uint32_t size = 0;
      if (pos_fs_join(full, root, relative) ||
          (!isdir && pos_fs_file_size(full, &size)) ||
          visit(ctx, relative, full, isdir, size)) {
        relative[base] = 0;
        return -1;
      }
      if (isdir && ordered_walk(root, relative, depth + 1, source, batch, visit,
                                ctx, error)) {
        relative[base] = 0;
        return -1;
      }
      relative[base] = 0;
      if (isdir) {
        recursed = true;
        break;
      } /* Child overwrote shared batch. */
    }
    if (!recursed && count < 16)
      return 0;
  }
}
static int walk(const char *root, bool source, visit_t visit, void *ctx,
                pos_error_t *e) {
  batch_t *batch = malloc(sizeof(*batch));
  if (!batch)
    return failure(e, "Not enough memory for backup scan");
  char relative[256] = "";
  int result = ordered_walk(root, relative, 0, source, batch, visit, ctx, e);
  free(batch);
  return result;
}

typedef struct {
  pos_backup_info_t *info;
  pos_recovery_progress_t progress;
  void *ctx;
  pos_error_t *error;
  const char *root, *target, *stage;
  bool resume;
  FILE *inventory;
  uint64_t processed;
  char id[65];
  pos_settings_t legacy;
  bool have_legacy;
} operation_t;
typedef struct {
  operation_t *operation;
  const char *relative;
} hash_progress_t;
static int hash_tick(void *opaque, uint64_t done, uint64_t total) {
  hash_progress_t *h = opaque;
  return tick(h->operation->progress, h->operation->ctx, "verify", h->relative,
              h->operation->processed + done, total, h->operation->error);
}
static int hash_file(operation_t *o, const char *path, uint32_t size,
                     char hash[65], const char *relative) {
  hash_progress_t h = {o, relative};
  return pos_hash_file_progress(path, size, hash, hash_tick, &h);
}
static int scan_entry(void *opaque, const char *relative, const char *full,
                      bool dir, uint32_t size) {
  operation_t *o = opaque;
  if (dir) {
    if (o->info->directories == UINT32_MAX)
      return -1;
    ++o->info->directories;
    DIR *directory = opendir(full);
    if (!directory)
      return -1;
    bool empty = true;
    struct dirent *item;
    errno = 0;
    while ((item = readdir(directory))) {
      if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
        empty = false;
      errno = 0;
    }
    int scan_error = errno;
    closedir(directory);
    if (scan_error)
      return -1;
    if (empty)
      o->info->directory_backup = true;
  } else {
    if (o->info->files == UINT32_MAX)
      return -1;
    ++o->info->files;
    o->info->bytes += size;
    if (o->info->bytes > 0x1fffffffffffffULL)
      return failure(o->error, "Backup size exceeds exact metadata range");
    if (size > POS_BACKUP_LIMIT)
      o->info->directory_backup = true;
  }
  o->info->required +=
      (uint64_t)size + 131072; /* file + inventory + FAT clusters */
  return tick(o->progress, o->ctx, "scan", relative, o->info->bytes,
              o->info->files, o->error);
}
int pos_backup_set_scan(const char *root, pos_backup_info_t *info,
                        pos_recovery_progress_t progress, void *ctx,
                        pos_error_t *error) {
  memset(info, 0, sizeof(*info));
  info->required = 524288;
  operation_t o = {
      .info = info, .progress = progress, .ctx = ctx, .error = error};
  if (walk(root, true, scan_entry, &o, error))
    return -1;
  if (info->files > POS_BACKUP_FILES ||
      info->bytes + (uint64_t)info->files * 1024 + 262144 > POS_BACKUP_LIMIT)
    info->directory_backup = true;
  return 0;
}

static int copy_file(operation_t *o, const char *source,
                     const char *destination, uint32_t size,
                     const char *relative, const char *phase) {
  if (parent(destination))
    return -1;
  FILE *in = fopen(source, "rb"), *out = fopen(destination, "wx");
  unsigned char *buffer = malloc(8192);
  int bad = !in || !out || !buffer;
  uint64_t done = 0;
  uint32_t crc = 0;
  while (!bad && done < size) {
    size_t n = size - done < 8192 ? (size_t)(size - done) : 8192;
    if (fread(buffer, 1, n, in) != n || fwrite(buffer, 1, n, out) != n) {
      bad = 1;
      break;
    }
    crc = pos_crc32(crc, buffer, n);
    done += n;
    if (tick(o->progress, o->ctx, phase, relative, o->processed + done,
             o->info->bytes, o->error))
      bad = 1;
  }
  if (in) {
    if (!bad && (fgetc(in) != EOF || ferror(in)))
      bad = 1;
    if (fclose(in))
      bad = 1;
  }
  if (out && sync_close(out))
    bad = 1;
  free(buffer);
  uint32_t checked;
  hash_progress_t h = {o, relative};
  if (!bad &&
      (pos_fs_crc_file_progress(destination, size, &checked, hash_tick, &h) ||
       checked != crc))
    bad = 1;
  return bad ? -1 : 0;
}
static int inventory_line(FILE *f, const char *relative, bool dir,
                          uint32_t size, const char *hash) {
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddStringToObject(j, "path", relative);
  cJSON_AddStringToObject(j, "type", dir ? "directory" : "file");
  cJSON_AddNumberToObject(j, "size", size);
  cJSON_AddStringToObject(j, "sha256", hash);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!text)
    return -1;
  int bad = fprintf(f, "%s\n", text) < 0;
  free(text);
  return bad ? -1 : 0;
}
static int retain_interrupted(const char *stage, const char *path,
                              const char *relative) {
  char hash[65], destination[POS_FS_PATH], name[120];
  if (pos_hash_data(relative, strlen(relative), hash))
    return -1;
  for (unsigned i = 0; i < 10000; ++i) {
    snprintf(name, sizeof(name), "interrupted/%.32s-%u", hash, i);
    if (pos_fs_join(destination, stage, name) || parent(destination))
      return -1;
    if (missing(destination))
      return rename(path, destination);
  }
  return -1;
}
static int create_entry(void *opaque, const char *relative, const char *full,
                        bool dir, uint32_t size) {
  operation_t *o = opaque;
  char dest[POS_FS_PATH], hash[65] = "";
  if (pos_fs_join(dest, o->target, relative))
    return -1;
  if (dir) {
    if (pos_fs_mkdirs(dest))
      return -1;
  } else {
    bool reused = false;
    if (!missing(dest)) {
      uint32_t existing;
      char original[65];
      if (!o->resume)
        return -1;
      reused = !pos_fs_file_size(dest, &existing) && existing == size &&
               !hash_file(o, dest, size, hash, relative) &&
               !hash_file(o, full, size, original, relative) &&
               !strcmp(hash, original);
      if (!reused && retain_interrupted(o->stage, dest, relative))
        return -1;
    }
    if (!reused && (copy_file(o, full, dest, size, relative, "backup") ||
                    hash_file(o, dest, size, hash, relative)))
      return -1;
    /* Re-read the original: same-size edits during copying must not pass. */
    char original[65];
    if (hash_file(o, full, size, original, relative) || strcmp(original, hash))
      return -1;
    o->processed += size;
  }
  return inventory_line(o->inventory, relative, dir, size, hash);
}
static cJSON *read_json(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  char text[2049];
  size_t n = fread(text, 1, sizeof(text), f);
  int bad = ferror(f);
  if (fclose(f))
    bad = 1;
  if (bad || n >= sizeof(text))
    return NULL;
  text[n] = 0;
  return pos_fs_json_safe(text, n) ? cJSON_ParseWithOpts(text, NULL, true)
                                   : NULL;
}
static bool integer(const cJSON *j, uint64_t max) {
  return cJSON_IsNumber(j) && j->valuedouble >= 0 &&
         j->valuedouble <= (double)max &&
         j->valuedouble == (double)(uint64_t)j->valuedouble;
}
static bool digest(const char *s) {
  if (!s || strlen(s) != 64)
    return false;
  for (; *s; ++s)
    if (!strchr("0123456789abcdef", *s))
      return false;
  return true;
}
static int verify_entry(void *opaque, const char *relative, const char *full,
                        bool dir, uint32_t size) {
  operation_t *o = opaque;
  char line[2049], hash[65];
  if (!fgets(line, sizeof(line), o->inventory) || !strchr(line, '\n') ||
      !pos_fs_json_safe(line, strlen(line)))
    return -1;
  cJSON *j = cJSON_ParseWithOpts(line, NULL, true),
        *p = cJSON_GetObjectItemCaseSensitive(j, "path"),
        *t = cJSON_GetObjectItemCaseSensitive(j, "type"),
        *s = cJSON_GetObjectItemCaseSensitive(j, "size"),
        *h = cJSON_GetObjectItemCaseSensitive(j, "sha256");
  bool ok =
      cJSON_IsObject(j) && cJSON_GetArraySize(j) == 4 && cJSON_IsString(p) &&
      !strcmp(p->valuestring, relative) && cJSON_IsString(t) &&
      !strcmp(t->valuestring, dir ? "directory" : "file") &&
      integer(s, UINT32_MAX) && s->valuedouble == size && cJSON_IsString(h);
  if (ok)
    ok = dir ? !*h->valuestring
             : digest(h->valuestring) &&
                   !hash_file(o, full, size, hash, relative) &&
                   !strcmp(hash, h->valuestring);
  cJSON_Delete(j);
  if (!ok)
    return -1;
  o->processed += size;
  if (dir)
    ++o->info->directories;
  else {
    ++o->info->files;
    o->info->bytes += size;
  }
  o->info->required += (uint64_t)size + 131072;
  return tick(o->progress, o->ctx, "verify", relative, o->processed, 0,
              o->error);
}
int pos_backup_set_verify(const char *archive, pos_backup_info_t *info,
                          pos_recovery_progress_t progress, void *ctx,
                          pos_error_t *error) {
  memset(info, 0, sizeof(*info));
  info->directory_backup = true;
  info->required = 524288;
  char metadata[POS_FS_PATH], inventory[POS_FS_PATH], files[POS_FS_PATH],
      hash[65];
  if (pos_fs_join(metadata, archive, "backup.json") ||
      pos_fs_join(inventory, archive, "inventory.jsonl") ||
      pos_fs_join(files, archive, "files"))
    return -1;
  struct stat st;
  if (pos_fs_lstat(archive, &st) || !S_ISDIR(st.st_mode) ||
      pos_fs_lstat(files, &st) || !S_ISDIR(st.st_mode))
    return failure(error, "Unsafe backup folder");
  uint32_t meta_size;
  if (pos_fs_file_size(metadata, &meta_size) || meta_size > 2048)
    return failure(error, "Missing or unsafe backup metadata");
  cJSON *j = read_json(metadata),
        *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *migration = cJSON_GetObjectItemCaseSensitive(j, "migration"),
        *bytes = cJSON_GetObjectItemCaseSensitive(j, "source_bytes"),
        *count = cJSON_GetObjectItemCaseSensitive(j, "files"),
        *dirs = cJSON_GetObjectItemCaseSensitive(j, "directories"),
        *version = cJSON_GetObjectItemCaseSensitive(j, "source_version"),
        *checksum = cJSON_GetObjectItemCaseSensitive(j, "inventory_sha256");
  uint32_t length;
  bool ok = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 8 &&
            cJSON_IsNumber(format) && format->valuedouble == 2 &&
            cJSON_IsNumber(migration) && migration->valuedouble == 1 &&
            integer(bytes, 0x1fffffffffffffULL) && integer(count, UINT32_MAX) &&
            integer(dirs, UINT32_MAX) && cJSON_IsString(version) &&
            strlen(version->valuestring) < sizeof(info->source_version) &&
            cJSON_IsString(checksum) && digest(checksum->valuestring) &&
            !pos_fs_file_size(inventory, &length) &&
            length <= POS_BACKUP_LIMIT &&
            !pos_hash_file(inventory, length, hash) &&
            !strcmp(hash, checksum->valuestring);
  int result = -1;
  if (ok) {
    strcpy(info->source_version, version->valuestring);
    FILE *f = fopen(inventory, "rb");
    if (f) {
      operation_t o = {.info = info,
                       .inventory = f,
                       .progress = progress,
                       .ctx = ctx,
                       .error = error};
      result = walk(files, false, verify_entry, &o, error);
      if (fgetc(f) != EOF || ferror(f) ||
          info->bytes != (uint64_t)bytes->valuedouble ||
          info->files != (uint32_t)count->valuedouble ||
          info->directories != (uint32_t)dirs->valuedouble)
        result = -1;
      if (fclose(f))
        result = -1;
    }
  }
  cJSON_Delete(j);
  return result ? failure(error, "Incomplete, unsupported, cancelled or "
                                 "corrupt folder backup; originals retained")
                : 0;
}

static int create_set(const char *root, const char *version, uint64_t available,
                      int64_t creation_time, pos_recovery_progress_t progress,
                      void *ctx, pos_error_t *error, bool resume) {
  pos_backup_info_t info;
  if (pos_backup_set_scan(root, &info, progress, ctx, error))
    return -1;
  if (available < info.required + 65536)
    return failure(error, "Insufficient temporary space; originals preserved");
  char final[POS_FS_PATH], stage[POS_FS_PATH], files[POS_FS_PATH],
      inventory[POS_FS_PATH], metadata[POS_FS_PATH], zip[POS_FS_PATH];
  if (pos_fs_join(final, root, POS_BACKUP_SET) ||
      pos_fs_join(stage, root, POS_BACKUP_SET ".part") ||
      pos_fs_join(zip, root, POS_BACKUP_NAME) || !missing(final) ||
      (!resume && !missing(stage)) || !missing(zip))
    return failure(error, "Existing or incomplete backup preserved; move it "
                          "aside before retrying");
  char owner[POS_FS_PATH];
  if (pos_fs_join(owner, stage, ".pos-backup-state.json"))
    return -1;
  if (resume) {
    struct stat state;
    if (pos_fs_lstat(stage, &state) || !S_ISDIR(state.st_mode) ||
        pos_fs_lstat(owner, &state) || !S_ISREG(state.st_mode) ||
        state.st_size > 2048)
      return failure(error,
                     "Unowned incomplete backup preserved; cannot resume");
    cJSON *j = read_json(owner),
          *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
          *v = cJSON_GetObjectItemCaseSensitive(j, "version"),
          *bytes = cJSON_GetObjectItemCaseSensitive(j, "bytes"),
          *count = cJSON_GetObjectItemCaseSensitive(j, "files"),
          *dirs = cJSON_GetObjectItemCaseSensitive(j, "directories");
    bool valid =
        cJSON_IsObject(j) && cJSON_GetArraySize(j) == 5 && integer(format, 2) &&
        format->valuedouble == 2 && cJSON_IsString(v) &&
        !strcmp(v->valuestring, version) &&
        integer(bytes, 0x1fffffffffffffULL) &&
        bytes->valuedouble == info.bytes && integer(count, UINT32_MAX) &&
        count->valuedouble == info.files && integer(dirs, UINT32_MAX) &&
        dirs->valuedouble == info.directories;
    cJSON_Delete(j);
    if (!valid)
      return failure(error, "Backup state or current filesystem differs; "
                            "incomplete backup preserved");
  } else {
    if (mkdir(stage, 0775))
      return -1;
    cJSON *j = cJSON_CreateObject();
    if (!j)
      return -1;
    cJSON_AddNumberToObject(j, "format", 2);
    cJSON_AddStringToObject(j, "version", version);
    cJSON_AddNumberToObject(j, "bytes", info.bytes);
    cJSON_AddNumberToObject(j, "files", info.files);
    cJSON_AddNumberToObject(j, "directories", info.directories);
    char *text = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    int bad = !text || pos_fs_write_new(owner, text, strlen(text));
    free(text);
    if (bad)
      return failure(error, "Backup owner state incomplete; preserve folder");
  }
  if (pos_fs_join(files, stage, "files") || pos_fs_mkdirs(files) ||
      pos_fs_join(inventory, stage, "inventory.jsonl") ||
      pos_fs_join(metadata, stage, "backup.json"))
    return -1;
  if (resume) {
    if ((!missing(inventory) &&
         retain_interrupted(stage, inventory, "inventory.jsonl")) ||
        (!missing(metadata) &&
         retain_interrupted(stage, metadata, "backup.json")))
      return -1;
  }
  FILE *f = fopen(inventory, "wx");
  if (!f)
    return -1;
  operation_t o = {.info = &info,
                   .target = files,
                   .stage = stage,
                   .resume = resume,
                   .inventory = f,
                   .progress = progress,
                   .ctx = ctx,
                   .error = error};
  int bad = walk(root, true, create_entry, &o, error);
  if (sync_close(f))
    bad = 1;
  char hash[65];
  uint32_t length;
  if (bad || o.processed != info.bytes ||
      pos_fs_file_size(inventory, &length) || length > POS_BACKUP_LIMIT ||
      pos_hash_file(inventory, length, hash))
    return failure(error,
                   "Backup incomplete; originals and .part folder retained");
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddNumberToObject(j, "format", 2);
  cJSON_AddNumberToObject(j, "migration", 1);
  cJSON_AddStringToObject(j, "source_version", version);
  cJSON_AddNumberToObject(j, "created_at", creation_time);
  cJSON_AddNumberToObject(j, "source_bytes", info.bytes);
  cJSON_AddNumberToObject(j, "files", info.files);
  cJSON_AddNumberToObject(j, "directories", info.directories);
  cJSON_AddStringToObject(j, "inventory_sha256", hash);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  bad = !text || pos_fs_write_new(metadata, text, strlen(text));
  free(text);
  pos_backup_info_t checked;
  if (bad ||
      tick(progress, ctx, "backup-finalize", "backup.json", info.bytes,
           info.bytes, error) ||
      pos_backup_set_verify(stage, &checked, progress, ctx, error) ||
      pos_backup_set_matches_progress(root, stage, progress, ctx, error) ||
      !missing(final) || rename(stage, final))
    return failure(error,
                   "Backup not published; originals and .part folder retained");
  return 0;
}

int pos_backup_set_create(const char *root, const char *version,
                          uint64_t available, int64_t creation_time,
                          pos_recovery_progress_t progress, void *ctx,
                          pos_error_t *error) {
  return create_set(root, version, available, creation_time, progress, ctx,
                    error, false);
}
int pos_backup_set_resume(const char *root, const char *version,
                          uint64_t available, int64_t creation_time,
                          pos_recovery_progress_t progress, void *ctx,
                          pos_error_t *error) {
  return create_set(root, version, available, creation_time, progress, ctx,
                    error, true);
}

static int matches_entry(void *opaque, const char *relative, const char *full,
                         bool dir, uint32_t size) {
  operation_t *o = opaque;
  char path[POS_FS_PATH], a[65], b[65];
  struct stat st;
  uint32_t actual;
  if (pos_fs_join(path, o->root, relative) || pos_fs_lstat(path, &st))
    return -1;
  if (dir)
    return S_ISDIR(st.st_mode) ? 0 : -1;
  return pos_fs_file_size(path, &actual) || actual != size ||
                 hash_file(o, path, size, a, relative) ||
                 hash_file(o, full, size, b, relative) || strcmp(a, b)
             ? -1
             : 0;
}
int pos_backup_set_matches(const char *root, const char *archive,
                           pos_error_t *error) {
  return pos_backup_set_matches_progress(root, archive, NULL, NULL, error);
}
int pos_backup_set_matches_progress(const char *root, const char *archive,
                                    pos_recovery_progress_t progress, void *ctx,
                                    pos_error_t *error) {
  pos_backup_info_t stored, current;
  if (pos_backup_set_verify(archive, &stored, progress, ctx, error) ||
      pos_backup_set_scan(root, &current, progress, ctx, error))
    return -1;
  if (stored.files != current.files || stored.bytes != current.bytes ||
      stored.directories != current.directories)
    return failure(
        error, "Backup differs from current filesystem; create a fresh backup");
  char files[POS_FS_PATH];
  if (pos_fs_join(files, archive, "files"))
    return -1;
  operation_t o = {.root = root,
                   .info = &stored,
                   .progress = progress,
                   .ctx = ctx,
                   .error = error};
  return walk(files, false, matches_entry, &o, error)
             ? failure(error,
                       "Backup differs from current data; originals retained")
             : 0;
}

static int restore_entry(void *opaque, const char *relative, const char *full,
                         bool dir, uint32_t size) {
  operation_t *o = opaque;
  char dest[POS_FS_PATH], relocated[320], a[65], b[65];
  struct stat st;
  pos_settings_t settings;
  int policy = dir ? 0 : pos_migration_settings(full, relative, &settings);
  if ((policy == 1 &&
       (!o->have_legacy || settings.generation > o->legacy.generation)) ||
      (policy == 2 && !o->have_legacy)) {
    o->legacy = settings;
    o->have_legacy = true;
  }
  if (pos_fs_join(dest, o->root, relative))
    return -1;
  bool exists = !pos_fs_lstat(dest, &st);
  if (!dir && policy == 0 && exists) {
    uint32_t actual;
    if (!pos_fs_file_size(dest, &actual) && actual == size &&
        !hash_file(o, dest, size, a, relative) &&
        !hash_file(o, full, size, b, relative) && !strcmp(a, b)) {
      o->processed += size;
      return pos_migration_record(o->root, o->id, relative, relative,
                                  "already verified");
    }
  }
  const char *destination = relative;
  if (policy != 0 || (exists && (!dir || !S_ISDIR(st.st_mode))) ||
      parent(dest)) {
    snprintf(relocated, sizeof(relocated), "POS/Recovered/%.12s/%s", o->id,
             relative);
    if (pos_fs_join(dest, o->root, relocated) || parent(dest))
      return -1;
    destination = relocated;
    ++o->info->conflicts;
  }
  if (dir) {
    if (pos_fs_mkdirs(dest))
      return -1;
  } else {
    uint32_t actual;
    if (!missing(dest)) {
      if (pos_fs_file_size(dest, &actual) || actual != size ||
          hash_file(o, dest, size, a, relative) ||
          hash_file(o, full, size, b, relative) || strcmp(a, b))
        return -1;
    } else {
      char temp[POS_FS_PATH];
      if (pos_fs_join(temp, o->root, "POS/Temp/restore-set.part") ||
          parent(temp))
        return -1;
      if (unlink(temp) && errno != ENOENT)
        return -1;
      if (copy_file(o, full, temp, size, relative, "restore") ||
          hash_file(o, temp, size, a, relative) ||
          hash_file(o, full, size, b, relative) || strcmp(a, b) ||
          !missing(dest) || pos_fs_publish_new(temp, dest))
        return -1;
    }
    o->processed += size;
  }
  return pos_migration_record(
      o->root, o->id, relative, destination,
      policy > 0   ? "settings original retained; migration pending"
      : policy < 0 ? "unsupported or corrupt settings preserved"
                   : "verified");
}
int pos_backup_set_restore(const char *root, const char *archive,
                           uint64_t available, pos_backup_info_t *info,
                           pos_recovery_progress_t progress, void *ctx,
                           pos_error_t *error) {
  if (pos_backup_set_verify(archive, info, progress, ctx, error))
    return -1;
  if (available < info->required)
    return failure(error, "Insufficient restore space; backup retained");
  char files[POS_FS_PATH];
  if (pos_fs_join(files, archive, "files"))
    return -1;
  operation_t o = {.root = root,
                   .info = info,
                   .progress = progress,
                   .ctx = ctx,
                   .error = error};
  if (pos_backup_identity(archive, o.id, error) ||
      walk(files, false, restore_entry, &o, error) ||
      (o.have_legacy && (tick(progress, ctx, "migration", "settings",
                              info->bytes, info->bytes, error) ||
                         pos_migration_apply(root, &o.legacy) ||
                         tick(progress, ctx, "migration-applied", "settings",
                              info->bytes, info->bytes, error))) ||
      pos_migration_record(root, o.id, "", "", "restore complete"))
    return failure(error, "Restore incomplete; backup retained. Retry safely");
  return 0;
}

int pos_backup_find(const char *root, char path[512], pos_error_t *error) {
  char zip[POS_FS_PATH], set[POS_FS_PATH];
  if (pos_fs_join(zip, root, POS_BACKUP_NAME) ||
      pos_fs_join(set, root, POS_BACKUP_SET))
    return -1;
  bool a = !missing(zip), b = !missing(set);
  if (a && b)
    return failure(error, "Both ZIP and folder backups exist; select one "
                          "explicitly or move the other aside");
  if (!a && !b)
    return failure(
        error,
        "No completed backup found; incomplete .part files are preserved");
  strcpy(path, a ? zip : set);
  return 0;
}
typedef struct {
  pos_recovery_progress_t progress;
  void *ctx;
  pos_error_t *error;
} identity_t;
static int identity_tick(void *opaque, uint64_t done, uint64_t total) {
  identity_t *i = opaque;
  return tick(i->progress, i->ctx, "backup identity", "SHA256", done, total,
              i->error);
}
int pos_backup_identity(const char *archive, char hash[65], pos_error_t *error) {
  return pos_backup_identity_progress(archive, hash, NULL, NULL, error);
}
int pos_backup_identity_progress(const char *archive, char hash[65],
                                 pos_recovery_progress_t progress, void *ctx,
                                 pos_error_t *error) {
  struct stat st;
  char path[POS_FS_PATH];
  uint32_t size;
  if (pos_fs_lstat(archive, &st))
    return -1;
  if (S_ISDIR(st.st_mode)) {
    if (pos_fs_join(path, archive, "backup.json"))
      return -1;
  } else {
    if (strlen(archive) >= sizeof(path))
      return -1;
    strcpy(path, archive);
  }
  identity_t i = {progress, ctx, error};
  return pos_fs_file_size(path, &size) ||
                 pos_hash_file_progress(path, size, hash, identity_tick, &i)
             ? failure(error, "Cannot identify backup; original preserved")
             : 0;
}
