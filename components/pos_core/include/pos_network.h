#pragma once
#include "pos_fs.h"
#include "pos_install.h"

#define POS_WIFI_PROFILES 4
#define POS_WIFI_VAULT_BYTES 447
typedef struct {
  char ssid[33];
  int rssi;
  bool secured;
} pos_network_ap_t;
/* One menu entry per SSID/security class, retaining the strongest AP.
 * Names and open/secured networks are never silently conflated. */
unsigned pos_wifi_scan_merge(pos_network_ap_t *, unsigned count,
                             unsigned capacity, const pos_network_ap_t *);
typedef struct {
  char ssid[33], password[65];
} pos_wifi_profile_t;
typedef struct {
  uint8_t count;
  pos_wifi_profile_t networks[POS_WIFI_PROFILES];
} pos_wifi_profiles_t;
void pos_secure_zero(void *data, size_t size);
bool pos_wifi_profile_valid(const pos_wifi_profile_t *profile);
int pos_wifi_vault_encrypt(const pos_wifi_profiles_t *profiles,
                           const char *passphrase, const uint8_t salt[16],
                           const uint8_t nonce[12],
                           uint8_t output[POS_WIFI_VAULT_BYTES]);
int pos_wifi_vault_unlock(const uint8_t input[POS_WIFI_VAULT_BYTES],
                          const char *passphrase,
                          pos_wifi_profiles_t *profiles);
bool pos_https_url_valid(const char *url);
bool pos_transfer_name_valid(const char *name);
typedef int (*pos_stream_read_t)(void *, void *, size_t);
/* Stream, sync and verify before publishing; never replace an existing file.
 * UINT64_MAX means unknown HTTP content length. Optional SHA-256 is hex. */
int pos_receive_file(const char *root, const char *relative, uint64_t expected,
                     const char *sha256, pos_stream_read_t reader,
                     void *reader_ctx, pos_io_progress_t progress,
                     void *progress_ctx, pos_error_t *error);

int pos_receive_file_limit(const char *, const char *, uint64_t expected,
                           uint64_t limit, const char *, pos_stream_read_t,
                           void *, pos_io_progress_t, void *, pos_error_t *);
