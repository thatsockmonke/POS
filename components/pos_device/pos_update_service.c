#include "pos_update_service.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pos_network_service.h"
#include <stdlib.h>
#include <string.h>
static bool valid(const pos_update_source_t *s) {
  char fingerprint[65];
  return s->format == 1 && memchr(s->url, 0, sizeof(s->url)) &&
         memchr(s->key, 0, sizeof(s->key)) &&
         (!*s->url || pos_https_url_valid(s->url)) &&
         (!*s->key || !pos_update_key_fingerprint(s->key, fingerprint));
}
int pos_update_source_load(pos_update_source_t *s) {
  memset(s, 0, sizeof(*s));
  s->format = 1;
  if (nvs_flash_init() != ESP_OK)
    return -1;
  nvs_handle_t h;
  esp_err_t result = nvs_open("pos_update", NVS_READONLY, &h);
  if (result == ESP_ERR_NVS_NOT_FOUND)
    return 0;
  if (result != ESP_OK)
    return -1;
  size_t size = sizeof(*s);
  result = nvs_get_blob(h, "source", s, &size);
  nvs_close(h);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    memset(s, 0, sizeof(*s));
    s->format = 1;
    return 0;
  }
  return result == ESP_OK && size == sizeof(*s) && valid(s) ? 0 : -1;
}
int pos_update_source_save(const pos_update_source_t *s) {
  if (!valid(s) || nvs_flash_init() != ESP_OK)
    return -1;
  nvs_handle_t h;
  if (nvs_open("pos_update", NVS_READWRITE, &h) != ESP_OK)
    return -1;
  int bad = nvs_set_blob(h, "source", s, sizeof(*s)) != ESP_OK ||
            nvs_commit(h) != ESP_OK;
  nvs_close(h);
  return bad ? -1 : 0;
}
int pos_update_service_confirm(const char *root, pos_error_t *e) {
  uint32_t sequence = 0;
  int result = pos_update_pending_sequence(root, &sequence, e);
  if (result)
    return result == 1 ? 0 : -1;
  pos_update_source_t *s = calloc(1, sizeof(*s));
  if (!s)
    return -1;
  int bad = pos_update_source_load(s);
  if (!bad && sequence > s->sequence) {
    s->sequence = sequence;
    bad = pos_update_source_save(s);
  }
  free(s);
  if (bad) {
    if (e)
      snprintf(e->message, sizeof(e->message),
               "Update sequence could not be saved; confirmation and previous "
               "System retained.");
    return -1;
  }
  return pos_update_confirm(root, NULL, e);
}
