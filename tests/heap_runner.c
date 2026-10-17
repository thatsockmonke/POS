/* POS/cJSON allocation budget, separate from shared OpenSSL and libc internals.
 * This models remaining application heap, not physical ESP32 heap usage. */
#include "pos_history.h"
#include "pos_network.h"
#include "pos_update.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
typedef union {
  max_align_t align;
  struct {
    size_t size;
    uint32_t magic;
  } h;
} allocation_t;
static size_t used, peak, largest, limit = SIZE_MAX, single = SIZE_MAX,
                                   attempts, fail_at, denied;
void *__wrap_malloc(size_t n) {
  ++attempts;
  if ((fail_at && attempts == fail_at) || n > single || n > limit - used ||
      n > SIZE_MAX - sizeof(allocation_t)) {
    ++denied;
    return NULL;
  }
  allocation_t *a = __real_malloc(sizeof(*a) + n);
  if (!a)
    return NULL;
  a->h.size = n;
  a->h.magic = 0x504f5348;
  used += n;
  if (used > peak)
    peak = used;
  if (n > largest)
    largest = n;
  return a + 1;
}
void __wrap_free(void *p) {
  if (!p)
    return;
  allocation_t *a = (allocation_t *)p - 1;
  assert(a->h.magic == 0x504f5348);
  used -= a->h.size;
  a->h.magic = 0;
  __real_free(a);
}
void *__wrap_calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size)
    return NULL;
  void *p = __wrap_malloc(n * size);
  if (p)
    memset(p, 0, n * size);
  return p;
}
void *__wrap_realloc(void *p, size_t n) {
  if (!p)
    return __wrap_malloc(n);
  if (!n) {
    __wrap_free(p);
    return NULL;
  }
  allocation_t *a = (allocation_t *)p - 1;
  assert(a->h.magic == 0x504f5348);
  ++attempts;
  if ((fail_at && attempts == fail_at) || n > single ||
      n > limit - used + a->h.size) {
    ++denied;
    return NULL;
  }
  size_t old = a->h.size;
  a = __real_realloc(a, sizeof(*a) + n);
  if (!a)
    return NULL;
  a->h.size = n;
  used = used - old + n;
  if (used > peak)
    peak = used;
  if (n > largest)
    largest = n;
  return a + 1;
}
static int flash_read(void *ctx, uint32_t offset, void *buffer, size_t n) {
  FILE *f = ctx;
  return fseek(f, offset, SEEK_SET) || fread(buffer, 1, n, f) != n;
}
static int space(void *ctx, const char *root, uint64_t *out) {
  (void)ctx;
  (void)root;
  *out = 1ULL << 40;
  return 0;
}
static int selected(void *ctx) {
  ++*(unsigned *)ctx;
  return 0;
}
static int written(void *ctx, const char *path, const pos_package_t *p) {
  (void)ctx;
  char hash[65];
  return pos_hash_file(path, p->firmware.size, hash) ||
         strcmp(hash, p->firmware.sha256);
}
static const char *feed;
static int reader(void *f, void *p, size_t n) {
  size_t got = fread(p, 1, n, f);
  return ferror(f) ? -1 : (int)got;
}
static int fetch(void *ctx, const char *url, const char *dir, const char *name,
                 uint32_t max, const char *sha, pos_error_t *e) {
  (void)ctx;
  const char *relative = strstr(url, "/feed/");
  assert(relative);
  char path[POS_FS_PATH];
  assert(!pos_fs_join(path, feed, relative + 6));
  FILE *f = fopen(path, "rb");
  assert(f);
  int r = pos_receive_file_limit(dir, name, UINT64_MAX, max, sha, reader, f,
                                 NULL, NULL, e);
  fclose(f);
  return r;
}
int main(int argc, char **argv) {
  assert(argc >= 3);
  pos_error_t e = {0};
  int r = -1;
  if (!strcmp(argv[1], "load") || !strcmp(argv[1], "failures")) {
    pos_package_t *p = NULL;
    if (!strcmp(argv[1], "failures")) {
      assert(!pos_load_manifest(argv[2], &p, &e));
      size_t total = attempts;
      pos_package_free(p);
      assert(!used);
      for (size_t i = 1; i <= total; ++i) {
        attempts = 0;
        fail_at = i;
        p = NULL;
        assert(pos_load_manifest(argv[2], &p, &e));
        assert(!p && !used);
      }
      fail_at = 0;
      r = 0;
    } else {
      assert(argc == 5);
      limit = strtoull(argv[3], NULL, 10);
      single = strtoull(argv[4], NULL, 10);
      r = pos_load_manifest(argv[2], &p, &e);
      if (!r)
        printf("records=%zu ", p->count);
      pos_package_free(p);
    }
  } else {
    /* held target -> archive current -> install target, with fresh signature
     * verification, streamed copies, hash checks and simulated flash hooks. */
    assert(argc == 9);
    char key[513];
    FILE *f = fopen(argv[3], "rb");
    assert(f);
    size_t n = fread(key, 1, 512, f);
    fclose(f);
    key[n] = 0;
    limit = 32768;
    single = 16384;
    pos_update_t *u = NULL;
    r = pos_update_load(argv[2], key, "0.6.4-dev", 0, &u, &e);
    unsigned selections = 0;
    pos_update_platform_t hooks = {.fetch = fetch,
                                   .platform = {.ctx = &selections,
                                                .free_bytes = space,
                                                .write_firmware = written,
                                                .select_firmware = selected}};
    feed = argv[8];
    if (!r) {
      f = fopen(argv[6], "rb");
      assert(f);
      r = pos_history_capture(argv[4], argv[5], key, flash_read, f,
                              &hooks.platform, NULL, &e);
      fclose(f);
    }
    if (!r)
      r = pos_update_apply(argv[4], u, &hooks, &e);
    if (!r)
      assert(selections == 1);
    pos_update_free(u);
  }
  assert(!used);
  printf("peak=%zu largest=%zu denied=%zu\n", peak, largest, denied);
  if (r)
    fprintf(stderr, "%s\n", e.message);
  return r ? 1 : 0;
}
