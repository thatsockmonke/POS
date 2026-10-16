#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "pos_device.h"
#include "pos_fs.h"
#include "pos_health.h"
#include "pos_install.h"
#include "pos_recovery.h"
#include "pos_update_service.h"
#include "sdmmc_cmd.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SD_ROOT "/sdcard"
/* Published M5Stack Cardputer SD example. ADV profile requires physical
   validation. Separate SPI3 host leaves SPI2 available for the future display
   driver. */
#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12

static const char *TAG = "POS";

static void idle(void) {
  for (;;) {
    pos_event_t event;
    if (pos_device_event(&event, 1000) && event.pressed &&
        (event.key == POS_KEY_HOME || event.key == POS_KEY_ENTER)) {
      (void)pos_ui_take_home();
      const char *items[] = {"Restart", "Sleep / shut down", "Back"};
      int choice = pos_ui_menu("POS system menu", items, 3);
      if (choice == 0)
        esp_restart();
      if (choice == 1)
        pos_device_sleep();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#ifdef POS_INSTALLER
static void read_line(char *line, size_t capacity, const char *word) {
  line[0] = 0;
  char label[40];
  snprintf(label, sizeof(label), "Type %s to continue", word);
  if (!pos_ui_text("Confirm installation", label, line, capacity, false))
    line[0] = 0;
}
#endif

static void heap_report(void) {
  ESP_LOGI(TAG, "Heap: current=%u minimum=%u largest internal block=%u bytes",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT));
}

static esp_err_t mount_sd(void) {
  /* Cap LoRa shares SPI3 with SD. Keep its NSS inactive during SD init. */
  gpio_config_t cap_cs = {.pin_bit_mask = 1ULL << 5, .mode = GPIO_MODE_OUTPUT};
  gpio_config(&cap_cs);
  gpio_set_level(5, 1);
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 10000;
  spi_bus_config_t bus = {
      .mosi_io_num = SD_MOSI,
      .miso_io_num = SD_MISO,
      .sclk_io_num = SD_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = POS_COPY_BUFFER,
  };
  esp_err_t err = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
  if (err != ESP_OK)
    return err;
  sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
  device.host_id = host.slot;
  device.gpio_cs = SD_CS;
  esp_vfs_fat_sdmmc_mount_config_t mount = {
      .format_if_mount_failed = false,
      .max_files = 10,
      .allocation_unit_size = 32768,
  };
  sdmmc_card_t *card = NULL;
  err = esp_vfs_fat_sdspi_mount(SD_ROOT, &host, &device, &mount, &card);
  if (err != ESP_OK) {
    spi_bus_free(host.slot);
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND)
      ESP_LOGE(TAG, "No microSD response: check card presence, contacts, "
                    "wiring and ADV revision");
    else
      ESP_LOGE(TAG,
               "Unreadable/corrupt card, unsupported filesystem, or SD I/O "
               "error: %s. Nothing formatted",
               esp_err_to_name(err));
    return err;
  }
  sdmmc_card_print_info(stdout, card);
  ESP_LOGI(TAG, "storage...ok (FAT; formatting is disabled)");
  return ESP_OK;
}

static bool digest_matches(mbedtls_sha256_context *hash, const char *expected) {
  unsigned char bytes[32];
  char hex[65];
  if (mbedtls_sha256_finish(hash, bytes))
    return false;
  for (size_t i = 0; i < 32; ++i)
    snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  return !strcmp(hex, expected);
}

static int verify_flash(const esp_partition_t *partition,
                        const pos_package_t *package, bool show_progress) {
  if (!partition || package->firmware.size > partition->size)
    return -1;
  unsigned char *buffer = malloc(POS_COPY_BUFFER);
  if (!buffer)
    return -1;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int result = mbedtls_sha256_starts(&hash, 0);
  for (size_t offset = 0; !result && offset < package->firmware.size;) {
    size_t bytes = package->firmware.size - offset;
    if (bytes > POS_COPY_BUFFER)
      bytes = POS_COPY_BUFFER;
    if (esp_partition_read(partition, offset, buffer, bytes) != ESP_OK ||
        mbedtls_sha256_update(&hash, buffer, bytes))
      result = -1;
    offset += bytes;
    if (!result && show_progress &&
        pos_ui_progress(NULL, "firmware_verify", "Runtime flash readback",
                        offset, package->firmware.size))
      result = -1;
  }
  if (!digest_matches(&hash, package->firmware.sha256))
    result = -1;
  mbedtls_sha256_free(&hash);
  free(buffer);
  return result;
}

typedef struct {
  const esp_partition_t *target;
  bool allow_runtime;
} installer_context_t;

static int free_space(void *context, const char *root, uint64_t *available) {
  (void)context;
  uint64_t total;
  return esp_vfs_fat_info(root, &total, available) == ESP_OK ? 0 : -1;
}

static int check_firmware(void *opaque, const char *path,
                          const pos_package_t *package) {
  installer_context_t *context = opaque;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running || (!context->allow_runtime &&
                   running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY))
    return -1;
  context->target = esp_ota_get_next_update_partition(NULL);
  if (!context->target || package->firmware.size > context->target->size)
    return -1;
  FILE *file = fopen(path, "rb");
  if (!file)
    return -1;
  esp_image_header_t image;
  esp_image_segment_header_t segment;
  esp_app_desc_t app;
  bool ok = fread(&image, 1, sizeof(image), file) == sizeof(image) &&
            fread(&segment, 1, sizeof(segment), file) == sizeof(segment) &&
            fread(&app, 1, sizeof(app), file) == sizeof(app);
  if (fclose(file))
    ok = false;
  return ok && image.magic == ESP_IMAGE_HEADER_MAGIC &&
                 image.chip_id == ESP_CHIP_ID_ESP32S3 &&
                 segment.data_len >= sizeof(app) &&
                 app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
                 memchr(app.project_name, 0, sizeof(app.project_name)) &&
                 !strcmp(app.project_name, "pos_runtime") &&
                 memchr(app.version, 0, sizeof(app.version)) &&
                 !strcmp(app.version, package->version)
             ? 0
             : -1;
}

