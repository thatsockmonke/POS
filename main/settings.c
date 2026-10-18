#include "pos_device.h"
#include "pos_health.h"
#include "pos_history.h"
#include "pos_network_service.h"
#include "pos_peripherals.h"
#include "pos_update_service.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static bool save(const char *root, pos_settings_t *active,
                 pos_settings_t *next) {
  if (!pos_settings_valid(next) || pos_settings_save(root, next)) {
    pos_ui_notice(
        "Settings not saved",
        "Existing preferences are preserved. Check SD space and try again.");
    return false;
  }
  *active = *next;
  pos_audio_feedback_configure(next->sounds_enabled, next->sounds_volume,
                               next->sounds_keys, next->sounds_alerts);
  pos_device_brightness(next->brightness);
  setenv("TZ", next->timezone, 1);
  tzset();
  return true;
}
static void profile(const char *root, pos_settings_t *settings) {
  const char *items[] = {"Display name", "Device name", "Back"};
  int selected = 0;
  while (!pos_home_pending()) {
    int choice = pos_ui_menu_from("Profile", items, 3, selected);
    if (choice < 0 || choice == 2)
      return;
    selected = choice;
    pos_settings_t next = *settings;
    char *value = choice ? next.device_name : next.display_name;
    while (pos_ui_text(
        "Profile", items[choice], value,
        choice ? sizeof(next.device_name) : sizeof(next.display_name), false)) {
      if (choice ? pos_settings_device_name_valid(value)
                 : pos_settings_display_name_valid(value)) {
        save(root, settings, &next);
        break;
      }
      pos_ui_notice("Name not valid",
                    choice ? "Use 1-24 letters, digits or hyphens."
                           : "Use 1-48 printable characters.");
    }
  }
}
static void display(const char *root, pos_settings_t *settings) {
  char value[4];
  snprintf(value, sizeof(value), "%u", settings->brightness);
  while (pos_ui_text("Display brightness", "Percent (10-100)", value,
                     sizeof(value), false)) {
    char *end;
    long percent = strtol(value, &end, 10);
    if (!*value || *end || percent < 10 || percent > 100) {
      pos_ui_notice("Invalid brightness",
                    "Enter a whole number from 10 to 100.");
      continue;
    }
    pos_settings_t next = *settings;
    next.brightness = percent;
    save(root, settings, &next);
    break;
  }
}
static void datetime(const char *root, pos_settings_t *settings) {
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Current date / time", "Change timezone",
                           "Sync time over Wi-Fi", "Set UTC date / time",
                           "Back"};
    int choice = pos_ui_menu_from("Date & time", items, 5, selected);
    if (choice < 0 || choice == 4)
      return;
    selected = choice;
    if (choice == 0) {
      time_t now = time(NULL);
      struct tm local, utc;
      localtime_r(&now, &local);
      gmtime_r(&now, &utc);
      char a[48], b[48], body[360];
      strftime(a, sizeof(a), "%Y-%m-%d %H:%M:%S %Z", &local);
      strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S UTC", &utc);
      snprintf(body, sizeof(body), "Local: %s\n%s\nRule: %.63s\n%s", a, b,
               settings->timezone,
               now < 1577836800 ? "Clock not set. Sync or enter UTC."
                                : "Clock resets without external time. GPS "
                                  "time is shown in Location when available.");
      pos_ui_notice("Date & time", body);
    }
    if (choice == 1) {
      pos_settings_t next = *settings;
      if (pos_ui_timezone(next.timezone))
        save(root, settings, &next);
    }
    if (choice == 2)
      pos_ui_notice(
          "Time synchronization",
          pos_network_sync_time()
              ? "Connect Wi-Fi first; time sync could not start."
              : "Time sync requested. Check Current date / time shortly.");
    if (choice == 3) {
      char text[17] = "2026-01-01 00:00";
      if (pos_ui_text("Set UTC time", "YYYY-MM-DD HH:MM (UTC)", text,
                      sizeof(text), false))
        pos_ui_notice("UTC clock",
                      pos_device_set_time_utc(text)
                          ? "Invalid date/time. Use 2020-2099, a real date, "
                            "and 24-hour UTC time."
                          : "Clock set for this session. Your timezone "
                            "converts UTC to local time.");
    }
  }
}
static void battery_details(void) {
  char body[320];
  int mv = pos_device_battery_mv();
  snprintf(
      body, sizeof(body),
      "Battery voltage: %d mV\nThe top-right percentage is a smoothed voltage "
      "estimate. Icon thresholds use hysteresis. A green full icon / 100%% "
      "means sustained near-full voltage, not a charger-termination "
      "measurement. This board profile has no verified charger-status input. "
      "Readings need calibration on your device.",
      mv);
  pos_ui_notice("Battery indicator", body);
}
static void display_off(void) {
  /* Discard the activation key's queued repeats/releases before arming wake. */
  pos_event_t event;
  while (pos_device_event(&event, 0))
    ;
  if (pos_home_pending())
    return;
  if (pos_device_display_enabled(false)) {
    pos_ui_notice("Display unavailable",
                  "The display could not be switched off.");
    return;
  }
  /* Read normally for health checks, but hidden-device events cannot request
   * Home or redraw the battery. Consume the wake press instead of routing it.
   */
  while (!pos_device_event(&event, 100) || !event.pressed)
    ;
  if (pos_device_display_enabled(true))
    pos_ui_notice("Display unavailable", "The display could not be restored.");
}
extern int pos_device_history_import(const char *, pos_error_t *);
extern int pos_device_history_rollback(const char *, const char *,
                                       pos_error_t *);
