/* Actual update UI, NVS source policy and signed metadata parser; SDK/NVS,
 * network and physical flash are simulated. No physical device is accessed. */
#include "esp_app_desc.h"
#include "nvs.h"
#include "pos_device.h"
#include "pos_mesh_mqtt.h"
#include "pos_network_service.h"
#include "pos_update_service.h"
#include "ui.h"
void pos_ui_help(const char *root, const char *title, const char *file) {
  (void)root;
  (void)file;
  pos_ui_notice(title, "SD help resource");
}
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static pos_update_source_t stored;
static bool has_source, nvs_failed, connected = true;
static unsigned installs, downloads, wifi_opened, time_syncs;
static bool clock_unset, mqtt_stopped;
void pos_mesh_mqtt_stop(void) { mqtt_stopped=true; }
time_t time(time_t *out) {
  time_t value = clock_unset ? 0 : 1792152000;
  if (out)
    *out = value;
  return value;
}
static int choices[16], choice_count, choice_next;
static const char *manifest, *mode;
static bool available, current, rejected, saved_error;
static const esp_app_desc_t description = {.version = "0.4.0-dev"};
const esp_app_desc_t *esp_app_get_description(void) { return &description; }
uint32_t esp_random(void) { return downloads + 1; }
int64_t esp_timer_get_time(void) { return 0; }
esp_err_t nvs_flash_init(void) { return nvs_failed ? -1 : ESP_OK; }
esp_err_t nvs_open(const char *name, int access, nvs_handle_t *h) {
  (void)access;
  assert(!strcmp(name, "pos_update"));
  *h = 1;
  return has_source || access == NVS_READWRITE ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *name, void *out, size_t *n) {
  (void)h;
  assert(!strcmp(name, "source") && *n == sizeof(stored));
  if (!has_source)
    return ESP_ERR_NVS_NOT_FOUND;
  memcpy(out, &stored, sizeof(stored));
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *name, const void *in,
                       size_t n) {
  (void)h;
  assert(!strcmp(name, "source") && n == sizeof(stored));
  memcpy(&stored, in, n);
  has_source = true;
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
  (void)h;
  return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
int pos_ui_menu_from(const char *title, const char *const *items, int count,
                     int initial) {
  (void)title;
  (void)items;
  (void)initial;
  assert(choice_next < choice_count);
  int selected = choices[choice_next++];
  assert(selected < count);
  return selected;
}
int pos_ui_menu(const char *title, const char *const *items, int count) {
  return pos_ui_menu_from(title, items, count, 0);
}
void pos_ui_notice(const char *title, const char *body) {
  assert(!strstr(body, "Minimum version") && !strstr(body, "minimum_version"));
  if (!strcmp(title, "Update available"))
    available = true;
  if (!strcmp(title, "POS is up to date"))
    current = true;
  if (!strcmp(title, "Update rejected"))
    rejected = true;
  if (!strcmp(title, "Update settings unavailable"))
    saved_error = true;
}
void pos_ui_message(const char *title, const char *body) {
  (void)title;
  (void)body;
}
int pos_ui_progress(void *ctx, const char *phase, const char *file, uint64_t a,
                    uint64_t b) {
  (void)ctx;
  (void)phase;
  (void)file;
  (void)a;
  (void)b;
  return 0;
}
bool pos_ui_text(const char *a, const char *b, char *c, size_t n, bool secret) {
  (void)a;
  (void)b;
  (void)c;
  (void)n;
  (void)secret;
  return false;
}
bool pos_ui_pick_file_from(const char *r, const char *t, const char *start,
                           char out[241]) {
  (void)r;
  (void)t;
  (void)start;
  (void)out;
  return false;
}
void pos_ui_browse(const char *r, const char *p) {
  (void)r;
  (void)p;
}
void pos_ui_wifi(const char *root) {
  (void)root;
  ++wifi_opened;
  connected = true;
}
pos_network_status_t pos_network_status(void) {
  return (pos_network_status_t){.connected = connected};
}
int pos_network_sync_time(void) {
  ++time_syncs;
  if (!strcmp(mode, "time-failed"))
    return -1;
  clock_unset = false;
  return 0;
}
int pos_network_deploy_start(const char *root, char key[17]) {
  (void)root;
  (void)key;
  return -1;
}
bool pos_network_deploy_take(char url[512]) {
  (void)url;
  return false;
}
void pos_network_deploy_result(bool success) { (void)success; }
void pos_network_share_stop(void) {}
void pos_device_show(const pos_view_t *v) { (void)v; }
bool pos_device_event(pos_event_t *e, uint32_t timeout) {
  (void)e;
  (void)timeout;
  return false;
}
static int read_file(void *f, void *out, size_t n) {
  size_t result = fread(out, 1, n, f);
  return ferror(f) ? -1 : (int)result;
}
int pos_network_https_limit(const char *url, const char *method,
                            const char *body, const char *type,
                            const char *root, const char *dest, const char *sha,
                            pos_io_progress_t progress, void *ctx,
                            pos_error_t *e, uint32_t limit) {
  (void)body;
  (void)type;
  assert(!strcmp(url, stored.url) && !strcmp(method, "GET") && connected);
  assert(mqtt_stopped);
  ++downloads;
  FILE *f = fopen(manifest, "rb");
  assert(f);
  int result = pos_receive_file_limit(root, dest, UINT64_MAX, limit, sha,
                                      read_file, f, progress, ctx, e);
  fclose(f);
  return result;
}
int pos_device_install_update(const char *root, const pos_update_t *u,
                              pos_error_t *e) {
  (void)root;
  (void)e;
  assert(u->firmware);
  ++installs;
  return 0;
}
static void queue(int choice) { choices[choice_count++] = choice; }
int main(int argc, char **argv) {
  assert(argc >= 3);
  mode = argv[1];
  const char *root = argv[2];
  pos_update_source_t s;
  if (!strcmp(mode, "source-defaults")) {
    assert(!pos_update_source_load(&s) &&
           !strcmp(s.url, pos_update_official_url));
    char hash[65];
    assert(!pos_update_key_fingerprint(s.key, hash));
    puts(hash);
    has_source = true;
    memset(&stored, 0, sizeof(stored));
    stored.format = 1;
    stored.sequence = 42;
    assert(!pos_update_source_load(&s) && s.sequence == 42 && *s.url && *s.key);
    pos_update_source_official(&s);
    stored = s;
    strcpy(stored.url, "https://custom.example.test/manifest.json");
    assert(!pos_update_source_load(&s) && !strcmp(s.url, stored.url));
    *stored.key = 0;
    assert(!pos_update_source_load(&s) && !*s.key);
    stored.format = 9;
    assert(pos_update_source_load(&s) == -1 && stored.format == 9);
    stored.format = 1;
    nvs_failed = true;
    assert(pos_update_source_load(&s) == -1);
    return 0;
  }
  if (!strcmp(mode, "nvs-error")) {
    nvs_failed = true;
    pos_ui_updates(root);
    assert(saved_error && !downloads);
    return 0;
  }
  assert(argc == 5);
  manifest = argv[3];
  pos_update_source_official(&stored);
  has_source = true;
  FILE *key = fopen(argv[4], "rb");
  assert(key);
  size_t n = fread(stored.key, 1, sizeof(stored.key) - 1, key);
  stored.key[n] = 0;
  fclose(key);
  clock_unset = !strcmp(mode, "sync-time") || !strcmp(mode, "time-failed");
  queue(0); /* Check */
  if (!strcmp(mode, "wifi")) {
    connected = false;
    queue(0);
  }
  if (!strcmp(mode, "install") || !strcmp(mode, "wifi") ||
      !strcmp(mode, "sync-time"))
    queue(1);
  else if (!strcmp(mode, "cancel"))
    queue(0);
  queue(3); /* Back */
  pos_ui_updates(root);
  assert(choice_next == choice_count);
  if (!strcmp(mode, "time-failed")) {
    assert(!downloads && !installs && time_syncs == 1);
    return 0;
  }
  assert(downloads == 1);
  if (!strcmp(mode, "sync-time"))
    assert(time_syncs == 1 && !clock_unset);
  if (!strcmp(mode, "install") || !strcmp(mode, "wifi") ||
      !strcmp(mode, "sync-time"))
    assert(available && installs == 1);
  if (!strcmp(mode, "cancel"))
    assert(available && !installs);
  if (!strcmp(mode, "current"))
    assert(current && !installs);
  if (!strcmp(mode, "bad-signature"))
    assert(rejected && !installs);
  if (!strcmp(mode, "wifi"))
    assert(wifi_opened == 1);
  return 0;
}
