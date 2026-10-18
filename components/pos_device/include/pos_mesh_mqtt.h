#pragma once
#include "pos_mesh.h"
/* Opt-in, text-only, one channel per session. Credentials are RAM-only. */
typedef struct {
  char uri[128], root[64], user[64], password[128];
  unsigned channel;
  bool downlink;
} pos_mesh_mqtt_config_t;
int pos_mesh_mqtt_start(const pos_mesh_mqtt_config_t *);
void pos_mesh_mqtt_stop(void);
void pos_mesh_mqtt_status(char out[128]);
void pos_mesh_mqtt_uplink(const uint8_t *, size_t);
int pos_meshtastic_mqtt_accept(const uint8_t *, size_t, unsigned channel,
                               uint32_t generation, bool incoming);
