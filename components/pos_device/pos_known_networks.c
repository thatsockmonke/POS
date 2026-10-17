#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pos_device.h"
#include "pos_network_service.h"
#include <stdio.h>
#include <string.h>
static int vault_handle(nvs_handle_t *h) {
  return nvs_flash_init() != ESP_OK ||
                 nvs_open("pos_wifi", NVS_READWRITE, h) != ESP_OK
             ? -1
             : 0;
}
/* The random per-device key is stored in NVS, never on SD or in logs. This
 * protects casual credential disclosure, not an attacker reading unencrypted
 * physical flash. We never burn eFuses or repartition an installed device. */
static int device_password(nvs_handle_t handle, char pass[65], bool create) {
  uint8_t key[32];
  size_t n = sizeof(key);
  esp_err_t status = nvs_get_blob(handle, "known_key", key, &n);
  if (status == ESP_ERR_NVS_NOT_FOUND && create) {
    if (pos_device_wifi_prepare())
      return -1;
    esp_fill_random(key, sizeof(key));
    if (nvs_set_blob(handle, "known_key", key, sizeof(key)) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
      pos_secure_zero(key, sizeof(key));
      return -1;
    }
    status = ESP_OK;
    n = sizeof(key);
  }
  if (status != ESP_OK || n != sizeof(key)) {
    pos_secure_zero(key, sizeof(key));
    return -1;
  }
  for (unsigned i = 0; i < sizeof(key); ++i)
    snprintf(pass + i * 2, 3, "%02x", key[i]);
  pos_secure_zero(key, sizeof(key));
  return 0;
}
int pos_network_known(pos_wifi_profiles_t *profiles) {
  nvs_handle_t h;
  memset(profiles, 0, sizeof(*profiles));
  if (vault_handle(&h))
    return -1;
  uint8_t blob[POS_WIFI_VAULT_BYTES];
  size_t n = sizeof(blob);
  esp_err_t status = nvs_get_blob(h, "known", blob, &n);
  char pass[65] = {0};
  int result =
      status == ESP_ERR_NVS_NOT_FOUND ? 1
      : status != ESP_OK || n != sizeof(blob) || device_password(h, pass, false)
          ? -1
          : pos_wifi_vault_unlock(blob, pass, profiles);
  nvs_close(h);
  pos_secure_zero(pass, sizeof(pass));
  pos_secure_zero(blob, sizeof(blob));
  return result;
}
int pos_network_known_save(const pos_wifi_profiles_t *profiles) {
  pos_wifi_profiles_t previous = {0};
  int existing = pos_network_known(&previous);
  pos_secure_zero(&previous, sizeof(previous));
  if (existing < 0)
    return -1;
  nvs_handle_t h;
  if (vault_handle(&h))
    return -1;
  char pass[65] = {0};
  uint8_t blob[POS_WIFI_VAULT_BYTES], salt[16], nonce[12];
  int bad = device_password(h, pass, true);
  esp_fill_random(salt, sizeof(salt));
  esp_fill_random(nonce, sizeof(nonce));
  if (!bad)
    bad = pos_wifi_vault_encrypt(profiles, pass, salt, nonce, blob);
  if (!bad)
    bad = nvs_set_blob(h, "known", blob, sizeof(blob)) != ESP_OK ||
          nvs_commit(h) != ESP_OK;
  nvs_close(h);
  pos_secure_zero(pass, sizeof(pass));
  pos_secure_zero(blob, sizeof(blob));
  return bad ? -1 : 0;
}
