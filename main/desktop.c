#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pos_device.h"
#include "pos_fs.h"
#include "pos_launcher.h"
#include "pos_recovery.h"
#include "pos_settings.h"
#include "ui.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  const char *root;
  pos_settings_t settings;
} services_t;
typedef struct {
  services_t *services;
  int count;
  uint64_t last;
} counter_t;

static void measurement(services_t *s, const char *phase) {
  uint32_t heap = esp_get_free_heap_size(),
           minimum = esp_get_minimum_free_heap_size();
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT),
           flash = 0;
  esp_flash_get_size(NULL, &flash);
  uint64_t total = 0, available = 0;
  esp_vfs_fat_info(s->root, &total, &available);
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  pos_device_info_t d = pos_device_info();
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return;
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddStringToObject(j, "kind", "pos-device-measurement");
  cJSON_AddBoolToObject(j, "measured_on_device", true);
  cJSON_AddStringToObject(j, "phase", phase);
  cJSON_AddStringToObject(j, "pos_version", esp_app_get_description()->version);
  cJSON_AddStringToObject(j, "idf_version", esp_get_idf_version());
  char build[65];
  for (unsigned i = 0; i < 32; ++i)
    snprintf(build + 2 * i, 3, "%02x",
             esp_app_get_description()->app_elf_sha256[i]);
  cJSON_AddStringToObject(j, "build_id", build);
  static char boot_id[17];
  if (!*boot_id)
    snprintf(boot_id, sizeof(boot_id), "%08x%08x", (unsigned)esp_random(),
             (unsigned)esp_random());
  cJSON_AddStringToObject(j, "boot_id", boot_id);
  cJSON_AddNumberToObject(j, "chip_revision", chip.revision);
  cJSON_AddNumberToObject(j, "reset_reason", esp_reset_reason());
  cJSON_AddNumberToObject(j, "heap", heap);
  cJSON_AddNumberToObject(j, "minimum_heap", minimum);
  cJSON_AddNumberToObject(j, "largest_internal_block", largest);
  cJSON_AddNumberToObject(j, "main_stack_free_bytes",
                          uxTaskGetStackHighWaterMark(NULL));
  cJSON_AddNumberToObject(j, "flash_bytes", flash);
  cJSON_AddNumberToObject(j, "firmware_bytes", d.firmware_bytes);
  cJSON_AddNumberToObject(j, "sd_bytes", total);
  cJSON_AddNumberToObject(j, "sd_free_bytes", available);
  cJSON_AddNumberToObject(j, "boot_ms", d.boot_ms);
  cJSON_AddNumberToObject(j, "uptime_ms", esp_timer_get_time() / 1000);
  cJSON_AddNumberToObject(j, "frame_count", d.frames);
  cJSON_AddNumberToObject(j, "frame_min_us", d.frame_min_us);
  cJSON_AddNumberToObject(j, "frame_max_us", d.frame_max_us);
  cJSON_AddNumberToObject(j, "frame_mean_us",
                          d.frames ? d.frame_total_us / d.frames : 0);
  cJSON_AddNumberToObject(j, "battery_mv", pos_device_battery_mv());
  cJSON_AddBoolToObject(j, "display_initialized", d.display_ok);
  cJSON_AddBoolToObject(j, "keyboard_initialized", d.keyboard_ok);
  cJSON_AddBoolToObject(j, "wifi_connected", d.wifi_connected);
  snprintf(build, sizeof(build), "%06x%08x", (unsigned)d.raw_keys[1],
           (unsigned)d.raw_keys[0]);
  cJSON_AddStringToObject(j, "keyboard_positions_seen", build);
  cJSON_AddNumberToObject(j, "keyboard_fifo_overflows", d.fifo_overflows);
  cJSON_AddNumberToObject(j, "input_queue_drops", d.queue_drops);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!text)
    return;
  printf("POS_MEASUREMENT %s\n", text);
  char folder[POS_FS_PATH], path[POS_FS_PATH], relative[120];
  /* Root-level evidence also works before a system is installed. */
  if (!pos_fs_join(folder, s->root, "Diagnostics") && !pos_fs_mkdirs(folder)) {
    snprintf(relative, sizeof(relative), "Diagnostics/device-%08x.json",
             (unsigned)esp_random());
    if (!pos_fs_join(path, s->root, relative))
      (void)pos_fs_write_new(path, text, strlen(text));
  }
  free(text);
}

