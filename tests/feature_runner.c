/* Reuse the simulated flash hooks, while exercising the real portable services.
 */
#define main pos_installer_runner_main
#include "host_runner.c"
#undef main
#include "cJSON.h"
#include "pos_ble.h"
#include "pos_console.h"
#include "pos_core.h"
#include "pos_device.h"
#include "pos_editor.h"
#include "pos_fs.h"
#include "pos_launcher.h"
#include "pos_navigation.h"
#include "pos_network_service.h"
#include "pos_peripherals.h"
#include "pos_mesh_service.h"
#include "pos_chat.h"
#include "pos_mesh_mqtt.h"
#include "pos_recovery.h"
#include "pos_settings.h"
#include "pos_timezone.h"
#include "pos_update_service.h"
#include "ui.h"
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <sys/resource.h>
#include <time.h>

uint32_t esp_random(void) { return 0x13572468u; }
static char calculator_answer[100];
static unsigned audio_holds, wifi_activity_steps, wifi_activity_seen;
static bool wifi_activity_test;
static bool maps_download_test, maps_guidance_seen, maps_turn_seen;
static bool map_controls_record;
static struct {
  double lat, lon, step;
  unsigned generation;
} map_frames[64];
static unsigned map_frame_count;
static pos_event_t ui_events[512];
static unsigned ui_next, ui_count, ui_frames;
static bool secret_leaked, notice_tail_seen, selection_seen;
static bool check_black_screens;
static pos_view_t ui_last;
static bool display_error, device_error, empty_folder_seen, last_page_seen,
    keep_sd_seen;
static unsigned folder_pages_seen, picker_frames;
static bool background_preview_seen;
static pos_view_t picker_last;
static pos_expansion_status_t fake_expansion;
int pos_meshtastic_notifications_configure(bool a,bool b){(void)a;(void)b;return 0;}
int pos_mesh_mqtt_start(const pos_mesh_mqtt_config_t *c){(void)c;return 0;}
void pos_mesh_mqtt_stop(void){}
void pos_mesh_mqtt_status(char out[128]){snprintf(out,128,"Off");}
static pos_meshtastic_status_t fake_mesh;
static pos_mesh_config_t fake_mesh_config;
static unsigned mesh_sends, mesh_locations, mesh_changes;
pos_meshtastic_status_t pos_meshtastic_status(void) { return fake_mesh; }
int pos_meshtastic_settings(pos_mesh_config_t *c) {
  if (!fake_mesh_config.region) pos_mesh_defaults(&fake_mesh_config);
  *c=fake_mesh_config; return 0;
}
int pos_meshtastic_configure(const pos_mesh_config_t *c,bool enable,pos_error_t *e) {
  (void)e;if(pos_mesh_config_valid(c)) return -1;
  fake_mesh_config=*c;fake_mesh.enabled=fake_mesh.ready=enable;
  fake_expansion.tx_enabled=false;++mesh_changes;return 0;
}
int pos_meshtastic_node(unsigned at,pos_mesh_node_t *n) { (void)at;(void)n;return -1; }
int pos_meshtastic_forget(uint32_t node,pos_error_t *e) { (void)node;(void)e;return 0; }
int pos_meshtastic_send(unsigned channel,uint32_t node,unsigned port,const void *text,size_t n,bool response,pos_error_t *e) {
  (void)channel;(void)node;(void)port;(void)response;(void)e;
  assert(n==5 && !memcmp(text,"hello",5));++mesh_sends;return 0;
}
int pos_meshtastic_announce(bool discover,pos_error_t *e) { (void)discover;(void)e;return 0; }
int pos_meshtastic_share(unsigned channel,uint32_t to,int32_t lat,int32_t lon,uint32_t utc,pos_error_t *e) {
  (void)channel;(void)to;(void)lat;(void)lon;(void)utc;(void)e;++mesh_locations;return 0;
}
int pos_meshtastic_retry(pos_error_t *e) { (void)e;return 0; }
static unsigned hardware_changes, audio_writes, audio_stops, ble_accepts,
    ble_pairings, microphone_starts, microphone_stops, microphone_reads;
static bool audio_active, ble_active, advance_after_audio,
    hardware_missing_seen, maps_ready_seen;
