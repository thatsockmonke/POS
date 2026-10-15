#include "cJSON.h"
#include "pos_fs.h"
#include "pos_migrate.h"
#include "pos_recovery.h"
#include "pos_settings.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define ZIP_FLAGS                                                              \
  0x808u /* UTF-8 names, streamed data descriptor, stored method */
typedef struct {
  char name[256];
  uint32_t size, crc, offset;
} entry_t;
typedef struct {
  FILE *f;
  uint32_t start, size, entries;
} directory_t;
typedef int (*file_callback_t)(void *, const char *, const char *, uint32_t);

static int fail(pos_error_t *e, const char *format, ...) {
  if (e) {
    va_list a;
    va_start(a, format);
    vsnprintf(e->message, sizeof(e->message), format, a);
    va_end(a);
  }
  return -1;
}
static uint16_t u16(const unsigned char *p) {
  return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const unsigned char *p) {
  return u16(p) | ((uint32_t)u16(p + 2) << 16);
}
static void w16(unsigned char *p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
static void w32(unsigned char *p, uint32_t v) {
  w16(p, v);
  w16(p + 2, v >> 16);
}
static int emit(pos_recovery_progress_t p, void *ctx, const char *phase,
                const char *name, uint64_t done, uint64_t total,
                pos_error_t *e) {
  return p && p(ctx, phase, name, done, total)
             ? fail(e, "Cancelled; original data and incomplete files retained")
             : 0;
}
static int close_sync(FILE *f) {
  int r = fflush(f);
  if (!r && fsync(fileno(f)))
    r = -1;
  if (fclose(f))
    r = -1;
  return r;
}
static int write_bytes(FILE *f, const void *data, size_t n) {
  return fwrite(data, 1, n, f) == n ? 0 : -1;
}
static int absent(const char *path) {
  struct stat s;
  return pos_fs_lstat(path, &s) && errno == ENOENT;
}

static int walk(const char *root, char *relative, unsigned depth,
                file_callback_t callback, void *ctx, pos_error_t *e,
                char full[POS_FS_PATH]) {
  if (depth > 16)
    return fail(
        e, "Directory nesting exceeds backup limit; use a computer backup");
  if (pos_fs_join(full, root, relative))
    return fail(e, "Path too long");
  DIR *dir = opendir(full);
  if (!dir)
    return fail(e, "Cannot scan %s", relative);
  size_t base = strlen(relative);
  int result = 0;
  struct dirent *item;
  errno = 0;
  while ((item = readdir(dir))) {
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
      continue;
    int n = snprintf(relative + base, 256 - base, "%s%s", base ? "/" : "",
                     item->d_name);
    if (n < 0 || (size_t)n >= 256 - base || strlen(relative) > 240) {
      result = fail(e, "Backup path exceeds 240 bytes");
      break;
    }
    if (!pos_fs_user_path(relative)) {
      relative[base] = 0;
      errno = 0;
      continue;
    }
    if (!pos_fs_archive_path(relative) || pos_fs_join(full, root, relative)) {
      result = fail(e, "Unsupported user path: %s", relative);
      break;
    }
    struct stat st;
    if (pos_fs_lstat(full, &st)) {
      result = fail(e, "Cannot inspect %s", relative);
      break;
    }
    if (S_ISLNK(st.st_mode)) {
      result = fail(e, "Symlink not supported: %s", relative);
      break;
    }
    if (S_ISDIR(st.st_mode))
      result = walk(root, relative, depth + 1, callback, ctx, e, full);
    else if (S_ISREG(st.st_mode) && st.st_size >= 0 &&
             (uint64_t)st.st_size <= POS_BACKUP_LIMIT)
      result = callback(ctx, relative, full, (uint32_t)st.st_size);
    else
      result = fail(e, "Unsupported or oversized file: %s", relative);
    relative[base] = 0;
    if (result)
      break;
    errno = 0;
  }
  if (!result && errno)
    result = fail(e, "Directory scan failed");
  closedir(dir);
  relative[base] = 0;
  return result;
}

typedef struct {
  pos_backup_info_t *info;
  pos_recovery_progress_t progress;
  void *ctx;
  pos_error_t *error;
} scan_t;
static int scan_file(void *opaque, const char *relative, const char *full,
                     uint32_t size) {
  (void)full;
  scan_t *s = opaque;
  if (++s->info->files > POS_BACKUP_FILES)
    return fail(s->error, "Backup exceeds %d files; use a computer backup",
                POS_BACKUP_FILES);
  s->info->bytes += size;
  s->info->required += size + 1024; /* local header, descriptor, central
                                       directory + conservative overhead */
  if (s->info->required > POS_BACKUP_LIMIT)
    return fail(
        s->error,
        "Backup exceeds the device's 2 GiB seek limit; use a computer backup");
  return emit(s->progress, s->ctx, "scan", relative, s->info->bytes,
              s->info->files, s->error);
}
static int zip_scan(const char *root, pos_backup_info_t *info,
                    pos_recovery_progress_t progress, void *ctx,
                    pos_error_t *error) {
  memset(info, 0, sizeof(*info));
  info->required = 262144;
  scan_t scan = {info, progress, ctx, error};
  char relative[256] = "", full[POS_FS_PATH];
  return walk(root, relative, 0, scan_file, &scan, error, full);
}

int pos_backup_scan(const char *root, pos_backup_info_t *info,
                    pos_recovery_progress_t progress, void *ctx,
                    pos_error_t *error) {
  if (pos_backup_set_scan(root, info, progress, ctx, error))
    return -1;
  if (info->directory_backup)
    return 0;
  return zip_scan(root, info, progress, ctx, error);
}

typedef struct {
  FILE *out, *central;
  unsigned char *buffer;
  uint32_t entries;
  uint64_t processed, total;
  pos_recovery_progress_t progress;
  void *ctx;
  pos_error_t *error;
} writer_t;

static int zip_entry(writer_t *w, const char *name, FILE *source,
                     const void *memory, uint32_t size) {
  long offset = ftell(w->out);
  size_t length = strlen(name);
  if (offset < 0 || length > 255)
    return -1;
  unsigned char local[30] = {0};
  w32(local, 0x04034b50);
  w16(local + 4, 20);
  w16(local + 6, ZIP_FLAGS);
  w16(local + 12, 0x21);
  w16(local + 26, length);
  if (write_bytes(w->out, local, sizeof(local)) ||
      write_bytes(w->out, name, length))
    return -1;
  uint32_t crc = 0, read = 0;
  while (read < size) {
    size_t n = size - read < 8192 ? size - read : 8192;
    if (source) {
      if (fread(w->buffer, 1, n, source) != n)
        return -1;
    } else
      memcpy(w->buffer, (const unsigned char *)memory + read, n);
    crc = pos_crc32(crc, w->buffer, n);
    if (write_bytes(w->out, w->buffer, n))
      return -1;
    read += n;
    if (source)
      w->processed += n;
    if (emit(w->progress, w->ctx, "backup", name, w->processed, w->total,
             w->error))
      return -1;
  }
  if (source && (fgetc(source) != EOF || ferror(source)))
    return -1;
  unsigned char descriptor[16];
  w32(descriptor, 0x08074b50);
  w32(descriptor + 4, crc);
  w32(descriptor + 8, size);
  w32(descriptor + 12, size);
  if (write_bytes(w->out, descriptor, sizeof(descriptor)))
    return -1;
  unsigned char central[46] = {0};
  w32(central, 0x02014b50);
  w16(central + 4, 20);
  w16(central + 6, 20);
  w16(central + 8, ZIP_FLAGS);
  w16(central + 14, 0x21);
  w32(central + 16, crc);
  w32(central + 20, size);
  w32(central + 24, size);
  w16(central + 28, length);
  w32(central + 42, (uint32_t)offset);
  if (write_bytes(w->central, central, sizeof(central)) ||
      write_bytes(w->central, name, length))
    return -1;
  ++w->entries;
  return 0;
}
static int backup_file(void *opaque, const char *relative, const char *full,
                       uint32_t size) {
  writer_t *w = opaque;
  char name[256];
  if (snprintf(name, sizeof(name), "files/%s", relative) >= (int)sizeof(name))
    return -1;
  FILE *f = fopen(full, "rb");
  if (!f)
    return fail(w->error, "Cannot read %s", relative);
  int result = zip_entry(w, name, f, NULL, size);
  if (fclose(f))
    result = -1;
  return result ? fail(w->error, "Backup incomplete at %s; originals retained",
                       relative)
                : 0;
}

static int open_directory(FILE *f, directory_t *d) {
  if (fseek(f, 0, SEEK_END))
    return -1;
  long size = ftell(f);
  if (size < 22 || size > POS_BACKUP_LIMIT)
    return -1;
  unsigned char end[22];
  if (fseek(f, size - 22, SEEK_SET) || fread(end, 1, 22, f) != 22 ||
      u32(end) != 0x06054b50 || u16(end + 4) || u16(end + 6) ||
      u16(end + 8) != u16(end + 10) || u16(end + 20))
    return -1;
  d->f = f;
  d->entries = u16(end + 10);
  d->size = u32(end + 12);
  d->start = u32(end + 16);
  if (!d->entries || d->entries > POS_BACKUP_FILES + 1 ||
      (uint64_t)d->start + d->size + 22 != (uint64_t)size)
    return -1;
  return fseek(f, d->start, SEEK_SET);
}
static int read_entry(directory_t *d, entry_t *entry) {
  unsigned char h[46];
  if (fread(h, 1, 46, d->f) != 46 || u32(h) != 0x02014b50 ||
      u16(h + 8) != ZIP_FLAGS || u16(h + 10) || u32(h + 20) != u32(h + 24) ||
      u16(h + 30) || u16(h + 32) || u16(h + 34) ||
      (((u32(h + 38) >> 16) & 0170000) != 0 &&
       ((u32(h + 38) >> 16) & 0170000) != 0100000))
    return -1;
  size_t n = u16(h + 28);
  if (!n || n >= sizeof(entry->name) || fread(entry->name, 1, n, d->f) != n ||
      memchr(entry->name, 0, n))
    return -1;
  entry->name[n] = 0;
  entry->crc = u32(h + 16);
  entry->size = u32(h + 24);
  entry->offset = u32(h + 42);
  return 0;
}
static int local_data(directory_t *d, const entry_t *entry, uint32_t *data) {
  unsigned char h[30];
  char name[256];
  size_t n = strlen(entry->name);
  if (entry->offset >= d->start || fseek(d->f, entry->offset, SEEK_SET) ||
      fread(h, 1, 30, d->f) != 30 || u32(h) != 0x04034b50 ||
      u16(h + 6) != ZIP_FLAGS || u16(h + 8) || u16(h + 26) != n ||
      u16(h + 28) || fread(name, 1, n, d->f) != n ||
      memcmp(name, entry->name, n))
    return -1;
  uint64_t start = (uint64_t)entry->offset + 30 + n;
  if (start + entry->size + 16 > d->start)
    return -1;
  *data = (uint32_t)start;
  return 0;
}
static uint64_t path_hash(const char *name, size_t n) {
  char lower[256], hex[65];
  for (size_t i = 0; i < n; ++i)
    lower[i] = (char)tolower((unsigned char)name[i]);
  if (pos_hash_data(lower, n, hex))
    return 0;
  uint64_t result = 0;
  for (int i = 0; i < 16; ++i)
    result = (result << 4) | (hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
  return result;
}
static int compare_hash(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

int pos_backup_verify(const char *archive, pos_backup_info_t *info,
                      pos_recovery_progress_t progress, void *ctx,
                      pos_error_t *error) {
  struct stat kind;
  if (!pos_fs_lstat(archive, &kind) && S_ISDIR(kind.st_mode))
    return pos_backup_set_verify(archive, info, progress, ctx, error);
  memset(info, 0, sizeof(*info));
  FILE *f = fopen(archive, "rb");
  if (!f)
    return fail(error, "Missing or unreadable backup");
  directory_t d;
  unsigned char *buffer = malloc(8192);
  uint64_t *names = NULL;
  int result = -1;
  if (!buffer || open_directory(f, &d))
    goto done;
  names = malloc(d.entries * sizeof(*names));
  if (!names)
    goto done;
  uint32_t expected = 0;
  uint64_t sum = 0, declared = 0, required = 8 * 65536;
  uint32_t declared_files = 0;
  for (uint32_t i = 0; i < d.entries; ++i) {
    entry_t entry;
    long next;
    if (read_entry(&d, &entry) || (next = ftell(f)) < 0 ||
        entry.offset != expected)
      goto done;
    if (i == 0) {
      if (strcmp(entry.name, "POS-Backup.json") || entry.size > 2048)
        goto done;
    } else if (strncmp(entry.name, "files/", 6) ||
               !pos_fs_archive_path(entry.name + 6) ||
               !pos_fs_user_path(entry.name + 6))
      goto done;
    names[i] = path_hash(entry.name, strlen(entry.name));
    if (!names[i])
      goto done;
    uint32_t data;
    if (local_data(&d, &entry, &data))
      goto done;
    uint32_t crc = 0, total = 0;
    while (total < entry.size) {
      size_t n = entry.size - total < 8192 ? entry.size - total : 8192;
      if (fread(buffer, 1, n, f) != n)
        goto done;
      crc = pos_crc32(crc, buffer, n);
      total += n;
      if (emit(progress, ctx, "verify", entry.name, sum + total, 0, error))
        goto done;
    }
    unsigned char descriptor[16];
    if (crc != entry.crc || fread(descriptor, 1, 16, f) != 16 ||
        u32(descriptor) != 0x08074b50 || u32(descriptor + 4) != crc ||
        u32(descriptor + 8) != entry.size || u32(descriptor + 12) != entry.size)
      goto done;
    expected = data + entry.size + 16;
    if (i == 0) {
      if (fseek(f, data, SEEK_SET) ||
          fread(buffer, 1, entry.size, f) != entry.size ||
          memchr(buffer, 0, entry.size))
        goto done;
      buffer[entry.size] = 0;
      if (!pos_fs_json_safe((char *)buffer, entry.size))
        goto done;
      cJSON *j = cJSON_ParseWithOpts((char *)buffer, NULL, true);
      cJSON *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
            *migration = cJSON_GetObjectItemCaseSensitive(j, "migration"),
            *bytes = cJSON_GetObjectItemCaseSensitive(j, "source_bytes"),
            *files = cJSON_GetObjectItemCaseSensitive(j, "files"),
            *version = cJSON_GetObjectItemCaseSensitive(j, "source_version");
      bool ok = cJSON_IsNumber(format) && format->valuedouble == 1 &&
                cJSON_IsNumber(migration) && migration->valuedouble == 1 &&
                cJSON_IsNumber(bytes) && bytes->valuedouble >= 0 &&
                bytes->valuedouble <= POS_BACKUP_LIMIT &&
                bytes->valuedouble == (uint32_t)bytes->valuedouble &&
                cJSON_IsNumber(files) && files->valuedouble == d.entries - 1 &&
                cJSON_IsString(version) &&
                strlen(version->valuestring) < sizeof(info->source_version);
      if (ok) {
        declared = (uint32_t)bytes->valuedouble;
        declared_files = files->valueint;
        strcpy(info->source_version, version->valuestring);
      }
      cJSON_Delete(j);
      if (!ok)
        goto done;
    } else {
      sum += entry.size;
      size_t depth = 0;
      for (const char *p = entry.name + 6; *p; ++p)
        depth += *p == '/';
      required += entry.size + (depth + 3) * 65536;
    }
    if (fseek(f, next, SEEK_SET))
      goto done;
  }
  if (ftell(f) != (long)(d.start + d.size) || expected != d.start ||
      sum != declared || d.entries != declared_files + 1)
    goto done;
  qsort(names, d.entries, sizeof(*names), compare_hash);
  for (uint32_t i = 1; i < d.entries; ++i)
    if (names[i] == names[i - 1])
      goto done;
  if (fseek(f, d.start, SEEK_SET))
    goto done;
  for (uint32_t i = 0; i < d.entries; ++i) {
    entry_t entry;
    if (read_entry(&d, &entry))
      goto done;
    for (const char *p = strchr(entry.name, '/'); p; p = strchr(p + 1, '/')) {
      uint64_t hash = path_hash(entry.name, (size_t)(p - entry.name));
      if (bsearch(&hash, names, d.entries, sizeof(*names), compare_hash))
        goto done;
    }
  }
  info->bytes = sum;
  info->files = declared_files;
  info->required = required;
  result = 0;
done:
  free(buffer);
  free(names);
  if (fclose(f))
    result = -1;
  return result ? fail(error, "Invalid, unsupported, cancelled or corrupt "
                              "backup; original archive preserved")
                : 0;
}

typedef struct {
  pos_recovery_progress_t progress;
  void *ctx;
  const char *name;
  uint64_t base, total;
  pos_error_t *error;
} match_progress_t;
static int match_tick(void *opaque, uint64_t done, uint64_t total) {
  (void)total;
  match_progress_t *m = opaque;
  return emit(m->progress, m->ctx, "compare", m->name, m->base + done, m->total,
              m->error);
}
int pos_backup_matches(const char *root, const char *archive,
                       pos_error_t *error) {
  return pos_backup_matches_progress(root, archive, NULL, NULL, error);
}
int pos_backup_matches_progress(const char *root, const char *archive,
                                pos_recovery_progress_t progress, void *ctx,
                                pos_error_t *error) {
  struct stat kind;
  if (!pos_fs_lstat(archive, &kind) && S_ISDIR(kind.st_mode))
    return pos_backup_set_matches_progress(root, archive, progress, ctx, error);
  pos_backup_info_t stored, current;
  if (pos_backup_verify(archive, &stored, progress, ctx, error) ||
      pos_backup_scan(root, &current, progress, ctx, error))
    return -1;
  if (stored.files != current.files || stored.bytes != current.bytes)
    return fail(
        error,
        "Backup does not match current user files; create a fresh backup");
  FILE *f = fopen(archive, "rb");
  if (!f)
    return -1;
  directory_t d;
  int result = -1;
  if (open_directory(f, &d))
    goto done;
  match_progress_t m = {progress, ctx, "", 0, stored.bytes, error};
  for (uint32_t i = 0; i < d.entries; ++i) {
    entry_t entry;
    char path[POS_FS_PATH];
    uint32_t crc;
    if (read_entry(&d, &entry))
      goto done;
    m.name = entry.name + 6;
    if (i &&
        (pos_fs_join(path, root, entry.name + 6) ||
         pos_fs_crc_file_progress(path, entry.size, &crc, match_tick, &m) ||
         crc != entry.crc))
      goto done;
    if (i)
      m.base += entry.size;
  }
  result = 0;
done:
  fclose(f);
  return result
             ? fail(
                   error,
                   "Backup differs from current user data; originals preserved")
             : 0;
}

int pos_backup_create(const char *root, const char *version, uint64_t available,
                      int64_t creation_time, pos_recovery_progress_t progress,
                      void *ctx, pos_error_t *error) {
  pos_backup_info_t info;
  if (pos_backup_scan(root, &info, progress, ctx, error))
    return -1;
  if (info.directory_backup)
    return pos_backup_set_create(root, version, available, creation_time,
                                 progress, ctx, error);
  char final[POS_FS_PATH], temp[POS_FS_PATH], central[POS_FS_PATH];
  if (pos_fs_join(final, root, POS_BACKUP_NAME) ||
      pos_fs_join(temp, root, POS_BACKUP_NAME ".part") ||
      pos_fs_join(central, root, POS_BACKUP_NAME ".central.part"))
    return fail(error, "Backup path too long");
  char other[POS_FS_PATH];
  if (pos_fs_join(other, root, POS_BACKUP_SET) || !absent(other))
    return fail(error,
                "Existing directory backup preserved; move it aside first");
  if (!absent(final))
    return fail(error, "Existing backup preserved. Copy it to your computer "
                       "and choose a fresh card/filename");
  if (available < info.required + 65536)
    return fail(error,
                "Insufficient temporary space: source %llu, required %llu, "
                "available %llu",
                (unsigned long long)info.bytes,
                (unsigned long long)(info.required + 65536),
                (unsigned long long)available);
  if (!absent(temp) || !absent(central))
    return fail(
        error,
        "Incomplete backup files preserved; move them aside before retrying");
  writer_t w = {
      .total = info.bytes, .progress = progress, .ctx = ctx, .error = error};
  w.out = fopen(temp, "wx");
  w.central = fopen(central, "wx");
  w.buffer = malloc(8192);
  int result = -1;
  if (!w.out || !w.central || !w.buffer)
    goto done;
  cJSON *j = cJSON_CreateObject();
  if (!j)
    goto done;
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddNumberToObject(j, "migration", 1);
  cJSON_AddStringToObject(j, "source_version", version);
  cJSON_AddNumberToObject(j, "created_at", creation_time);
  cJSON_AddNumberToObject(j, "source_bytes", info.bytes);
  cJSON_AddNumberToObject(j, "files", info.files);
  cJSON_AddStringToObject(j, "integrity",
                          "ZIP CRC32 per file; archive SHA256 sidecar");
  char *metadata = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!metadata)
    goto done;
  int bad = zip_entry(&w, "POS-Backup.json", NULL, metadata,
                      (uint32_t)strlen(metadata));
  free(metadata);
  if (bad)
    goto done;
  char relative[256] = "", full[POS_FS_PATH];
  if (walk(root, relative, 0, backup_file, &w, error, full) ||
      w.entries != info.files + 1 || w.processed != info.bytes)
    goto done;
  long start = ftell(w.out);
  if (start < 0 || close_sync(w.central)) {
    w.central = NULL;
    goto done;
  }
  w.central = NULL;
  FILE *index = fopen(central, "rb");
  if (!index)
    goto done;
  size_t n;
  while ((n = fread(w.buffer, 1, 8192, index)))
    if (write_bytes(w.out, w.buffer, n)) {
      fclose(index);
      goto done;
    }
  bad = ferror(index);
  if (fclose(index))
    bad = 1;
  if (bad)
    goto done;
  long end = ftell(w.out);
  if (end < 0 || (uint64_t)end + 22 > POS_BACKUP_LIMIT)
    goto done;
  unsigned char eocd[22] = {0};
  w32(eocd, 0x06054b50);
  w16(eocd + 8, w.entries);
  w16(eocd + 10, w.entries);
  w32(eocd + 12, end - start);
  w32(eocd + 16, start);
  if (write_bytes(w.out, eocd, sizeof(eocd)))
    goto done;
  bad = close_sync(w.out);
  w.out = NULL;
  if (bad)
    goto done;
  pos_backup_info_t checked;
  if (pos_backup_verify(temp, &checked, progress, ctx, error))
    goto done;
  char hash[65];
  if (pos_hash_file(temp, (uint32_t)(end + 22), hash))
    goto done;
  if (pos_backup_matches_progress(root, temp, progress, ctx, error))
    goto done;
  if (pos_fs_publish_new(temp, final))
    goto done;
  char sidecar[POS_FS_PATH];
  if (!pos_fs_join(sidecar, root, POS_BACKUP_NAME ".sha256"))
    (void)pos_fs_write_new(
        sidecar, hash,
        64); /* CRC-verified ZIP remains useful if sidecar write fails */
  unlink(central);
  result = 0;
done:
  if (w.out)
    fclose(w.out);
  if (w.central)
    fclose(w.central);
  free(w.buffer);
  if (result && (!error || !error->message[0]))
    return fail(error, "Backup incomplete; originals and .part files retained");
  return result;
}

static int settings_policy(FILE *f, uint32_t data, const entry_t *entry,
                           pos_settings_t *settings) {
  if (strncasecmp(entry->name + 6, "POS/Users/settings.", 19))
    return 0;
  if (entry->size > 2048)
    return -1;
  char text[2049];
  if (fseek(f, data, SEEK_SET) || fread(text, 1, entry->size, f) != entry->size)
    return -1;
  text[entry->size] = 0;
  return pos_migration_decode(entry->name + 6, text, entry->size, settings);
}

int pos_backup_restore(const char *root, const char *archive,
                       uint64_t available, pos_backup_info_t *info,
                       pos_recovery_progress_t progress, void *ctx,
                       pos_error_t *error) {
  struct stat kind;
  if (!pos_fs_lstat(archive, &kind) && S_ISDIR(kind.st_mode))
    return pos_backup_set_restore(root, archive, available, info, progress, ctx,
                                  error);
  if (pos_backup_verify(archive, info, progress, ctx, error))
    return -1;
  if (available < info->required)
    return fail(error, "Insufficient space to restore safely; backup retained");
  struct stat st;
  if (stat(archive, &st) || st.st_size < 0 || st.st_size > POS_BACKUP_LIMIT)
    return -1;
  char id[65];
  if (pos_backup_identity_progress(archive, id, progress, ctx, error))
    return -1;
  char scratch[POS_FS_PATH];
  if (pos_fs_join(scratch, root, "POS/Temp") || pos_fs_mkdirs(scratch))
    return fail(error,
                "Cannot initialize restore scratch folder; backup retained");
  FILE *f = fopen(archive, "rb");
  if (!f)
    return -1;
  directory_t d;
  unsigned char *buffer = malloc(8192);
  int result = -1;
  if (!buffer || open_directory(f, &d))
    goto done;
  uint64_t restored = 0;
  pos_settings_t migrated = {0};
  bool have_legacy = false;
  for (uint32_t i = 0; i < d.entries; ++i) {
    entry_t entry;
    if (read_entry(&d, &entry))
      goto done;
    long next = ftell(f);
    if (i == 0)
      continue;
    uint32_t data;
    if (local_data(&d, &entry, &data))
      goto done;
    char dest[POS_FS_PATH], relative[300];
    pos_settings_t candidate;
    int policy = settings_policy(f, data, &entry, &candidate);
    if ((policy == 1 &&
         (!have_legacy || candidate.generation > migrated.generation)) ||
        (policy == 2 && !have_legacy)) {
      migrated = candidate;
      have_legacy = true;
    }
    if (pos_fs_join(dest, root, entry.name + 6))
      goto done;
    uint32_t existing;
    bool compatible = policy == 0;
    bool exists = !pos_fs_lstat(dest, &st);
    if (exists && compatible && !pos_fs_crc_file(dest, entry.size, &existing) &&
        existing == entry.crc) {
      if (pos_migration_record(root, id, entry.name + 6, entry.name + 6,
                               "already verified"))
        goto done;
      restored += entry.size;
      if (fseek(f, next, SEEK_SET))
        goto done;
      continue;
    }
    if (exists || !compatible) {
      snprintf(relative, sizeof(relative), "POS/Recovered/%.12s/%s", id,
               entry.name + 6);
      if (pos_fs_join(dest, root, relative))
        goto done;
      ++info->conflicts;
      if (!stat(dest, &st)) {
        if (pos_fs_crc_file(dest, entry.size, &existing) ||
            existing != entry.crc)
          goto done;
        restored += entry.size;
        if (fseek(f, next, SEEK_SET))
          goto done;
        continue;
      }
    }
    /* Parent-directory conflicts are preserved under Recovered as well. */
    char parent[POS_FS_PATH];
    strcpy(parent, dest);
    char *slash = strrchr(parent, '/');
    if (!slash)
      goto done;
    *slash = 0;
    if (pos_fs_mkdirs(parent)) {
      snprintf(relative, sizeof(relative), "POS/Recovered/%.12s/%s", id,
               entry.name + 6);
      if (pos_fs_join(dest, root, relative))
        goto done;
      strcpy(parent, dest);
      slash = strrchr(parent, '/');
      *slash = 0;
      if (pos_fs_mkdirs(parent))
        goto done;
      ++info->conflicts;
    }
    char temp[POS_FS_PATH];
    snprintf(relative, sizeof(relative), "POS/Temp/restore-%u.part",
             (unsigned)i);
    if (pos_fs_join(temp, root, relative))
      goto done;
    if (unlink(temp) && errno != ENOENT)
      goto done;
    FILE *out = fopen(temp, "wx");
    if (!out)
      goto done;
    int bad = fseek(f, data, SEEK_SET);
    uint32_t read = 0, crc = 0;
    while (!bad && read < entry.size) {
      size_t n = entry.size - read < 8192 ? entry.size - read : 8192;
      if (fread(buffer, 1, n, f) != n || write_bytes(out, buffer, n)) {
        bad = 1;
        break;
      }
      crc = pos_crc32(crc, buffer, n);
      read += n;
      if (emit(progress, ctx, "restore", entry.name + 6, restored + read,
               info->bytes, error))
        bad = 1;
    }
    if (close_sync(out))
      bad = 1;
    uint32_t check;
    match_progress_t m = {progress, ctx,         entry.name + 6,
                          restored, info->bytes, error};
    if (bad || crc != entry.crc ||
        pos_fs_crc_file_progress(temp, entry.size, &check, match_tick, &m) ||
        check != entry.crc)
      goto done;
    if (!absent(dest) || pos_fs_publish_new(temp, dest))
      goto done; /* Never overwrite a user file. */
    if (pos_migration_record(
            root, id, entry.name + 6, dest + strlen(root) + 1,
            policy > 0   ? "settings original retained; migration pending"
            : policy < 0 ? "unsupported or corrupt settings preserved"
                         : "verified"))
      goto done;
    restored += entry.size;
    if (fseek(f, next, SEEK_SET))
      goto done;
  }
  if (have_legacy && (emit(progress, ctx, "migration", "settings", info->bytes,
                           info->bytes, error) ||
                      pos_migration_apply(root, &migrated) ||
                      emit(progress, ctx, "migration-applied", "settings",
                           info->bytes, info->bytes, error)))
    goto done;
  if (pos_migration_record(root, id, "", "", "restore complete"))
    goto done;
  result = 0;
done:
  free(buffer);
  if (fclose(f))
    result = -1;
  return result ? fail(error, "Restore incomplete; original backup retained. "
                              "Retry safely or open ZIP on a computer")
                : 0;
}