static void wipe(void *data, size_t size) {
  volatile unsigned char *bytes = data;
  while (size--)
    *bytes++ = 0;
}

static uint64_t free_space(const char *root) {
  uint64_t total, free = 0;
  esp_vfs_fat_info(root, &total, &free);
  return free;
}
static void factory(void) {
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (p && esp_ota_set_boot_partition(p) == ESP_OK)
    esp_restart();
  pos_ui_notice("Recovery unavailable",
                "Factory installer could not be selected. Use USB recovery.");
}
static void wifi_setup(services_t *services) {
  const char *options[] = {"Connect for this session", "Skip Wi-Fi"};
  if (pos_ui_menu("Optional Wi-Fi", options, 2) != 0)
    return;
  char ssid[33] = "", password[64] = "";
  if (!pos_ui_text("Wi-Fi", "Network name (SSID)", ssid, sizeof(ssid), false) ||
      !*ssid)
    return;
  if (!pos_ui_text("Wi-Fi", "Password (empty = open)", password,
                   sizeof(password), true)) {
    wipe(password, sizeof(password));
    return;
  }
  pos_ui_message("Wi-Fi", "Connecting...\nCredentials stay in RAM only.");
  measurement(services, "wifi-before");
  int result = pos_device_wifi(ssid, password, 15000);
  measurement(services, result ? "wifi-failed" : "wifi-connected");
  wipe(password, sizeof(password));
  pos_ui_notice(
      "Wi-Fi",
      result
          ? "No connection. Check network and password; offline use still "
            "works."
          : "Connected. Password is not stored. Reconnect after restarting.");
}
static void setup(services_t *services, bool first) {
  if (!pos_ui_setup(services->root, &services->settings, first))
    return;
  setenv("TZ", services->settings.timezone, 1);
  tzset();
  pos_device_brightness(services->settings.brightness);
  wifi_setup(services);
  pos_ui_notice(
      "Setup complete",
      "Your files live in POS/Users/Owner. Settings survive normal restarts.");
}
static bool restore(services_t *services) {
  char installed[POS_FS_PATH];
  struct stat system;
  pos_fs_join(installed, services->root, "POS/System/installed.pos");
  if (stat(installed, &system)) {
    pos_ui_notice("Install POS first",
                  "Install the new system before restoring user data. Your ZIP "
                  "remains readable on a computer.");
    return false;
  }
  char archive[POS_FS_PATH];
  pos_error_t find_error = {0};
  if (pos_backup_find(services->root, archive, &find_error)) {
    pos_ui_notice("Backup unavailable", find_error.message);
    return false;
  }
  pos_backup_info_t info;
  pos_error_t error = {0};
  if (pos_backup_verify(archive, &info, pos_ui_progress, NULL, &error)) {
    pos_ui_notice("Backup unavailable", error.message);
    return false;
  }
  char summary[300];
  snprintf(summary, sizeof(summary),
           "Source POS: %s\nFiles: %u\nUser bytes: %llu\nRequired free: "
           "%llu\nAvailable: %llu\nConflicts go to POS/Recovered.",
           info.source_version, (unsigned)info.files,
           (unsigned long long)info.bytes, (unsigned long long)info.required,
           (unsigned long long)free_space(services->root));
  pos_ui_notice("Restore verified backup", summary);
  const char *options[] = {"Restore user files", "Cancel"};
  if (pos_ui_menu("Restore backup?", options, 2) != 0)
    return false;
  if (pos_backup_restore(services->root, archive, free_space(services->root),
                         &info, pos_ui_progress, NULL, &error)) {
    pos_ui_notice("Restore incomplete", error.message);
    return false;
  } else {
    snprintf(summary, sizeof(summary),
             "Restore verified.\nConflicts preserved: %u\nThe original ZIP is "
             "retained.",
             (unsigned)info.conflicts);
    char marker[POS_FS_PATH];
    pos_fs_join(marker, services->root, "POS/Users/.restore-complete");
    (void)pos_fs_write_new(marker, "1", 1);
    pos_package_t *installed_package = NULL;
    if (!pos_load_manifest(installed, &installed_package, &error)) {
      (void)pos_cleanup_package(services->root, installed_package, &error);
      pos_package_free(installed_package);
    }
    pos_ui_notice("Restore complete", summary);
  }
  return true;
}
static bool backup(services_t *services) {
  pos_backup_info_t info;
  pos_error_t error = {0};
  if (pos_backup_scan(services->root, &info, pos_ui_progress, NULL, &error)) {
    pos_ui_notice("Backup scan failed", error.message);
    return false;
  }
  char summary[300];
  snprintf(summary, sizeof(summary),
           "Would you like to save your filesystem?\nUsing: %llu "
           "bytes\nTemporary space: %llu\nAvailable: %llu\nFiles: %u",
           (unsigned long long)info.bytes,
           (unsigned long long)(info.required + 65536),
           (unsigned long long)free_space(services->root),
           (unsigned)info.files);
  pos_ui_notice("Filesystem backup", summary);
  char partial[POS_FS_PATH];
  struct stat partial_info;
  pos_fs_join(partial, services->root, POS_BACKUP_SET ".part");
  bool resume = !stat(partial, &partial_info);
  const char *options[] = {resume ? "Resume interrupted folder backup"
                           : info.directory_backup
                               ? "Create verified folder backup"
                               : "Create verified ZIP backup",
                           "Create folder (resumable)", "Cancel"};
  int backup_choice = pos_ui_menu("Back up filesystem?", options, 3);
  if (backup_choice < 0 || backup_choice == 2)
    return false;
  int64_t now = time(NULL);
  if (now < 1577836800)
    now = 0;
  char source_version[32];
  pos_recovery_source_version(services->root, source_version);
  int result =
      resume ? pos_backup_set_resume(services->root, source_version,
                                     free_space(services->root), now,
                                     pos_ui_progress, NULL, &error)
             : (backup_choice == 1 ? pos_backup_set_create : pos_backup_create)(
                   services->root, source_version, free_space(services->root),
                   now, pos_ui_progress, NULL, &error);
  if (result) {
    pos_ui_notice("Backup incomplete", error.message);
    return false;
  }
  pos_ui_notice("Backup verified",
                "Power off and remove the SD. Copy the whole "
                "POS-Filesystem-Backup folder or ZIP "
                "to your computer before reinstalling. Keep it beside the "
                "new installer to restore it.");
  return true;
}