static pos_ble_status_t fake_ble;
int pos_audio_write(const int16_t *pcm, size_t frames, unsigned rate,
                    pos_error_t *e) {
  (void)e;
  assert(pcm && frames && rate == 22050);
  audio_active = true;
  ++audio_writes;
  if (advance_after_audio && ui_next < ui_count &&
      ui_events[ui_next].key == INT_MIN)
    ++ui_next;
  return 0;
}
void pos_audio_stop(void) {
  audio_active = false;
  ++audio_stops;
}
pos_expansion_status_t pos_expansion_status(void) { return fake_expansion; }
pos_location_t pos_expansion_location(void) { return fake_expansion.location; }
int pos_expansion_configure(bool enabled, uint32_t hz, bool tx,
                            pos_error_t *e) {
  if (enabled && !fake_expansion.cap_detected) {
    snprintf(e->message, sizeof(e->message), "LoRa cap not detected.");
    return -1;
  }
  ++hardware_changes;
  fake_expansion.lora_enabled = enabled;
  fake_expansion.frequency_hz = hz;
  fake_expansion.tx_enabled = enabled && tx;
  return 0;
}
bool pos_expansion_maps_available(void) {
  return fake_expansion.maps_available;
}
int pos_expansion_send(uint8_t type, uint32_t dest, const char *text,
                       bool location, pos_error_t *e) {
  (void)type;
  (void)dest;
  (void)text;
  (void)location;
  (void)e;
  return 0;
}
int pos_expansion_retry(pos_error_t *e) {
  (void)e;
  return 0;
}
int pos_expansion_range(bool enabled, pos_error_t *e) {
  (void)e;
  fake_expansion.logging = enabled;
  return 0;
}
int pos_ble_start(const char *root, const char *name, bool receive,
                  pos_error_t *e) {
  (void)root;
  (void)name;
  (void)e;
  assert(!ble_active);
  ble_active = true;
  fake_ble.ready = true;
  fake_ble.connected = true;
  fake_ble.secure = !receive;
  fake_ble.pairing = receive;
  fake_ble.passkey = 123456;
  fake_ble.offered = receive;
  strcpy(fake_ble.offer.name, "Incoming.txt");
  fake_ble.offer.size = 5;
  return 0;
}
void pos_ble_stop(void) { ble_active = false; }
pos_ble_status_t pos_ble_status(void) { return fake_ble; }
void pos_ble_poll(void) {}
void pos_ble_poll_progress(pos_io_progress_t progress, void *ctx) {
  (void)progress;
  (void)ctx;
  if (fake_ble.receiving)
    fake_ble.complete = true;
}
int pos_ble_pair(bool accept, pos_error_t *e) {
  (void)e;
  fake_ble.pairing = false;
  fake_ble.secure = accept;
  if (accept)
    ++ble_pairings;
  return 0;
}
int pos_ble_accept(bool accept, pos_error_t *e) {
  (void)e;
  fake_ble.offered = false;
  fake_ble.receiving = accept;
  if (accept)
    ++ble_accepts;
  return 0;
}
int pos_ble_accept_progress(bool accept, pos_io_progress_t progress, void *ctx,
                            pos_error_t *e) {
  (void)progress;
  (void)ctx;
  return pos_ble_accept(accept, e);
}
int pos_ble_scan(pos_error_t *e) {
  (void)e;
  fake_ble.scanning = false;
  return 0;
}
unsigned pos_ble_peers(pos_ble_peer_t peers[8]) {
  memset(peers, 0, 8 * sizeof(*peers));
  strcpy(peers[0].name, "Other Panputer");
  return 1;
}
int pos_ble_connect(unsigned peer, pos_error_t *e) {
  (void)e;
  assert(peer == 0);
  fake_ble.secure = true;
  return 0;
}
int pos_ble_send(const char *relative, pos_io_progress_t progress, void *ctx,
                 pos_error_t *e) {
  (void)progress;
  (void)ctx;
  (void)e;
  assert(relative && *relative);
  return 0;
}
static unsigned fake_gain = 8;
static bool microphone_ended, microphone_tail_enabled, microphone_silent;
static bool mic_quiet_meter_seen, mic_silence_seen;
static unsigned folder_hint_seen, microphone_backlog;
static unsigned microphone_tail_frames;
static const char *microphone_test_root;
static unsigned brightness_applied;
static bool safe_requested, display_hidden, display_wake_seen;
static unsigned display_off_count;
void pos_device_brightness(unsigned percent) { brightness_applied = percent; }
int pos_device_display_enabled(bool enabled) {
  assert(enabled == display_hidden);
  display_hidden = !enabled;
  if (!enabled) {
    ++display_off_count;
    assert(ui_next < ui_count && ui_events[ui_next].key == INT_MIN);
    ++ui_next; /* Input after the screen is off, rather than queued activation.
                */
  }
  return 0;
}
int pos_device_battery_mv(void) { return 3800; }
int pos_device_set_time_utc(const char *text) {
  int64_t epoch;
  return pos_time_parse_utc(text, &epoch) ? 0 : -1;
}
int pos_health_request_safe(bool enabled) {
  safe_requested = enabled;
  return 0;
}
void pos_ui_updates(const char *root) { (void)root; }
static void push_key(int key, unsigned mods) {
  assert(ui_count < sizeof(ui_events) / sizeof(ui_events[0]));
  ui_events[ui_count++] = (pos_event_t){key, mods, true};
}
static void keys_down(unsigned count) {
  for (unsigned i = 0; i < count; ++i)
    push_key(POS_KEY_DOWN, 0);
}
static void push_text(const char *text) {
  for (; *text; ++text)
    push_key(*text, 0);
}
static void replace_field(const char *text) {
  push_key('u', POS_MOD_CTRL);
  push_text(text);
  push_key(POS_KEY_ENTER, 0);
}
int64_t esp_timer_get_time(void) {
  static int64_t time;
  return time += 200000;
}
bool pos_device_event(pos_event_t *event, uint32_t timeout) {
  if (!timeout && ui_next < ui_count && ui_events[ui_next].key == INT_MIN) {
    return false; /* Later input arrives after the current operation finishes.
                   */
  }
  if (ui_next >= ui_count) {
    if (timeout)
      fprintf(stderr, "UI blocked at: %s / %s / %s\n", ui_last.lines[0],
              ui_last.lines[2], ui_last.lines[3]);
    assert(!timeout);
    return false;
  }
  *event = ui_events[ui_next++];
  if (!display_hidden && event->pressed && event->key == POS_KEY_HOME)
    pos_home_request();
  return true;
}
void pos_device_show(const pos_view_t *view) {
  if (wifi_activity_test && !strcmp(view->lines[0], "PC file transfer")) {
    if (!strcmp(view->lines[6], "Transferring")) wifi_activity_seen |= 1;
    if (!strcmp(view->lines[6], "Verifying on microSD")) wifi_activity_seen |= 2;
    if (!strcmp(view->lines[6], "Transfer complete")) wifi_activity_seen |= 4;
  }
  if (!strncmp(view->lines[0], "Calculator /", 12)) {
    snprintf(calculator_answer,sizeof(calculator_answer),"%s%s%s",view->lines[4],view->lines[5],view->lines[6]);
  }
  assert(!display_hidden);
  for (unsigned row = 0; row < POS_ROWS; ++row) {
    assert(!strstr(view->lines[row], "Old vault passphrase"));
    assert(!strstr(view->lines[row], "Import old saved networks"));
  }
  if (view->map_world && view->map_fix &&
      !strncmp(view->lines[1], "Follow", 6)) {
    assert(view->map_gps_x == 120 && view->map_gps_y == 44);
    assert(pos_view_pixel(view, 123, 54) == 0xf911); /* Pink GPS pin head. */
  }
  if (map_controls_record && view->map_pixels) {
    double lat, lon, step;
    const char *p = strstr(view->lines[1], "P:");
    assert(p && sscanf(p + 2, "%lf", &step) == 1);
    assert(sscanf(view->lines[13], "%lf %lf", &lat, &lon) == 2);
    if (!map_frame_count ||
        map_frames[map_frame_count - 1].generation != view->map_generation ||
        map_frames[map_frame_count - 1].step != step) {
      assert(map_frame_count < 64);
      map_frames[map_frame_count].lat = lat;
      map_frames[map_frame_count].lon = lon;
      map_frames[map_frame_count].step = step;
      map_frames[map_frame_count++].generation = view->map_generation;
    }
  }
  if (!strcmp(view->lines[0], "Recorder")) {
    if (view->visual == POS_VISUAL_MIC)
      assert(!microphone_backlog);
    if (view->visual == POS_VISUAL_MIC && strstr(view->lines[12], "0.7%"))
      mic_quiet_meter_seen = true;
    if (!strcmp(view->lines[1], "No input detected"))
      mic_silence_seen = true;
  }
  if (!strcmp(view->lines[0], "POS/Users/Owner")) {
    assert(view->visual == POS_VISUAL_NONE);
    const char *names[] = {"Documents", "Music", "Pictures", "Mixed", "Links"};
    for (unsigned i = 0; i < 5; ++i)
      for (unsigned row = 3; row < 13; ++row)
        if (!strcmp(view->lines[row], names[i]))
          folder_hint_seen |= 1u << i;
  }
  if (display_off_count && !strcmp(view->lines[0], "System")) {
    assert(view->selected_row == 6); /* Turn display off remains selected. */
    display_wake_seen = true;
  }
  if (view->map_route_pixels && view->map_turn == 2)
    maps_turn_seen = true;
  if (view->map_pixels && view->map_fix && view->map_waypoint &&
      !view->map_route_pixels) {
    assert(strstr(view->lines[1], "deg"));
    maps_guidance_seen = true;
  }
  ++ui_frames;
  ui_last = *view;
  if (check_black_screens) {
    assert(view->black_background);
    assert(pos_view_pixel(view, 239, 130) == 0);
    assert(pos_view_pixel(view, 239, 0) == 0);
    if (view->selected_row >= 0)
      assert(pos_view_pixel(view, 239, view->selected_row * 8) == 0xffff);
  }
  for (unsigned i = 0; i < 12; ++i)
    if (view->editor_selected[i])
      selection_seen = true;
  if (getenv("POS_TRACE_UI")) {
    fprintf(stderr, "TITLE %s selected=%d\n", view->lines[0],
            view->selected_row);
    for (unsigned i = 1; i < POS_ROWS; ++i)
      if (*view->lines[i])
        fprintf(stderr, "  %s\n", view->lines[i]);
  }
  for (unsigned i = 0; i < POS_ROWS; ++i) {
    if (strstr(view->lines[i], "not detected"))
      hardware_missing_seen = true;
    if (strstr(view->lines[i], "GPS requirement ready"))
      maps_ready_seen = true;
  }
  if (view->color_picker) {
    ++picker_frames;
    picker_last = *view;
  }
  if (view->visual == POS_VISUAL_STRIPES)
    background_preview_seen = true;
  if (!strcmp(view->lines[0], "Display name not valid"))
    display_error = true;
  if (!strcmp(view->lines[0], "Device name not valid"))
    device_error = true;
  unsigned page = 0;
  if (sscanf(view->lines[1], "Folder page %u/", &page) == 1 && page <= 3 &&
      page)
    folder_pages_seen |= 1u << (page - 1);
  for (int row = 0; row < POS_ROWS; ++row) {
    if (strstr(view->lines[row], "Empty folder"))
      empty_folder_seen = true;
    if (strstr(view->lines[row], "Next page (last)"))
      last_page_seen = true;
    if (strstr(view->lines[row], "Keep the SD card inserted."))
      keep_sd_seen = true;
  }
  for (int row = 0; row < POS_ROWS; ++row)
    if (strstr(view->lines[row], "s3cret"))
      secret_leaked = true;
    else if (strstr(view->lines[row], "FINAL_LINE"))
      notice_tail_seen = true;
}
static pos_network_status_t fake_network;
static uint8_t fake_vault[POS_WIFI_VAULT_BYTES];
static bool has_vault, share_active;
static unsigned share_stops;
int pos_device_wifi(const char *ssid, const char *password, uint32_t timeout) {
  (void)timeout;
  assert(!strcmp(ssid, "Test WiFi"));
  assert(!strcmp(password, "network-password"));
  fake_network.connected = true;
  strcpy(fake_network.ssid, ssid);
  strcpy(fake_network.ip, "192.0.2.1");
  return 0;
}
void pos_device_wifi_stop(void) { fake_network.connected = false; }
pos_network_status_t pos_network_status(void) { return fake_network; }
int pos_network_scan(pos_network_ap_t *aps, unsigned capacity,
                     unsigned *count) {
  assert(capacity);
  memset(aps, 0, sizeof(*aps));
  strcpy(aps[0].ssid, "Test WiFi");
  aps[0].secured = true;
  *count = 1;
  return 0;
}
int pos_network_profiles(const char *pass, pos_wifi_profiles_t *profiles) {
  if (!has_vault) {
    memset(profiles, 0, sizeof(*profiles));
    return 1;
  }
  return pos_wifi_vault_unlock(fake_vault, pass, profiles);
}
int pos_network_remember(const char *pass,
                         const pos_wifi_profiles_t *profiles) {
  uint8_t salt[16] = {1}, nonce[12] = {2};
  int result = pos_wifi_vault_encrypt(profiles, pass, salt, nonce, fake_vault);
  if (!result)
    has_vault = true;
  return result;
}
static bool fake_auto;
int pos_network_known(pos_wifi_profiles_t *p) {
  return pos_network_profiles("device-generated-test-key", p);
}
int pos_network_known_save(const pos_wifi_profiles_t *p) {
  return pos_network_remember("device-generated-test-key", p);
}
bool pos_network_auto_enabled(void) { return fake_auto; }
int pos_network_auto_set(bool enabled) {
  fake_auto = enabled;
  return 0;
}
void pos_network_auto_pause(bool paused) { (void)paused; }
int pos_update_source_load(pos_update_source_t *s) {
  pos_update_source_official(s);
  return 0;
}
int pos_device_history_import(const char *r, pos_error_t *e) {
  (void)r;
  (void)e;
  return -1;
}
int pos_device_history_rollback(const char *r, const char *id, pos_error_t *e) {
  (void)r;
  (void)id;
  (void)e;
  return -1;
}
int pos_network_sync_time(void) { return fake_network.connected ? 0 : -1; }
int pos_network_https(const char *url, const char *method, const char *body,
                      const char *type, const char *root, const char *dest,
                      const char *hash, pos_io_progress_t progress, void *ctx,
                      pos_error_t *error) {
  (void)url;
  (void)method;
  (void)body;
  (void)type;
  (void)root;
  (void)dest;
  (void)hash;
  (void)progress;
  (void)ctx;
  strcpy(error->message, "simulated transport failure");
  return -1;
}
int pos_network_https_limit(const char *u, const char *m, const char *b,
                            const char *t, const char *r, const char *d,
                            const char *h, pos_io_progress_t p, void *c,
                            pos_error_t *e, uint32_t limit) {
  assert(limit == POS_MAP_DOWNLOAD_LIMIT);
  if (maps_download_test) {
    assert(strstr(u, "https://api.openstreetmap.org/api/0.6/map?bbox=") == u &&
           !strcmp(m, "GET"));
    char full[POS_FS_PATH];
    assert(!pos_fs_join(full, r, d));
    const char *xml =
        "<osm version=\"0.6\"><node id=\"2\" lat=\"34.051\" lon=\"-118.247\"/>"
        "<node id=\"1\" lat=\"34.050\" lon=\"-118.248\"/>"
        "<way id=\"1\"><nd ref=\"1\"/><nd ref=\"2\"/><tag "
        "k=\"highway\" v=\"residential\"/></way></osm>";
    return pos_fs_write_new(full, xml, strlen(xml));
  }
  return pos_network_https(u, m, b, t, r, d, h, p, c, e);
}
int pos_network_share_start(const char *root, const char *file, char key[17]) {
  (void)root;
  (void)file;
  assert(!share_active);
  share_active = true;
  strcpy(key, "0123456789abcdef");
  return 0;
}
void pos_network_share_stop(void) {
  share_active = false;
  ++share_stops;
}
static void idle_event(void) {
  ui_events[ui_count++] = (pos_event_t){INT_MIN, 0, false};
}
static int editor_ui_tests(const char *root, const char *mode) {
  assert(!pos_fs_layout(root));
  ui_next = ui_count = 0;
  push_key(POS_KEY_ENTER, 0); /* New */
  push_key(POS_KEY_ENTER, 0); /* Untitled.txt */
  push_text("wasd");
  if (!strcmp(mode, "editor-ui-advanced")) {
    push_key('a', POS_MOD_CTRL);
    push_key('c', POS_MOD_CTRL);
    idle_event();
    push_key('z', 0);
    push_key('z', POS_MOD_CTRL);
    push_key('y', POS_MOD_CTRL);
    push_key('v', POS_MOD_CTRL);
    idle_event();
    push_key(POS_KEY_LEFT, POS_MOD_CTRL);
    push_key(POS_KEY_RIGHT, POS_MOD_SHIFT);
    push_key('c', POS_MOD_CTRL);
    idle_event();
    push_key(POS_KEY_RIGHT, POS_MOD_CTRL);
    push_key('s', POS_MOD_CTRL);
    idle_event();
    push_key(POS_KEY_HOME, 0);
  } else if (!strcmp(mode, "editor-ui-delete")) {
    push_key('l', POS_MOD_CTRL); /* Start of line. */
    push_key(POS_KEY_DELETE, POS_MOD_FN);
    push_key('s', POS_MOD_CTRL);
    idle_event();
    push_key(POS_KEY_HOME, 0);
  } else if (!strcmp(mode, "editor-ui-idle")) {
    for (unsigned i = 0; i < 30; ++i)
      idle_event();
    push_key('s', POS_MOD_CTRL);
    idle_event();
    push_key(POS_KEY_HOME, 0);
  } else if (!strcmp(mode, "editor-ui-trash")) {
    push_key('s', POS_MOD_CTRL);
    idle_event();
    push_key('x', 0);
    push_key(POS_KEY_ESCAPE, 0);
    keys_down(18);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0); /* explanation */
    keys_down(1);
    push_key(POS_KEY_ENTER, 0); /* confirm */
    keys_down(3);
    push_key(POS_KEY_ENTER, 0); /* editor app Back */
  } else if (!strcmp(mode, "editor-ui-save") ||
             !strcmp(mode, "editor-ui-punctuation")) {
    if (!strcmp(mode, "editor-ui-punctuation"))
      push_text(";,./:<>?");
    push_key('s', POS_MOD_CTRL);
    idle_event();
    push_key(POS_KEY_HOME, 0);
  } else if (!strcmp(mode, "editor-ui-draft")) {
    push_key(POS_KEY_HOME, 0);
    push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_ENTER, 0);
    idle_event();
    push_key(POS_KEY_ENTER, 0); /* draft save and notice */
  } else {
    push_key(POS_KEY_HOME, 0);
    keys_down(2);
    push_key(POS_KEY_ENTER, 0);
  }
  pos_ui_editor(root);
  assert(ui_next == ui_count);
  if (!strcmp(mode, "editor-ui-advanced"))
    assert(selection_seen);
  if (!strcmp(mode, "editor-ui-idle"))
    assert(ui_frames < 15);
  puts("editor UI passed");
  return 0;
}
static int wifi_ui_tests(const char *root, const char *mode) {
  ui_next = ui_count = 0;
  if (!strcmp(mode, "wifi-ui-remember")) {
    keys_down(2);
    push_key(POS_KEY_ENTER, 0); /* manual network */
    push_text("Test WiFi");
    push_key(POS_KEY_ENTER, 0);
    push_text("network-password");
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0); /* connected notice */
    push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0); /* saved notice */
    keys_down(7);
    push_key(POS_KEY_ENTER, 0); /* Back */
    pos_ui_wifi(root);
    assert(has_vault && fake_network.connected);
    pos_wifi_profiles_t profiles = {0};
    assert(!pos_wifi_vault_unlock(fake_vault, "device-generated-test-key",
                                  &profiles));
    assert(profiles.count == 1 &&
           !strcmp(profiles.networks[0].ssid, "Test WiFi"));
    assert(!strcmp(profiles.networks[0].password, "network-password"));
    pos_secure_zero(&profiles, sizeof(profiles));
  } else if (!strcmp(mode, "wifi-ui-known")) {
    pos_wifi_profiles_t profiles = {.count = 1};
    strcpy(profiles.networks[0].ssid, "Test WiFi");
    strcpy(profiles.networks[0].password, "network-password");
    assert(!pos_network_known_save(&profiles));
    keys_down(3);
    push_key(POS_KEY_ENTER, 0);
    keys_down(1);
    push_key(POS_KEY_ENTER, 0); /* enable auto */
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0); /* connect + notice */
    push_key(POS_KEY_ENTER, 0);
    keys_down(1);
    push_key(POS_KEY_ENTER, 0); /* Forget */
    keys_down(1);
    push_key(POS_KEY_ENTER, 0); /* confirm */
    keys_down(1);
    push_key(POS_KEY_ENTER, 0); /* empty known menu Back */
    keys_down(6);
    push_key(POS_KEY_ENTER, 0); /* Wi-Fi Back */
    pos_ui_wifi(root);
    assert(fake_auto && fake_network.connected);
    assert(!pos_network_known(&profiles) && !profiles.count);
  } else if (!strcmp(mode, "wifi-ui-progress")) {
    fake_network.connected = true;
    strcpy(fake_network.ip, "192.0.2.1");
    wifi_activity_test = true;
    push_key(POS_KEY_ENTER, 0); /* LAN notice */
    push_key(POS_KEY_ENTER, 0); /* Start */
    for(unsigned i=0;i<3;++i)ui_events[ui_count++]=(pos_event_t){INT_MIN,0,false};
    push_key(POS_KEY_HOME, 0);
    pos_ui_wifi_export(root, "");
    assert(wifi_activity_seen == 7 && !audio_holds && !share_active);
  } else {
    fake_network.connected = true;
    strcpy(fake_network.ip, "192.0.2.1");
    keys_down(7);
    push_key(POS_KEY_ENTER, 0); /* transfer */
    push_key(POS_KEY_ENTER, 0); /* receive only */
    push_key(POS_KEY_ENTER, 0); /* LAN notice */
    push_key(POS_KEY_ENTER, 0); /* start */
    push_key(POS_KEY_HOME, 0);
    pos_ui_wifi(root);
    assert(!share_active && share_stops >= 2);
    assert(pos_ui_take_home());
  }
  assert(ui_next == ui_count);
  puts("Wi-Fi UI passed");
  return 0;
}
static void queue_text(const char *text) {
  ui_next = ui_count = 0;
  for (; *text; ++text)
    ui_events[ui_count++] = (pos_event_t){*text, 0, true};
  ui_events[ui_count++] = (pos_event_t){POS_KEY_ENTER, 0, true};
}
static int ui_tests(void) {
  char text[32] = "";
  queue_text("wasd");
  assert(pos_ui_text("Text", "Normal letters", text, sizeof(text), false));
  assert(!strcmp(text, "wasd"));
  queue_text("s3cret");
  text[0] = 0;
  assert(pos_ui_text("Wi-Fi", "Password", text, sizeof(text), true));
  assert(!secret_leaked);
  ui_next = 0;
  ui_count = 2;
  ui_events[0] = (pos_event_t){POS_KEY_HOME, 0, true};
  ui_events[1] = (pos_event_t){POS_KEY_ENTER, 0, true};
  assert(!pos_ui_text("Text", "cancel", text, sizeof(text), false));
  unsigned before = ui_frames;
  const char *items[] = {"first", "second"};
  assert(pos_ui_menu("Nested menu", items, 2) == -1);
  pos_ui_notice("Nested notice", "must not block");
  assert(ui_frames == before && ui_next == 1);
  assert(pos_ui_take_home());
  assert(pos_ui_menu("Launcher", items, 2) == 0);
  ui_count = 1;
  ui_next = 0;
  ui_events[0] = (pos_event_t){POS_KEY_HOME, 0, true};
  assert(pos_ui_progress(NULL, "backup", "file", 8, 16) == 1);
  assert(pos_ui_take_home());
  ui_next = 0;
  ui_count = 3;
  for (unsigned i = 0; i < 3; ++i)
    ui_events[i] = (pos_event_t){POS_KEY_ENTER, 0, true};
  char body[128] = "";
  for (unsigned i = 0; i < 30; ++i)
    strcat(body, "a\n");
  strcat(body, "FINAL_LINE");
  pos_ui_notice("Long message", body);
  assert(notice_tail_seen && ui_next == 3);
  const char *long_menu[12];
  for (unsigned i = 0; i < 12; ++i)
    long_menu[i] = "Item";
  ui_next = ui_count = 0;
  keys_down(11);
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_menu("Paged menu", long_menu, 12) == 11);
  puts("{\"home_bubbles_to_launcher\":true,\"password_display_leaked\":false}");
  return 0;
}

