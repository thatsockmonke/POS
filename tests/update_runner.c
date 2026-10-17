#include "cJSON.h"
#include "pos_battery.h"
#include "pos_core.h"
#include "pos_diagnostics.h"
#include "pos_network.h"
#include "pos_timezone.h"
#include "pos_update.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {
  const char *root, *feed, *prefix, *interrupt, *failure;
  unsigned writes, selections;
  uint64_t space;
} context_t;
static int free_bytes(void *opaque, const char *root, uint64_t *bytes) {
  (void)root;
  *bytes = ((context_t *)opaque)->space;
  return 0;
}
static int read_file(void *f, void *buffer, size_t cap) {
  size_t n = fread(buffer, 1, cap, f);
  return ferror(f) ? -1 : (int)n;
}
static int fetch(void *opaque, const char *url, const char *root,
                 const char *name, uint32_t limit, const char *sha,
                 pos_error_t *e) {
  context_t *c = opaque;
  const char *prefix = c->prefix;
  if (strncmp(url, prefix, strlen(prefix)))
    return -1;
  char full[POS_FS_PATH];
  if (pos_fs_join(full, c->feed, url + strlen(prefix)))
    return -1;
  FILE *in = fopen(full, "rb");
  if (!in)
    return -1;
  int result = pos_receive_file_limit(root, name, UINT64_MAX, limit, sha,
                                      read_file, in, NULL, NULL, e);
  fclose(in);
  return result;
}
static int write_firmware(void *opaque, const char *file,
                          const pos_package_t *p) {
  context_t *c = opaque;
  ++c->writes;
  if (!strcmp(c->failure, "write"))
    return -1;
  char hash[65];
  if (pos_hash_file(file, p->firmware.size, hash) ||
      strcmp(hash, p->firmware.sha256))
    return -1;
  return 0;
}
static int select_firmware(void *opaque) {
  context_t *c = opaque;
  ++c->selections;
  return !strcmp(c->failure, "select") ? -1 : 0;
}
static void report(void *opaque, const char *phase, const char *detail) {
  (void)detail;
  context_t *c = opaque;
  if (!strcmp(phase, c->interrupt))
    _Exit(99);
}
static int cancel(void *opaque, const char *phase, const char *file,
                  uint64_t done, uint64_t total) {
  (void)phase;
  (void)file;
  (void)done;
  (void)total;
  return !strcmp(((context_t *)opaque)->failure, "cancel");
}
static char *read_text(const char *file) {
  FILE *f = fopen(file, "rb");
  assert(f);
  char *s = calloc(1, 4096);
  assert(s);
  assert(fread(s, 1, 4095, f) < 4095);
  fclose(f);
  return s;
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  pos_error_t e = {0};
  const char *mode = argv[1], *root = argv[2];
  if (!strcmp(mode, "boot-policy")) {
    pos_boot_policy_t p = {0};
    assert(!pos_boot_policy_begin(&p, true, false) && p.pending);
    assert(!pos_boot_policy_begin(&p, true, false) && p.failures == 1);
    assert(!pos_boot_policy_begin(&p, true, true) && p.failures == 2);
    assert(pos_boot_policy_begin(&p, true, true) && p.failures == 3);
    pos_boot_policy_healthy(&p);
    assert(!p.pending && !p.failures && !p.forced);
    assert(!pos_boot_policy_begin(&p, true, true) && p.failures == 1);
    pos_boot_policy_healthy(&p);
    p.forced = true;
    assert(pos_boot_policy_begin(&p, false, false) && !p.pending &&
           !p.failures);
    return 0;
  }
  if (!strcmp(mode, "crash")) {
    pos_crash_record_t r = {.panic_snapshot = true,
                            .reset_reason = 4,
                            .failed_boots = 2,
                            .pc = 0x42001234,
                            .sp = 0x3fc01000,
                            .cause = 28};
    strcpy(r.boot_version, "0.4.0-dev");
    strcpy(r.panic_version, "0.3.9-dev");
    strcpy(r.panic_build, "abc123");
    char file[POS_FS_PATH];
    int result = pos_crash_record_write(root, &r, file);
    if (!result)
      puts(file);
    return result ? 1 : 0;
  }
  if (!strcmp(mode, "overlay")) {
    pos_view_t v;
    pos_view_clear(&v, "Long title covering the battery region");
    v.battery_overlay = true;
    v.battery_available = true;
    v.battery_percent = 42;
    v.battery_bars = 2;
    assert(pos_view_pixel(&v, 223, 1) == 0xffff);
    assert(pos_view_pixel(&v, 225, 3) == 0xffff);
    assert(pos_view_pixel(&v, 234, 3) == 0);
    assert(pos_view_pixel(&v, 239, 7) == 0);
    v.battery_full = true;
    v.battery_bars = 4;
    assert(pos_view_pixel(&v, 225, 3) == 0x07e0);
    v.battery_available = false;
    assert(pos_view_pixel(&v, 225, 3) == 0);
    v.solid_color = true;
    v.color_rgb565 = 0xf800;
    assert(pos_view_pixel(&v, 239, 7) == 0);
    assert(pos_view_pixel(&v, 100, 7) == 0xf800);
    v.solid_color = false;
    v.text_editor = true;
    v.editor_selected[0] = 1;
    assert(pos_view_pixel(&v, 0, 16) == 0x034c);
    return 0;
  }
  if (!strcmp(mode, "version")) {
    int c;
    if (pos_version_compare(argv[2], argv[3], &c))
      return 1;
    printf("%d\n", c);
    return 0;
  }
  if (!strcmp(mode, "timezone")) {
    printf("%u\n", (unsigned)pos_timezone_count);
    for (size_t i = 0; i < pos_timezone_count; ++i)
      assert(!pos_timezones[i].rule ||
             pos_timezone_valid(pos_timezones[i].rule));
    return pos_timezone_valid(argv[2]) ? 0 : 1;
  }
  if (!strcmp(mode, "utc")) {
    int64_t epoch;
    if (!pos_time_parse_utc(argv[2], &epoch))
      return 1;
    printf("%lld\n", (long long)epoch);
    return 0;
  }
  if (!strcmp(mode, "battery")) {
    pos_battery_t s = {0};
    FILE *f = fopen(argv[2], "rb");
    assert(f);
    unsigned long long t;
    int mv;
    while (fscanf(f, "%llu %d", &t, &mv) == 2) {
      pos_battery_sample(&s, mv, t);
      printf("%llu %d %d %u %d %d\n", t, s.filtered_mv, s.percent, s.bars,
             s.available, s.full);
    }
    fclose(f);
    return 0;
  }
  if (!strcmp(mode, "limit")) {
    FILE *f = fopen(argv[3], "rb");
    assert(f);
    int r = pos_receive_file_limit(root, "download.bin", UINT64_MAX,
                                   strtoul(argv[4], NULL, 10), NULL, read_file,
                                   f, NULL, NULL, &e);
    fclose(f);
    return r ? 1 : 0;
  }
  if (!strcmp(mode, "fingerprint")) {
    char *key = read_text(argv[2]), hash[65];
    int r = pos_update_key_fingerprint(key, hash);
    free(key);
    if (r)
      return 1;
    puts(hash);
    return 0;
  }
  if (!strcmp(mode, "recover")) {
    int r = pos_update_recover(root, argv[3], &e);
    printf("%d\n", r);
    if (r < 0)
      fprintf(stderr, "%s\n", e.message);
    return r < 0 ? 1 : 0;
  }
  if (!strcmp(mode, "confirm")) {
    uint32_t seq = 0;
    int r = pos_update_confirm(root, &seq, &e);
    printf("%u\n", seq);
    if (r)
      fprintf(stderr, "%s\n", e.message);
    return r ? 1 : 0;
  }
  assert(argc >= 7);
  char *key = read_text(argv[4]);
  pos_update_t *u = NULL;
  int r = pos_update_load(argv[3], key, argv[5], strtoul(argv[6], NULL, 10), &u,
                          &e);
  free(key);
  if (r) {
    fprintf(stderr, "%d: %s\n", r, e.message);
    return 1;
  }
  if (!strcmp(mode, "meta")) {
    printf("%s %u %u\n", u->release, u->sequence, u->firmware);
    pos_update_free(u);
    return 0;
  }
  context_t c = {.root = root,
                 .feed = argv[7],
                 .prefix = u->base_url,
                 .interrupt = argc > 8 ? argv[8] : "",
                 .failure = argc > 9 ? argv[9] : "",
                 .space =
                     argc > 10 ? strtoull(argv[10], NULL, 10) : 1ULL << 40};
  pos_update_platform_t p = {.ctx = &c,
                             .fetch = fetch,
                             .platform = {.ctx = &c,
                                          .free_bytes = free_bytes,
                                          .write_firmware = write_firmware,
                                          .select_firmware = select_firmware,
                                          .report = report,
                                          .progress = cancel}};
  r = pos_update_apply(root, u, &p, &e);
  printf("%u %u\n", c.writes, c.selections);
  pos_update_free(u);
  if (r)
    fprintf(stderr, "%s\n", e.message);
  return r ? 1 : 0;
}