static int counter_read(const char *path, int *value) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  char text[40];
  bool read = fgets(text, sizeof(text), f) != NULL;
  int bad = ferror(f);
  if (fclose(f))
    bad = 1;
  if (!read || bad)
    return -1;
  errno = 0;
  char *end = NULL;
  long n = strtol(text, &end, 10);
  if (errno || end == text || (*end && *end != '\n') || n < -1000000 ||
      n > 1000000)
    return -1;
  *value = (int)n;
  return 0;
}
static int counter_init(void *opaque, void *services) {
  counter_t *s = opaque;
  s->services = services;
  char path[POS_FS_PATH];
  pos_fs_join(path, s->services->root, "POS/Users/Owner/Saves/counter.txt");
  if (counter_read(path, &s->count)) {
    pos_fs_join(path, s->services->root,
                "POS/Users/Owner/Saves/counter.previous.txt");
    if (counter_read(path, &s->count))
      s->count = 0;
  }
  return 0;
}
static void counter_event(void *opaque, const pos_event_t *event) {
  counter_t *s = opaque;
  int key = pos_navigation(event);
  if (key == POS_KEY_UP && s->count < 1000000)
    ++s->count;
  if (key == POS_KEY_DOWN && s->count > -1000000)
    --s->count;
}
static int counter_save(void *opaque) {
  counter_t *s = opaque;
  char path[POS_FS_PATH], temp[POS_FS_PATH], text[32];
  pos_fs_join(path, s->services->root, "POS/Users/Owner/Saves/counter.txt");
  pos_fs_join(temp, s->services->root, "POS/Temp/counter.part");
  if (unlink(temp) && errno != ENOENT)
    return -1;
  int n = snprintf(text, sizeof(text), "%d\n", s->count);
  if (pos_fs_write_new(temp, text, (size_t)n))
    return -1;
  int verified;
  if (counter_read(temp, &verified) || verified != s->count)
    return -1;
  /* A toy app save; the prior value stays recoverable in a separate previous
   * file. */
  char old[POS_FS_PATH];
  pos_fs_join(old, s->services->root,
              "POS/Users/Owner/Saves/counter.previous.txt");
  struct stat st;
  if (!stat(path, &st)) {
    if (!stat(old, &st)) {
      if (unlink(old))
        return -1;
    }
    if (rename(path, old))
      return -1;
  }
  return rename(temp, path);
}
static void counter_render(void *opaque, pos_view_t *v) {
  counter_t *s = opaque;
  pos_view_clear(v, "Counter");
  pos_view_line(v, 4, "Count: %d", s->count);
  pos_view_line(v, 7, "W/S changes the value");
  pos_view_line(v, 10, "Saved when the app closes");
  pos_view_line(v, 15, "Esc / Fn+Space: launcher");
}
static const pos_app_t counter_app = {.name = "Counter",
                                      .state_size = sizeof(counter_t),
                                      .initialize = counter_init,
                                      .event = counter_event,
                                      .render = counter_render,
                                      .save = counter_save};

