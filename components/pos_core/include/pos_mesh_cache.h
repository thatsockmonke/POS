#pragma once
#include "pos_mesh_modules.h"
#define POS_MESH_CACHE_RECORDS 128
typedef struct {
  char root[512];
  uint32_t next, session;
} pos_mesh_cache_t;
typedef struct {
  uint32_t sequence, utc, session, from, id;
  unsigned channel;
  uint8_t token[32], text[220];
  size_t length;
} pos_mesh_cached_t;
int pos_mesh_cache_open(pos_mesh_cache_t *, const char *, uint32_t session);
int pos_mesh_cache_put(pos_mesh_cache_t *, const pos_mesh_message_t *,
                       const uint8_t token[32], uint32_t utc);
/* Invalid/expired/overwritten slots are skipped. Only matching channel identity
 * can be replayed. Without UTC, records are limited to the current boot. */
int pos_mesh_cache_read(const pos_mesh_cache_t *, uint32_t sequence,
                        const uint8_t token[32], uint32_t utc,
                        unsigned window_minutes, pos_mesh_cached_t *);