static int write_firmware(void *opaque, const char *path,
                          const pos_package_t *package) {
  installer_context_t *context = opaque;
  if (check_firmware(opaque, path, package))
    return -1;
  FILE *file = fopen(path, "rb");
  unsigned char *buffer = malloc(POS_COPY_BUFFER);
  if (!file || !buffer) {
    if (file)
      fclose(file);
    free(buffer);
    return -1;
  }
  esp_ota_handle_t handle;
  esp_err_t err =
      esp_ota_begin(context->target, package->firmware.size, &handle);
  if (err != ESP_OK) {
    fclose(file);
    free(buffer);
    return -1;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int result = mbedtls_sha256_starts(&hash, 0);
  size_t n, total = 0;
  while (!result && (n = fread(buffer, 1, POS_COPY_BUFFER, file)) != 0) {
    total += n;
    if (total > package->firmware.size ||
        mbedtls_sha256_update(&hash, buffer, n) ||
        esp_ota_write(handle, buffer, n) != ESP_OK)
      result = -1;
    if (pos_ui_progress(NULL, "firmware_write", "Inactive runtime slot", total,
                        package->firmware.size))
      result = -1;
    vTaskDelay(1);
  }
  if (ferror(file) || total != package->firmware.size ||
      !digest_matches(&hash, package->firmware.sha256))
    result = -1;
  if (fclose(file))
    result = -1;
  mbedtls_sha256_free(&hash);
  if (result)
    esp_ota_abort(handle);
  else if (esp_ota_end(handle) != ESP_OK)
    result = -1; /* IDF validates chip, image checksum/hash and revision */
  /* Independently hash exactly the packaged byte range from flash before
   * selecting it. */
  free(buffer);
  if (!result)
    result = verify_flash(context->target, package, true);
  return result;
}

static int select_firmware(void *opaque) {
  installer_context_t *context = opaque;
  return esp_ota_set_boot_partition(context->target) == ESP_OK ? 0 : -1;
}

#ifndef POS_INSTALLER
int pos_device_install_update(const char *root, const pos_update_t *u,
                              pos_error_t *error) {
  installer_context_t context = {.allow_runtime = true};
  pos_update_platform_t hooks = {
      .fetch = pos_update_fetch,
      .platform = {.ctx = &context,
                   .free_bytes = free_space,
                   .write_firmware = write_firmware,
                   .select_firmware = select_firmware,
                   .progress = pos_ui_progress}};
  if (pos_update_apply(root, u, &hooks, error))
    return -1;
  if (u->firmware) {
    pos_ui_notice("Firmware update ready",
                  "System and inactive firmware are verified. Restarting to "
                  "test the new runtime. Keep the microSD inserted. It must "
                  "run reliably for 60 seconds before OTA confirmation.");
    pos_health_clear();
    esp_restart();
    return 0;
  }
  return pos_update_service_confirm(root, error);
}
#endif

#ifdef POS_INSTALLER
static void progress(void *context, const char *phase, const char *detail) {
  (void)context;
  ESP_LOGI(TAG, "%s: %s", phase, detail);
  const char *message = detail;
  if (!strcmp(phase, "validated"))
    message = "Package checks passed. Preparing the SD system.";
  if (!strcmp(phase, "staged"))
    message = "All staged SD files passed verification.";
  if (!strcmp(phase, "sd_committed"))
    message = "SD system ready. Writing runtime firmware next.";
  if (!strcmp(phase, "firmware_written"))
    message = "Runtime firmware passed flash verification.";
  if (!strcmp(phase, "boot_selected"))
    message = "Power off and back on. Keep the SD card inserted.";
  pos_ui_message(pos_ui_phase_title(phase), message);
}

static void boot_existing(void) {
  pos_package_t *installed = NULL;
  pos_error_t error = {0};
  if (pos_load_manifest(SD_ROOT "/POS/System/installed.pos", &installed,
                        &error) ||
      pos_validate_system(SD_ROOT, installed, &error)) {
    pos_package_free(installed);
    pos_ui_notice("Cannot boot POS", error.message);
    return;
  }
  const esp_partition_t *match = NULL;
  for (int slot = 0; slot < 2; ++slot) {
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
    if (!verify_flash(p, installed, true)) {
      match = p;
      break;
    }
  }
  pos_package_free(installed);
  if (!match || esp_ota_set_boot_partition(match) != ESP_OK) {
    pos_ui_notice(
        "Cannot boot POS",
        "No verified matching runtime slot. Use repair or USB recovery.");
    return;
  }
  pos_ui_notice("Runtime selected",
                "Power off, then power on. Your files were preserved.");
  idle();
}

static void installer(void) {
  char title[POS_COLS + 1];
  snprintf(title, sizeof(title), "POS %.16s Installer",
           esp_app_get_description()->version);
  const char *choices[] = {"Boot existing POS",
                           "Install POS / resume",
                           "Repair from original package",
                           "Back up & reinstall",
                           "Recovery / file tools",
                           "Sleep / shut down"};
  for (;;) {
    (void)pos_ui_take_home();
    int choice = pos_ui_menu(title, choices, 6);
    if (choice < 0)
      continue;
    if (choice == 0) {
      boot_existing();
      continue;
    }
    if (choice == 4) {
      pos_desktop(SD_ROOT, true);
      continue;
    }
    if (choice == 5)
      pos_device_sleep();
    pos_package_t *package = NULL;
    pos_error_t error = {0};
    if (pos_load_manifest(SD_ROOT "/pos-install/manifest.pos", &package,
                          &error)) {
      pos_ui_notice("Package unavailable", error.message);
      continue;
    }
    installer_context_t context = {0};
    pos_platform_t platform = {.ctx = &context,
                               .free_bytes = free_space,
                               .check_firmware = check_firmware,
                               .write_firmware = write_firmware,
                               .select_firmware = select_firmware,
                               .report = progress,
                               .progress = pos_ui_progress};
    int result = choice == 1
                     ? pos_preflight(SD_ROOT, package, &platform, &error)
                     : pos_check_package(SD_ROOT, package, &platform, &error);
    if (result)
      goto failed;
    if (choice == 3) {
      char journal[POS_FS_PATH], backup[POS_FS_PATH];
      struct stat st;
      pos_fs_join(journal, SD_ROOT, "pos-reinstall.pos");
      bool backup_found = !pos_backup_find(SD_ROOT, backup, &error);
      if (!backup_found) {
        char other[POS_FS_PATH];
        pos_fs_join(other, SD_ROOT, POS_BACKUP_SET);
        if (!stat(other, &st) || !stat(SD_ROOT "/" POS_BACKUP_NAME, &st))
          goto failed;
        error.message[0] = 0;
      }
      if (stat(journal, &st)) {
        pos_backup_info_t info;
        if (pos_backup_scan(SD_ROOT, &info, pos_ui_progress, NULL, &error))
          goto failed;
        uint64_t available = 0;
        free_space(NULL, SD_ROOT, &available);
        char summary[300];
        snprintf(summary, sizeof(summary),
                 "Save your filesystem before reinstalling.\nUsing: %llu "
                 "bytes\nTemporary: %llu bytes\nAvailable: %llu bytes\nFiles: "
                 "%u\nOld POS will be retained.",
                 (unsigned long long)info.bytes,
                 (unsigned long long)(info.required + 65536),
                 (unsigned long long)available, (unsigned)info.files);
        pos_ui_notice("Back Up & Reinstall", summary);
        if (backup_found) {
          if (pos_backup_matches_progress(SD_ROOT, backup, pos_ui_progress,
                                          NULL, &error))
            goto failed;
        } else {
          int64_t now = time(NULL);
          if (now < 1577836800)
            now = 0;
          const char *confirm[] = {"Create verified backup", "Cancel"};
          if (pos_ui_menu("Save filesystem?", confirm, 2) != 0)
            goto done;
          char source_version[32];
          pos_recovery_source_version(SD_ROOT, source_version);
          char partial[POS_FS_PATH];
          pos_fs_join(partial, SD_ROOT, POS_BACKUP_SET ".part");
          bool resume = !stat(partial, &st);
          if ((resume ? pos_backup_set_resume
                      : pos_backup_create)(SD_ROOT, source_version, available,
                                           now, pos_ui_progress, NULL, &error))
            goto failed;
        }
        pos_ui_notice(
            "Backup verified",
            "Keep this backup. Power off and copy it to your computer first if "
            "you have not already. Continue only when ready to reinstall.");
      }
    }
    const char *word = choice == 1   ? "INSTALL"
                       : choice == 2 ? "REPAIR"
                                     : "REINSTALL";
    char summary[220];
    snprintf(summary, sizeof(summary),
             "Unsigned POS %s\n%s\nNo card formatting.\nType %s to "
             "continue.\nAny other response cancels.",
             package->version, POS_MODEL, word);
    pos_ui_notice("Ready", summary);
    char command[32];
    read_line(command, sizeof(command), word);
    if (strcmp(command, word))
      goto done;
    if (choice == 2)
      result = pos_repair_system(SD_ROOT, package, &platform, &error);
    else {
      if (choice == 3 &&
          pos_reinstall_prepare(SD_ROOT, package, &platform, &error))
        goto failed;
      result = pos_install(SD_ROOT, package, &platform, &error);
    }
    if (result)
      goto failed;
    pos_package_free(package);
    pos_ui_install_complete();
    idle();
  failed:
    pos_ui_notice("Operation stopped", error.message);
  done:
    pos_package_free(package);
  }
}

#else
static void select_recovery(void) {
  const esp_partition_t *factory = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (!factory || esp_ota_set_boot_partition(factory) != ESP_OK)
    ESP_LOGE(TAG, "Cannot select factory installer. USB recovery is required");
  else
    ESP_LOGW(TAG, "Factory installer selected for next boot. Preserve the SD "
                  "and restart when ready");
}

static int confirm_updated_boot(void) {
  pos_package_t *p = NULL;
  pos_error_t e = {0};
  if (pos_load_manifest(SD_ROOT "/POS/System/installed.pos", &p, &e))
    return -1;
  int bad = strcmp(p->version, esp_app_get_description()->version) ||
            pos_validate_system(SD_ROOT, p, &e) ||
            verify_flash(esp_ota_get_running_partition(), p, false);
  if (!bad) {
    esp_ota_img_states_t state;
    esp_err_t result =
        esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    if (result != ESP_OK)
      bad = 1;
    else if (state == ESP_OTA_IMG_PENDING_VERIFY &&
             esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
      bad = 1;
    else if (state != ESP_OTA_IMG_PENDING_VERIFY && state != ESP_OTA_IMG_VALID)
      bad = 1;
  }
  if (!bad)
    bad = pos_confirm_system(SD_ROOT, p, &e) ||
          pos_update_service_confirm(SD_ROOT, &e);
  pos_package_free(p);
  return bad ? -1 : 0;
}

static void runtime(void) {
  pos_package_t *package = NULL;
  pos_error_t error = {0};
  int update_state =
      pos_update_recover(SD_ROOT, esp_app_get_description()->version, &error);
  if (update_state == 2) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
    select_recovery();
    return;
  }
  if (update_state < 0) {
    pos_ui_notice("Update recovery", error.message);
    select_recovery();
    pos_desktop(SD_ROOT, true);
    return;
  }

  int invalid =
      pos_load_manifest(SD_ROOT "/POS/System/installed.pos", &package, &error);
  if (!invalid &&
      strcmp(package->version, esp_app_get_description()->version)) {
    snprintf(error.message, sizeof(error.message),
             "SD system version does not match running firmware");
    invalid = -1;
  }
  if (!invalid)
    invalid = pos_validate_system(SD_ROOT, package, &error);
  if (!invalid &&
      verify_flash(esp_ota_get_running_partition(), package, false)) {
    snprintf(error.message, sizeof(error.message),
             "Running flash bytes do not match SD manifest");
    invalid = -1;
  }
  if (invalid) {
    ESP_LOGE(TAG, "POS Boot Manager: system validation failed: %s",
             error.message);
    pos_package_free(package);
    pos_ui_notice("POS Boot Manager", error.message);
    select_recovery();
    pos_desktop(SD_ROOT, true);
    esp_restart();
  }
  ESP_LOGI(TAG, "system files...ok / firmware version...ok");
  heap_report();
  esp_ota_img_states_t state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_err_t err = esp_ota_get_state_partition(running, &state);
  if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY &&
      update_state != 1) {
    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
      err = esp_ota_get_state_partition(running, &state);
  }
  if (err != ESP_OK ||
      (state != ESP_OTA_IMG_VALID &&
       !(update_state == 1 && state == ESP_OTA_IMG_PENDING_VERIFY))) {
    ESP_LOGE(TAG, "Cannot confirm firmware boot; installer payload retained");
    pos_package_free(package);
    return;
  }
  /* Only after the complete SD validation and IDF boot confirmation may cleanup
   * run. */
  if (update_state != 1 && pos_confirm_system(SD_ROOT, package, &error))
    pos_ui_notice("Boot marker failed", error.message);
  else if (update_state != 1) {
    struct stat backup;
    if (stat(SD_ROOT "/" POS_BACKUP_NAME, &backup) &&
        stat(SD_ROOT "/" POS_BACKUP_SET, &backup) &&
        pos_cleanup_package(SD_ROOT, package, &error))
      ESP_LOGW(TAG, "%s", error.message);
  }
  if (update_state == 1)
    pos_health_set_confirm_hook(confirm_updated_boot);
  bool safe_boot = pos_device_safe_boot();
  pos_device_boot_ready(package->firmware.size);
  pos_package_free(package);
  while (safe_boot) {
    const char *items[] = {"Open recovery tools", "Try normal boot",
                           "Factory installer", "Restart"};
    int choice = pos_ui_menu("POS Safe Mode", items, 4);
    if (choice == 0)
      pos_desktop(SD_ROOT, true);
    if (choice == 1) {
      if (pos_health_resume())
        pos_ui_notice("Safe mode retained",
                      "Could not save boot policy in NVS. Existing user data "
                      "has not changed.");
      else
        break;
    }
    if (choice == 2)
      select_recovery();
    if (choice == 3)
      esp_restart();
  }
  pos_desktop(SD_ROOT, false);
}
#endif