static void info_render(void *opaque, pos_view_t *v) {
  services_t *s = *(services_t **)opaque;
  pos_device_info_t d = pos_device_info();
  uint64_t total = 0, free = 0;
  uint32_t flash = 0;
  esp_vfs_fat_info(s->root, &total, &free);
  esp_flash_get_size(NULL, &flash);
  pos_view_clear(v, "System Information");
  pos_view_line(v, 2, "POS %s", esp_app_get_description()->version);
  pos_view_line(v, 3, "%s", s->settings.device_name);
  pos_view_line(v, 4, "Cardputer ADV / ESP32-S3");
  pos_view_line(v, 5, "Flash:%uK FW:%uK", (unsigned)(flash / 1024),
                (unsigned)(d.firmware_bytes / 1024));
  pos_view_line(v, 6, "Heap: %u min: %u", (unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size());
  pos_view_line(v, 7, "Largest block: %u",
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                           MALLOC_CAP_8BIT));
  pos_view_line(v, 8, "SD: %llu MiB free",
                (unsigned long long)(free / 1048576));
  pos_view_line(v, 9, "SD total: %llu MiB",
                (unsigned long long)(total / 1048576));
  pos_view_line(v, 10, "Battery: %d mV (0=unknown)", pos_device_battery_mv());
  pos_view_line(v, 11, "Boot: %u ms Frame: %u us", (unsigned)d.boot_ms,
                (unsigned)d.frame_us);
  pos_view_line(v, 12, "LCD:%s KB:%s keys:%u", d.display_ok ? "ok" : "ERR",
                d.keyboard_ok ? "ok" : "ERR", (unsigned)d.keys);
  pos_view_line(v, 13, "Wi-Fi: %s",
                d.wifi_connected ? "connected" : "off / offline");
  pos_view_line(v, 14, "Uptime: %llu s",
                (unsigned long long)(esp_timer_get_time() / 1000000));
  pos_view_line(v, 15, "Esc / Fn+Space: launcher");
}
static int service_init(void *state, void *services) {
  *(services_t **)state = services;
  return 0;
}
static const pos_app_t info_app = {.name = "About",
                                   .state_size = sizeof(services_t *),
                                   .initialize = service_init,
                                   .render = info_render};

