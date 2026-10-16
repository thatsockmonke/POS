#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "pos_device.h"
#include "pos_network_service.h"
#include "pos_update_service.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>
extern int pos_device_install_update(const char *, const pos_update_t *,
                                     pos_error_t *);
static int downloading(void *, const char *, uint64_t, uint64_t);
int pos_update_fetch(void *ctx, const char *url, const char *root,
                     const char *name, uint32_t limit, const char *sha,
                     pos_error_t *e) {
  (void)ctx;
  return pos_network_https_limit(url, "GET", NULL, NULL, root, name, sha,
                                 downloading, NULL, e, limit);
}
static int downloading(void *ctx, const char *phase, uint64_t done,
                       uint64_t total) {
  return pos_ui_progress(ctx, phase, "Update download", done, total);
}
static int manifest_download(const char *url, const char *root,
                             const char *name, pos_error_t *e) {
  return pos_network_https_limit(url, "GET", NULL, NULL, root, name, NULL,
                                 downloading, NULL, e, POS_UPDATE_MAX);
}
static void source(const char *root, pos_update_source_t *s) {
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Source and trusted key", "Set HTTPS manifest URL",
                           "Import signing public key", "Back"};
    int choice = pos_ui_menu_from("Update source", items, 4, selected);
    if (choice < 0 || choice == 3)
      return;
    selected = choice;
    if (choice == 0) {
      char fingerprint[65] = "not configured", body[800];
      if (*s->key)
        pos_update_key_fingerprint(s->key, fingerprint);
      snprintf(body, sizeof(body),
               "Manifest: %s\nTrusted key SHA-256 (DER SPKI): %s\nAccepted "
               "sequence: %u\nOnly HTTPS with valid certificates and trusted "
               "P-256 signatures is accepted.",
               *s->url ? s->url : "not configured", fingerprint,
               (unsigned)s->sequence);
      pos_ui_notice("Update trust", body);
    }
    if (choice == 1) {
      char url[512];
      strcpy(url, s->url);
      if (pos_ui_text("Update source", "HTTPS manifest URL", url, sizeof(url),
                      false)) {
        if (!pos_https_url_valid(url))
          pos_ui_notice(
              "Invalid source",
              "Enter a complete HTTPS URL without embedded credentials.");
        else {
          pos_update_source_t next = *s;
          strcpy(next.url, url);
          if (pos_update_source_save(&next))
            pos_ui_notice("Source not saved",
                          "Existing source remains selected.");
          else
            *s = next;
        }
      }
    }
    if (choice == 2) {
      char relative[241], full[POS_FS_PATH];
      if (!pos_ui_pick_file_from(root, "Signing public key", "Downloads",
                                 relative) ||
          pos_fs_join(full, root, relative))
        continue;
      FILE *f = fopen(full, "rb");
      if (!f) {
        pos_ui_notice(
            "Key unavailable",
            "Choose a PEM P-256 public key copied from a trusted publisher.");
        continue;
      }
      char pem[514];
      size_t n = fread(pem, 1, sizeof(pem) - 1, f);
      int bad = ferror(f);
      fclose(f);
      pem[n] = 0;
      char fingerprint[65];
      if (bad || n > 512 || memchr(pem, 0, n) ||
          pos_update_key_fingerprint(pem, fingerprint)) {
        pos_ui_notice("Invalid public key",
                      "Choose a P-256 public key in PEM format (at most 512 "
                      "bytes). Private keys are never installed.");
        continue;
      }
      pos_ui_notice("Verify signing key", fingerprint);
      const char *options[] = {"Cancel", "Fingerprint verified: trust key"};
      if (pos_ui_menu("Compare with publisher separately", options, 2) == 1) {
        pos_update_source_t next = *s;
        char previous[65];
        if (!*s->key || pos_update_key_fingerprint(s->key, previous) ||
            strcmp(previous, fingerprint))
          next.sequence = 0;
        strcpy(next.key, pem);
        if (pos_update_source_save(&next))
          pos_ui_notice("Key not saved",
                        "Previous trusted key remains selected.");
        else
          *s = next;
      }
    }
  }
}
static bool install_url(const char *root, const char *url,
                        pos_update_source_t *s) {
  char parent[POS_FS_PATH], folder[80], full[POS_FS_PATH];
  snprintf(folder, sizeof(folder), "POS/Updates/check-%08x",
           (unsigned)esp_random());
  if (pos_fs_join(parent, root, folder) || pos_fs_mkdirs(parent) ||
      pos_fs_join(full, parent, "manifest.json"))
    return false;
  pos_error_t error = {0};
  if (manifest_download(url, parent, "manifest.json", &error)) {
    pos_ui_notice("Update check failed", error.message);
    return false;
  }
  pos_update_t *u = NULL;
  int result = pos_update_load(full, s->key, esp_app_get_description()->version,
                               s->sequence, &u, &error);
  if (result) {
    pos_ui_notice(result == 1    ? "No new compatible update"
                  : result == -2 ? "Clean installation required"
                                 : "Update rejected",
                  error.message);
    return false;
  }
  uint64_t size = u->firmware ? u->package->firmware.size : 0;
  for (size_t i = 0; i < u->package->count; ++i)
    size += u->package->files[i].size;
  char body[350];
  snprintf(body, sizeof(body),
           "Release: %s\nType: %s\nMinimum version: %s\nSystem files: "
           "%u\nMaximum download: %llu bytes\nSignature verified. Existing "
           "user files stay in place; previous System is retained. Firmware "
           "updates restart into the inactive OTA slot.",
           u->release, u->firmware ? "Firmware + SD system" : "SD assets only",
           u->minimum, (unsigned)u->package->count, (unsigned long long)size);
  pos_ui_notice("Update available", body);
  const char *options[] = {"Cancel", "Download and install verified update"};
  bool success = false;
  if (pos_ui_menu("Install this signed update?", options, 2) == 1) {
    if (pos_device_install_update(root, u, &error))
      pos_ui_notice("Update not completed", error.message);
    else {
      pos_ui_notice("Asset update installed",
                    "System files verified. No firmware was flashed. "
                    "Previous System remains under POS/Updates.");
      pos_update_source_load(s);
      success = true;
    }
  }
  pos_update_free(u);
  return success;
}
void pos_ui_updates(const char *root) {
  pos_update_source_t *s = calloc(1, sizeof(*s));
  if (!s)
    return;
  if (pos_update_source_load(s)) {
    pos_ui_notice("Update settings unavailable",
                  "Existing update settings were preserved. Check internal "
                  "NVS; do not erase it to bypass this error.");
    free(s);
    return;
  }
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Check for Updates",
                           "Configure source / trusted key",
                           "Inspect downloaded update files", "Back"};
    int choice = pos_ui_menu_from("Software Update", items, 4, selected);
    if (choice < 0 || choice == 3)
      break;
    selected = choice;
    if (choice == 1)
      source(root, s);
    if (choice == 2)
      pos_ui_browse(root, "POS/Updates");
    if (choice != 0)
      continue;
    if (!*s->url || !*s->key) {
      pos_ui_notice(
          "Configure Software Update",
          "Set an HTTPS manifest URL and import a trusted publisher's P-256 "
          "public key. Verify its fingerprint separately. No public update "
          "endpoint or signing key is assumed.");
      continue;
    }
    (void)install_url(root, s->url, s);
  }
  free(s);
}