static int boot_splash_tests(void) {
  pos_view_t view;
  uint32_t hold = 0, total = 0;
  /* Independently check every pixel while each stripe grows over black.
   * Boot artwork also takes priority over the normal battery overlay. */
  for (unsigned frame = 0; frame <= 12; ++frame) {
    assert(pos_boot_frame(frame, &view, &hold));
    assert(hold == 40 && view.boot_splash && !view.solid_color);
    assert(!view.boot_text_alpha);
    view.battery_overlay = true;
    view.battery_available = true;
    view.battery_percent = 75;
    unsigned pink = frame < 6 ? frame * 40 : 240;
    unsigned yellow = frame <= 3 ? 0 : frame < 9 ? (frame - 3) * 40 : 240;
    unsigned blue = frame <= 6 ? 0 : frame < 12 ? (frame - 6) * 40 : 240;
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x) {
        uint16_t expected = y < 45 && x < (int)pink ? pos_boot_colors[0]
                            : y >= 45 && y < 90 && x < (int)yellow
                                ? pos_boot_colors[1]
                            : y >= 90 && x < (int)blue ? pos_boot_colors[2]
                                                       : 0;
        assert(pos_view_pixel(&view, x, y) == expected);
      }
    total += hold;
  }
  uint16_t previous[3] = {pos_boot_colors[0], pos_boot_colors[1],
                          pos_boot_colors[2]};
  for (unsigned frame = 1; frame <= 10; ++frame) {
    assert(pos_boot_frame(12 + frame, &view, &hold));
    assert(hold == (frame == 10 ? 1000 : 40) && view.boot_pink_width == 240 &&
           view.boot_yellow_width == 240 && view.boot_blue_width == 240);
    for (unsigned stripe = 0; stripe < 3; ++stripe) {
      uint16_t pixel = pos_view_pixel(&view, 30, 22 + stripe * 39);
      assert((pixel >> 11) <= (previous[stripe] >> 11));
      assert(((pixel >> 5) & 63) <= ((previous[stripe] >> 5) & 63));
      assert((pixel & 31) <= (previous[stripe] & 31));
      if (frame == 1)
        assert(pixel != 0 && pixel != pos_boot_colors[stripe]);
      previous[stripe] = pixel;
      assert(pos_view_pixel(&view, 239, stripe * 45) ==
             pos_boot_colors[stripe]);
    }
    total += hold;
  }
  assert(!previous[0] && !previous[1] && !previous[2]);
  assert(pos_boot_frame(POS_BOOT_HOLD_FRAME, &view, &hold));
  assert(hold == 1000 && view.boot_text_alpha == 255);
  unsigned ink[3] = {0}, ink_stripes[3] = {0};
  int left = 240, right = 0, top = 135, bottom = 0;
  for (int y = 0; y < 135; ++y)
    for (int x = 0; x < 240; ++x) {
      uint16_t pixel = pos_view_pixel(&view, x, y);
      if (pixel == 0) {
        assert(y >= 22 && y < 113 && x >= 21 && x < 219);
        ++ink[(x - 21) / 72];
        ++ink_stripes[y / 45];
        if (x < left)
          left = x;
        if (x > right)
          right = x;
        if (y < top)
          top = y;
        if (y > bottom)
          bottom = y;
      } else
        assert(pixel == pos_boot_colors[y / 45]);
    }
  for (unsigned i = 0; i < 3; ++i)
    assert(ink[i] > 500 && ink_stripes[i] > 500);
  assert(left == 21 && right == 218 && top == 22 && bottom == 112);
  assert(total == 1880);
  assert(!pos_boot_frame(POS_BOOT_HOLD_FRAME + 1, &view, &hold));
  assert(!pos_boot_frame(UINT32_MAX, &view, &hold));
  assert(!pos_boot_frame(0, NULL, &hold));
  assert(!pos_boot_frame(0, &view, NULL));
  pos_view_clear(&view, "Home");
  assert(!view.boot_splash && !view.solid_color && !view.boot_text_alpha);
  assert(!view.boot_pink_width && !view.boot_yellow_width &&
         !view.boot_blue_width);
  assert(pos_view_pixel(&view, 0, 90) != pos_boot_colors[2]);
  puts("{\"black_start\":true,\"pink_first\":true,\"yellow_staggered\":true,"
       "\"blue_staggered\":true,\"smooth_fade\":true,"
       "\"large_centered_letters\":true,\"splash_ms\":1000}");
  return 0;
}
static int text_qol_tests(void) {
  char text[32] = "old contents";
  pos_keyboard_t keyboard = {0};
  pos_event_t event;
  assert(!pos_keyboard_decode(&keyboard, 0x84, &event)); /* physical Ctrl */
  assert(pos_keyboard_decode(&keyboard, 0xa4, &event) && event.key == 'u' &&
         (event.modifiers & POS_MOD_CTRL));
  ui_next = ui_count = 0;
  ui_events[ui_count++] = event;
  push_text("new");
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_text("Text", "Replace", text, sizeof(text), false));
  assert(!strcmp(text, "new") && !strcmp(ui_last.lines[10], "3/31 characters"));
  ui_next = ui_count = 0;
  push_key(21, 0);
  push_text("s3cret");
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_text("Password", "Masked", text, sizeof(text), true));
  assert(!secret_leaked && !strcmp(text, "s3cret"));
  ui_next = ui_count = 0;
  push_key('U', POS_MOD_CTRL | POS_MOD_SHIFT);
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_text("Text", "Clear", text, sizeof(text), false));
  for (unsigned i = 0; i < sizeof(text); ++i)
    assert(!text[i]);
  char short_text[4] = "";
  queue_text("abcdef");
  assert(pos_ui_text("Text", "Limit", short_text, sizeof(short_text), false));
  assert(!strcmp(short_text, "abc") &&
         !strcmp(ui_last.lines[10], "3/3 characters"));
  puts("{\"ctrl_u\":true,\"usb_ctrl_u\":true,\"password_masked\":true,\"limit_"
       "enforced\":true}");
  return 0;
}
static int setup_tests(const char *root, const char *mode) {
  check_black_screens = true;
  pos_settings_t current;
  pos_settings_defaults(&current);
  int loaded = pos_settings_load(root, &current);
  assert(loaded == 0 || loaded == 1);
  pos_settings_t before = current;
  bool retry = strstr(mode, "retry") != NULL;
  ui_next = ui_count = 0;
  if (retry) {
    replace_field("");
    push_key(POS_KEY_ENTER, 0);
  }
  replace_field("Stevie");
  if (retry) {
    replace_field("PAN PUTER");
    push_key(POS_KEY_ENTER, 0);
  }
  replace_field("PANPUTER");
  if (strstr(mode, "cancel-time"))
    push_key(POS_KEY_ESCAPE, 0);
  else {
    push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_ENTER, 0); /* Pacific */
    if (strstr(mode, "home"))
      push_key(POS_KEY_HOME, 0);
    else if (strstr(mode, "cancel"))
      push_key(POS_KEY_ESCAPE, 0);
    else {
      push_key(POS_KEY_DOWN, 0);
      push_key(POS_KEY_ENTER, 0);
    }
  }
  if (strstr(mode, "fail"))
    push_key(POS_KEY_ENTER, 0); /* save-error notice */
  bool saved = pos_ui_setup(root, &current, loaded == 1);
  assert(ui_next == ui_count);
  if (!saved)
    assert(!memcmp(&current, &before, sizeof(current)));
  if (saved)
    assert(!strcmp(current.display_name, "Stevie") &&
           !strcmp(current.device_name, "PANPUTER") &&
           current.brightness == 70 &&
           !strcmp(current.timezone, "PST8PDT,M3.2.0,M11.1.0"));
  printf("{\"saved\":%s,\"display_error\":%s,\"device_error\":%s}\n",
         saved ? "true" : "false", display_error ? "true" : "false",
         device_error ? "true" : "false");
  return 0;
}
static int keyboard_ui_tests(bool fn) {
  /* Stock physical Down + Enter work both with and without Fn. */
  const uint8_t raw[] = {0x83, 0xba, 0x3a, 0xc3, 0x43};
  pos_keyboard_t keyboard = {0};
  ui_next = ui_count = 0;
  check_black_screens = true;
  for (unsigned i = fn ? 0 : 1; i < sizeof(raw); ++i) {
    pos_event_t e;
    if (pos_keyboard_decode(&keyboard, raw[i], &e))
      ui_events[ui_count++] = e;
  }
  const char *items[] = {"First", "Second"};
  assert(pos_ui_menu("Installer", items, 2) == 1);
  assert(ui_last.selected_row == 4);
  const uint8_t scans[] = {57, 54, 58, 64};
  const char *punctuation = ";,./";
  const char *shifted = ":<>?";
  const int arrows[] = {POS_KEY_UP, POS_KEY_LEFT, POS_KEY_DOWN, POS_KEY_RIGHT};
  memset(&keyboard, 0, sizeof(keyboard));
  ui_next = ui_count = 0;
  for (unsigned i = 0; i < 4; ++i) {
    pos_event_t e;
    assert(pos_keyboard_decode(&keyboard, scans[i] | 128, &e));
    assert(e.key == punctuation[i] && pos_navigation(&e) == arrows[i]);
    assert(pos_launcher_action(&e) == (i == 0   ? POS_KEY_ENTER
                                       : i == 2 ? 0
                                                : arrows[i]));
    ui_events[ui_count++] = e;
    assert(pos_keyboard_decode(&keyboard, scans[i], &e) && !pos_navigation(&e));
    e = (pos_event_t){shifted[i], POS_MOD_SHIFT, true};
    assert(pos_navigation(&e) == arrows[i]);
    e = (pos_event_t){punctuation[i], POS_MOD_CTRL, true};
    assert(pos_navigation(&e) == punctuation[i]);
  }
  push_key(POS_KEY_ENTER, 0);
  char text[16] = "";
  assert(pos_ui_text("Text", "Punctuation", text, sizeof(text), false));
  assert(!strcmp(text, ";,./"));
  puts("Physical arrows navigate without Fn; text punctuation preserved");
  return 0;
}
static void choose(unsigned index) {
  for (unsigned i = 0; i < index; ++i)
    push_key(POS_KEY_DOWN, 0);
  push_key(POS_KEY_ENTER, 0);
}
static int settings_ui_tests(const char *root, const char *mode) {
  pos_settings_t s;
  pos_settings_defaults(&s);
  s.setup_complete = true;
  strcpy(s.display_name, "Keep Stevie");
  assert(!pos_settings_save(root, &s));
  ui_next = ui_count = 0;
  if (!strcmp(mode, "settings-ui-sounds")) {
    choose(11);
    push_key(POS_KEY_ENTER, 0); // Master off.
    push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_ENTER, 0);
    replace_field("37");
    push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_HOME, 0);
    pos_ui_settings(root, &s);
    pos_settings_t loaded;
    assert(!pos_settings_load(root, &loaded));
    assert(!loaded.sounds_enabled && !loaded.sounds_keys &&
           !loaded.sounds_alerts && loaded.sounds_volume == 37);
    assert(loaded.microphone_gain == 8 &&
           !strcmp(loaded.display_name, "Keep Stevie"));
    return 0;
  }
  if (!strcmp(mode, "settings-ui-brightness")) {
    choose(1);
    replace_field("5");
    push_key(POS_KEY_ENTER, 0);
    replace_field("55");
    push_key(POS_KEY_ESCAPE, 0);
  } else if (!strcmp(mode, "settings-ui-profile")) {
    choose(0);
    choose(0);
    replace_field("Changed Stevie");
    push_key(POS_KEY_ESCAPE, 0);
    push_key(POS_KEY_ESCAPE, 0);
  } else if (!strcmp(mode, "settings-ui-cancel")) {
    choose(1);
    push_key('u', POS_MOD_CTRL);
    push_text("90");
    push_key(POS_KEY_ESCAPE, 0);
    push_key(POS_KEY_ESCAPE, 0);
  } else if (!strcmp(mode, "settings-ui-timezone")) {
    choose(2);
    choose(1);
    choose(15);
    push_key(POS_KEY_ESCAPE, 0);
    push_key(POS_KEY_ESCAPE, 0);
  } else if (!strncmp(mode, "settings-ui-display-", 20)) {
    choose(6);
    choose(3);
    idle_event();
    push_key(strstr(mode, "home") ? POS_KEY_HOME : POS_KEY_ENTER, 0);
    push_key(POS_KEY_ESCAPE, 0); /* Return from System after wake. */
    push_key(POS_KEY_ESCAPE, 0);
  } else {
    choose(6);
    choose(2);
    choose(1);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ESCAPE, 0);
    push_key(POS_KEY_ESCAPE, 0);
  }
  pos_ui_settings(root, &s);
  assert(ui_next == ui_count);
  assert(!strcmp(s.display_name, !strcmp(mode, "settings-ui-profile")
                                     ? "Changed Stevie"
                                     : "Keep Stevie"));
  assert(s.brightness == (!strcmp(mode, "settings-ui-brightness") ? 55 : 70));
  if (!strcmp(mode, "settings-ui-timezone"))
    assert(!strcmp(s.timezone, "NPT-5:45"));
  if (!strcmp(mode, "settings-ui-safe"))
    assert(safe_requested);
  if (!strncmp(mode, "settings-ui-display-", 20))
    assert(display_off_count == 1 && display_wake_seen && !display_hidden &&
           !pos_home_pending());
  assert(brightness_applied == s.brightness);
  return 0;
}
static int browser_tests(const char *root, const char *mode) {
  ui_next = ui_count = 0;
  if (!strcmp(mode, "browse-pages")) {
    choose(2);
    choose(2);
    choose(1);
    choose(1);
  }
  if (!strcmp(mode, "browse-empty")) {
    choose(2);
    choose(1);
  }
  if (!strcmp(mode, "browse-preview")) {
    choose(2);
    choose(3);
    push_key(POS_KEY_ENTER, 0);
  }
  push_key(POS_KEY_ESCAPE, 0);
  pos_ui_browse(root, "");
  assert(ui_next == ui_count);
  printf("{\"pages\":%u,\"empty\":%s,\"last_page\":%s,\"final_page\":\"%s\"}\n",
         folder_pages_seen, empty_folder_seen ? "true" : "false",
         last_page_seen ? "true" : "false", ui_last.lines[1]);
  return 0;
}
static int phase_tests(void) {
  const char *phases[] = {"copy", "verify", "firmware_write",
                          "firmware_verify"};
  const char *titles[] = {"Prepare SD system", "Verify files",
                          "Write runtime firmware", "Verify runtime firmware"};
  ui_next = ui_count = 0;
  for (unsigned i = 0; i < 4; ++i) {
    assert(!pos_ui_progress(NULL, phases[i], "file", 1, 2));
    assert(!strcmp(ui_last.lines[0], titles[i]));
  }
  push_key(POS_KEY_ENTER, 0);
  pos_ui_install_complete();
  assert(keep_sd_seen && ui_next == ui_count);
  puts("{\"phases_clear\":true,\"keep_sd_inserted\":true}");
  return 0;
}
static int background_tests(const char *root, const char *mode) {
  pos_settings_t settings;
  pos_settings_defaults(&settings);
  (void)pos_settings_load(root, &settings);
  pos_settings_t before = settings;
  ui_next = ui_count = 0;
  if (!strncmp(mode, "background-preset-", 18)) {
    bool pan = strstr(mode, "pan") != NULL;
    bool cancel = strstr(mode, "cancel") != NULL;
    /* Invalid presets must not modify the caller's current background. */
    assert(!pos_background_preset(NULL, POS_BACKGROUND_PANSEXUAL));
    assert(!pos_background_preset(&settings.background,
                                  (pos_background_preset_t)99));
    assert(!memcmp(&settings, &before, sizeof(settings)));
    push_key('p', 0);
    choose(pan ? 0 : 1);
    push_key(cancel ? POS_KEY_ESCAPE : 's', 0);
    bool saved = pos_ui_background(root, &settings);
    assert(saved == !cancel && ui_next == ui_count);
    if (cancel) {
      assert(!memcmp(&settings, &before, sizeof(settings)));
    } else {
      pos_background_t expected;
      assert(pos_background_preset(&expected, pan ? POS_BACKGROUND_PANSEXUAL
                                                  : POS_BACKGROUND_NONBINARY));
      assert(!memcmp(&settings.background, &expected, sizeof(expected)));
      pos_settings_t loaded;
      assert(!pos_settings_load(root, &loaded));
      assert(!memcmp(&loaded.background, &expected, sizeof(expected)));
    }
    return 0;
  }
  if (!strcmp(mode, "background-invalid-count")) {
    for (unsigned i = 0; i < 15; ++i)
      push_key(POS_KEY_LEFT, 0);
    push_key(POS_KEY_ESCAPE, 0);
  } else {
    push_key(POS_KEY_RIGHT, 0); /* Add a fourth stripe in the live canvas. */
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_RIGHT, 0);
    push_key(POS_KEY_DOWN, 0);
    push_key('e', 0);
    push_key(POS_KEY_UP, POS_MOD_SHIFT);
    if (!strcmp(mode, "background-home"))
      push_key(POS_KEY_HOME, 0);
    else {
      push_key(POS_KEY_ENTER, 0);
      if (!strcmp(mode, "background-cancel"))
        push_key(POS_KEY_ESCAPE, 0);
      else {
        push_key('s', 0);
        if (!strcmp(mode, "background-save-failure")) {
          push_key(POS_KEY_ENTER, 0);
          push_key(POS_KEY_ESCAPE, 0);
        }
      }
    }
  }
  bool saved_background = pos_ui_background(root, &settings);
  bool success = !strcmp(mode, "background-save");
  assert(saved_background == success);
  if (success) {
    assert(background_preview_seen && picker_frames > 1);
    assert(settings.background.count == 4);
    assert(settings.background.colors[0] != before.background.colors[0]);
    assert(settings.background.colors[3] == 0xffffff);
    pos_settings_t reloaded;
    assert(!pos_settings_load(root, &reloaded));
    assert(!memcmp(&settings.background, &reloaded.background,
                   sizeof(settings.background)));
    assert(!strcmp(settings.display_name, before.display_name));
  } else {
    assert(!memcmp(&settings, &before, sizeof(settings)));
  }
  if (!strcmp(mode, "background-home"))
    assert(pos_ui_take_home());
  assert(ui_next == ui_count);
  puts("{\"draft_save_cancel_verified\":true}");
  return 0;
}
static int picker_tests(void) {
  uint32_t rgb = 0x808080, original = rgb;
  ui_next = ui_count = 0;
  push_key(POS_KEY_RIGHT, 0);
  push_key(POS_KEY_DOWN, 0);
  push_key('e', 0);
  push_key(POS_KEY_UP, POS_MOD_SHIFT);
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Pick a color", &rgb));
  assert(picker_last.picker_hue == 10 && picker_last.picker_saturation == 25 &&
         picker_last.picker_value == 481 && rgb != original);
  assert(picker_frames == 5);
  original = rgb;
  ui_next = ui_count = 0;
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Keep exact color", &rgb) && rgb == original);
  ui_next = ui_count = 0;
  for (unsigned i = 0; i < 3; ++i)
    push_key(POS_KEY_LEFT, 0);
  push_key(POS_KEY_ESCAPE, 0);
  assert(!pos_ui_color("Cancel color", &rgb) && rgb == original);
  ui_next = ui_count = 0;
  push_key(POS_KEY_HOME, 0);
  assert(!pos_ui_color("Close app", &rgb) && rgb == original &&
         pos_ui_take_home());
  rgb = 0xffffff;
  ui_next = ui_count = 0;
  push_key(POS_KEY_LEFT, 0);
  push_key(POS_KEY_UP, 0); /* clamp saturation and brightness at the edges */
  push_key('d', POS_MOD_CTRL);
  ui_events[ui_count++] = (pos_event_t){'s', 0, false};
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Bounds", &rgb) && rgb == 0xffffff);
  ui_next = ui_count = 0;
  push_text("wasdWASD");
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Letters do not navigate", &rgb) && rgb == 0xffffff);
  ui_next = ui_count = 0;
  push_text("wasdWASD");
  push_key(POS_KEY_DOWN, 0);
  push_key(POS_KEY_ENTER, 0);
  const char *items[] = {"First", "Second", "Third"};
  assert(pos_ui_menu("Arrow navigation", items, 3) == 1);
  pos_view_t color_view;
  pos_color_picker_view("Color", 0xabcdef, 0, 500, 500, &color_view);
  assert(pos_view_pixel(&color_view, 8, 28) == 0xffff);
  assert(pos_view_pixel(&color_view, 167, 28) == 0xf800);
  assert(pos_view_pixel(&color_view, 8, 99) == 0);
  assert(pos_view_pixel(&color_view, 90, 64) == 0xffff); /* cursor ring */
  assert(pos_view_pixel(&color_view, 91, 64) == 0);      /* cursor contrast */
  assert(pos_view_pixel(&color_view, 200, 40) == pos_color_rgb565(0xabcdef));
  assert(pos_color_hsv_rgb(0, 1000, 1000) == 0xff0000);
  assert(pos_color_hsv_rgb(60, 1000, 1000) == 0xffff00);
  assert(pos_color_hsv_rgb(120, 1000, 1000) == 0x00ff00);
  assert(pos_color_hsv_rgb(180, 1000, 1000) == 0x00ffff);
  assert(pos_color_hsv_rgb(240, 1000, 1000) == 0x0000ff);
  assert(pos_color_hsv_rgb(300, 1000, 1000) == 0xff00ff);
  assert(pos_color_parse("#aB12ef", &rgb) && rgb == 0xab12ef);
  assert(!pos_color_parse("#GG0000", &rgb) &&
         !pos_color_parse("FFFFFFF", &rgb));
  puts("{\"arrow_picker\":true,\"fine_adjustment\":true,\"cancel_preserves_"
       "color\":true}");
  return 0;
}
static int launcher_tests(const char *root) {
  assert(POS_LAUNCHER_APP_COUNT == 17);
  pos_event_t event = {POS_KEY_UP, 0, true};
  assert(pos_launcher_action(&event) == POS_KEY_ENTER);
  event.key = POS_KEY_ENTER;
  assert(pos_launcher_action(&event) == POS_KEY_ENTER);
  event.key = POS_KEY_LEFT;
  assert(pos_launcher_action(&event) == POS_KEY_LEFT);
  event.key = POS_KEY_RIGHT;
  assert(pos_launcher_action(&event) == POS_KEY_RIGHT);
  for (const char *letter = "wasdWASD"; *letter; ++letter) {
    event.key = *letter;
    assert(!pos_launcher_action(&event));
    assert(pos_navigation(&event) == *letter);
  }
  event.key = POS_KEY_DOWN;
  assert(!pos_launcher_action(&event));
  event.key = POS_KEY_UP;
  event.pressed = false;
  assert(!pos_launcher_action(&event) && !pos_navigation(&event));
  event.pressed = true;
  event.modifiers = POS_MOD_CTRL;
  assert(!pos_launcher_action(&event));
  pos_view_t view;
  pos_background_t background;
  pos_background_defaults(&background);
  for (unsigned count = 1; count <= 12; ++count) {
    assert(pos_background_resize(&background, count));
    for (unsigned i = 0; i < count; ++i)
      background.colors[i] = (i + 1) * 0x112233;
    for (int app = 0; app < POS_LAUNCHER_APP_COUNT; ++app) {
      pos_launcher_view(root, "PANPUTER", &background, app, &view);
      assert(view.selected_tile == 1 &&
             strstr(view.lines[12], pos_launcher_names[app]));
      for (int y = 8; y < 112; ++y)
        assert(pos_view_pixel(&view, 0, y) ==
               pos_color_rgb565(background.colors[y * count / 135]));
      /* Icons can only contribute black ink or a white outline. */
      unsigned black = 0, white = 0;
      for (int y = 47; y <= 80; ++y)
        for (int x = 23; x <= 56; ++x) {
          uint16_t pixel = pos_view_pixel(&view, x, y);
          if (pixel == 0)
            ++black;
          else if (pixel == 0xffff)
            ++white;
          else
            assert(pixel ==
                   pos_color_rgb565(background.colors[y * count / 135]));
        }
      assert(black && white);
    }
  }
  /* Selection rebasing must not jump the visible ribbon, including wraparound.
   */
  for (int selected = 0; selected < POS_LAUNCHER_APP_COUNT; ++selected) {
    pos_view_t previous, next;
    pos_launcher_view(root, "PANPUTER", &background, selected, &previous);
    for (int direction = -1; direction <= 1; direction += 2) {
      int target = (selected + direction + POS_LAUNCHER_APP_COUNT) %
                   POS_LAUNCHER_APP_COUNT;
      pos_launcher_view(root, "PANPUTER", &background, target, &next);
      next.ribbon_offset = direction * 80;
      for (int y = 38; y < 90; ++y)
        for (int x = 0; x < 240; ++x)
          assert(pos_view_pixel(&previous, x, y) ==
                 pos_view_pixel(&next, x, y));
    }
  }
  assert(!pos_background_resize(&background, 0) &&
         !pos_background_resize(&background, 13));
  puts("{\"launcher_controls\":true,\"all_stripe_counts\":true,\"outlined_"
       "black_icons\":true}");
  return 0;
}
static const char *cancel_phase;
static uint64_t cancel_after;
static int recovery_progress(void *opaque, const char *phase, const char *name,
                             uint64_t done, uint64_t total) {
  (void)name;
  (void)total;
  context_t *ctx = opaque;
  if (ctx->interrupt && !strcmp(ctx->interrupt, phase))
    _exit(99);
  return cancel_phase && !strcmp(cancel_phase, phase) && done >= cancel_after;
}