void app_main(void) {
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("POS_BUILD %s %s\n", esp_app_get_description()->project_name,
         esp_app_get_description()->version);
  ESP_LOGI(TAG, "POS / booting... / %s / %s",
           esp_app_get_description()->project_name,
           esp_app_get_description()->version);
  pos_health_begin(
#ifdef POS_INSTALLER
      false
#else
      true
#endif
  );
  if (pos_device_init()) {
    ESP_LOGE(TAG, "Cannot initialize input/display services");
    idle();
  }
  pos_device_boot_flash();
  pos_device_info_t device = pos_device_info();
  char boot[220];
  snprintf(boot, sizeof(boot),
           "POS / booting...\ndisplay...%s\nkeyboard...%s\nstorage...waiting",
           device.display_ok ? "ok" : "serial fallback",
           device.keyboard_ok ? "ok" : "USB fallback");
  pos_ui_message("PANPUTER", boot);
  uint32_t flash_size = 0;
  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK ||
      flash_size < 0x400000) {
    ESP_LOGE(TAG, "Unsupported flash capacity; this development layout "
                  "requires at least 4 MiB");
    idle();
  }
  ESP_LOGI(
      TAG,
      "Physical flash: %u bytes; development partition layout uses first 4 MiB",
      (unsigned)flash_size);
  heap_report();
  while (mount_sd() != ESP_OK) {
    (void)pos_ui_take_home();
    const char *options[] = {"Retry SD mount", "Sleep / shut down"};
    pos_ui_notice("POS Boot Manager",
                  "No bootable POS system drive found. Insert a readable FAT32 "
                  "microSD. No card has been formatted.");
    if (pos_ui_menu("Storage unavailable", options, 2) == 1)
      pos_device_sleep();
  }
#ifdef POS_INSTALLER
  pos_health_export(SD_ROOT);
  installer();
#else
  pos_health_export(SD_ROOT);
  runtime();
#endif
  idle();
}
