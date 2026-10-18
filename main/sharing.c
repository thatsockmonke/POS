#include "pos_ble.h"
#include "pos_device.h"
#include "pos_settings.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  (void)ctx;
  return pos_ui_progress(NULL, phase, "", done, total);
}
static bool pair(pos_error_t *e) {
  pos_ble_status_t s = pos_ble_status();
  if (!s.pairing)
    return true;
  char text[180];
  snprintf(text, sizeof(text),
           "Compare %06u on both devices. Only accept if the numbers match.",
           (unsigned)s.passkey);
  pos_ui_notice("Secure BLE pairing", text);
  const char *items[] = {"Reject / codes differ", "Codes match: accept"};
  bool accept = pos_ui_menu("Confirm matching numbers", items, 2) == 1;
  if (pos_ble_pair(accept, e))
    return false;
  if (!accept)
    snprintf(e->message, sizeof(e->message), "Pairing rejected or cancelled.");
  return accept;
}
static bool wait_ready(pos_error_t *e, bool secure) {
  for (unsigned i = 0; i < 600 && !pos_home_pending(); ++i) {
    pos_ble_poll();
    pos_ble_status_t status = pos_ble_status();
    if (status.pairing) {
      if (!pair(e))
        return false;
      i = 0;
    }
    if (secure ? status.secure : status.ready)
      return true;
    if (*status.error) {
      snprintf(e->message, sizeof(e->message), "%s", status.error);
      return false;
    }
    pos_event_t event;
    if (pos_device_event(&event, 50) && event.pressed &&
        (event.key == POS_KEY_HOME || event.key == POS_KEY_ESCAPE))
      return false;
  }
  snprintf(e->message, sizeof(e->message),
           "BLE connection timed out or cancelled.");
  return false;
}
void pos_ui_ble(const char *root, const char *relative, bool receive) {
  pos_settings_t settings;
  pos_settings_defaults(&settings);
  pos_settings_load(root, &settings);
  pos_error_t e = {0};
  pos_ui_message("BLE sharing", "POS / Linux PC sharing\nStarting secure "
                                "Bluetooth...\nEsc cancels; Fn+Space closes.");
  if (pos_ble_start(root, settings.device_name, receive, &e)) {
    pos_ui_notice("BLE unavailable", e.message);
    return;
  }
  if (!wait_ready(&e, false)) {
    pos_ble_stop();
    if (!pos_home_pending())
      pos_ui_notice("BLE stopped", e.message);
    return;
  }
  if (receive) {
    while (!pos_home_pending()) {
      pos_ble_poll_progress(progress, NULL);
      pos_ble_status_t s = pos_ble_status();
      if (s.pairing) {
        pair(&e);
        continue;
      }
      if (s.offered) {
        char text[230];
        snprintf(
            text, sizeof(text),
            "File: %s\nSize: %u bytes\nSave in Downloads. Same-file partial "
            "transfers resume; existing completed files are preserved.",
            s.offer.name, (unsigned)s.offer.size);
        pos_ui_notice("Incoming file offer", text);
        const char *options[] = {"Decline", "Accept file"};
        bool accept = pos_ui_menu("Receive this file?", options, 2) == 1;
        if (pos_ble_accept_progress(accept, progress, NULL, &e))
          pos_ui_notice("File refused", e.message);
        continue;
      }
      if (s.complete) {
        pos_ui_notice("Received & verified", s.offer.name);
        break;
      }
      if (*s.error) {
        pos_ui_notice("Transfer stopped", s.error);
        break;
      }
      pos_view_t view;
      pos_view_clear(&view, "Receive BLE file");
      pos_view_line(&view, 2, "%s",
                    s.secure      ? "Encrypted connection"
                    : s.connected ? "Pairing..."
                                  : "Discoverable nearby");
      pos_view_line(&view, 4, "%.30s", s.offer.name);
      pos_view_line(&view, 6, "%u / %u bytes", s.offset, s.offer.size);
      pos_view_line(&view, 9, "PC: POS Transfer > Send");
      pos_view_line(&view, 10, "Choose Bluetooth on PC");
      pos_view_line(&view, 12, "Confirm matching codes");
      pos_view_line(&view, 15, "Esc:stop Fn+Space:close");
      pos_device_show(&view);
      pos_event_t event;
      if (pos_device_event(&event, 20) && event.pressed &&
          (event.key == POS_KEY_HOME || event.key == POS_KEY_ESCAPE))
        break;
    }
  } else {
    bool connected = false;
    while (!pos_home_pending() && !connected) {
      pos_ui_message(
          "Find Bluetooth receiver",
          "Panputer: Share > Receive BLE file.\nLinux PC: POS "
          "Transfer > Receive > Bluetooth.\nScanning... Esc cancels.");
      if (pos_ble_scan(&e)) {
        pos_ui_notice("Scan failed", e.message);
        break;
      }
      while (pos_ble_status().scanning && !pos_home_pending()) {
        pos_event_t event;
        if (pos_device_event(&event, 100) && event.pressed &&
            (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME))
          goto done;
      }
      pos_ble_peer_t peers[8];
      unsigned count = pos_ble_peers(peers);
      char labels[8][48];
      const char *items[10];
      for (unsigned i = 0; i < count; ++i) {
        snprintf(labels[i], 48, "%.24s (%d dBm)", peers[i].name, peers[i].rssi);
        items[i] = labels[i];
      }
      items[count] = "Scan again";
      items[count + 1] = "Cancel";
      int choice = pos_ui_menu(count ? "Choose POS / PC" : "No receivers found",
                               items, count + 2);
      if (choice < 0 || choice == (int)count + 1)
        break;
      if (choice == (int)count)
        continue;
      pos_ui_message("BLE connection", "Connecting... Compare pairing numbers "
                                       "on both devices. Esc cancels.");
      if (pos_ble_connect(choice, &e) || !wait_ready(&e, true)) {
        if (!pos_home_pending())
          pos_ui_notice("Not connected", e.message);
        break;
      }
      connected = true;
    }
    if (connected) {
      int result = pos_ble_send(relative, progress, NULL, &e);
      if (!pos_home_pending())
        pos_ui_notice(
            result ? "Transfer incomplete" : "Sent & verified",
            result
                ? e.message
                : "The receiver verified SHA-256 and saved the file. Partial "
                  "transfers can resume by sending the same file again.");
    }
  }
done:
  pos_ble_stop();
}
void pos_ui_share(const char *root, const char *relative) {
  const char *items[] = {"Save local copy", "Wi-Fi file transfer",
                         "Send over BLE to POS / PC", "USB cable to PC", "Back"};
  int choice = pos_ui_menu("Share file", items, 5);
  if (choice == 0) {
    char target[241];
    snprintf(target, sizeof(target), "Downloads/%s",
             strrchr(relative, '/') ? strrchr(relative, '/') + 1 : relative);
    if (pos_ui_text("Save local copy", "New SD-relative path", target,
                    sizeof(target), false)) {
      pos_error_t e = {0};
      int result = pos_files_copy(root, relative, target, progress, NULL, &e);
      pos_ui_notice(result ? "Copy incomplete" : "Saved copy",
                    result ? e.message : target);
    }
  }
  if (choice == 1)
    pos_ui_wifi_export(root, relative);
  if (choice == 2)
    pos_ui_ble(root, relative, false);
  if (choice == 3)
    pos_ui_cable(root, relative);
}
void pos_ui_sharing(const char *root) {
  while (!pos_home_pending()) {
    const char *items[] = {"Share a file with POS / PC",
                           "Receive BLE file / PC", "Receive Wi-Fi files / PC",
                           "Receive USB cable / PC", "Back"};
    int choice = pos_ui_menu("Share with POS / PC", items, 5);
    if (choice < 0 || choice == 4)
      break;
    if (choice == 0) {
      char relative[241];
      if (pos_ui_pick_file(root, "Choose file to share", relative))
        pos_ui_share(root, relative);
    }
    if (choice == 1)
      pos_ui_ble(root, NULL, true);
    if (choice == 2)
      pos_ui_wifi_export(root, "");
    if (choice == 3)
      pos_ui_cable(root, "");
  }
}