void pos_ui_deploy(const char *root) {
  pos_update_source_t *source = calloc(1, sizeof(*source));
  pos_view_t *view = calloc(1, sizeof(*view));
  if (!source || !view) {
    free(source);
    free(view);
    return;
  }
  if (pos_update_source_load(source) || !*source->key) {
    pos_ui_notice(
        "Configure trusted signing key",
        "Software Update must have a separately verified publisher public key. "
        "Development sessions use the same signed update checks.");
    goto done;
  }
  const char *items[] = {"Cancel", "USB recovery and OTA tested: open session"};
  pos_ui_notice(
      "Developer deployment",
      "Use only after testing USB/factory recovery, OTA confirmation, rollback "
      "and filesystem interruption recovery on this device/build. posctl "
      "requires your recorded physical evidence before requesting an update. "
      "Use a trusted LAN. Session expires after five minutes; each signed "
      "update still requires approval here.");
  if (pos_ui_menu("Have those physical tests passed?", items, 2) != 1)
    goto done;
  char key[17];
  if (pos_network_deploy_start(root, key)) {
    pos_ui_notice("Session unavailable",
                  "Connect Wi-Fi first and close any file-transfer session.");
    goto done;
  }
  int64_t end = esp_timer_get_time() + 300000000LL;
  while (!pos_home_pending() && esp_timer_get_time() < end) {
    pos_network_status_t network = pos_network_status();
    pos_view_clear(view, "Developer deployment");
    pos_view_line(view, 2, "http://%s:8080", network.ip);
    pos_view_line(view, 4, "Session key: %s", key);
    pos_view_line(view, 6, "posctl deploy / files");
    pos_view_line(view, 8, "Expires in %lld seconds",
                  (long long)(end - esp_timer_get_time()) / 1000000);
    pos_view_line(view, 11, "Signed updates + approval");
    pos_view_line(view, 15, "Esc:stop Fn+Space:close");
    pos_device_show(view);
    char url[512];
    if (pos_network_deploy_take(url)) {
      pos_ui_notice("Deployment request", url);
      bool success = !pos_home_pending() && install_url(root, url, source);
      pos_network_deploy_result(success);
    }
    pos_event_t event;
    if (pos_device_event(&event, 200) && event.pressed &&
        (event.key == POS_KEY_HOME || event.key == POS_KEY_ESCAPE))
      break;
  }
  pos_network_share_stop();
done:
  free(source);
  free(view);
}
