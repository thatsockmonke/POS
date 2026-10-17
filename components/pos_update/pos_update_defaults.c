#include "pos_update.h"
#include <string.h>

const char pos_update_official_url[] =
    "https://raw.githubusercontent.com/thatsockmonke/POS/main/updates/"
    "latest.json";
const char pos_update_official_key[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEZgIZGnvoXQkb/NGtDqEiYyFeHga+\n"
    "Kek84N4C/EOa0AZq57GMwAydO2pjYWfRwXDFv3DZ1M4GXbU+1XjA4RL6vw==\n"
    "-----END PUBLIC KEY-----\n";
void pos_update_source_official(pos_update_source_t *source) {
  memset(source, 0, sizeof(*source));
  source->format = 1;
  strcpy(source->url, pos_update_official_url);
  strcpy(source->key, pos_update_official_key);
}
bool pos_update_source_use_default(pos_update_source_t *source) {
  if (source->format != 1)
    return false;
  if (*source->url || *source->key)
    return false;
  strcpy(source->url, pos_update_official_url);
  strcpy(source->key, pos_update_official_key);
  return true;
}
