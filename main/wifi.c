#include "pos_device.h"
#include "pos_network_service.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  (void)ctx;
  return pos_ui_progress(NULL, phase, "", done, total);
}
static void remember(const pos_wifi_profile_t *profile) {
  const char *options[] = {"Use for this session only", "Remember network"};
  if (pos_ui_menu("Remember this network?", options, 2) != 1)
    return;
  pos_wifi_profiles_t profiles = {0};
  int result = pos_network_known(&profiles);
  if (result < 0) {
    pos_ui_notice("Known Networks unavailable",
                  "Stored data was preserved. Session connection still works.");
    return;
  }
  unsigned slot = profiles.count;
  for (unsigned i = 0; i < profiles.count; ++i)
    if (!strcmp(profile->ssid, profiles.networks[i].ssid))
      slot = i;
  if (slot == POS_WIFI_PROFILES) {
    pos_ui_notice("Known Networks full", "Forget a saved network first.");
    return;
  }
  profiles.networks[slot] = *profile;
  if (slot == profiles.count)
    ++profiles.count;
  pos_ui_notice("Known Networks",
                pos_network_known_save(&profiles)
                    ? "Could not save. Previous networks retained."
                    : "Network saved on this device.");
  pos_secure_zero(&profiles, sizeof(profiles));
}
static void connect_network(const char *ssid, bool secured, bool manual) {
  pos_wifi_profile_t profile = {0};
  if (ssid)
    snprintf(profile.ssid, sizeof(profile.ssid), "%s", ssid);
  if (manual && !pos_ui_text("Connect Wi-Fi", "Network name (SSID)",
                             profile.ssid, sizeof(profile.ssid), false))
    return;
  if (secured && !pos_ui_text("Connect Wi-Fi",
                              manual ? "Password (empty = open network)"
                                     : "Network password",
                              profile.password, sizeof(profile.password), true))
    goto done;
  if (!pos_wifi_profile_valid(&profile)) {
    pos_ui_notice("Invalid network",
                  "SSID: 1-32 bytes. Password: 8-63 characters, 64 hex digits, "
                  "or empty for an open network.");
    goto done;
  }
  pos_ui_message("Wi-Fi",
                 "Connecting...\nEsc cancels. Offline use remains available.");
  if (pos_device_wifi(profile.ssid, profile.password, 15000))
    pos_ui_notice("Not connected",
                  "Connection failed or was cancelled. Check signal, password "
                  "and 2.4 GHz compatibility.");
  else {
    pos_ui_notice("Connected",
                  "Wi-Fi is ready. Use Sync time before HTTPS downloads. Local "
                  "transfers work without Internet.");
    remember(&profile);
  }
done:
  pos_secure_zero(&profile, sizeof(profile));
}
static void saved_networks(void) {
  pos_wifi_profiles_t profiles = {0};
  if (pos_network_known(&profiles) < 0) {
    pos_ui_notice("Known Networks unavailable",
                  "Stored connections were preserved.");
    return;
  }
  for (;;) {
    const char *items[POS_WIFI_PROFILES + 2];
    char automatic[40];
    snprintf(automatic, sizeof(automatic), "Auto-connect: %s",
             pos_network_auto_enabled() ? "On" : "Off");
    for (unsigned i = 0; i < profiles.count; ++i)
      items[i] = profiles.networks[i].ssid;
    items[profiles.count] = automatic;
    items[profiles.count + 1] = "Back";
    int selected = pos_ui_menu("Known Networks", items, profiles.count + 2);
    if (selected < 0 || selected == profiles.count + 1)
      break;
    if (selected == profiles.count) {
      if (pos_network_auto_set(!pos_network_auto_enabled()))
        pos_ui_notice("Preference not saved",
                      "Previous auto-connect preference retained.");
      continue;
    }
    const char *actions[] = {"Connect", "Forget network", "Back"};
    int action = pos_ui_menu(profiles.networks[selected].ssid, actions, 3);
    if (action == 0) {
      pos_ui_message("Wi-Fi", "Connecting to known network...");
      pos_ui_notice("Wi-Fi",
                    pos_device_wifi(profiles.networks[selected].ssid,
                                    profiles.networks[selected].password, 15000)
                        ? "Connection failed or cancelled."
                        : "Connected.");
    }
    if (action == 1) {
      const char *confirm[] = {"Keep network", "Forget it"};
      if (pos_ui_menu("Forget this network?", confirm, 2) == 1) {
        pos_wifi_profiles_t next = profiles;
        for (unsigned i = selected; i + 1 < next.count; ++i)
          next.networks[i] = next.networks[i + 1];
        --next.count;
        pos_secure_zero(&next.networks[next.count], sizeof(next.networks[0]));
        if (pos_network_known_save(&next))
          pos_ui_notice("Not forgotten", "Previous networks retained.");
        else
          profiles = next;
        pos_secure_zero(&next, sizeof(next));
      }
    }
    if (pos_home_pending())
      break;
  }
  pos_secure_zero(&profiles, sizeof(profiles));
}
static void scan(void) {
  pos_network_ap_t *aps = calloc(12, sizeof(*aps));
  if (!aps)
    return;
  unsigned count;
  pos_ui_message("Wi-Fi", "Scanning nearby networks...");
  if (pos_network_scan(aps, 12, &count))
    pos_ui_notice("Scan failed", "Try manual connection or retry the scan.");
  else {
    char labels[13][40];
    const char *items[13];
    for (unsigned i = 0; i < count; ++i) {
      snprintf(labels[i], sizeof(labels[i]), "%.22s %s %d", aps[i].ssid,
               aps[i].secured ? "*" : "open", aps[i].rssi);
      items[i] = labels[i];
    }
    items[count] = "Manual / hidden network";
    int choice = pos_ui_menu("Choose network (* = password)", items, count + 1);
    if (choice == (int)count)
      connect_network(NULL, true, true);
    else if (choice >= 0)
      connect_network(aps[choice].ssid, aps[choice].secured, false);
  }
  free(aps);
}
static void download(const char *root, bool api) {
  char url[512] = "https://", name[121] = "download.bin", hash[65] = "",
       body[257] = "";
  const char *method = "GET";
  if (api) {
    const char *methods[] = {"GET response", "POST JSON", "Cancel"};
    int choice = pos_ui_menu("HTTPS API request", methods, 3);
    if (choice < 0 || choice == 2)
      return;
    if (choice == 1)
      method = "POST";
    strcpy(name, "response.json");
  }
  if (!pos_ui_text(api ? "API URL" : "Download URL", "HTTPS URL", url,
                   sizeof(url), false))
    return;
  if (!strcmp(method, "POST") &&
      !pos_ui_text("JSON request body", "Small JSON body (optional)", body,
                   sizeof(body), false))
    return;
  if (!pos_ui_text("Save in Downloads", "New filename", name, sizeof(name),
                   false))
    return;
  if (!pos_transfer_name_valid(name) || !pos_https_url_valid(url)) {
    pos_ui_notice("Invalid URL or filename",
                  "Use HTTPS and a filename without directories.");
    return;
  }
  if (!pos_ui_text("Verify download", "Expected SHA-256 (optional)", hash,
                   sizeof(hash), false))
    return;
  char folder[POS_FS_PATH], relative[160];
  if (pos_fs_join(folder, root, "Downloads") || pos_fs_mkdirs(folder))
    return;
  snprintf(relative, sizeof(relative), "Downloads/%s", name);
  pos_error_t error = {0};
  pos_ui_message(
      "HTTPS",
      "Connecting securely...\nCertificate and hostname validation enabled.");
  int result =
      pos_network_https(url, method, body, api ? "application/json" : NULL,
                        root, relative, hash, progress, NULL, &error);
  pos_ui_notice(result ? "Download incomplete" : "Downloaded & verified",
                result ? error.message : relative);
}
static void transfer(const char *root) {
  const char *items[] = {"Receive uploads only", "Also share one selected file",
                         "Cancel"};
  int choice = pos_ui_menu("Local file transfer", items, 3);
  if (choice < 0 || choice == 2)
    return;
  char selected[241] = "";
  if (choice == 1 && !pos_ui_pick_file(root, "Choose file to share", selected))
    return;
  pos_ui_wifi_export(root, selected);
}
void pos_ui_wifi_export(const char *root, const char *selected) {
  const char *confirm[] = {"Start trusted-LAN session", "Cancel"};
  pos_ui_notice("Local transfer",
                "This uses HTTP on your local Wi-Fi with a temporary session "
                "key. Use a trusted network. Uploads only go to Downloads; "
                "existing files are preserved.");
  if (pos_ui_menu("Start file transfer?", confirm, 2) != 0)
    return;
  char key[17];
  if (pos_network_share_start(root, selected, key)) {
    pos_ui_notice("Transfer unavailable",
                  "Connect Wi-Fi and check SD storage first.");
    return;
  }
  pos_network_status_t status = pos_network_status();
  char message[350];
  snprintf(message, sizeof(message),
           "PC: POS Transfer > Wi-Fi\nOr browser: "
           "http://%s:8080/\n?key=%s\nUploads: "
           "Downloads\n%s\nEnter/Esc: stop session\nFn+Space: close app",
           status.ip, key,
           *selected ? "One selected file shared" : "Uploads only");
  pos_ui_message("File transfer active", message);
  pos_event_t event;
  while (!pos_home_pending())
    if (pos_device_event(&event, 100) && event.pressed &&
        (event.key == POS_KEY_ENTER || event.key == POS_KEY_ESCAPE ||
         event.key == POS_KEY_HOME))
      break;
  pos_network_share_stop();
  pos_secure_zero(key, sizeof(key));
}
void pos_ui_wifi(const char *root) {
  pos_network_auto_pause(true);
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Connection status",
                           "Connect / scan networks",
                           "Manual / hidden network",
                           "Known Networks",
                           "Sync time",
                           "HTTPS download",
                           "HTTPS API request",
                           "Local file transfer",
                           "Disconnect Wi-Fi",
                           "Back"};
    pos_network_status_t status = pos_network_status();
    selected = pos_ui_menu_from(status.connected ? "Wi-Fi: connected"
                                                 : "Wi-Fi: offline",
                                items, 10, selected);
    if (selected < 0 || selected == 9)
      break;
    if (selected == 0) {
      char message[240];
      time_t now = time(NULL);
      char date[64];
      struct tm local;
      localtime_r(&now, &local);
      strftime(date, sizeof(date), "%Y-%m-%d %H:%M %Z", &local);
      snprintf(message, sizeof(message),
               "Network: %s\nIP: %s\nSignal: %d dBm\nTime: %s\nFn+Space closes "
               "this app. Wi-Fi stays shared until disconnected.",
               status.connected ? status.ssid : "offline", status.ip,
               status.rssi, now < 1704067200 ? "not synchronized" : date);
      pos_ui_notice("Wi-Fi status", message);
    }
    if (selected == 1)
      scan();
    if (selected == 2)
      connect_network(NULL, true, true);
    if (selected == 3)
      saved_networks();
    if (selected == 4) {
      pos_ui_message("Time service", "Synchronizing time...\nEsc cancels.");
      pos_ui_notice("Time service", pos_network_sync_time()
                                        ? "Time sync failed or cancelled. "
                                          "Check Internet access and retry."
                                        : "Clock synchronized. Your timezone "
                                          "preference controls display.");
    }
    if (selected == 5 || selected == 6)
      download(root, selected == 6);
    if (selected == 7)
      transfer(root);
    if (selected == 8) {
      pos_network_auto_set(false);
      pos_network_share_stop();
      pos_device_wifi_stop();
    }
  }
  pos_network_share_stop();
  pos_network_auto_pause(false);
}