static void sd_benchmark(services_t *s) {
  char path[POS_FS_PATH], relative[80], scratch[POS_FS_PATH];
  pos_fs_join(scratch, s->root, ".POS-Recovery-Temp");
  if (pos_fs_mkdirs(scratch))
    return;
  snprintf(relative, sizeof(relative), ".POS-Recovery-Temp/bringup-%08x.bin",
           (unsigned)esp_random());
  pos_fs_join(path, s->root, relative);
  FILE *f = fopen(path, "wx");
  unsigned char *buffer = malloc(8192);
  if (!f || !buffer) {
    if (f)
      fclose(f);
    free(buffer);
    pos_ui_notice("SD test",
                  "Could not create test file; existing files preserved.");
    return;
  }
  for (int i = 0; i < 8192; ++i)
    buffer[i] = (unsigned char)i;
  uint32_t crc = 0;
  int bad = 0;
  int64_t start = esp_timer_get_time();
  for (int i = 0; i < 128; ++i) {
    if (fwrite(buffer, 1, 8192, f) != 8192 ||
        pos_ui_progress(NULL, "SD write", relative, (i + 1) * 8192, 1048576)) {
      bad = 1;
      break;
    }
    crc = pos_crc32(crc, buffer, 8192);
  }
  if (fflush(f) || fsync(fileno(f)))
    bad = 1;
  if (fclose(f))
    bad = 1;
  int64_t written = esp_timer_get_time() - start;
  start = esp_timer_get_time();
  uint32_t read_crc = 0;
  if (!bad && pos_fs_crc_file(path, 1048576, &read_crc))
    bad = 1;
  int64_t read = esp_timer_get_time() - start;
  free(buffer);
  if (read_crc != crc)
    bad = 1;
  char summary[300];
  snprintf(summary, sizeof(summary),
           "1 MiB streamed write/read\nIntegrity: %s\nWrite: %llu us\nRead: "
           "%llu us\nMeasured on this device.",
           bad ? "FAILED" : "verified", (unsigned long long)written,
           (unsigned long long)read);
  pos_ui_notice("SD measurement", summary);
  if (!bad) {
    unlink(path);
    char dir[POS_FS_PATH];
    pos_fs_join(dir, s->root, "Diagnostics");
    pos_fs_mkdirs(dir);
    snprintf(relative, sizeof(relative), "Diagnostics/sd-%08x.json",
             (unsigned)esp_random());
    pos_fs_join(path, s->root, relative);
    char json[480];
    snprintf(
        json, sizeof(json),
        "{\"format\":1,\"kind\":\"pos-sd-measurement\",\"measured_on_device\":"
        "true,\"bytes\":1048576,\"crc32\":%u,\"write_us\":%llu,\"read_us\":%"
        "llu,\"heap\":%u,\"minimum_heap\":%u}\n",
        (unsigned)crc, (unsigned long long)written, (unsigned long long)read,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)esp_get_minimum_free_heap_size());
    pos_fs_write_new(path, json, strlen(json));
    printf("POS_MEASUREMENT %s", json);
    measurement(s, "sd-test");
  }
}
static void diagnostics(services_t *s) {
  const char *items[] = {
      "SD streaming / integrity test", "Display / frame timing test",
      "Keyboard event test",           "Save device measurements",
      "100 app open/close cycles",     "Back"};
  for (;;) {
    int choice = pos_ui_menu("Hardware bring-up", items, 6);
    if (choice < 0 || choice == 5)
      return;
    if (choice == 0)
      sd_benchmark(s);
    if (choice == 1) {
      for (int i = 0; i < 8; ++i) {
        pos_view_t v;
        pos_view_clear(&v, "Display calibration");
        v.selected_row = 2 + i;
        pos_view_line(&v, 2, "TOP LEFT  240 x 135");
        pos_view_line(&v, 6, "0123456789 ABCDEFGHIJKLMNOPQRST");
        pos_view_line(&v, 10, "Cycle %d / 8", i + 1);
        pos_view_line(&v, 15, "BOTTOM  Esc / Fn+Space: back");
        pos_device_show(&v);
        vTaskDelay(pdMS_TO_TICKS(120));
      }
      char text[180];
      snprintf(text, sizeof(text),
               "Latest complete frame: %u us\nCheck edges, orientation, colors "
               "and text on the actual screen.",
               (unsigned)pos_device_info().frame_us);
      measurement(s, "display-test");
      pos_ui_notice("Display measurement", text);
    }
    if (choice == 2) {
      pos_device_keyboard_begin();
      pos_ui_message("Keyboard test",
                     "Press all 56 keys, including modifiers. Fn+` exits; "
                     "Fn+Space returns home. Each raw position is recorded.");
      for (;;) {
        pos_event_t e;
        if (!pos_device_event(&e, 100) || !e.pressed)
          continue;
        if (e.key == POS_KEY_ESCAPE || e.key == POS_KEY_HOME)
          break;
        char text[150];
        pos_device_info_t keys = pos_device_info();
        unsigned seen = __builtin_popcount(keys.raw_keys[0]) +
                        __builtin_popcount(keys.raw_keys[1]);
        snprintf(text, sizeof(text),
                 "Key: %d (%c)\nModifiers: %u\nPositions seen: %u/56\nWASD "
                 "remain letters.\nFn+Space: home",
                 e.key, e.key >= 32 && e.key < 127 ? e.key : '?', e.modifiers,
                 seen);
        pos_ui_message("Keyboard events", text);
      }
      measurement(s, "keyboard-test");
    }
    if (choice == 3) {
      measurement(s, "manual-snapshot");
      pos_ui_notice("Measurements saved",
                    "JSON evidence is in /Diagnostics and USB serial output. "
                    "Initialization flags do not prove physical behavior.");
    }
    if (choice == 4) {
      measurement(s, "apps-before");
      pos_app_t app = counter_app;
      app.save = NULL;
      pos_core_t core = {.services = s};
      bool bad = false;
      unsigned completed = 0;
      for (unsigned i = 0; i < 100; ++i) {
        if (pos_ui_progress(NULL, "App lifecycle", "Counter (without saving)",
                            i, 100))
          break;
        if (pos_core_open(&core, &app) || pos_core_close(&core)) {
          bad = true;
          break;
        }
        ++completed;
      }
      (void)pos_core_close(&core);
      measurement(s, completed == 100 ? "apps-after"
                     : bad            ? "apps-failed"
                                      : "apps-cancelled");
      pos_ui_notice(
          "App lifecycle",
          bad ? "A lifecycle failed. Evidence saved; inspect logs."
              : "Measurements saved before and after the run. Compare heap and "
                "stack; foreground cancellation can shorten the run.");
    }
  }
}

