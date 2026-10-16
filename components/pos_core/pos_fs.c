#include "pos_fs.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int pos_fs_join(char out[POS_FS_PATH], const char *root, const char *relative) {
  int n = snprintf(out, POS_FS_PATH, "%s%s%s", root, *relative ? "/" : "",
                   relative);
  return n < 0 || n >= POS_FS_PATH ? -1 : 0;
}

int pos_fs_mkdirs(const char *path) {
  char copy[POS_FS_PATH];
  if (strlen(path) >= sizeof(copy))
    return -1;
  strcpy(copy, path);
  for (char *s = copy + 1;; ++s)
    if (*s == '/' || !*s) {
      char saved = *s;
      *s = 0;
      struct stat st;
      if (mkdir(copy, 0775) &&
          (errno != EEXIST || pos_fs_lstat(copy, &st) || !S_ISDIR(st.st_mode)))
        return -1;
      *s = saved;
      if (!saved)
        break;
    }
  return 0;
}

int pos_fs_write_new(const char *path, const void *bytes, size_t length) {
  FILE *f = fopen(path, "wx");
  if (!f)
    return -1;
  int result = fwrite(bytes, 1, length, f) == length ? 0 : -1;
  if (fflush(f) || fsync(fileno(f)))
    result = -1;
  if (fclose(f))
    result = -1;
  return result;
}

int pos_fs_layout(const char *root) {
  const char *dirs[] = {"POS/Users/Owner/Documents",
                        "POS/Users/Owner/Saves",
                        "POS/Maps",
                        "POS/Temp",
                        "POS/Updates",
                        "Downloads",
                        "Music",
                        "Recordings",
                        "Music/Playlists",
                        "POS/Users/Owner/Radio",
                        "POS/Users/Owner/Diagnostics/Crashes",
                        "POS/Users/Owner/RangeTests"};
  for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); ++i) {
    char path[POS_FS_PATH];
    if (pos_fs_join(path, root, dirs[i]) || pos_fs_mkdirs(path))
      return -1;
  }
  return 0;
}

int pos_fs_publish_new(const char *temporary, const char *destination) {
#ifdef ESP_PLATFORM
  /* FatFs rename refuses an existing destination. */
  return rename(temporary, destination);
#else
  /* POSIX rename overwrites files; use exclusive link publication instead. */
  if (link(temporary, destination))
    return -1;
  return unlink(temporary);
#endif
}

static bool under(const char *path, const char *reserved) {
  size_t n = strlen(reserved);
  return !strncasecmp(path, reserved, n) && (!path[n] || path[n] == '/');
}

bool pos_fs_user_path(const char *p) {
  const char *reserved[] = {"pos-update.pos",
                            "pos-update.pos.part",
                            "POS/System",
                            "POS/Temp",
                            "POS/Cache",
                            "POS/Updates",
                            "POS/.pos-transaction",
                            "POS.installing",
                            "POS.previous",
                            "pos-install",
                            "pos-reinstall.pos",
                            "pos-reinstall.pos.part",
                            ".POS-Recovery-Temp",
                            "POS/Users/.restore-dismissed",
                            "POS/Users/.restore-complete",
                            "POS-Filesystem-Backup",
                            "POS-Filesystem-Backup.part",
                            "POS-Filesystem-Backup.zip",
                            "POS-Filesystem-Backup.zip.part",
                            "POS-Filesystem-Backup.zip.central.part",
                            "POS-Filesystem-Backup.zip.sha256"};
  if (!p || !*p)
    return false;
  for (size_t i = 0; i < sizeof(reserved) / sizeof(*reserved); ++i)
    if (under(p, reserved[i]))
      return false;
  return true;
}

bool pos_fs_archive_path(const char *p) {
  if (!p || !*p || strlen(p) > 240 || *p == '/' || strchr(p, '\\') ||
      strchr(p, ':'))
    return false;
  for (const char *s = p; *s;) {
    const char *e = strchr(s, '/');
    size_t n = e ? (size_t)(e - s) : strlen(s);
    if (!n || n > 255 || (n == 1 && *s == '.') ||
        (n == 2 && !memcmp(s, "..", 2)) || s[n - 1] == '.' || s[n - 1] == ' ')
      return false;
    for (size_t i = 0; i < n; ++i)
      if ((unsigned char)s[i] < 32 || strchr("*?<>|\"", s[i]))
        return false;
    size_t stem = 0;
    while (stem < n && s[stem] != '.')
      ++stem;
    if (stem == 3 && (!strncasecmp(s, "con", 3) || !strncasecmp(s, "nul", 3) ||
                      !strncasecmp(s, "aux", 3) || !strncasecmp(s, "prn", 3)))
      return false;
    if (stem == 4 && (!strncasecmp(s, "com", 3) || !strncasecmp(s, "lpt", 3)) &&
        s[3] >= '1' && s[3] <= '9')
      return false;
    if (!e)
      break;
    s = e + 1;
    if (!*s)
      return false;
  }
  return true;
}

uint32_t pos_crc32(uint32_t previous, const void *data, size_t length) {
  uint32_t crc = ~previous;
  const unsigned char *p = data;
  while (length--) {
    crc ^= *p++;
    for (int i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

int pos_fs_crc_file(const char *path, uint32_t size, uint32_t *crc) {
  return pos_fs_crc_file_progress(path, size, crc, NULL, NULL);
}
int pos_fs_crc_file_progress(const char *path, uint32_t size, uint32_t *crc,
                             int (*progress)(void *, uint64_t, uint64_t),
                             void *ctx) {
  FILE *f = fopen(path, "rb");
  unsigned char *buffer = malloc(8192);
  if (!f || !buffer) {
    if (f)
      fclose(f);
    free(buffer);
    return -1;
  }
  uint64_t total = 0;
  size_t n;
  *crc = 0;
  int result = 0;
  while ((n = fread(buffer, 1, 8192, f))) {
    total += n;
    if (total > size) {
      result = -1;
      break;
    }
    *crc = pos_crc32(*crc, buffer, n);
    if (progress && progress(ctx, total, size)) {
      result = -1;
      break;
    }
  }
  if (ferror(f) || total != size)
    result = -1;
  if (fclose(f))
    result = -1;
  free(buffer);
  return result;
}

int pos_fs_lstat(const char *path, struct stat *st) {
#ifdef ESP_PLATFORM
  /* ESP VFS FAT has no symlinks or lstat; native tests must not follow them. */
  return stat(path, st);
#else
  return lstat(path, st);
#endif
}

bool pos_fs_json_safe(const char *text, size_t length) {
  if (memchr(text, 0, length) || strstr(text, "\\u0000"))
    return false;
  bool quoted = false, escaped = false;
  int depth = 0;
  for (size_t i = 0; i < length; ++i) {
    char c = text[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quoted && c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"')
      quoted = !quoted;
    if (!quoted && (c == '{' || c == '[') && ++depth > 8)
      return false;
    if (!quoted && (c == '}' || c == ']') && --depth < 0)
      return false;
  }
  return !quoted && depth == 0;
}

int pos_fs_file_size(const char *path, uint32_t *size) {
  struct stat st;
  if (pos_fs_lstat(path, &st) || !S_ISREG(st.st_mode))
    return -1;
#ifdef ESP_PLATFORM
  *size = (uint32_t)st.st_size;
#else
  if (st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX)
    return -1;
  *size = (uint32_t)st.st_size;
#endif
  return 0;
}