static int initialized, foregrounded, handled, saved, exited, allocations;
static bool fail_save;
typedef struct {
  void *resource;
} test_app_state_t;
static int app_initialize(void *opaque, void *services) {
  test_app_state_t *s = opaque;
  assert(!allocations); /* Prior app must release resources before
                           initialization. */
  s->resource = malloc(128);
  if (!s->resource)
    return -1;
  ++allocations;
  ++initialized;
  return services ? -1 : 0;
}
static void app_foreground(void *opaque) {
  (void)opaque;
  ++foregrounded;
}
static void app_event(void *opaque, const pos_event_t *e) {
  (void)opaque;
  assert(e->key == 'w');
  ++handled;
}
static void app_render(void *opaque, pos_view_t *view) {
  (void)opaque;
  pos_view_line(view, 4, "hello");
}
static int app_save(void *opaque) {
  (void)opaque;
  ++saved;
  return fail_save ? -1 : 0;
}
static void app_exit(void *opaque) {
  test_app_state_t *s = opaque;
  if (s->resource) {
    free(s->resource);
    --allocations;
  }
  ++exited;
}
static int core_tests(void) {
  pos_keyboard_t keyboard = {0};
  pos_event_t event;
  assert(pos_keyboard_decode(&keyboard, 0x8c, &event) && event.key == 'w' &&
         event.pressed);
  assert(pos_navigation(&event) == 'w' && event.key == 'w');
  assert(pos_keyboard_decode(&keyboard, 0x0c, &event) && !event.pressed);
  assert(!pos_keyboard_decode(&keyboard, 0x87, &event)); /* shift */
  assert(pos_keyboard_decode(&keyboard, 0x8c, &event) && event.key == 'W');
  assert(pos_keyboard_decode(&keyboard, 0x0c, &event));
  assert(!pos_keyboard_decode(&keyboard, 0x07, &event));
  assert(!pos_keyboard_decode(&keyboard, 0x83, &event)); /* Fn */
  const uint8_t scans[] = {0xb9, 0xb6, 0xba, 0xc0};
  const int arrows[] = {POS_KEY_UP, POS_KEY_LEFT, POS_KEY_DOWN, POS_KEY_RIGHT};
  for (unsigned i = 0; i < 4; ++i) {
    assert(pos_keyboard_decode(&keyboard, scans[i], &event));
    assert(event.key == arrows[i] && pos_navigation(&event) == arrows[i]);
    assert(event.modifiers & POS_MOD_FN);
    assert(pos_keyboard_decode(&keyboard, scans[i] & 0x7f, &event));
    assert(!pos_navigation(&event));
  }
  assert(pos_keyboard_decode(&keyboard, 0xc4, &event) &&
         event.key == POS_KEY_HOME);
  assert(!pos_keyboard_decode(&keyboard, 0xc4,
                              &event)); /* duplicate does not repeat Home */
  assert(!pos_keyboard_decode(&keyboard, 0, &event));
  assert(!pos_keyboard_decode(&keyboard, 0xff, &event));
  assert(pos_keyboard_decode(&keyboard, 0x81, &event) &&
         event.key == POS_KEY_ESCAPE);
  const pos_app_t app = {.name = "test",
                         .state_size = sizeof(test_app_state_t),
                         .initialize = app_initialize,
                         .foreground = app_foreground,
                         .event = app_event,
                         .render = app_render,
                         .save = app_save,
                         .exit = app_exit};
  pos_core_t core = {0};
  pos_view_t view;
  for (int i = 0; i < 100; ++i) {
    assert(!pos_core_open(&core, &app));
    assert(core.state);
    event = (pos_event_t){'w', 0, true};
    assert(!pos_core_event(&core, &event));
    pos_core_tick(&core, 1, &view);
    assert(!strcmp(view.lines[4], "hello"));
    event = (pos_event_t){POS_KEY_HOME, 0, true};
    assert(!pos_core_event(&core, &event));
    assert(!core.app && !core.state && !allocations);
  }
  core.services = &core;
  assert(pos_core_open(&core, &app) == -1);
  assert(!allocations && !core.app);
  assert(initialized == 101 && foregrounded == 100 && handled == 100 &&
         saved == 100 && exited == 101);
  core.services = NULL;
  assert(!pos_core_open(&core, &app));
  int init_before = initialized, exits_before = exited, saves_before = saved;
  assert(!pos_core_open(&core, &app));
  assert(initialized == init_before + 1 && exited == exits_before + 1 &&
         saved == saves_before + 1 && allocations == 1);
  core.services =
      &core; /* Failed replacement must clean up both old and new. */
  assert(pos_core_open(&core, &app) == -1);
  assert(!allocations && !core.app && !core.state);
  core.services = NULL;
  assert(!pos_core_open(&core, &app));
  init_before = initialized;
  fail_save = true;
  assert(pos_core_open(&core, &app) == -1);
  assert(initialized == init_before && !allocations && !core.app &&
         !core.state);
  fail_save = false;
  const uint32_t rgb[] = {0xff218c, 0xffd800, 0x21b1ff};
  assert(POS_BOOT_COLOR_COUNT == 3);
  for (unsigned i = 0; i < POS_BOOT_COLOR_COUNT; ++i) {
    uint16_t expected = ((rgb[i] >> 19) << 11) | (((rgb[i] >> 10) & 63) << 5) |
                        ((rgb[i] >> 3) & 31);
    assert(pos_boot_colors[i] == expected);
    pos_view_clear(&view, "Text must not cover solid splash");
    view.solid_color = true;
    view.color_rgb565 = pos_boot_colors[i];
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x)
        assert(pos_view_pixel(&view, x, y) == pos_boot_colors[i]);
  }
  pos_view_clear(&view, "title");
  pos_view_line(&view, -1, "bad");
  pos_view_line(&view, POS_ROWS, "bad");
  pos_view_line(&view, 1, "%0500d", 1);
  assert(strlen(view.lines[1]) == POS_COLS);
  puts("{\"lifecycle_cycles\":100,\"resources_remaining\":0,\"global_home_"
       "intercepted\":true}");
  return 0;
}