static void terminal(services_t *s) {
  for (;;) {
    char command[96] = "";
    if (!pos_ui_text("Recovery terminal", "ls, cat PATH, cp SRC DST, free",
                     command, sizeof(command), false))
      return;
    char *arg = strchr(command, ' ');
    if (arg)
      *arg++ = 0;
    if (!strcmp(command, "ls")) {
      pos_ui_browse(s->root, arg ? arg : "");
      continue;
    }
    if (!strcmp(command, "pwd")) {
      pos_ui_notice("Working directory", "/");
      continue;
    }
    if (!strcmp(command, "free")) {
      char text[120];
      snprintf(text, sizeof(text), "SD free: %llu bytes\nHeap: %u bytes",
               (unsigned long long)free_space(s->root),
               (unsigned)esp_get_free_heap_size());
      pos_ui_notice("free", text);
      continue;
    }
    if (!strcmp(command, "cat") && arg && pos_fs_archive_path(arg)) {
      char path[POS_FS_PATH], text[361];
      pos_fs_join(path, s->root, arg);
      FILE *f = fopen(path, "rb");
      if (!f) {
        pos_ui_notice("cat", "File unavailable");
        continue;
      }
      size_t n = fread(text, 1, 360, f);
      fclose(f);
      text[n] = 0;
      for (size_t i = 0; i < n; ++i)
        if ((unsigned char)text[i] < 32 && text[i] != '\n')
          text[i] = '.';
      pos_ui_notice(arg, text);
      continue;
    }
    if (!strcmp(command, "cp") && arg) {
      char *to = strchr(arg, ' ');
      if (to)
        *to++ = 0;
      if (!to || !pos_fs_archive_path(arg) || !pos_fs_archive_path(to) ||
          !pos_fs_user_path(to)) {
        pos_ui_notice("cp", "Use relative SD paths. Writes to system/reserved "
                            "locations are refused.");
        continue;
      }
      char src[POS_FS_PATH], dest[POS_FS_PATH], temp[POS_FS_PATH],
          scratch[POS_FS_PATH];
      pos_fs_join(src, s->root, arg);
      pos_fs_join(dest, s->root, to);
      pos_fs_join(scratch, s->root, ".POS-Recovery-Temp");
      if (pos_fs_mkdirs(scratch))
        continue;
      pos_fs_join(temp, s->root, ".POS-Recovery-Temp/recovery-copy.part");
      struct stat st;
      if (!stat(dest, &st)) {
        pos_ui_notice("cp", "Destination exists; it was preserved.");
        continue;
      }
      FILE *in = fopen(src, "rb");
      if (!in)
        continue;
      if (unlink(temp) && errno != ENOENT) {
        fclose(in);
        continue;
      }
      FILE *out = fopen(temp, "wx");
      unsigned char *buffer = malloc(8192);
      int bad = !out || !buffer;
      uint64_t total = 0;
      uint32_t crc = 0;
      size_t n;
      while (!bad && (n = fread(buffer, 1, 8192, in))) {
        if (fwrite(buffer, 1, n, out) != n)
          bad = 1;
        crc = pos_crc32(crc, buffer, n);
        total += n;
        if (total > UINT32_MAX ||
            pos_ui_progress(NULL, "File recovery", arg, total, 0))
          bad = 1;
      }
      if (ferror(in))
        bad = 1;
      fclose(in);
      if (out) {
        if (fflush(out) || fsync(fileno(out)))
          bad = 1;
        if (fclose(out))
          bad = 1;
      }
      free(buffer);
      uint32_t actual;
      if (!bad &&
          (pos_fs_crc_file(temp, (uint32_t)total, &actual) || actual != crc))
        bad = 1;
      char parent[POS_FS_PATH];
      strcpy(parent, dest);
      char *slash = strrchr(parent, '/');
      if (slash)
        *slash = 0;
      if (!bad && (pos_fs_mkdirs(parent) || rename(temp, dest)))
        bad = 1;
      pos_ui_notice("cp", bad ? "Copy incomplete; source preserved."
                              : "Copied and verified. Source preserved.");
      continue;
    }
    if (!strcmp(command, "reboot"))
      esp_restart();
    if (!strcmp(command, "shutdown"))
      pos_device_sleep();
    pos_ui_notice("Commands",
                  "ls [PATH], pwd, cat PATH, cp SRC DST, free, reboot, "
                  "shutdown. Paths are relative to the SD root.");
  }
}

