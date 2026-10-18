#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef void *esp_mqtt_client_handle_t;
typedef const char *esp_event_base_t;
typedef struct {
  esp_mqtt_client_handle_t client;
  bool retain;
  int current_data_offset, total_data_len, data_len, topic_len;
  char *data, *topic;
} esp_mqtt_event_t, *esp_mqtt_event_handle_t;
enum {
  MQTT_EVENT_CONNECTED = 1,
  MQTT_EVENT_DISCONNECTED,
  MQTT_EVENT_ERROR,
  MQTT_EVENT_DATA
};
#define ESP_EVENT_ANY_ID -1
#define ESP_OK 0
typedef struct {
  struct {
    struct {
      const char *uri;
    } address;
    struct {
      void *crt_bundle_attach;
    } verification;
  } broker;
  struct {
    const char *username;
    struct {
      const char *password;
    } authentication;
  } credentials;
  struct {
    int keepalive;
    bool disable_clean_session;
  } session;
  struct {
    int timeout_ms, reconnect_timeout_ms;
  } network;
  struct {
    int stack_size, priority;
  } task;
  struct {
    int size, out_size;
  } buffer;
  struct {
    uint64_t limit;
  } outbox;
} esp_mqtt_client_config_t;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *);
int esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int,
                                   void (*)(void *, esp_event_base_t, int32_t,
                                            void *),
                                   void *);
int esp_mqtt_client_start(esp_mqtt_client_handle_t);
int esp_mqtt_client_stop(esp_mqtt_client_handle_t);
int esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char *, int);
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char *,
                            const char *, int, int, int, bool);
