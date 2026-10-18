#pragma once
#include "pos_install.h"
#include "pos_mesh.h"
typedef struct {
  bool enabled, ready;
  uint32_t node, generation, frequency_hz;
  unsigned queued, nodes;
  pos_mesh_stats_t stats;
  uint8_t public_key[32];
  char latest[240], error[128];
} pos_meshtastic_status_t;
int pos_meshtastic_init(const char *root, uint32_t node);
pos_meshtastic_status_t pos_meshtastic_status(void);
int pos_meshtastic_settings(pos_mesh_config_t *);
/* Saves atomically before applying; changing protocol/config always drops TX
 * consent in the expansion service. Channel keys/private identity stay off SD.
 */
int pos_meshtastic_configure(const pos_mesh_config_t *, bool enabled,
                             pos_error_t *);
int pos_meshtastic_node(unsigned index, pos_mesh_node_t *);
int pos_meshtastic_forget(uint32_t node, pos_error_t *);
int pos_meshtastic_send(unsigned channel, uint32_t to, unsigned port,
                        const void *, size_t, bool want_response,
                        pos_error_t *);
int pos_meshtastic_announce(bool discover, pos_error_t *);
int pos_meshtastic_share(unsigned channel, uint32_t to, int32_t lat,
                         int32_t lon, uint32_t utc, pos_error_t *);
int pos_meshtastic_retry(pos_error_t *);
/* Expansion-task-only hardware bridge. It owns all SX1262/SPI operations. */
bool pos_meshtastic_modem(pos_mesh_modem_t *, uint32_t *generation);
void pos_meshtastic_receive(const uint8_t *, size_t, int rssi, int snr);
int pos_meshtastic_due(uint8_t wire[255], size_t *length);
void pos_meshtastic_tx_result(bool success);
void pos_meshtastic_pause(void);
void pos_meshtastic_stop(void);

/* Cached text only, nonblocking; no SD access on the display path. */
uint32_t pos_meshtastic_notification(char text[64], bool *sound);
int pos_meshtastic_notifications_configure(bool banner, bool sound);
