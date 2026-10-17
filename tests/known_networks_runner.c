#include "nvs.h"
#include "pos_network_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned entropy;
static bool failing;
static unsigned char key[32], known[POS_WIFI_VAULT_BYTES];
static size_t key_size, known_size;
int pos_device_wifi_prepare(void) { return 0; }
void esp_fill_random(void *p, size_t n) {
  unsigned char *b = p;
  for (size_t i = 0; i < n; ++i)
    b[i] = (unsigned char)(++entropy * 17);
}
int nvs_flash_init(void) { return 0; }
int nvs_open(const char *name, int mode, nvs_handle_t *h) {
  assert(!strcmp(name, "pos_wifi") && mode == NVS_READWRITE);
  *h = 1;
  return 0;
}
int nvs_get_blob(nvs_handle_t h, const char *name, void *p, size_t *n) {
  (void)h;
  size_t size = !strcmp(name, "known_key") ? key_size : known_size;
  const void *data =
      !strcmp(name, "known_key") ? (const void *)key : (const void *)known;
  if (!size)
    return ESP_ERR_NVS_NOT_FOUND;
  assert(*n >= size);
  memcpy(p, data, size);
  *n = size;
  return 0;
}
int nvs_set_blob(nvs_handle_t h, const char *name, const void *p, size_t n) {
  (void)h;
  if (failing)
    return -1;
  if (!strcmp(name, "known_key")) {
    assert(n == 32);
    memcpy(key, p, n);
    key_size = n;
  } else {
    assert(!strcmp(name, "known") && n == sizeof(known));
    memcpy(known, p, n);
    known_size = n;
  }
  return 0;
}
int nvs_commit(nvs_handle_t h) {
  (void)h;
  return 0;
}
void nvs_close(nvs_handle_t h) { (void)h; }
int main(void) {
  pos_wifi_profiles_t p = {0}, loaded;
  assert(pos_network_known(&loaded) == 1 && !loaded.count);
  p.count = 1;
  strcpy(p.networks[0].ssid, "Home");
  strcpy(p.networks[0].password, "really-secret-password");
  assert(!pos_network_known_save(&p));
  assert(!pos_network_known(&loaded) && !memcmp(&p, &loaded, sizeof(p)));
  unsigned char first[sizeof(known)], same_key[32];
  memcpy(first, known, sizeof(first));
  memcpy(same_key, key, 32);
  for (size_t i = 0; i + strlen("really-secret-password") <= sizeof(known); ++i)
    assert(memcmp(known + i, "really-secret-password",
                  strlen("really-secret-password")));
  assert(!pos_network_known_save(&p) && memcmp(first, known, sizeof(first)) &&
         !memcmp(same_key, key, 32));
  failing = true;
  p.count = 0;
  assert(pos_network_known_save(&p));
  failing = false;
  assert(!pos_network_known(&loaded) && loaded.count == 1);
  known[200] ^= 1;
  memcpy(first, known, sizeof(first));
  assert(pos_network_known(&loaded) < 0);
  assert(pos_network_known_save(&p));
  assert(!memcmp(first, known, sizeof(first)));
  puts("Device-key profiles round-trip without a passphrase; fresh nonces; "
       "failed writes and corrupt data preserved.");
}