static void recovery(services_t *s) {
  const char *items[] = {"Repair / reinstall (factory)",
                         "Back up user filesystem",
                         "Restore verified backup",
                         "Browse previous installation",
                         "File recovery terminal",
                         "Hardware diagnostics",
                         "Sleep / shut down",
                         "Back"};
  for (;;) {
    int choice = pos_ui_menu("POS Recovery", items, 8);
    if (choice < 0 || choice == 7)
      return;
    if (choice == 0) {
      const char *yes[] = {"Restart into factory installer", "Cancel"};
      if (pos_ui_menu("Keep files; enter installer?", yes, 2) == 0)
        factory();
    }
    if (choice == 1)
      backup(s);
    if (choice == 2)
      restore(s);
    if (choice == 3)
      pos_ui_browse(s->root, "POS.previous");
    if (choice == 4)
      terminal(s);
    if (choice == 5)
      diagnostics(s);
    if (choice == 6)
      pos_device_sleep();
  }
}

static void settings_foreground(void *state) {
  setup(*(services_t **)state, false);
}
static void diagnostics_foreground(void *state) {
  diagnostics(*(services_t **)state);
}
static void files_foreground(void *state) {
  pos_ui_browse((*(services_t **)state)->root, "POS/Users/Owner");
}
static void background_foreground(void *state) {
  services_t *services = *(services_t **)state;
  (void)pos_ui_background(services->root, &services->settings);
}
static void recovery_foreground(void *state) {
  recovery(*(services_t **)state);
}
static void modal_render(void *state, pos_view_t *view) {
  (void)state;
  pos_view_clear(view, "Launcher");
}
static const pos_app_t modal_apps[] = {{.name = "Settings",
                                        .state_size = sizeof(services_t *),
                                        .initialize = service_init,
                                        .foreground = settings_foreground,
                                        .render = modal_render},
                                       {.name = "Diagnostics",
                                        .state_size = sizeof(services_t *),
                                        .initialize = service_init,
                                        .foreground = diagnostics_foreground,
                                        .render = modal_render},
                                       {.name = "Files",
                                        .state_size = sizeof(services_t *),
                                        .initialize = service_init,
                                        .foreground = files_foreground,
                                        .render = modal_render},
                                       {.name = "Recovery",
                                        .state_size = sizeof(services_t *),
                                        .initialize = service_init,
                                        .foreground = recovery_foreground,
                                        .render = modal_render},
                                       {.name = "Background",
                                        .state_size = sizeof(services_t *),
                                        .initialize = service_init,
                                        .foreground = background_foreground,
                                        .render = modal_render}};