static void firmware_history(const char *root) {
  struct {
    pos_history_entry_t entries[16];
    pos_update_source_t source;
    char labels[17][72];
    const char *items[18];
  } *w = calloc(1, sizeof(*w));
  if (!w)
    return;
  while (!pos_home_pending()) {
    unsigned count = 0;
    pos_error_t e = {0};
    if (pos_update_source_load(&w->source) ||
        pos_history_list(root, w->source.key, w->entries, 16, &count, &e)) {
      pos_ui_notice(
          "History unavailable",
          *e.message ? e.message
                     : "History or trusted key unavailable. Files preserved.");
      break;
    }
    for (unsigned i = 0; i < count; ++i) {
      snprintf(w->labels[i], sizeof(w->labels[i]), "%s (sequence %u)",
               w->entries[i].release, (unsigned)w->entries[i].sequence);
      w->items[i] = w->labels[i];
    }
    w->items[count] = "Import previous installation";
    w->items[count + 1] = "Back";
    int choice =
        pos_ui_menu(count ? "Firmware history" : "History: no releases yet",
                    w->items, count + 2);
    if (choice < 0 || choice == (int)count + 1)
      break;
    if (choice == (int)count) {
      pos_ui_message(
          "Import previous installation",
          "Looking for a matching previous OTA image and retained System. "
          "Wi-Fi may be needed to recover its signed metadata.");
      int result = pos_device_history_import(root, &e);
      pos_ui_notice(
          "Firmware history",
          result
              ? e.message
              : "Previous firmware and matching System archived and verified.");
      continue;
    }
    pos_ui_help(root, "Roll back firmware + System",
                "notices.txt#settings-roll-back-firmware-system");
    const char *confirm[] = {"Cancel", "Verify and roll back"};
    if (pos_ui_menu(w->entries[choice].release, confirm, 2) == 1) {
      pos_ui_message("Firmware history",
                     "Verifying archive and preserving current release...");
      if (pos_device_history_rollback(root, w->entries[choice].id, &e))
        pos_ui_notice("Rollback not completed",
                      *e.message
                          ? e.message
                          : "Verification failed. Existing data preserved.");
    }
  }
  free(w);
}
static void system_settings(const char *root) {
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Software Update",        "Crash reports",
                           "Safe mode on next boot", "Turn display off",
                           "Firmware history",       "Back"};
    int action = pos_ui_menu_from("System", items, 6, selected);
    if (action < 0 || action == 5)
      return;
    selected = action;
    if (action == 0)
      pos_ui_updates(root);
    if (action == 1)
      pos_ui_browse(root, "POS/Users/Owner/Diagnostics/Crashes");
    if (action == 2) {
      const char *options[] = {"Cancel", "Enable safe mode next boot"};
      if (pos_ui_menu("Safe mode", options, 2) == 1) {
        if (pos_health_request_safe(true))
          pos_ui_help(root, "Safe mode not saved",
                      "notices.txt#settings-safe-mode-not-saved");
        else
          pos_ui_help(root, "Safe mode requested",
                      "notices.txt#settings-safe-mode-requested");
      }
    }
    if (action == 3)
      display_off();
    if (action == 4)
      firmware_history(root);
  }
}
static void sounds(const char *root, pos_settings_t *settings) {
  int selected = 0;
  while (!pos_home_pending()) {
    char enabled[40], volume[32], keys[40], alerts[40];
    snprintf(enabled, sizeof(enabled), "UI sounds: %s",
             settings->sounds_enabled ? "On" : "Off");
    snprintf(volume, sizeof(volume), "Volume: %u%%", settings->sounds_volume);
    snprintf(keys, sizeof(keys), "Key presses: %s",
             settings->sounds_keys ? "On" : "Off");
    snprintf(alerts, sizeof(alerts), "Apps / notifications: %s",
             settings->sounds_alerts ? "On" : "Off");
    const char *items[] = {enabled, volume,          keys,
                           alerts,  "Preview sound", "Back"};
    int choice = pos_ui_menu_from("Sounds", items, 6, selected);
    if (choice < 0 || choice == 5)
      return;
    selected = choice;
    if (choice == 4) {
      pos_audio_feedback(POS_SOUND_PREVIEW);
      continue;
    }
    pos_settings_t next = *settings;
    if (choice == 0)
      next.sounds_enabled = !next.sounds_enabled;
    if (choice == 2)
      next.sounds_keys = !next.sounds_keys;
    if (choice == 3)
      next.sounds_alerts = !next.sounds_alerts;
    if (choice == 1) {
      char text[4];
      snprintf(text, sizeof(text), "%u", next.sounds_volume);
      if (!pos_ui_text("UI sound volume", "Percent (0-100, 0 is silent)", text,
                       sizeof(text), false))
        continue;
      char *end;
      long value = strtol(text, &end, 10);
      if (!*text || *end || value < 0 || value > 100) {
        pos_ui_notice("Invalid volume", "Enter a whole number from 0 to 100.");
        continue;
      }
      next.sounds_volume = value;
    }
    if (save(root, settings, &next))
      pos_audio_feedback(POS_SOUND_PREVIEW);
  }
}
void pos_ui_settings(const char *root, pos_settings_t *settings) {
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Profile / device name",
                           "Display brightness",
                           "Date & time",
                           "Background",
                           "Wi-Fi",
                           "Expansion hardware",
                           "System / software update",
                           "Battery information",
                           "Restore default preferences",
                           "IR & motion",
                           "Developer deployment",
                           "Sounds",
                           "Back"};
    int choice = pos_ui_menu_from("Settings", items, 13, selected);
    if (choice < 0 || choice == 12)
      return;
    selected = choice;
    if (choice == 0)
      profile(root, settings);
    if (choice == 1)
      display(root, settings);
    if (choice == 2)
      datetime(root, settings);
    if (choice == 3)
      pos_ui_background(root, settings);
    if (choice == 4)
      pos_ui_wifi(root);
    if (choice == 5)
      pos_ui_expansion(root);
    if (choice == 6)
      system_settings(root);
    if (choice == 7)
      battery_details();
    if (choice == 8) {
      const char *options[] = {"Cancel", "Reset preferences only"};
      if (pos_ui_menu("Keep documents and credentials", options, 2) == 1) {
        pos_settings_t next;
        pos_settings_defaults(&next);
        next.setup_complete = true;
        save(root, settings, &next);
      }
    }
    if (choice == 9)
      pos_ui_instruments(root);
    if (choice == 10)
      pos_ui_deploy(root);
    if (choice == 11)
      sounds(root, settings);
    pos_device_brightness(settings->brightness);
    setenv("TZ", settings->timezone, 1);
    tzset();
  }
}
