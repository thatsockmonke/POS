#include "pos_mesh_cache.h"
#include "pos_fs.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void put(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = v >> (8 * i);
}
static uint32_t get(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static int filename(const pos_mesh_cache_t *c, uint32_t slot, char path[512]) {
  char rel[96];
  snprintf(rel, sizeof(rel), "POS/Users/Owner/Radio/store-forward/%03u.bin",
           (unsigned)(slot % 128));
  return pos_fs_join(path, c->root, rel);
}
static int read_raw(const pos_mesh_cache_t *c, uint32_t slot, uint8_t b[288]) {
  char path[512];
  if (filename(c, slot, path))
    return -1;
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  bool bad = fread(b, 1, 288, f) != 288 || fgetc(f) != EOF || ferror(f);
  fclose(f);
  return bad || memcmp(b, "PSF1", 4) || get(b + 284) != pos_crc32(0, b, 284) ||
                 get(b + 28) > 220
             ? -1
             : 0;
}
int pos_mesh_cache_open(pos_mesh_cache_t *c, const char *root,
                        uint32_t session) {
  if (strlen(root) >= sizeof(c->root))
    return -1;
  memset(c, 0, sizeof(*c));
  strcpy(c->root, root);
  c->session = session;
  c->next = 1;
  char path[512];
  if (pos_fs_join(path, root, "POS/Users/Owner/Radio/store-forward") ||
      pos_fs_mkdirs(path))
    return -1;
  uint8_t b[288];
  for (unsigned i = 0; i < 128; ++i)
    if (!read_raw(c, i, b) && get(b + 4) >= c->next)
      c->next = get(b + 4) + 1;
  return c->next ? 0 : -1;
}
int pos_mesh_cache_put(pos_mesh_cache_t *c, const pos_mesh_message_t *m,
                       const uint8_t token[32], uint32_t utc) {
  if (!c->next || m->pki || m->header.to != UINT32_MAX ||
      m->data.port != POS_MESH_TEXT || m->data.length > 198)
    return -1;
  uint8_t b[288] = {0};
  memcpy(b, "PSF1", 4);
  put(b + 4, c->next);
  put(b + 8, utc);
  put(b + 12, c->session);
  put(b + 16, m->header.from);
  put(b + 20, m->header.id);
  put(b + 24, m->channel_index);
  put(b + 28, m->data.length);
  memcpy(b + 32, token, 32);
  memcpy(b + 64, m->data.payload, m->data.length);
  put(b + 284, pos_crc32(0, b, 284));
  char path[512], part[512];
  if (filename(c, c->next, path) ||
      snprintf(part, sizeof(part), "%s.part", path) >= 512)
    return -1;
  /* Cache is expendable: only replace a slot after its successor verifies. */
  (void)unlink(part);
  if (pos_fs_write_new(part, b, 288))
    return -1;
  FILE *f = fopen(part, "rb");
  uint8_t check[288];
  bool bad = !f;
  if (f) {
    bad = fread(check, 1, 288, f) != 288 || memcmp(check, b, 288);
    fclose(f);
  }
  if (bad)
    return -1;
  if (unlink(path) && access(path, F_OK) == 0)
    return -1;
  if (pos_fs_publish_new(part, path))
    return -1;
  ++c->next;
  return 0;
}
int pos_mesh_cache_read(const pos_mesh_cache_t *c, uint32_t sequence,
                        const uint8_t token[32], uint32_t utc, unsigned window,
                        pos_mesh_cached_t *r) {
  uint8_t b[288];
  if (!sequence || read_raw(c, sequence, b) || get(b + 4) != sequence ||
      memcmp(token, b + 32, 32) || get(b + 24) >= 8)
    return -1;
  uint32_t at = get(b + 8);
  if (at && utc) {
    if (at > utc || utc - at > (uint64_t)window * 60)
      return -1;
  } else if (get(b + 12) != c->session)
    return -1;
  memset(r, 0, sizeof(*r));
  r->sequence = sequence;
  r->utc = at;
  r->session = get(b + 12);
  r->from = get(b + 16);
  r->id = get(b + 20);
  r->channel = get(b + 24);
  r->length = get(b + 28);
  memcpy(r->token, b + 32, 32);
  memcpy(r->text, b + 64, r->length);
  return 0;
}
