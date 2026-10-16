#include "pos_diagnostics.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
bool pos_boot_policy_begin(pos_boot_policy_t *s, bool supervised,
                           bool crash_reset) {
  if (supervised) {
    if ((s->pending || crash_reset) && s->failures < 1000000)
      ++s->failures;
    s->pending = true;
  }
  return s->forced || s->failures >= 3;
}
void pos_boot_policy_healthy(pos_boot_policy_t *s) {
  s->failures = 0;
  s->pending = false;
  s->forced = false;
}
static bool label(const char *s, size_t capacity) {
  if (!memchr(s, 0, capacity))
    return false;
  for (; *s; ++s)
    if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') ||
          (*s >= 'A' && *s <= 'Z') || *s == '.' || *s == '-' || *s == '_'))
      return false;
  return true;
}
int pos_crash_record_write(const char *root, const pos_crash_record_t *r,
                           char destination[POS_FS_PATH]) {
  if (!r || !label(r->boot_version, sizeof(r->boot_version)) ||
      !label(r->panic_version, sizeof(r->panic_version)) ||
      !label(r->panic_build, sizeof(r->panic_build)))
    return -1;
  char folder[POS_FS_PATH], text[768];
  char pos[POS_FS_PATH];
  struct stat st;
  bool installed = true;
  if (pos_fs_join(pos, root, "POS"))
    return -1;
  if (pos_fs_lstat(pos, &st)) {
    if (errno != ENOENT)
      return -1;
    installed = false;
  } else if (!S_ISDIR(st.st_mode))
    return -1;
  if (pos_fs_join(folder, root,
                  installed ? "POS/Users/Owner/Diagnostics/Crashes"
                            : "CrashReports") ||
      pos_fs_mkdirs(folder))
    return -1;
  int n = snprintf(
      text, sizeof(text),
      "{\"format\":1,\"kind\":\"reset-crash-record\",\"boot_version\":\"%s\","
      "\"panic_version\":\"%s\",\"panic_build\":\"%s\",\"reset_reason\":%u,"
      "\"utc\":%lld,\"failed_boots\":%u,\"panic_snapshot\":%s,\"core\":%u,"
      "\"exception\":%u,\"pc\":%u,\"sp\":%u,\"a0\":%u,\"cause\":%u,\"address\":"
      "%u}\n",
      r->boot_version, r->panic_version, r->panic_build,
      (unsigned)r->reset_reason, (long long)r->utc, (unsigned)r->failed_boots,
      r->panic_snapshot ? "true" : "false", (unsigned)r->core,
      (unsigned)r->exception, (unsigned)r->pc, (unsigned)r->sp, (unsigned)r->a0,
      (unsigned)r->cause, (unsigned)r->address);
  if (n < 0 || n >= (int)sizeof(text))
    return -1;
  for (unsigned i = 0; i < 10000; ++i) {
    char name[40];
    snprintf(name, sizeof(name), "crash-%u.json", i);
    if (pos_fs_join(destination, folder, name))
      return -1;
    struct stat st;
    if (!pos_fs_lstat(destination, &st))
      continue;
    if (errno != ENOENT || pos_fs_write_new(destination, text, n))
      return -1;
    uint32_t crc;
    return pos_fs_crc_file(destination, n, &crc) || crc != pos_crc32(0, text, n)
               ? -1
               : 0;
  }
  return -1;
}