static void map_controls(const char *root, bool world) {
  fake_expansion.gps_detected = fake_expansion.maps_available =
      fake_expansion.location.fix = true;
  double lat = 34.0505, lon = -118.2475;
  fake_expansion.location.latitude = lat;
  fake_expansion.location.longitude = lon;
  map_controls_record = true;
  if (!world) {
    for (unsigned i = 0; i < 4; ++i)
      push_key(POS_KEY_UP, 0);
    choose(0); /* Saved regions */
  }
  choose(0); /* World or first saved region */
  idle_event();
  const int keys[] = {
      POS_KEY_RIGHT, 'p', POS_KEY_RIGHT, 'P', POS_KEY_LEFT, POS_KEY_UP,
      POS_KEY_DOWN,  '+', POS_KEY_RIGHT, '=', '-'};
  for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
    push_key(keys[i], 0);
    idle_event();
  }
  if (world) {
    push_key('0', 0);
    idle_event();
  }
  push_key('c', 0);
  idle_event();
  push_key('p', 0); /* Wrap Fine back to Normal. */
  idle_event();
  push_key(POS_KEY_HOME, 0);
  pos_ui_maps(root);
  assert(ui_next == ui_count && pos_home_pending() && !ui_last.map_pixels);
  assert(map_frame_count == (world ? 15u : 14u));
  double normal = world ? 30 : 250;
  double factor =
      world ? 1 : 1 / (111195.08 * cos(lat * 3.14159265358979323846 / 180));
  double lat_step = world ? 7.5 : 62.5 / 111195.08;
  const double dx[] = {0, 1, 1, 3, 3, 2.75, 2.75, 2.75, 2.75, 2.875};
  for (unsigned i = 0; i < 10; ++i) {
    assert(fabs(map_frames[i].lon - (lon + normal * factor * dx[i])) < .00006);
    assert(fabs(map_frames[i].lat - (lat + (i == 6 ? lat_step : 0))) < .00006);
  }
  double expected[] = {normal,     normal,     normal * 2,  normal * 2,
                       normal / 4, normal / 4, normal / 4,  normal / 4,
                       normal / 8, normal / 8, normal / 16, normal / 8};
  for (unsigned i = 0; i < 12; ++i)
    assert(fabs(map_frames[i].step - expected[i]) < .051);
  assert(map_frames[1].generation == map_frames[2].generation);
  assert(map_frames[3].generation == map_frames[4].generation);
  unsigned last = map_frame_count - 1;
  assert(fabs(map_frames[last].lat - lat) < .00006);
  assert(fabs(map_frames[last].lon - lon) < .00006);
  assert(fabs(map_frames[last].step - (world ? normal : normal / 2)) < .051);
  assert(map_frames[last].generation == map_frames[last - 1].generation);
}
static int apps_ui(const char *root, const char *mode) {
  if (!strncmp(mode, "maps-ui-delete", 14)) {
    bool cancel = strstr(mode, "cancel") != NULL;
    bool blocked = strstr(mode, "protected") != NULL;
    fake_expansion.gps_detected = fake_expansion.maps_available = true;
    for (unsigned i = 0; i < 4; ++i)
      push_key(POS_KEY_UP, 0);
    choose(0); /* Saved regions */
    choose(0); /* First region */
    idle_event();
    push_key(POS_KEY_ENTER, 0);
    choose(9); /* Delete region */
    if (!blocked)
      choose(cancel ? 0 : 1);
    idle_event();
    if (!cancel)
      push_key(POS_KEY_ENTER, 0); /* Completion/failure notice */
    idle_event();
    push_key(POS_KEY_HOME, 0);
    pos_ui_maps(root);
    assert(ui_next == ui_count && pos_home_pending() && !ui_last.map_pixels &&
           !ui_last.map_route_pixels);
    return 0;
  }
  if (!strcmp(mode, "maps-ui-world-controls") ||
      !strcmp(mode, "maps-ui-regional-controls")) {
    map_controls(root, !strcmp(mode, "maps-ui-world-controls"));
    return 0;
  }
  if (!strcmp(mode, "maps-ui-missing")) {
    push_key(POS_KEY_ENTER, 0);
    pos_ui_maps(root);
    assert(!strcmp(ui_last.lines[0], "Maps need the GPS cap"));
    assert(ui_next == ui_count);
    return 0;
  }
  if (!strcmp(mode, "maps-ui-world")) {
    fake_expansion.gps_detected = fake_expansion.maps_available =
        fake_expansion.location.fix = true;
    fake_expansion.location.latitude = 34.05;
    fake_expansion.location.longitude = -118.248;
    push_key(POS_KEY_ENTER, 0);
    idle_event();
    push_key('+', 0);
    idle_event();
    push_key('-', 0);
    idle_event();
    push_key('0', 0);
    idle_event();
    push_key('c', 0);
    idle_event();
    push_key(POS_KEY_HOME, 0);
    pos_ui_maps(root);
    assert(pos_home_pending() && ui_next == ui_count && !ui_last.map_pixels);
    return 0;
  }
  if (!strcmp(mode, "maps-ui-no-fix")) {
    fake_expansion.gps_detected = fake_expansion.maps_available = true;
    for (unsigned i = 0; i < 3; ++i)
      push_key(POS_KEY_UP, 0);
    push_key(POS_KEY_ENTER, 0); /* Download */
    push_key(POS_KEY_ENTER, 0); /* Around GPS */
    push_key(POS_KEY_ENTER, 0); /* No fix notice */
    push_key(POS_KEY_ESCAPE, 0);
    pos_ui_maps(root);
    assert(ui_next == ui_count);
    assert(!strcmp(ui_last.lines[0], "Maps"));
    return 0;
  }
  if (!strcmp(mode, "maps-ui-download")) {
    maps_download_test = true;
    fake_network.connected = true;
    fake_expansion.gps_detected = fake_expansion.maps_available =
        fake_expansion.location.fix = true;
    fake_expansion.location.latitude = 34.0505;
    fake_expansion.location.longitude = -118.2475;
    for (unsigned i = 0; i < 3; ++i)
      push_key(POS_KEY_UP, 0);
    push_key(POS_KEY_ENTER, 0);
    choose(0);
    choose(0);
    push_key(POS_KEY_ENTER, 0);
    choose(0);
    push_key(POS_KEY_ESCAPE, 0); /* Close review */
    choose(0);
    idle_event(); /* Download and compile complete before later keys */
    push_key(POS_KEY_ESCAPE, 0);
    push_key(POS_KEY_HOME, 0);
    pos_ui_maps(root);
    assert(ui_next == ui_count && pos_home_pending());
    return 0;
  }
  if (!strcmp(mode, "maps-ui-route") || !strcmp(mode, "maps-ui-search") ||
      !strcmp(mode, "maps-ui-route-delete")) {
    bool search = !strcmp(mode, "maps-ui-search");
    bool deleting = !strcmp(mode, "maps-ui-route-delete");
    fake_expansion.gps_detected = fake_expansion.maps_available =
        fake_expansion.location.fix = true;
    fake_expansion.location.latitude = 34.05;
    fake_expansion.location.longitude = -118.248;
    char folder[POS_FS_PATH], raw[POS_FS_PATH], map[POS_FS_PATH];
    assert(!pos_fs_join(folder, root, "POS/Maps") && !pos_fs_mkdirs(folder));
    assert(!pos_fs_join(raw, root, "POS/Maps/local.osm") &&
           !pos_fs_join(map, root, "POS/Maps/local.pmap"));
    const char *xml =
        "<osm version=\"0.6\"><way id=\"10\"><nd ref=\"1\" lat=\"34.05\" "
        "lon=\"-118.248\"/><nd ref=\"2\" lat=\"34.05\" lon=\"-118.247\"/><tag "
        "k=\"highway\" v=\"residential\"/><tag k=\"name\" "
        "v=\"First\"/></way><way id=\"20\"><nd ref=\"2\" lat=\"34.05\" "
        "lon=\"-118.247\"/><nd ref=\"3\" lat=\"34.051\" lon=\"-118.247\"/><tag "
        "k=\"highway\" v=\"residential\"/><tag k=\"name\" "
        "v=\"North\"/></way></osm>";
    assert(!pos_fs_write_new(raw, xml, strlen(xml)));
    pos_map_info_t m = {
        .latitude = 34.051, .longitude = -118.247, .width_m = 1000};
    strcpy(m.name, "Local");
    pos_error_t e = {0};
    assert(!pos_map_compile(raw, map, &m, NULL, NULL, &e));
    assert(!pos_nav_compile(raw, map, NULL, NULL, &e));
    for (unsigned i = 0; i < 4; ++i)
      push_key(POS_KEY_UP, 0);
    choose(0);
    choose(0);
    idle_event();
    push_key(POS_KEY_ENTER, 0);
    choose(search ? 1 : 0);
    if (search) {
      push_text("North");
      push_key(POS_KEY_ENTER, 0);
      idle_event();
      choose(0);
      choose(1);
      idle_event();
    } else {
      push_key(POS_KEY_ESCAPE, 0);
      choose(0);
      idle_event();
      push_key(POS_KEY_ENTER, 0);
      choose(2);
      idle_event();
    }
    if (deleting) {
      push_key(POS_KEY_ENTER, 0);
      choose(9);
      choose(1);
      idle_event();
      push_key(POS_KEY_ENTER, 0);
      idle_event();
    }
    push_key(POS_KEY_HOME, 0);
    pos_ui_maps(root);
    assert(ui_next == ui_count && pos_home_pending() && !ui_last.map_pixels &&
           !ui_last.map_route_pixels);
    assert(search ? maps_guidance_seen : maps_turn_seen);
    return 0;
  }
  if (!strcmp(mode, "maps-ui-open")) {
    fake_expansion.gps_detected = fake_expansion.maps_available =
        fake_expansion.location.fix = true;
    fake_expansion.location.latitude = 34.0505;
    fake_expansion.location.longitude = -118.2475;
    for (unsigned i = 0; i < 4; ++i)
      push_key(POS_KEY_UP, 0);
    push_key(POS_KEY_ENTER, 0); /* Saved regions */
    push_key(POS_KEY_ENTER, 0); /* Local */
    idle_event(); /* Raster progress cannot consume later navigation. */
    push_key(POS_KEY_RIGHT, 0);
    idle_event();
    push_key(POS_KEY_ENTER, 0);
    choose(6); /* Straight-line guide to panned crosshair */
    idle_event();
    push_key('c', 0);
    idle_event(); /* Follow live GPS */
    push_key(POS_KEY_HOME, 0);
    pos_ui_maps(root);
    assert(ui_next == ui_count && pos_home_pending());
    assert(!ui_last.map_pixels &&
           maps_guidance_seen); /* Drop borrowed memory before freeing. */
    return 0;
  }
  if (!strcmp(mode, "hardware-ui-missing")) {
    fake_expansion.frequency_hz = 915000000;
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_HOME, 0);
    pos_ui_expansion(root);
    assert(hardware_missing_seen && !fake_expansion.lora_enabled &&
           !hardware_changes && pos_home_pending());
    return 0;
  }
  if (!strcmp(mode, "hardware-ui-toggle")) {
    fake_expansion.cap_detected = true;
    fake_expansion.gps_detected = true;
    fake_expansion.maps_available = true;
    fake_expansion.frequency_hz = 915000000;
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_HOME, 0);
    pos_ui_expansion(root);
    assert(hardware_changes == 2 && !fake_expansion.lora_enabled &&
           !fake_expansion.tx_enabled && fake_expansion.gps_detected &&
           fake_expansion.maps_available && maps_ready_seen);
    return 0;
  }
  if (!strcmp(mode, "audio-ui-file-eof")) {
    pos_ui_music_file(root, "Music/tone.wav");
    assert(audio_writes && !audio_active && audio_stops && !pos_home_pending());
    assert(!strcmp(ui_last.lines[0], "Audio Player"));
    return 0;
  }
  if (!strcmp(mode, "recorder-ui-gain") ||
      !strcmp(mode, "recorder-ui-custom")) {
    bool custom = !strcmp(mode, "recorder-ui-custom");
    unsigned expected = custom ? 1537 : 64;
    pos_settings_t settings;
    pos_settings_defaults(&settings);
    pos_ui_set_theme(&settings.background);
    choose(1); /* Numeric gain, preserving punctuation as text input. */
    push_key(21, 0);
    if (custom) {
      push_text("4097");
      push_key(POS_KEY_ENTER, 0);
      push_key(POS_KEY_ENTER, 0); /* Invalid gain notice */
      push_key(21, 0);
    }
    push_text(custom ? "1537" : "32");
    push_key(POS_KEY_ENTER, 0); /* 32x */
    push_key(POS_KEY_ENTER, 0); /* Start */
    idle_event();
    if (!custom)
      push_key('/', 0);
    idle_event(); /* Live 64x, or retain arbitrary numeric gain. */
    push_key(POS_KEY_HOME, 0);
    pos_ui_recorder(root, &settings);
    assert(settings.microphone_gain == expected && fake_gain == expected);
    pos_settings_t loaded;
    assert(!pos_settings_load(root, &loaded));
    assert(loaded.microphone_gain == expected && loaded.brightness == 70);
    assert(pos_home_pending() && microphone_stops);
    return 0;
  }
  if (!strcmp(mode, "recorder-ui-drain")) {
    microphone_backlog = 32;
    push_key(POS_KEY_ENTER, 0);
    idle_event();
    push_key(POS_KEY_HOME, 0);
    pos_settings_t settings;
    pos_settings_defaults(&settings);
    pos_ui_recorder(root, &settings);
    assert(!microphone_backlog && microphone_reads == 32);
    return 0;
  }
  if (!strcmp(mode, "recorder-ui-home") || !strcmp(mode, "recorder-ui-tail") ||
      !strcmp(mode, "recorder-ui-silent")) {
    microphone_test_root = root;
    microphone_silent = !strcmp(mode, "recorder-ui-silent");
    microphone_tail_enabled = !strcmp(mode, "recorder-ui-tail");
    push_key(POS_KEY_ENTER, 0);
    unsigned reads = microphone_silent ? 200 : 1;
    for (unsigned i = 0; i < reads; ++i)
      idle_event();
    push_key(POS_KEY_HOME, 0);
    pos_settings_t recorder_settings;
    pos_settings_defaults(&recorder_settings);
    pos_ui_recorder(root, &recorder_settings);
    assert(microphone_starts == 1 && microphone_reads == reads &&
           microphone_stops && pos_home_pending());
    char file[POS_FS_PATH];
    assert(!pos_fs_join(file, root, "Recordings/Recording-00001.wav"));
    struct stat st;
    assert(!stat(file, &st) &&
           st.st_size ==
               44 + reads * 512 + (microphone_tail_enabled ? 256 : 0));
    assert(microphone_silent ? mic_silence_seen : mic_quiet_meter_seen);
    return 0;
  }
  if (!strncmp(mode, "music-ui-", 9)) {
    advance_after_audio = true;
    idle_event();
    if (!strcmp(mode, "music-ui-pause")) {
      push_key(' ', 0);
      push_key(' ', 0);
    }
    push_key(POS_KEY_HOME, 0);
    pos_ui_music_file(root, "Music/tone.wav");
    assert(audio_writes && !audio_active && audio_stops && pos_home_pending());
    if (strstr(mode, "pause"))
      assert(audio_writes >= 2 && audio_stops >= 2);
    return 0;
  }
  if (!strcmp(mode, "files-ui-icons")) {
    for (unsigned i = 0; i < 15; ++i)
      push_key(POS_KEY_DOWN, 0);
    push_key(POS_KEY_HOME, 0);
    pos_ui_files(root);
    assert(folder_hint_seen == 31 && pos_home_pending());
    return 0;
  }
  if (!strncmp(mode, "files-ui-", 9)) {
    char folder[POS_FS_PATH];
    pos_fs_join(folder, root, "POS/Users/Owner");
    DIR *directory = opendir(folder);
    assert(directory);
    struct dirent *entry;
    unsigned at = 4, file_index = 4, directory_index = 4;
    while ((entry = readdir(directory)))
      if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
        if (!strcmp(entry->d_name, "a.txt"))
          file_index = at;
        if (!strcmp(entry->d_name, "destination"))
          directory_index = at;
        ++at;
      }
    closedir(directory);
    keys_down(file_index);
    push_key(POS_KEY_ENTER, 0);
    if (!strcmp(mode, "files-ui-delete-cancel")) {
      keys_down(6);
      push_key(POS_KEY_ENTER, 0);
      push_text("NO");
      push_key(POS_KEY_ENTER, 0);
      push_key(POS_KEY_HOME, 0);
      pos_ui_files(root);
      return 0;
    }
    keys_down(strstr(mode, "cut") ? 2 : 1);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    keys_down(directory_index);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    keys_down(3);
    push_key(POS_KEY_ENTER, 0);
    keys_down(1);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0); /* Accept default paste filename. */
    idle_event();
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_HOME, 0);
    pos_ui_files(root);
    assert(pos_home_pending());
    return 0;
  }
  if (!strcmp(mode, "ble-ui-receive")) {
    push_key(POS_KEY_ENTER, 0);
    keys_down(1);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    keys_down(1);
    push_key(POS_KEY_ENTER, 0);
    push_key(POS_KEY_ENTER, 0);
    pos_ui_ble(root, NULL, true);
    assert(!ble_active && ble_accepts == 1 && ble_pairings == 1);
    return 0;
  }
  if (!strcmp(mode, "ble-ui-home")) {
    push_key(POS_KEY_HOME, 0);
    pos_ui_ble(root, NULL, true);
    assert(!ble_active && !ble_accepts && !ble_pairings && pos_home_pending());
    return 0;
  }
  return 2;
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  if (!strncmp(argv[2], "mesh-ui-", 8)) {
    if (!strcmp(argv[2],"mesh-ui-setup")) {
      choose(0); choose(0); push_key(POS_KEY_ENTER,0); push_key(POS_KEY_HOME,0);
      pos_ui_mesh_settings(argv[1]);
      assert(fake_mesh.enabled && mesh_changes==1 && !fake_expansion.tx_enabled);
    } else {
      fake_mesh.enabled=fake_mesh.ready=true;
      fake_expansion.cap_detected=fake_expansion.lora_enabled=true;
      fake_expansion.radio_ready=fake_expansion.tx_enabled=true;
      fake_expansion.location.fix=true;
      if (!strcmp(argv[2],"mesh-ui-send")) {
        choose(1);choose(0);push_text("hello");push_key(POS_KEY_ENTER,0);
        push_key(POS_KEY_ENTER,0);push_key(POS_KEY_HOME,0);
        pos_ui_radio(argv[1]);assert(mesh_sends==1 && !mesh_locations);
      } else {
        choose(5);choose(0);push_key(POS_KEY_ENTER,0);choose(0);
        push_key(POS_KEY_HOME,0);pos_ui_meshtastic(argv[1]);
        assert(!mesh_sends && !mesh_locations);
      }
    }
    assert(ui_next==ui_count && pos_home_pending());return 0;
  }

  if(!strcmp(argv[2],"chat-read")) {
    assert(argc==6);pos_chat_message_t messages[16];unsigned count,unread;uint32_t end;
    uint32_t peer=(uint32_t)strtoul(argv[3],NULL,16);unsigned ch=strtoul(argv[4],NULL,10);
    assert(!pos_chat_read(argv[1],0x12345678,peer,ch,messages,&count,&end,&unread));
    if(atoi(argv[5])){pos_chat_contact_t contact;assert(!pos_chat_contact(argv[1],peer,ch,&contact,false));contact.read_offset=end;strcpy(contact.alias,"Friend");strcpy(contact.quick,"Hello");contact.favourite=true;assert(!pos_chat_contact(argv[1],peer,ch,&contact,true));}
    cJSON *j=cJSON_CreateObject(),*rows=cJSON_AddArrayToObject(j,"messages");cJSON_AddNumberToObject(j,"unread",unread);cJSON_AddNumberToObject(j,"end",end);
    for(unsigned i=0;i<count;++i){cJSON *m=cJSON_CreateObject();cJSON_AddStringToObject(m,"text",messages[i].text);cJSON_AddStringToObject(m,"status",messages[i].status);cJSON_AddBoolToObject(m,"incoming",messages[i].incoming);cJSON_AddItemToArray(rows,m);}
    char *text=cJSON_PrintUnformatted(j);puts(text);free(text);cJSON_Delete(j);return 0;
  }
  if(!strcmp(argv[2],"flags")) {
    pos_background_t bg;pos_view_t view;
    for(unsigned i=0;i<POS_BACKGROUND_PRESET_COUNT;++i){assert(pos_background_preset(&bg,(pos_background_preset_t)i)&&pos_background_valid(&bg));pos_view_clear(&view,"Boot");view.background=bg;
      for(int x=0;x<240;++x)assert(pos_view_pixel(&view,x,134)==pos_color_rgb565(bg.colors[x*bg.count/240]));
    }assert(!pos_background_preset(&bg,POS_BACKGROUND_PRESET_COUNT));return 0;
  }
  if(!strcmp(argv[2],"hex-ui")) {
    assert(!pos_fs_layout(argv[1]));pos_settings_t settings;pos_settings_defaults(&settings);
    push_key('h',0);for(unsigned i=0;i<7;++i)push_key(POS_KEY_BACKSPACE,0);push_text("#Ab12F0");push_key(POS_KEY_ENTER,0);push_key('s',0);
    assert(pos_ui_background(argv[1],&settings)&&settings.background.colors[0]==0xab12f0&&ui_next==ui_count);return 0;
  }
  if(!strcmp(argv[2],"worksheet-open")) {
    assert(argc==4);assert(!pos_fs_layout(argv[1]));push_key(POS_KEY_ENTER,0);push_key(POS_KEY_ESCAPE,0);pos_ui_calculator_open(argv[1],argv[3]);assert(!strcmp(calculator_answer,"21"));assert(ui_next==ui_count);return 0;
  }
  if (!strncmp(argv[2], "calculator-", 11)) {
    const char *mode=argv[2];
    if (!strcmp(mode,"calculator-corrupt")) push_key(POS_KEY_ENTER,0);
    if (!strcmp(mode,"calculator-buttons")) {
      push_key(POS_KEY_UP,0);push_key(POS_KEY_ENTER,0);
      push_key('+',0);push_key(POS_KEY_UP,0);push_key(POS_KEY_ENTER,0);push_key(POS_KEY_DOWN,0);push_key(POS_KEY_ENTER,0);
    } else {
      const char *text=!strcmp(mode,"calculator-punctuation")?"mean(.5,1.5)/2":!strcmp(mode,"calculator-read")?"a/4":!strcmp(mode,"calculator-cas")?"x^2-5*x+6=0":"a=12";
      for(;*text;++text)push_key(*text,0);
      if (!strcmp(mode,"calculator-cas")) {
        push_key('\t',0);keys_down(3);push_key(POS_KEY_ENTER,0);push_key(POS_KEY_ENTER,0);
      }else push_key(POS_KEY_ENTER,0);
    }
    push_key(POS_KEY_ESCAPE,0);
    pos_ui_calculator(argv[1]);
    if(!strcmp(mode,"calculator-cas"))assert(!strcmp(calculator_answer,"x = 3 or 2"));
    else if(!strcmp(mode,"calculator-punctuation"))assert(!strcmp(calculator_answer,"1/2 = 0.5"));
    else if(!strcmp(mode,"calculator-read"))assert(!strcmp(calculator_answer,"3"));
    else if(!strcmp(mode,"calculator-buttons"))assert(!strcmp(calculator_answer,"6"));
    else assert(!strcmp(calculator_answer,"12"));
    assert(ui_next==ui_count);
    return 0;
  }
  if (strstr(argv[2], "-ui-") && strncmp(argv[2], "editor-ui-", 10) &&
      strncmp(argv[2], "wifi-ui-", 8) && strncmp(argv[2], "settings-ui-", 12))
    return apps_ui(argv[1], argv[2]);
  if (!strcmp(argv[2], "install") || !strcmp(argv[2], "boot") ||
      !strcmp(argv[2], "validate"))
    return pos_installer_runner_main(argc, argv);
  if (!strcmp(argv[2], "console")) {
    int descriptors[2];
    assert(!pipe(descriptors));
    assert(!pos_console_nonblocking(descriptors[0]));
    assert(pos_console_poll(descriptors[0]) == -1);
    assert(write(descriptors[1], "w", 1) == 1);
    assert(pos_console_poll(descriptors[0]) == 'w');
    assert(pos_console_poll(descriptors[0]) == -1);
    const char *sequences[] = {"\033[A", "\033[B", "\033[C", "\033[D",
                               "\033OA", "\033OB", "\033OC", "\033OD"};
    const int arrows[] = {POS_KEY_UP, POS_KEY_DOWN, POS_KEY_RIGHT,
                          POS_KEY_LEFT};
    for (unsigned i = 0; i < 8; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
        assert(write(descriptors[1], sequences[i] + j, 1) == 1);
        assert(pos_console_poll(descriptors[0]) ==
               (j == 2 ? arrows[i % 4] : -1));
      }
    }
    assert(write(descriptors[1], "\033[3~wasdWASD", 12) == 12);
    for (const char *letter = "wasdWASD"; *letter; ++letter)
      assert(pos_console_poll(descriptors[0]) == *letter);
    assert(write(descriptors[1], "\033x", 2) == 2);
    assert(pos_console_poll(descriptors[0]) == 27);
    assert(pos_console_poll(descriptors[0]) == 'x');
    assert(write(descriptors[1], "\033", 1) == 1);
    assert(pos_console_poll(descriptors[0]) == -1);
    struct timespec delay = {.tv_nsec = 110000000};
    nanosleep(&delay, NULL);
    assert(pos_console_poll(descriptors[0]) == 27);
    assert(pos_console_poll(descriptors[0]) == -1);
    assert(write(descriptors[1], "\033[\035", 3) == 3);
    assert(pos_console_poll(descriptors[0]) == 29);
    assert(pos_console_poll(descriptors[0]) == -1);
    assert(pos_console_nonblocking(-1) == -1);
    close(descriptors[0]);
    close(descriptors[1]);
    puts("{\"nonblocking\":true}");
    return 0;
  }
  if (!strncmp(argv[2], "settings-ui-", 12))
    return settings_ui_tests(argv[1], argv[2]);
  if (!strncmp(argv[2], "editor-ui-", 10))
    return editor_ui_tests(argv[1], argv[2]);
  if (!strncmp(argv[2], "wifi-ui-", 8))
    return wifi_ui_tests(argv[1], argv[2]);
  if (!strcmp(argv[2], "picker"))
    return picker_tests();
  if (!strncmp(argv[2], "maps-ui-", 8))
    return apps_ui(argv[1], argv[2]);
  if (!strcmp(argv[2], "launcher"))
    return launcher_tests(argv[1]);
  if (!strncmp(argv[2], "background-", 11))
    return background_tests(argv[1], argv[2]);
  if (!strcmp(argv[2], "boot-splash"))
    return boot_splash_tests();
  if (!strcmp(argv[2], "text-qol"))
    return text_qol_tests();
  if (!strcmp(argv[2], "file-restore") && argc == 4) {
    pos_error_t error = {0};
    int result = pos_files_restore(argv[1], argv[3], &error);
    if (result)
      fprintf(stderr, "%s\n", error.message);
    return result ? 1 : 0;
  }
  if (!strcmp(argv[2], "keyboard-ui"))
    return keyboard_ui_tests(true);
  if (!strcmp(argv[2], "keyboard-bare"))
    return keyboard_ui_tests(false);
  if (!strcmp(argv[2], "keyboard-events")) {
    pos_keyboard_t keyboard = {0};
    for (int i = 3; i < argc; ++i) {
      pos_event_t e;
      if (pos_keyboard_decode(&keyboard, strtoul(argv[i], NULL, 0), &e))
        printf("%d %u %d\n", e.key, e.modifiers, e.pressed);
    }
    return 0;
  }
  if (!strncmp(argv[2], "setup-", 6))
    return setup_tests(argv[1], argv[2]);
  if (!strncmp(argv[2], "browse-", 7))
    return browser_tests(argv[1], argv[2]);
  if (!strcmp(argv[2], "phase-labels"))
    return phase_tests();
  if (!strcmp(argv[2], "core"))
    return core_tests();
  if (!strcmp(argv[2], "ui"))
    return ui_tests();
  context_t ctx = {.root = argv[1], .available = 1024ULL * 1024 * 1024};
  const char *archive = NULL, *name = NULL;
  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "--free") && i + 1 < argc)
      ctx.available = strtoull(argv[++i], NULL, 10);
    else if (!strcmp(argv[i], "--archive") && i + 1 < argc)
      archive = argv[++i];
    else if (!strcmp(argv[i], "--name") && i + 1 < argc)
      name = argv[++i];
    else if (!strcmp(argv[i], "--interrupt") && i + 1 < argc)
      ctx.interrupt = argv[++i];
    else if (!strcmp(argv[i], "--cancel") && i + 1 < argc)
      cancel_phase = argv[++i];
    else if (!strcmp(argv[i], "--file-limit") && i + 1 < argc) {
      rlim_t limit = (rlim_t)strtoull(argv[++i], NULL, 10);
      struct rlimit limits = {limit, limit};
      assert(!setrlimit(RLIMIT_FSIZE, &limits));
      signal(SIGXFSZ, SIG_IGN);
    } else if (!strcmp(argv[i], "--after") && i + 1 < argc)
      cancel_after = strtoull(argv[++i], NULL, 10);
    else
      return 2;
  }
  char backup[POS_FS_PATH];
  pos_fs_join(backup, ctx.root, POS_BACKUP_NAME);
  if (!archive) {
    pos_error_t ignored = {0};
    (void)pos_backup_find(ctx.root, backup, &ignored);
    archive = backup;
  }
  pos_error_t error = {0};
  pos_backup_info_t info = {0};
  int result = -1;
  if (!strcmp(argv[2], "scan"))
    result = pos_backup_scan(ctx.root, &info, recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "backup"))
    result = pos_backup_create(ctx.root, "0.2.0-dev", ctx.available, 0,
                               recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "resume-backup"))
    result = pos_backup_set_resume(ctx.root, "0.2.0-dev", ctx.available, 0,
                                   recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "backup-set"))
    result = pos_backup_set_create(ctx.root, "0.2.0-dev", ctx.available, 0,
                                   recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "verify"))
    result = pos_backup_verify(archive, &info, recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "restore"))
    result = pos_backup_restore(ctx.root, archive, ctx.available, &info,
                                recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "match"))
    result = pos_backup_matches(ctx.root, archive, &error);
  else if (!strcmp(argv[2], "layout"))
    result = pos_fs_layout(ctx.root);
  else if (!strncmp(argv[2], "settings-", 9)) {
    pos_settings_t settings;
    pos_settings_defaults(&settings);
    int loaded = pos_settings_load(ctx.root, &settings);
    if (!strcmp(argv[2], "settings-read"))
      result = loaded;
    else {
      if (name) {
        if (strlen(name) >= sizeof(settings.display_name))
          return 1;
        strcpy(settings.display_name, name);
      }
      settings.setup_complete = true;
      result = pos_settings_save(ctx.root, &settings);
    }
    if (!result) {
      cJSON *json = cJSON_CreateObject();
      cJSON_AddStringToObject(json, "name", settings.display_name);
      cJSON_AddNumberToObject(json, "generation", settings.generation);
      cJSON_AddNumberToObject(json, "brightness", settings.brightness);
      cJSON_AddNumberToObject(json, "microphone_gain",
                              settings.microphone_gain);
      cJSON_AddBoolToObject(json, "sounds_enabled", settings.sounds_enabled);
      cJSON_AddBoolToObject(json, "sounds_keys", settings.sounds_keys);
      cJSON_AddBoolToObject(json, "sounds_alerts", settings.sounds_alerts);
      cJSON_AddNumberToObject(json, "sounds_volume", settings.sounds_volume);
      cJSON *colors = cJSON_AddArrayToObject(json, "background");
      for (unsigned i = 0; i < settings.background.count; ++i)
        cJSON_AddItemToArray(colors,
                             cJSON_CreateNumber(settings.background.colors[i]));
      char *text = cJSON_PrintUnformatted(json);
      puts(text);
      free(text);
      cJSON_Delete(json);
    }
  } else if (!strcmp(argv[2], "repair") || !strcmp(argv[2], "reinstall")) {
    char path[POS_FS_PATH];
    pos_fs_join(path, ctx.root, "pos-install/manifest.pos");
    pos_package_t *p = NULL;
    result = pos_load_manifest(path, &p, &error);
    pos_platform_t platform = {.ctx = &ctx,
                               .free_bytes = free_space,
                               .check_firmware = check_image,
                               .write_firmware = write_image,
                               .select_firmware = select_image,
                               .report = progress};
    if (!result) {
      if (!strcmp(argv[2], "repair"))
        result = pos_repair_system(ctx.root, p, &platform, &error);
      else {
        result = pos_reinstall_prepare(ctx.root, p, &platform, &error);
        if (!result && ctx.interrupt && !strcmp(ctx.interrupt, "retired"))
          _exit(99);
        if (!result)
          result = pos_install(ctx.root, p, &platform, &error);
      }
    }
    pos_package_free(p);
  }
  if (result) {
    fprintf(stderr, "%s\n",
            *error.message ? error.message : "Operation failed safely");
    return 1;
  }
  if (!strcmp(argv[2], "scan") || !strcmp(argv[2], "verify") ||
      !strcmp(argv[2], "restore"))
    printf("{\"files\":%u,\"bytes\":%llu,\"required\":%llu,\"conflicts\":%u}\n",
           info.files, (unsigned long long)info.bytes,
           (unsigned long long)info.required, info.conflicts);
  return 0;
}

