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

int pos_network_https_limit(const char *,const char *,const char *,const char *,const char *,const char *,const char *,pos_io_progress_t,void *,pos_error_t *,uint32_t limit);

/* Deployment routes only exist in an explicit foreground developer session. */
int pos_network_deploy_start(const char *root,char key[17]);
bool pos_network_deploy_take(char url[512]);
void pos_network_deploy_result(bool success);

/* Device-bound authenticated profiles; no repeated human vault password. */
int pos_network_known(pos_wifi_profiles_t *);
int pos_network_known_save(const pos_wifi_profiles_t *);
bool pos_network_auto_enabled(void);
int pos_network_auto_set(bool);
void pos_network_auto_pause(bool);
void pos_network_auto_start(void);
void pos_network_auto_stop(void);
