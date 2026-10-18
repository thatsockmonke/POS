#pragma once
#include "pos_mesh.h"
typedef struct {
  uint32_t time, present;
  unsigned kind;
  double values[22];
} pos_mesh_telemetry_t;
int pos_mesh_telemetry_decode(const void *, size_t, pos_mesh_telemetry_t *);
size_t pos_mesh_telemetry_device(uint8_t out[64], uint32_t utc, int battery_mv,
                                 uint32_t uptime);
typedef struct {
  unsigned rr, variant;
  uint32_t count, window, last_request, stats[9];
  uint8_t text[220];
  size_t length;
} pos_mesh_store_t;
int pos_mesh_store_decode(const void *, size_t, pos_mesh_store_t *);
size_t pos_mesh_store_encode(const pos_mesh_store_t *, uint8_t out[233]);
/* Channel identity fingerprint, never the key itself. */
int pos_mesh_channel_token(const pos_mesh_config_t *, unsigned,
                           uint8_t token[32]);