void pos_ui_deploy(const char *root) { (void)root; }
int pos_microphone_set_gain(unsigned gain) {
  fake_gain = gain;
  return 0;
}
int pos_microphone_start(pos_error_t *e) {
  (void)e;
  if (microphone_test_root) {
    char path[POS_FS_PATH];
    assert(!pos_fs_join(path, microphone_test_root,
                        "Recordings/Recording-00001.wav.part"));
    struct stat st;
    assert(!stat(path, &st) &&
           st.st_size == 44); /* Header synced before I2S. */
  }
  ++microphone_starts;
  microphone_ended = false;
  return 0;
}
int pos_microphone_read(int16_t *m, size_t capacity, size_t *n,
                        pos_error_t *e) {
  (void)e;
  if (microphone_ended) {
    *n = microphone_tail_frames < capacity ? microphone_tail_frames : capacity;
    for (size_t i = 0; i < *n; ++i)
      m[i] = 1234;
    microphone_tail_frames -= (unsigned)*n;
    return 0;
  }
  ++microphone_reads;
  if (microphone_backlog)
    --microphone_backlog;
  if (ui_next < ui_count && ui_events[ui_next].key == INT_MIN)
    ++ui_next;
  if (capacity > 256)
    capacity = 256;
  *n = capacity;
  for (size_t i = 0; i < capacity; ++i)
    m[i] = microphone_silent ? 0 : (int16_t)i;
  return 0;
}
void pos_microphone_levels(unsigned *left, unsigned *right) {
  *left = 0;
  *right = microphone_silent ? 0 : 32;
}
void pos_microphone_end(void) {
  microphone_ended = true;
  microphone_tail_frames = microphone_tail_enabled ? 128 : 0;
}
void pos_microphone_stop(void) { ++microphone_stops; }
int pos_motion_start(pos_error_t *e) {
  (void)e;
  return 0;
}
int pos_motion_read(pos_imu_sample_t *s, pos_error_t *e) {
  (void)e;
  memset(s, 0, sizeof(*s));
  return 0;
}
void pos_motion_stop(void) {}
int pos_infrared_send(uint8_t a, uint8_t c, pos_error_t *e) {
  (void)a;
  (void)c;
  (void)e;
  return 0;
}

int pos_audio_feedback_configure(bool enabled, unsigned volume, bool keys,
                                 bool alerts) {
  (void)enabled;
  (void)keys;
  (void)alerts;
  assert(volume <= 100);
  return 0;
}
void pos_audio_feedback(pos_sound_t sound) { (void)sound; }
void pos_audio_feedback_shutdown(void) {}
bool pos_microphone_active(void) {
  return microphone_starts > microphone_stops;
}
size_t pos_microphone_pending(void) { return microphone_backlog * 256; }

pos_transfer_status_t pos_network_share_activity(void) {
  if (!wifi_activity_test) return (pos_transfer_status_t){0};
  unsigned phase=++wifi_activity_steps;if(phase>3)phase=3;
  return (pos_transfer_status_t){phase,phase==3?100:50,100};
}

void pos_audio_feedback_hold(bool hold) {if(hold)++audio_holds;else {assert(audio_holds);--audio_holds;}}
__attribute__((destructor)) static void verify_audio_released(void){assert(!audio_holds);}
