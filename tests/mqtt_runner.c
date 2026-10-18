#include "mqtt_client.h"
#include "pos_mesh_mqtt.h"
#include "pos_mesh_service.h"
#include "pos_network_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned clients, published, accepted, subscribed;
static void (*handler)(void *, esp_event_base_t, int32_t, void *);
int esp_crt_bundle_attach(void *p) {
  (void)p;
  return 0;
}
esp_mqtt_client_handle_t
esp_mqtt_client_init(const esp_mqtt_client_config_t *c) {
  assert(c->broker.verification.crt_bundle_attach == esp_crt_bundle_attach);
  assert(c->outbox.limit <= 2048 && c->buffer.size <= 1024);
  ++clients;
  return (void *)1;
}
int esp_mqtt_client_register_event(esp_mqtt_client_handle_t c, int event,
                                   void (*fn)(void *, esp_event_base_t, int32_t,
                                              void *),
                                   void *arg) {
  (void)c;
  (void)event;
  (void)arg;
  handler = fn;
  return 0;
}
int esp_mqtt_client_start(esp_mqtt_client_handle_t c) {
  esp_mqtt_event_t e = {.client = c};
  handler(NULL, NULL, MQTT_EVENT_CONNECTED, &e);
  return 0;
}
int esp_mqtt_client_stop(esp_mqtt_client_handle_t c) {
  (void)c;
  return 0;
}
int esp_mqtt_client_destroy(esp_mqtt_client_handle_t c) {
  (void)c;
  assert(clients);
  --clients;
  return 0;
}
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t c, const char *t,
                              int q) {
  (void)c;
  assert(!strcmp(t, "msh/US/2/e/LongFast/+") && q == 0);
  ++subscribed;
  return 0;
}
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t c) {
  (void)c;
  return 0;
}
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t c, const char *topic,
                            const char *data, int n, int qos, int retain,
                            bool store) {
  (void)c;
  (void)data;
  assert(!strcmp(topic, "msh/US/2/e/LongFast/!12345678") && n && qos == 0 &&
         !retain && !store);
  ++published;
  return 0;
}
pos_meshtastic_status_t pos_meshtastic_status(void) {
  return (pos_meshtastic_status_t){
      .ready = true, .generation = 3, .node = 0x12345678};
}
pos_network_status_t pos_network_status(void) {
  return (pos_network_status_t){.connected = true};
}
int pos_meshtastic_settings(pos_mesh_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->channels[0].enabled = true;
  return 0;
}
const char *pos_mesh_channel_name(const pos_mesh_config_t *c, unsigned i) {
  (void)c;
  (void)i;
  return "LongFast";
}
int pos_meshtastic_mqtt_accept(const uint8_t *p, size_t n, unsigned channel,
                               uint32_t generation, bool incoming) {
  (void)p;
  assert(n >= 17 && channel == 0 && generation == 3);
  if (incoming)
    ++accepted;
  return 0;
}
int pos_mesh_header(const void *p, size_t n, pos_mesh_header_t *h) {
  (void)p;
  if (n < 17)
    return -1;
  *h = (pos_mesh_header_t){.id = 1, .from = 2};
  return 0;
}
size_t pos_mesh_envelope(const void *p, size_t n, const char *ch, const char *g,
                         uint8_t *out, size_t cap) {
  (void)p;
  (void)n;
  (void)ch;
  (void)g;
  assert(cap >= 2);
  memcpy(out, "ok", 2);
  return 2;
}
int pos_mesh_unenvelope(const void *p, size_t n, const char *ch, const char *g,
                        uint8_t out[255], size_t *len) {
  (void)ch;
  (void)g;
  if (n != 2 || memcmp(p, "ok", 2))
    return -1;
  memset(out, 0, 17);
  *len = 17;
  return 0;
}
int main(void) {
  pos_mesh_mqtt_config_t c = {
      .uri = "mqtt://bad", .root = "msh/US", .downlink = true};
  char text[128];
  assert(pos_mesh_mqtt_start(&c) < 0 && !clients);
  strcpy(c.uri, "mqtts://example.org:8883");
  assert(!pos_mesh_mqtt_start(&c) && clients == 1 && subscribed == 1);
  pos_mesh_mqtt_status(text);
  assert(!strcmp(text, "Connected"));
  uint8_t wire[17] = {0};
  pos_mesh_mqtt_uplink(wire, sizeof(wire));
  pos_mesh_mqtt_uplink(wire, sizeof(wire));
  assert(published == 1);
  wire[12] = 16;
  pos_mesh_mqtt_uplink(wire, sizeof(wire));
  assert(published == 1);
  esp_mqtt_event_t e = {.data = "ok",
                        .topic = "msh/US/2/e/LongFast/!22222222",
                        .topic_len = 31,
                        .data_len = 2,
                        .total_data_len = 2};
  e.topic_len = strlen(e.topic);
  handler(NULL, NULL, MQTT_EVENT_DATA, &e);
  assert(accepted == 1);
  e.retain = true;
  handler(NULL, NULL, MQTT_EVENT_DATA, &e);
  assert(accepted == 1);
  e.retain = false;
  e.current_data_offset = 1;
  handler(NULL, NULL, MQTT_EVENT_DATA, &e);
  assert(accepted == 1);
  e.current_data_offset = 0;
  e.data_len = 1;
  handler(NULL, NULL, MQTT_EVENT_DATA, &e);
  assert(accepted == 1);
  e.data_len = 2;
  e.topic = "other/channel";
  e.topic_len = strlen(e.topic);
  handler(NULL, NULL, MQTT_EVENT_DATA, &e);
  assert(accepted == 1);
  pos_mesh_mqtt_stop();
  assert(!clients);
  pos_mesh_mqtt_status(text);
  assert(!strcmp(text, "Off"));
  puts("PASS: verified TLS configuration, lifecycle, limits, duplicate/loop "
       "and fragment/retained/topic gates");
  return 0;
}
