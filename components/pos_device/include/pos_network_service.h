#pragma once
#include "pos_network.h"
typedef struct {
  char ssid[33];
  int rssi;
  bool secured;
} pos_network_ap_t;
typedef struct {
  bool connected;
  char ssid[33], ip[16];
  int rssi;
} pos_network_status_t;
int pos_network_scan(pos_network_ap_t *aps, unsigned capacity, unsigned *count);
pos_network_status_t pos_network_status(void);
/* Vault returns 1 when absent; secrets are only stored as authenticated
 * ciphertext. No eFuses, flash-encryption configuration or partitions change.
 */
int pos_network_profiles(const char *passphrase, pos_wifi_profiles_t *profiles);
int pos_network_remember(const char *passphrase,
                         const pos_wifi_profiles_t *profiles);
int pos_network_sync_time(void);
int pos_network_https(const char *url, const char *method, const char *body,
                      const char *content_type, const char *root,
                      const char *destination, const char *sha256,
                      pos_io_progress_t progress, void *ctx,
                      pos_error_t *error);
/* Explicit trusted-LAN session: one selected export; uploads only to Downloads.
 * Returns a random session key to display locally. Never log this key. */
int pos_network_share_start(const char *root, const char *export_file,
                            char key[17]);
void pos_network_share_stop(void);
