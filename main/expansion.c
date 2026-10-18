#include "cJSON.h"
#include "pos_device.h"
#include "pos_files.h"
#include "pos_mesh_service.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void capabilities(pos_view_t *view, const pos_expansion_status_t *s) {
  pos_view_clear(view, "Expansion hardware");
  pos_view_line(view, 2, "LoRa cap: %s",
                s->cap_detected ? "detected" : "not detected");
  pos_view_line(view, 3, "LoRa features: %s",
                s->lora_enabled ? "enabled" : "disabled");
  pos_view_line(view, 4, "Transmit: %s",
                s->tx_enabled ? "enabled (+10 dBm)" : "off");
  pos_view_line(view, 5, "GPS: %s",
                s->gps_detected ? "detected" : "not detected");
  pos_view_line(view, 6, "GPS fix: %s",
                s->location.fix ? "valid" : "waiting / unavailable");
  pos_view_line(view, 7, "Maps: %s",
                s->maps_available ? "GPS requirement ready"
                                  : "disabled (no GPS)");
  pos_meshtastic_status_t mesh = pos_meshtastic_status();
  pos_view_line(view, 8, "%s %.3f MHz", mesh.enabled ? "Mesh" : "POS",
                s->frequency_hz / 1000000.0);
  pos_view_line(view, 9, "%.30s", s->error);
}
void pos_ui_expansion(const char *root) {
  (void)root;
  int selected = 0;
  const char *items[] = {"Toggle LoRa features", "Set permitted frequency",
                         "Antenna / transmit control",
                         "Radio protocol / Meshtastic", "Back"};
  while (!pos_home_pending()) {
    pos_expansion_status_t s = pos_expansion_status();
    pos_view_t view;
    capabilities(&view, &s);
    for (int i = 0; i < 5; ++i)
      pos_view_line(&view, i + 10, "%s", items[i]);
    view.selected_row = selected + 10;
    pos_view_line(&view, 15, "Arrows:move Enter:OK Esc:back");
    pos_device_show(&view);
    pos_event_t event;
    if (!pos_device_event(&event, 250) || !event.pressed)
      continue;
    if (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME)
      break;
    int action = pos_navigation(&event);
    if (action == POS_KEY_UP) {
      selected = (selected + 4) % 5;
      continue;
    }
    if (action == POS_KEY_DOWN) {
      selected = (selected + 1) % 5;
      continue;
    }
    if (action != POS_KEY_ENTER)
      continue;
    if (selected == 4)
      break;
    if (selected == 3) {
      pos_ui_mesh_settings(root);
      continue;
    }
    pos_error_t e = {0};
    if (selected == 0) {
      int result =
          pos_expansion_configure(!s.lora_enabled, s.frequency_hz, false, &e);
      pos_ui_notice(result ? "Hardware preference" : "LoRa features",
                    result ? e.message
                    : s.lora_enabled
                        ? "Disabled. Radio-dependent apps are unavailable "
                          "until re-enabled."
                        : "Enabled for a detected cap. Receive-only until "
                          "antenna / transmit consent is given. GPS presence "
                          "independently gates Maps.");
    }
    if (selected == 1) {
      if (pos_meshtastic_status().enabled) {
        pos_ui_mesh_settings(root);
        continue;
      }
      char text[24];
      snprintf(text, sizeof(text), "%.3f", s.frequency_hz / 1000000.0);
      if (pos_ui_text("LoRa frequency", "Permitted frequency in MHz", text,
                      sizeof(text), false)) {
        char *end;
        double value = strtod(text, &end);
        if (!*text || *end || !isfinite(value) || value < 868 || value > 923)
          pos_ui_notice("Invalid frequency",
                        "The cap supports 868-923 MHz. Choose a frequency and "
                        "operating rules permitted where you use the device.");
        else {
          int result = pos_expansion_configure(
              s.lora_enabled, (uint32_t)llround(value * 1000000), false, &e);
          pos_ui_notice(result ? "Preference not saved" : "Frequency set",
                        result
                            ? e.message
                            : "TX was turned off. Reconfirm the antenna and "
                              "permitted operating frequency before sending.");
        }
      }
    }
    if (selected == 2)
      pos_ui_radio_transmit();
  }
}
void pos_ui_radio_transmit(void) {
  pos_expansion_status_t s = pos_expansion_status();
  pos_error_t e = {0};
  pos_ui_notice(
      "Antenna / transmit",
      "Connect the correct antenna before transmitting. Choose a frequency "
      "permitted in your location. POS uses +10 dBm and a conservative 1% "
      "transmit duty budget; this alone is not a claim of regulatory "
      "compliance. POS radio messages are not encrypted. Meshtastic's "
      "default channel uses a publicly known key.");
  const char *options[] = {"Keep transmit OFF", "Antenna attached: enable TX"};
  int choice = pos_ui_menu("Transmit permission (session)", options, 2);
  if (choice >= 0) {
    int result = pos_expansion_configure(s.lora_enabled, s.frequency_hz,
                                         choice == 1, &e);
    pos_ui_notice(result ? "Transmit unavailable" : "Transmit control",
                  result   ? e.message
                  : choice ? "TX enabled for this session. Location "
                             "sharing still requires separate consent."
                           : "Transmit disabled.");
  }
}
static bool radio_available(void) {
  pos_expansion_status_t s = pos_expansion_status();
  if (!s.cap_detected || !s.lora_enabled) {
    pos_ui_notice("LoRa unavailable",
                  !s.cap_detected
                      ? "No LoRa cap detected. Power off before attaching the "
                        "cap. Check Settings > Expansion hardware."
                      : "LoRa features are disabled. Enable them in Settings > "
                        "Expansion hardware.");
    return false;
  }
  return true;
}
void pos_ui_location(const char *root) {
  (void)root;
  while (!pos_home_pending()) {
    pos_expansion_status_t s = pos_expansion_status();
    pos_view_t view;
    pos_view_clear(&view, "GPS / Location");
    pos_view_line(&view, 2, "Receiver: %s",
                  s.gps_detected ? "detected" : "not detected");
    pos_view_line(&view, 3, "Fix: %s", s.location.fix ? "valid" : "waiting");
    if (s.location.fix) {
      pos_view_line(&view, 5, "Lat: %.7f", s.location.latitude);
      pos_view_line(&view, 6, "Lon: %.7f", s.location.longitude);
      pos_view_line(&view, 7, "Altitude: %.1f m", s.location.altitude_m);
      pos_view_line(&view, 8, "Speed: %.1f km/h", s.location.speed_kph);
      pos_view_line(&view, 9, "HDOP: %.2f", s.location.hdop);
    }
    pos_view_line(&view, 11, "Satellites: %u / %u", s.location.satellites,
                  s.location.satellites_visible);
    pos_view_line(&view, 12, "Maps GPS gate: %s",
                  s.maps_available ? "available" : "disabled");
    pos_view_line(&view, 13, "Precise location is private");
    pos_view_line(&view, 14, "until explicitly shared.");
    pos_view_line(&view, 15, "Esc:back Fn+Space:close");
    pos_device_show(&view);
    pos_event_t event;
    if (pos_device_event(&event, 500) && event.pressed &&
        (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME))
      break;
  }
}
static bool destination(uint32_t *node, bool unicast) {
  char value[9] = "";
  if (!pos_ui_text("Destination node",
                   unicast ? "Peer node ID (8 hex digits)"
                           : "Node ID; empty = broadcast",
                   value, sizeof(value), false))
    return false;
  if (!*value && !unicast) {
    *node = 0;
    return true;
  }
  if (strlen(value) != 8 || strspn(value, "0123456789abcdefABCDEF") != 8 ||
      !(*node = strtoul(value, NULL, 16))) {
    pos_ui_notice(
        "Invalid node",
        "Use the 8-digit node ID shown on the other Panputer's Radio screen.");
    return false;
  }
  return true;
}
static void recent(const char *root, bool mesh) {
  char path[POS_FS_PATH];
  pos_fs_join(path, root,
              mesh ? "POS/Users/Owner/Radio/meshtastic.jsonl"
                   : "POS/Users/Owner/Radio/messages.jsonl");
  FILE *f = fopen(path, "rb");
  if (!f) {
    pos_ui_notice("Radio inbox", "No saved messages yet.");
    return;
  }
  char *text = calloc(1, 8193);
  if (!text) {
    fclose(f);
    return;
  }
  if (fseek(f, 0, SEEK_END)) {
    free(text);
    fclose(f);
    return;
  }
  long length = ftell(f);
  long start = length > 8192 ? length - 8192 : 0;
  if (length < 0 || fseek(f, start, SEEK_SET)) {
    free(text);
    fclose(f);
    return;
  }
  size_t n = fread(text, 1, 8192, f);
  fclose(f);
  text[n] = 0;
  char *line = text;
  if (start) {
    line = strchr(line, '\n');
    if (line)
      ++line;
  }
  char messages[8][241] = {{0}}, labels[8][31] = {{0}};
  unsigned count = 0;
  while (line && *line) {
    char *end = strchr(line, '\n');
    if (end)
      *end = 0;
    else
      break;
    cJSON *j = cJSON_Parse(line),
          *from = cJSON_GetObjectItemCaseSensitive(j, "from"),
          *body = cJSON_GetObjectItemCaseSensitive(j, "text");
    if (cJSON_IsString(from) && cJSON_IsString(body)) {
      unsigned slot = count++ % 8;
      snprintf(messages[slot], 241, "From %s\n%s", from->valuestring,
               body->valuestring);
      snprintf(labels[slot], 31, "%.8s: %.19s", from->valuestring,
               body->valuestring);
    }
    cJSON_Delete(j);
    line = end + 1;
  }
  free(text);
  if (!count) {
    pos_ui_notice("Radio inbox", "No complete saved messages.");
    return;
  }
  const char *items[9];
  unsigned shown = count < 8 ? count : 8;
  for (unsigned i = 0; i < shown; ++i)
    items[i] = labels[(count - 1 - i) % 8];
  items[shown] = "Back";
  for (;;) {
    int choice = pos_ui_menu("Recent radio messages", items, shown + 1);
    if (choice < 0 || choice == (int)shown)
      break;
    pos_ui_notice("Radio message", messages[(count - 1 - choice) % 8]);
  }
}
void pos_ui_mesh_recent(const char *root) { recent(root, true); }
void pos_ui_radio(const char *root) {
  if (!radio_available())
    return;
  if (pos_meshtastic_status().enabled) {
    pos_ui_meshtastic(root);
    return;
  }
  while (!pos_home_pending()) {
    pos_expansion_status_t s = pos_expansion_status();
    char title[40];
    snprintf(title, sizeof(title), "Radio %08x", (unsigned)s.node);
    const char *items[] = {"Status / latest message",
                           "Compose message",
                           "Send ping",
                           "Share location once",
                           "Recent messages",
                           "Retry saved outbox",
                           "Hardware settings",
                           "Back"};
    int choice = pos_ui_menu(title, items, 8);
    if (choice < 0 || choice == 7)
      break;
    pos_error_t e = {0};
    if (choice == 0) {
      char text[600];
      snprintf(
          text, sizeof(text),
          "Cap: %s\nFeatures: %s\nTX: %s\nFrequency: %.3f MHz\nSent: %u RX: "
          "%u\nACK: %u Failed: %u\nRSSI: %d dBm SNR: %.2f dB\nLatest: %s\n%s",
          s.cap_detected ? "detected" : "missing",
          s.lora_enabled ? "on" : "off", s.tx_enabled ? "on" : "off",
          s.frequency_hz / 1000000.0, (unsigned)s.radio.sent,
          (unsigned)s.radio.received, (unsigned)s.radio.acked,
          (unsigned)s.radio.failed, s.rssi, (double)s.snr, s.last_message,
          s.error);
      pos_ui_notice("Radio status", text);
    }
    if (choice == 1 || choice == 2 || choice == 3) {
      uint32_t node;
      if (!destination(&node, false))
        continue;
      char text[161] = "";
      uint8_t type = choice == 1   ? POS_RADIO_MSG
                     : choice == 2 ? POS_RADIO_PING
                                   : POS_RADIO_LOC;
      bool location = false;
      if (choice == 1 &&
          !pos_ui_text("Radio message", "Up to 160 ASCII characters", text,
                       sizeof(text), false))
        continue;
      if (choice == 3) {
        const char *consent[] = {"Cancel", "Share precise location once"};
        location = pos_ui_menu("Broadcast GPS coordinates?", consent, 2) == 1;
        if (!location)
          continue;
      }
      int result = pos_expansion_send(type, node, text, location, &e);
      pos_ui_notice(result ? "Not queued" : "Queued to outbox",
                    result ? e.message
                    : node ? "Delivery uses IDs, ACKs and bounded retries. "
                             "Follow the outbox/status for confirmation."
                           : "Broadcast is unacknowledged. Other nodes must "
                             "use the same POS radio protocol and frequency.");
    }
    if (choice == 4)
      recent(root, false);
    if (choice == 5) {
      int result = pos_expansion_retry(&e);
      pos_ui_notice(result ? "Retry unavailable" : "Outbox retry",
                    result ? e.message
                           : "Saved packets queued where slots are available. "
                             "Enable TX before retrying.");
    }
    if (choice == 6)
      pos_ui_expansion(root);
  }
}
void pos_ui_range(const char *root) {
  if (!radio_available())
    return;
  if (pos_meshtastic_status().enabled) {
    pos_ui_notice(
        "Range Test / protocol",
        "The CSV range test uses POS radio. "
        "Select POS radio in Hardware settings to use it. Meshtastic delivery "
        "status is shown in Radio; signal strength does not establish "
        "distance.");
    return;
  }
  const char *items[] = {"Receive / log only", "Send ping probes + log",
                         "Browse saved CSV logs", "Back"};
  int choice = pos_ui_menu("Radio Range Test", items, 4);
  if (choice < 0 || choice == 3)
    return;
  if (choice == 2) {
    pos_ui_browse(root, "POS/Users/Owner/RangeTests");
    return;
  }
  if (choice == 1 && !pos_expansion_status().tx_enabled) {
    pos_ui_notice("Transmit disabled", "Enable antenna / transmit control in "
                                       "Settings > Expansion hardware first.");
    return;
  }
  uint32_t peer = 0;
  if (choice == 1 && !destination(&peer, true))
    return;
  pos_error_t e = {0};
  if (pos_expansion_range(true, &e)) {
    pos_ui_notice("Cannot log range", e.message);
    return;
  }
  uint64_t next = 0;
  unsigned probes = 0;
  while (!pos_home_pending()) {
    pos_expansion_status_t s = pos_expansion_status();
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    uint64_t now = (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
    if (choice == 1 && now >= next) {
      if (!pos_expansion_send(POS_RADIO_PING, peer, "range", false, &e))
        ++probes;
      next = now + 60000;
    }
    pos_view_t view;
    pos_view_clear(&view, "Radio Range Test");
    pos_view_line(&view, 2, "Log: %s",
                  s.logging ? "recording" : "starting / stopped");
    pos_view_line(&view, 3, "GPS: %s",
                  s.location.fix ? "fix" : "no fix (blank coords)");
    pos_view_line(&view, 5, "Probes queued: %u", probes);
    pos_view_line(&view, 6, "TX attempts: %u RX: %u", s.radio.sent,
                  s.radio.received);
    pos_view_line(&view, 7, "ACK: %u Failed: %u", s.radio.acked,
                  s.radio.failed);
    pos_view_line(&view, 8, "RSSI: %d SNR: %.2f", s.rssi, (double)s.snr);
    pos_view_line(&view, 10, "%.30s",
                  strrchr(s.log_path, '/') ? strrchr(s.log_path, '/') + 1
                                           : s.log_path);
    pos_view_line(&view, 12, "CSV includes IDs and GPS.");
    pos_view_line(&view, 13, "Unresolved probes are not");
    pos_view_line(&view, 14, "called lost until timeout.");
    pos_view_line(&view, 15, "Esc:stop Fn+Space:close");
    pos_device_show(&view);
    pos_event_t event;
    if (pos_device_event(&event, 100) && event.pressed &&
        (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME))
      break;
    if (!s.cap_detected || !s.lora_enabled)
      break;
  }
  pos_expansion_range(false, &e);
}