void pos_desktop(const char *root, bool recovery_only) {
  (void)pos_ui_take_home();
  services_t services = {.root = root};
  pos_settings_defaults(&services.settings);
  int settings = pos_settings_load(root, &services.settings);
  if (recovery_only) {
    recovery(&services);
    return;
  }
  if (pos_fs_layout(root)) {
    pos_ui_notice("User folders unavailable",
                  "Storage may be full or unreadable. Recovery will preserve "
                  "existing files.");
    recovery_only = true;
  }
  if (recovery_only) {
    recovery(&services);
    return;
  }
  /* Offer backup restoration before first-time setup can create conflicting
   * preferences. */
  char archive[POS_FS_PATH], journal[POS_FS_PATH];
  struct stat st;
  pos_error_t find_error = {0};
  int found = pos_backup_find(root, archive, &find_error);
  pos_fs_join(journal, root, "POS/Users/.restore-dismissed");
  if (!found && stat(journal, &st)) {
    const char *options[] = {"Restore backup now", "Later (keep backup)"};
    int answer = pos_ui_menu("Filesystem backup detected", options, 2);
    bool restored = answer == 0 && restore(&services);
    if (answer == 1 || restored)
      pos_fs_write_new(journal, "1", 1);
    settings = pos_settings_load(root, &services.settings);
  }
  if (settings == -2)
    pos_ui_notice("Newer settings preserved",
                  "Settings use an unsupported future format. Recovery can "
                  "copy them; current defaults are temporary.");
  else if (settings || !services.settings.setup_complete)
    setup(&services, true);
  setenv("TZ", services.settings.timezone, 1);
  tzset();
  pos_device_brightness(services.settings.brightness);
  (void)pos_ui_take_home();
  pos_core_t core = {.services = &services};
  int selected = 0;
  uint64_t refreshed = 0;
  char system_root[POS_FS_PATH];
  pos_fs_join(system_root, root, "POS");
  for (;;) {
    pos_event_t event;
    if (pos_device_event(&event, 30)) {
      if (core.app) {
        if (pos_core_event(&core, &event))
          pos_ui_notice("App save failed",
                        "The app closed, but its state could not be saved. "
                        "Existing saved files are retained.");
      } else {
        int key = pos_launcher_action(&event);
        if (key == POS_KEY_RIGHT)
          selected = (selected + 1) % POS_LAUNCHER_APP_COUNT;
        if (key == POS_KEY_LEFT)
          selected = (selected + POS_LAUNCHER_APP_COUNT - 1) % POS_LAUNCHER_APP_COUNT;
        if (key == POS_KEY_ENTER) {
          if (selected == 0) {
            if (pos_core_open(&core, &counter_app))
              pos_ui_notice("App unavailable",
                            "Could not allocate or initialize the app.");
          }
          if ((selected >= 1 && selected <= 4) || selected == 6) {
            int modal = selected == 6 ? 4 : selected - 1;
            if (pos_core_open(&core, &modal_apps[modal]))
              pos_ui_notice("App unavailable", "Initialization failed.");
            else if (pos_core_close(&core))
              pos_ui_notice("App save failed",
                            "Existing saved files retained.");
            (void)pos_ui_take_home();
          }
          if (selected == 5)
            pos_core_open(&core, &info_app);
        }
      }
      (void)pos_ui_take_home();
      refreshed = 0;
    }
    uint64_t now = esp_timer_get_time() / 1000;
    if (now - refreshed < 250 && refreshed)
      continue;
    refreshed = now;
    pos_view_t view;
    if (core.app)
      pos_core_tick(&core, now, &view);
    else {
      pos_launcher_view(system_root, services.settings.device_name,
                        &services.settings.background, selected, &view);
    }
    pos_device_show(&view);
  }
}
