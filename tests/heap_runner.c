/* POS/cJSON allocation budget, separate from shared OpenSSL and libc internals.
 * This models remaining application heap, not physical ESP32 heap usage. */
#include "heap_budget.h"
#include "pos_history.h"
#include "pos_music.h"
#include "pos_network.h"
#include "pos_update.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  if (!strcmp(argv[1], "audio") || !strcmp(argv[1], "audio-failures")) {
    assert(argc == 6);
    limit = strtoull(argv[4], NULL, 10);
    single = strtoull(argv[5], NULL, 10);
    if (!strcmp(argv[1], "audio-failures")) {
      pos_music_t *m = pos_music_open(argv[2], argv[3], &e);
      assert(m);
      size_t total = attempts;
      pos_music_close(m);
      assert(!used);
      for (size_t i = 1; i <= total; ++i) {
        attempts = 0;
        fail_at = i;
        m = pos_music_open(argv[2], argv[3], &e);
        assert(!m && !used && *e.message);
      }
      r = 0;
    } else {
      char path[POS_FS_PATH];
      assert(!pos_fs_join(path, argv[2], "decoded.raw"));
      /* Repeat in one process: opening the next track must reclaim everything.
       */
      for (unsigned run = 0; run < 3; ++run) {
        pos_music_t *m = pos_music_open(argv[2], argv[3], &e);
        if (!m)
          break;
        FILE *out = fopen(path, "wb");
        assert(out);
        r = 0;
        for (unsigned i = 0; i < 100000; ++i) {
          const int16_t *pcm;
          size_t frames;
          r = pos_music_read(m, &pcm, &frames, 100, &e);
          if (r < 0)
            break;
          assert(fwrite(pcm, 4, frames, out) == frames);
          if (pos_music_status(m).ended) {
            r = 0;
            break;
          }
          r = -1;
        }
        fclose(out);
        pos_music_close(m);
        assert(!used);
        if (r)
          break;
      }
    }
  } else if (!strcmp(argv[1], "load") || !strcmp(argv[1], "failures")) {
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
