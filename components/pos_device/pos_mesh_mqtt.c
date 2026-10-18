#include "pos_mesh_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pos_mesh_service.h"
#include "pos_network.h"
#include "pos_network_service.h"
#include <stdio.h>
#include <string.h>
#ifndef POS_INSTALLER
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include <stdatomic.h>
static esp_mqtt_client_handle_t client;
static SemaphoreHandle_t mutex;
static char topic[160], subscription[160], channel[12], gateway[12];
static atomic_uint state;
static uint32_t generation;
static unsigned channel_index;
static uint32_t seen_from[16], seen_id[16];
static unsigned seen_next;
static bool allow_downlink;
static atomic_bool connected;
static void mqtt_event(void *arg, esp_event_base_t base, int32_t id,
                       void *data) {
  (void)arg;
  (void)base;
  esp_mqtt_event_handle_t e = data;
  if (id == MQTT_EVENT_CONNECTED) {
    atomic_store(&connected, true);
    atomic_store(&state, 2);
    if (allow_downlink)
      esp_mqtt_client_subscribe(e->client, subscription, 0);
  }
  if (id == MQTT_EVENT_DISCONNECTED) {
    atomic_store(&connected, false);
    atomic_store(&state, 3);
  }
  if (id == MQTT_EVENT_ERROR)
    atomic_store(&state, 4);
  if (id == MQTT_EVENT_DATA && allow_downlink) {
    /* A packet is <=400 bytes; reject fragments and retained replay. Broker
     * authentication/TLS do not replace channel decryption and payload validation below. */
    size_t prefix = strlen(subscription) - 1;
    if (e->retain || e->current_data_offset ||
        e->total_data_len != e->data_len || e->data_len <= 0 ||
        e->data_len > 400 || e->topic_len <= (int)prefix ||
        memcmp(e->topic, subscription, prefix))
      return;
    uint8_t wire[255];
    size_t n;
    if (pos_meshtastic_status().generation != generation)
      return;
    if (!pos_mesh_unenvelope(e->data, e->data_len, channel, gateway, wire, &n))
      (void)pos_meshtastic_mqtt_accept(wire, n, channel_index, generation,
                                       true);
  }
}
void pos_mesh_mqtt_stop(void) {
  if (!mutex)
    return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  esp_mqtt_client_handle_t old = client;
  client = NULL;
  atomic_store(&connected, false);
  xSemaphoreGive(mutex);
  if (old) {
    esp_mqtt_client_stop(old);
    esp_mqtt_client_destroy(old);
  }
  atomic_store(&state, 0);
}
int pos_mesh_mqtt_start(const pos_mesh_mqtt_config_t *c) {
  pos_mesh_config_t mesh;
  pos_meshtastic_status_t status = pos_meshtastic_status();
  if (!c || strncmp(c->uri, "mqtts://", 8) || !c->uri[8] ||
      strchr(c->uri, '@') || !*c->root || strpbrk(c->root, "#+") ||
      c->channel >= 8 || !status.ready || !pos_network_status().connected ||
      pos_meshtastic_settings(&mesh) || !mesh.channels[c->channel].enabled)
    return -1;
  const char *name = pos_mesh_channel_name(&mesh, c->channel);
  if (strpbrk(name, "/+#"))
    return -1;
  if (!mutex)
    mutex = xSemaphoreCreateMutex();
  if (!mutex)
    return -1;
  pos_mesh_mqtt_stop();
  snprintf(channel, sizeof(channel), "%s", name);
  snprintf(gateway, sizeof(gateway), "!%08x", (unsigned)status.node);
  if (snprintf(topic, sizeof(topic), "%s/2/e/%s/%s", c->root, channel,
               gateway) >= (int)sizeof(topic) ||
      snprintf(subscription, sizeof(subscription), "%s/2/e/%s/+", c->root,
               channel) >= (int)sizeof(subscription))
    return -1;
  allow_downlink = c->downlink;
  generation = status.generation;
  channel_index = c->channel;
  memset(seen_id, 0, sizeof(seen_id));
  memset(seen_from, 0, sizeof(seen_from));
  seen_next = 0;
  esp_mqtt_client_config_t config = {
      .broker = {.address.uri = c->uri,
                 .verification.crt_bundle_attach = esp_crt_bundle_attach},
      .credentials = {.username = *c->user ? c->user : NULL,
                      .authentication.password =
                          *c->password ? c->password : NULL},
      .session = {.keepalive = 60, .disable_clean_session = false},
      .network = {.timeout_ms = 5000, .reconnect_timeout_ms = 10000},
      .task = {.stack_size = 6144, .priority = 3},
      .buffer = {.size = 1024, .out_size = 512},
      .outbox.limit = 2048};
  esp_mqtt_client_handle_t next = esp_mqtt_client_init(&config);
  if (!next)
    return -1;
  atomic_store(&state, 1);
  if (esp_mqtt_client_register_event(next, ESP_EVENT_ANY_ID, mqtt_event,
                                     NULL) != ESP_OK ||
      esp_mqtt_client_start(next) != ESP_OK) {
    esp_mqtt_client_destroy(next);
    return -1;
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  client = next;
  xSemaphoreGive(mutex);
  return 0;
}
void pos_mesh_mqtt_status(char out[128]) {
  const char *names[] = {
      "Off", "Connecting (TLS verified)", "Connected",
      "Disconnected; reconnecting",
      "MQTT/TLS error: check broker, clock, credentials and CA"};
  unsigned code = atomic_load(&state);
  snprintf(out, 128, "%s", code < 5 ? names[code] : "Unknown");
}
void pos_mesh_mqtt_uplink(const uint8_t *wire, size_t n) {
  if (!mutex || !atomic_load(&connected) || n < 17 || wire[12] & 16)
    return;
  pos_meshtastic_status_t s = pos_meshtastic_status();
  if (s.generation != generation)
    return;
  if (pos_meshtastic_mqtt_accept(wire, n, channel_index, generation, false))
    return;
  pos_mesh_header_t h;
  if (pos_mesh_header(wire, n, &h))
    return;
  if (xSemaphoreTake(mutex, 0) != pdTRUE)
    return;
  for (unsigned i = 0; i < 16; ++i)
    if (seen_from[i] == h.from && seen_id[i] == h.id) {
      xSemaphoreGive(mutex);
      return;
    }
  seen_from[seen_next % 16] = h.from;
  seen_id[seen_next++ % 16] = h.id;
  xSemaphoreGive(mutex);
  uint8_t envelope[400];
  size_t length =
      pos_mesh_envelope(wire, n, channel, gateway, envelope, sizeof(envelope));
  if (!length)
    return;
  if (xSemaphoreTake(mutex, 0) != pdTRUE)
    return;
  if (client && esp_mqtt_client_get_outbox_size(client) < 1600)
    (void)esp_mqtt_client_enqueue(client, topic, (char *)envelope, length, 0, 0,
                                  false);
  xSemaphoreGive(mutex);
}
#else
int pos_mesh_mqtt_start(const pos_mesh_mqtt_config_t *c) {
  (void)c;
  return -1;
}
void pos_mesh_mqtt_stop(void) {}
void pos_mesh_mqtt_status(char out[128]) {
  snprintf(out, 128, "Unavailable in recovery");
}
void pos_mesh_mqtt_uplink(const uint8_t *p, size_t n) {
  (void)p;
  (void)n;
}
#endif
