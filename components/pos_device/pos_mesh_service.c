#include "pos_mesh_service.h"
#include "cJSON.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/gcm.h"
#include "nvs.h"
#include "pos_appdata.h"
#include "pos_device.h"
#include "pos_fs.h"
#include "pos_network.h"
#include "pos_peripherals.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static SemaphoreHandle_t lock;
static pos_mesh_t *mesh;
static pos_mesh_config_t saved;
static bool selected;
static uint32_t identity, generation, next_id, id_limit;
static char storage[POS_FS_PATH], error[128], latest[240];
static uint64_t announce_at;
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static int fail(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static uint32_t random_word(void *unused) {
  (void)unused;
  return esp_random();
}
static uint32_t allocate_id(void *unused) {
  (void)unused;
  if (next_id == id_limit) {
    nvs_handle_t h;
    if (nvs_open("pos_mesh", NVS_READWRITE, &h) != ESP_OK)
      return 0;
    uint32_t start = 0;
    esp_err_t r = nvs_get_u32(h, "id_limit", &start);
    if (r == ESP_ERR_NVS_NOT_FOUND)
      start = 4 + (esp_random() & 0x3fffffff);
    else if (r != ESP_OK) {
      nvs_close(h);
      return 0;
    }
    if (start < 4 || start > UINT32_MAX - 1024 ||
        nvs_set_u32(h, "id_limit", start + 1024) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
      nvs_close(h);
      snprintf(error, sizeof(error),
               "Packet ID reservation failed; transmission stopped.");
      return 0;
    }
    nvs_close(h);
    next_id = start;
    id_limit = start + 1024;
  }
  return next_id++;
}
/* Same physical-flash threat boundary as Known Networks: a random device key
 * in NVS, AES-GCM encrypted configuration, no passphrase and no SD secrets. */
static int preferences(bool write, pos_mesh_config_t *c, bool *enabled) {
  enum { SIZE = sizeof(pos_mesh_config_t) + 1 };
  uint8_t blob[12 + SIZE + 16], plain[SIZE], key[32];
  nvs_handle_t h;
  if (nvs_open("pos_mesh", write ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK)
    return write ? -1 : 1;
  size_t n = sizeof(key);
  esp_err_t r = nvs_get_blob(h, "wrap_key", key, &n);
  int result = -1;
  if (r == ESP_ERR_NVS_NOT_FOUND && write) {
    if (pos_device_wifi_prepare())
      goto done;
    esp_fill_random(key, 32);
    if (nvs_set_blob(h, "wrap_key", key, 32) != ESP_OK ||
        nvs_commit(h) != ESP_OK)
      goto done;
    r = ESP_OK;
    n = 32;
  }
  if (r == ESP_ERR_NVS_NOT_FOUND && !write) {
    result = 1;
    goto done;
  }
  if (r != ESP_OK || n != 32)
    goto done;
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  if (mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256)) {
    mbedtls_gcm_free(&g);
    goto done;
  }
  static const uint8_t aad[] = "POS Meshtastic settings v1";
  if (write) {
    memcpy(plain, c, sizeof(*c));
    plain[sizeof(*c)] = *enabled;
    esp_fill_random(blob, 12);
    if (!mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, SIZE, blob, 12, aad,
                                   sizeof(aad), plain, blob + 12, 16,
                                   blob + 12 + SIZE) &&
        nvs_set_blob(h, "config", blob, sizeof(blob)) == ESP_OK &&
        nvs_commit(h) == ESP_OK)
      result = 0;
  } else {
    n = sizeof(blob);
    r = nvs_get_blob(h, "config", blob, &n);
    if (r == ESP_ERR_NVS_NOT_FOUND)
      result = 1;
    else if (r == ESP_OK && n == sizeof(blob) &&
             !mbedtls_gcm_auth_decrypt(&g, SIZE, blob, 12, aad, sizeof(aad),
                                       blob + 12 + SIZE, 16, blob + 12,
                                       plain)) {
      memcpy(c, plain, sizeof(*c));
      if (plain[sizeof(*c)] <= 1 && !pos_mesh_config_valid(c)) {
        *enabled = plain[sizeof(*c)];
        result = 0;
      }
    }
  }
  mbedtls_gcm_free(&g);
done:
  nvs_close(h);
  pos_secure_zero(blob, sizeof(blob));
  pos_secure_zero(plain, sizeof(plain));
  pos_secure_zero(key, sizeof(key));
  return result;
}
static void app_name(uint32_t id, char out[20]) {
  snprintf(out, 20, "mesh-%08x", (unsigned)id);
}
static int save_node(const pos_mesh_node_t *n, bool forgotten) {
  pos_appdata_t *data = calloc(1, sizeof(*data));
  if (!data)
    return -1;
  char app[20];
  app_name(n->id, app);
  pos_error_t e = {0};
  int r = pos_appdata_load(storage, app, 1, NULL, NULL, data, &e);
  if (r < 0) {
    free(data);
    return -1;
  }
  data->schema = 1;
  cJSON *j = cJSON_CreateObject();
  char key[65];
  for (unsigned i = 0; i < 32; ++i)
    snprintf(key + i * 2, 3, "%02x", n->public_key[i]);
  bool ok = j && cJSON_AddNumberToObject(j, "id", n->id) &&
            cJSON_AddStringToObject(j, "name", n->long_name) &&
            cJSON_AddStringToObject(j, "short", n->short_name) &&
            cJSON_AddStringToObject(j, "public_key", n->has_key ? key : "") &&
            cJSON_AddBoolToObject(j, "key_changed", n->key_changed) &&
            cJSON_AddBoolToObject(j, "forgotten", forgotten);
  char *text = ok ? cJSON_PrintUnformatted(j) : NULL;
  if (!text || strlen(text) > POS_APPDATA_MAX)
    r = -1;
  else {
    strcpy(data->json, text);
    r = pos_appdata_save(storage, app, data, &e);
  }
  free(text);
  cJSON_Delete(j);
  free(data);
  return r;
}
static void load_nodes(void) {
  char path[POS_FS_PATH];
  pos_fs_join(path, storage, "POS/Users/Owner/Saves");
  DIR *d = opendir(path);
  if (!d)
    return;
  struct dirent *ent;
  unsigned at = 0;
  pos_appdata_t *data = calloc(1, sizeof(*data));
  if (!data) {
    closedir(d);
    return;
  }
  while (at < POS_MESH_NODES && (ent = readdir(d))) {
    if (strlen(ent->d_name) != 13 || strncmp(ent->d_name, "mesh-", 5) ||
        strspn(ent->d_name + 5, "0123456789abcdef") != 8)
      continue;
    pos_error_t e = {0};
    if (pos_appdata_load(storage, ent->d_name, 1, NULL, NULL, data, &e))
      continue;
    cJSON *j = cJSON_Parse(data->json), *id = cJSON_GetObjectItem(j, "id"),
          *name = cJSON_GetObjectItem(j, "name"),
          *shortname = cJSON_GetObjectItem(j, "short"),
          *key = cJSON_GetObjectItem(j, "public_key"),
          *changed = cJSON_GetObjectItem(j, "key_changed");
    uint32_t expected = strtoul(ent->d_name + 5, NULL, 16);
    if (cJSON_IsNumber(id) && id->valuedouble == expected && expected >= 4 &&
        expected != UINT32_MAX && cJSON_IsString(name) &&
        strlen(name->valuestring) < 40 && cJSON_IsString(shortname) &&
        strlen(shortname->valuestring) < 5 && cJSON_IsString(key) &&
        (!*key->valuestring ||
         (strlen(key->valuestring) == 64 &&
          strspn(key->valuestring, "0123456789abcdef") == 64)) &&
        cJSON_IsBool(changed) &&
        !cJSON_IsTrue(cJSON_GetObjectItem(j, "forgotten"))) {
      pos_mesh_node_t *n = &mesh->nodes[at++];
      n->id = expected;
      strcpy(n->long_name, name->valuestring);
      strcpy(n->short_name, shortname->valuestring);
      n->has_key = *key->valuestring != 0;
      n->key_changed = cJSON_IsTrue(changed);
      for (unsigned i = 0; n->has_key && i < 32; ++i) {
        char b[3] = {key->valuestring[i * 2], key->valuestring[i * 2 + 1], 0};
        n->public_key[i] = strtoul(b, NULL, 16);
      }
    }
    cJSON_Delete(j);
  }
  free(data);
  closedir(d);
}
static int outbox_path(uint32_t id, char path[POS_FS_PATH]) {
  char rel[100];
  snprintf(rel, sizeof(rel), "POS/Users/Owner/Radio/mesh-outbox/%08x.mesh",
           (unsigned)id);
  return pos_fs_join(path, storage, rel);
}
static int append_event(pos_mesh_event_t type, const pos_mesh_message_t *m) {
  char path[POS_FS_PATH];
  pos_fs_join(path, storage, "POS/Users/Owner/Radio/meshtastic.jsonl");
  cJSON *j = cJSON_CreateObject();
  char from[10], to[10], id[9];
  snprintf(from, sizeof(from), "!%08x", (unsigned)m->header.from);
  snprintf(to, sizeof(to), "!%08x", (unsigned)m->header.to);
  snprintf(id, sizeof(id), "%08x", (unsigned)m->header.id);
  static const char *const events[] = {"received",     "sent",     "relayed",
                                       "acknowledged", "rejected", "timeout",
                                       "forwarded"};
  char body[POS_MESH_PAYLOAD + 1];
  memcpy(body, m->data.payload, m->data.length);
  body[m->data.length] = 0;
  bool ok = j && cJSON_AddStringToObject(j, "event", events[type]) &&
            cJSON_AddStringToObject(j, "from", from) &&
            cJSON_AddStringToObject(j, "to", to) &&
            cJSON_AddStringToObject(j, "id", id) &&
            cJSON_AddNumberToObject(j, "port", m->data.port) &&
            cJSON_AddNumberToObject(j, "channel", m->channel_index) &&
            cJSON_AddBoolToObject(j, "public_key_encrypted", m->pki);
  if (ok && m->data.port == POS_MESH_TEXT)
    ok = cJSON_AddStringToObject(j, "text", body) != NULL;
  char *text = ok ? cJSON_PrintUnformatted(j) : NULL;
  FILE *f = text ? fopen(path, "ab") : NULL;
  int bad = !f;
  if (f) {
    bad = fprintf(f, "%s\n", text) < 0 || fflush(f) || fsync(fileno(f));
    if (fclose(f))
      bad = 1;
  }
  free(text);
  cJSON_Delete(j);
  return bad ? -1 : 0;
}
static int sink(void *unused, pos_mesh_event_t type,
                const pos_mesh_message_t *m) {
  (void)unused;
  if (type == POS_MESH_RX && m->data.port == POS_MESH_TEXT) {
    if (memchr(m->data.payload, 0, m->data.length) || append_event(type, m)) {
      snprintf(error, sizeof(error),
               "Cannot retain received text; no delivery ACK sent.");
      return -1;
    }
    snprintf(latest, sizeof(latest), "!%08x: %.*s", (unsigned)m->header.from,
             (int)m->data.length, m->data.payload);
  }
  if (type == POS_MESH_RX && m->data.port == POS_MESH_USER) {
    for (unsigned i = 0; i < 32; ++i)
      if (mesh->nodes[i].id == m->header.from &&
          save_node(&mesh->nodes[i], false)) {
        snprintf(error, sizeof(error),
                 "Node identity save failed; direct messages remain blocked.");
        mesh->nodes[i].key_changed = true;
        return -1;
      }
  }
  if (type != POS_MESH_RX && type != POS_MESH_FORWARDED) {
    if (append_event(type, m))
      snprintf(error, sizeof(error), "Message status log could not be saved.");
    if (type == POS_MESH_ACKED ||
        (type == POS_MESH_SENT && m->header.to == UINT32_MAX)) {
      char path[POS_FS_PATH];
      outbox_path(m->header.id, path);
      (void)unlink(path);
    }
  }
  return 0;
}
static int activate(void) {
  if (mesh)
    return 0;
  uint8_t key[32];
  size_t n = sizeof(key);
  nvs_handle_t h;
  if (nvs_open("pos_mesh", NVS_READWRITE, &h) != ESP_OK)
    return -1;
  esp_err_t r = nvs_get_blob(h, "private", key, &n);
  if (r == ESP_ERR_NVS_NOT_FOUND) {
    if (pos_device_wifi_prepare()) {
      nvs_close(h);
      return -1;
    }
    esp_fill_random(key, 32);
    r = nvs_set_blob(h, "private", key, 32);
    if (r == ESP_OK)
      r = nvs_commit(h);
  }
  nvs_close(h);
  if (r != ESP_OK || n != 32) {
    pos_secure_zero(key, 32);
    return -1;
  }
  mesh = calloc(1, sizeof(*mesh));
  int bad = !mesh || pos_mesh_init(mesh, identity, &saved, key);
  pos_secure_zero(key, 32);
  if (bad) {
    free(mesh);
    mesh = NULL;
    return -1;
  }
  mesh->random = random_word;
  mesh->next_id = allocate_id;
  mesh->sink = sink;
  load_nodes();
  announce_at = now_ms() + 30000 + esp_random() % 30000;
  return 0;
}
int pos_meshtastic_init(const char *root, uint32_t id) {
  if (lock)
    return 0;
  if (strlen(root) >= sizeof(storage) || id < 4 || id == UINT32_MAX)
    return -1;
  strcpy(storage, root);
  identity = id;
  generation = 1;
  lock = xSemaphoreCreateMutex();
  if (!lock)
    return -1;
  pos_mesh_defaults(&saved);
  selected = false;
  int r = preferences(false, &saved, &selected);
  if (r < 0) {
    selected = false;
    pos_mesh_defaults(&saved);
    snprintf(error, sizeof(error),
             "Saved mesh settings are invalid; preserved, mesh disabled.");
  }
  char path[POS_FS_PATH];
  pos_fs_join(path, root, "POS/Users/Owner/Radio/mesh-outbox");
  if (pos_fs_mkdirs(path))
    snprintf(error, sizeof(error), "Cannot create mesh outbox.");
  if (selected && activate()) {
    snprintf(error, sizeof(error), "Cannot allocate mesh identity/service.");
    return -1;
  }
  return 0;
}
pos_meshtastic_status_t pos_meshtastic_status(void) {
  pos_meshtastic_status_t s = {0};
  if (!lock)
    return s;
  xSemaphoreTake(lock, portMAX_DELAY);
  s.enabled = selected;
  s.ready = mesh != NULL;
  s.node = identity;
  s.generation = generation;
  pos_mesh_modem_t m;
  if (!pos_mesh_modem(&saved, &m))
    s.frequency_hz = m.frequency_hz;
  if (mesh) {
    s.stats = mesh->stats;
    memcpy(s.public_key, mesh->public_key, 32);
    for (unsigned i = 0; i < 32; ++i)
      s.nodes += mesh->nodes[i].id != 0;
    for (unsigned i = 0; i < POS_MESH_QUEUE; ++i)
      s.queued += mesh->queue[i].used;
  }
  snprintf(s.error, sizeof(s.error), "%s", error);
  snprintf(s.latest, sizeof(s.latest), "%s", latest);
  xSemaphoreGive(lock);
  return s;
}
int pos_meshtastic_settings(pos_mesh_config_t *c) {
  if (!lock)
    return -1;
  xSemaphoreTake(lock, portMAX_DELAY);
  *c = saved;
  xSemaphoreGive(lock);
  return 0;
}
int pos_meshtastic_configure(const pos_mesh_config_t *c, bool use,
                             pos_error_t *e) {
  if (!lock || pos_mesh_config_valid(c))
    return fail(e,
                "Invalid region, preset, frequency slot, name or channel key.");
  pos_expansion_revoke_transmit();
  xSemaphoreTake(lock, portMAX_DELAY);
  /* Never let background relays race a UI settings change. */
  pos_mesh_config_t candidate = *c;
  if (preferences(true, &candidate, &use)) {
    xSemaphoreGive(lock);
    return fail(
        e, "Mesh settings were not saved; previous configuration retained.");
  }
  saved = *c;
  selected = use;
  ++generation;
  if (mesh) {
    pos_mesh_cancel(mesh);
    memset(mesh->seen, 0, sizeof(mesh->seen));
    mesh->config = saved;
    if (!selected) {
      pos_secure_zero(mesh, sizeof(*mesh));
      free(mesh);
      mesh = NULL;
    }
  }
  int bad = selected && activate();
  if (!bad)
    error[0] = 0;
  announce_at = now_ms() + 30000 + esp_random() % 30000;
  xSemaphoreGive(lock);
  return bad ? fail(e, "Mesh preferences saved, but service/identity could not "
                       "start.")
             : 0;
}
int pos_meshtastic_node(unsigned i, pos_mesh_node_t *n) {
  if (!lock || i >= 32)
    return -1;
  xSemaphoreTake(lock, portMAX_DELAY);
  int bad = !mesh || !mesh->nodes[i].id;
  if (!bad)
    *n = mesh->nodes[i];
  xSemaphoreGive(lock);
  return bad ? -1 : 0;
}
int pos_meshtastic_forget(uint32_t id, pos_error_t *e) {
  if (!lock)
    return fail(e, "Mesh service unavailable.");
  xSemaphoreTake(lock, portMAX_DELAY);
  int result = -1;
  for (unsigned i = 0; mesh && i < 32; ++i)
    if (mesh->nodes[i].id == id) {
      pos_mesh_node_t empty = mesh->nodes[i];
      memset(empty.public_key, 0, 32);
      empty.has_key = false;
      empty.key_changed = false;
      if (!save_node(&empty, true)) {
        memset(&mesh->nodes[i], 0, sizeof(mesh->nodes[i]));
        result = 0;
      }
    }
  xSemaphoreGive(lock);
  return result ? fail(e, "Node key could not be forgotten; previous identity "
                          "retained.")
                : 0;
}
static int persist_queue(void) {
  for (unsigned i = 0; i < POS_MESH_QUEUE; ++i) {
    pos_mesh_tx_t *q = &mesh->queue[i];
    if (!q->used || !q->own || q->attempts)
      continue;
    uint8_t blob[272] = {0};
    memcpy(blob, "MSO1", 4);
    uint32_t crc = pos_crc32(0, &saved, sizeof(saved));
    memcpy(blob + 4, &crc, 4);
    blob[8] = q->length;
    memcpy(blob + 9, q->wire, q->length);
    crc = pos_crc32(0, blob, 9 + q->length);
    memcpy(blob + 9 + q->length, &crc, 4);
    uint32_t packet_id;
    memcpy(&packet_id, q->wire + 8, 4);
    char path[POS_FS_PATH];
    outbox_path(packet_id, path);
    struct stat st;
    if (!pos_fs_lstat(path, &st))
      continue;
    char part[POS_FS_PATH];
    int written = snprintf(part, sizeof(part), "%s.part", path);
    if (written < 0 || written >= (int)sizeof(part) ||
        pos_fs_write_new(part, blob, 13 + q->length) ||
        pos_fs_crc_file(part, 13 + q->length, &crc) ||
        crc != pos_crc32(0, blob, 13 + q->length) ||
        pos_fs_publish_new(part, path)) {
      q->used = false;
      return -1;
    }
  }
  return 0;
}
int pos_meshtastic_send(unsigned channel, uint32_t to, unsigned port,
                        const void *p, size_t n, bool response,
                        pos_error_t *e) {
  if (!lock)
    return fail(e, "Mesh unavailable.");
  xSemaphoreTake(lock, portMAX_DELAY);
  int bad = !selected || !mesh ||
            pos_mesh_send(mesh, channel, to, port, p, n, response, now_ms()) ||
            persist_queue();
  xSemaphoreGive(lock);
  return bad ? fail(e,
                    "Not queued. Check channel, free space and queue. Direct "
                    "messages require an unchanged discovered public key.")
             : 0;
}
int pos_meshtastic_announce(bool discover, pos_error_t *e) {
  if (!lock)
    return -1;
  xSemaphoreTake(lock, portMAX_DELAY);
  int bad = !selected || !mesh ||
            pos_mesh_announce(mesh, UINT32_MAX, discover, now_ms());
  xSemaphoreGive(lock);
  return bad ? fail(e, "Discovery could not be queued.") : 0;
}
int pos_meshtastic_share(unsigned ch, uint32_t to, int32_t lat, int32_t lon,
                         uint32_t utc, pos_error_t *e) {
  if (!lock)
    return -1;
  xSemaphoreTake(lock, portMAX_DELAY);
  int bad = !selected || !mesh ||
            pos_mesh_position(mesh, ch, to, lat, lon, utc, now_ms()) ||
            persist_queue();
  xSemaphoreGive(lock);
  return bad ? fail(e, "Location could not be queued.") : 0;
}
int pos_meshtastic_retry(pos_error_t *e) {
  if (!lock)
    return -1;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!selected || !mesh) {
    xSemaphoreGive(lock);
    return fail(e, "Select Meshtastic mode first.");
  }
  char path[POS_FS_PATH];
  pos_fs_join(path, storage, "POS/Users/Owner/Radio/mesh-outbox");
  DIR *d = opendir(path);
  struct dirent *ent;
  int count = 0;
  while (d && (ent = readdir(d))) {
    if (strlen(ent->d_name) != 13 || strcmp(ent->d_name + 8, ".mesh") ||
        strspn(ent->d_name, "0123456789abcdef") != 8)
      continue;
    char file[POS_FS_PATH];
    if (snprintf(file, sizeof(file), "%s/%s", path, ent->d_name) >=
        (int)sizeof(file))
      continue;
    FILE *f = fopen(file, "rb");
    if (!f)
      continue;
    uint8_t blob[273];
    size_t n = fread(blob, 1, sizeof(blob), f);
    fclose(f);
    uint32_t cfg, crc;
    memcpy(&cfg, blob + 4, 4);
    if (n < 31 || memcmp(blob, "MSO1", 4) || blob[8] + 13u != n ||
        cfg != pos_crc32(0, &saved, sizeof(saved)))
      continue;
    memcpy(&crc, blob + n - 4, 4);
    if (crc != pos_crc32(0, blob, n - 4))
      continue;
    pos_mesh_header_t h;
    if (pos_mesh_header(blob + 9, blob[8], &h) || h.from != identity)
      continue;
    bool exists = false;
    int slot = -1;
    for (unsigned i = 0; i < POS_MESH_QUEUE; ++i) {
      pos_mesh_tx_t *q = &mesh->queue[i];
      uint32_t id;
      memcpy(&id, q->wire + 8, 4);
      if (q->used && id == h.id && q->own)
        exists = true;
      if (!q->used)
        slot = i;
    }
    if (exists || slot < 0)
      continue;
    pos_mesh_tx_t *q = &mesh->queue[slot];
    memset(q, 0, sizeof(*q));
    memcpy(q->wire, blob + 9, blob[8]);
    q->length = blob[8];
    q->used = q->own = true;
    q->reliable = h.flags & 8;
    q->priority = 1;
    q->due_ms = now_ms() + 1000;
    q->expires_ms = now_ms() + 30 * 60 * 1000;
    ++count;
  }
  if (d)
    closedir(d);
  xSemaphoreGive(lock);
  return count ? 0
               : fail(e, "No matching saved packets available, or queue full. "
                         "Old settings/outbox files are preserved.");
}
bool pos_meshtastic_modem(pos_mesh_modem_t *m, uint32_t *gen) {
  if (!lock)
    return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  *gen = generation;
  bool active = selected && !pos_mesh_modem(&saved, m);
  xSemaphoreGive(lock);
  return active;
}
void pos_meshtastic_receive(const uint8_t *wire, size_t n, int rssi, int snr) {
  if (!lock)
    return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (selected && mesh)
    (void)pos_mesh_receive(mesh, wire, n, rssi, snr, now_ms());
  xSemaphoreGive(lock);
}
int pos_meshtastic_due(uint8_t wire[255], size_t *length) {
  if (!lock)
    return 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  int result = 0;
  if (selected && mesh) {
    if (now_ms() >= announce_at) {
      (void)pos_mesh_announce(mesh, UINT32_MAX, false, now_ms());
      announce_at = now_ms() + 3 * 60 * 60 * 1000 + esp_random() % 60000;
    }
    const uint8_t *p = NULL;
    result = pos_mesh_due(mesh, now_ms(), &p, length);
    if (result == 1)
      memcpy(wire, p, *length);
  }
  xSemaphoreGive(lock);
  return result;
}
void pos_meshtastic_tx_result(bool success) {
  if (!lock)
    return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (mesh)
    pos_mesh_tx_result(mesh, success, now_ms());
  xSemaphoreGive(lock);
}
void pos_meshtastic_pause(void) {
  if (!lock)
    return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (mesh)
    pos_mesh_cancel(mesh);
  xSemaphoreGive(lock);
}
void pos_meshtastic_stop(void) {
  if (!lock)
    return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (mesh) {
    pos_secure_zero(mesh, sizeof(*mesh));
    free(mesh);
    mesh = NULL;
  }
  xSemaphoreGive(lock);
}
