#include "pos_network_service.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pos_device.h"
#include "pos_health.h"
#include "pos_update_service.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static int error(pos_error_t *e, const char *message) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", message);
  return -1;
}
pos_network_status_t pos_network_status(void) {
  pos_network_status_t status = {0};
  wifi_ap_record_t ap;
  esp_netif_t *net = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  if (net && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
      esp_netif_get_ip_info(net, &ip) == ESP_OK && ip.ip.addr) {
    status.connected = true;
    status.rssi = ap.rssi;
    memcpy(status.ssid, ap.ssid, 32);
    snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&ip.ip));
  }
  return status;
}
int pos_network_scan(pos_network_ap_t *aps, unsigned capacity,
                     unsigned *count) {
  *count = 0;
  if (!capacity || capacity > 16 || pos_device_wifi_prepare())
    return -1;
  wifi_scan_config_t scan = {.show_hidden = false};
  if (esp_wifi_scan_start(&scan, true) != ESP_OK)
    return -1;
  /* Read more physical APs than displayed networks: mesh/repeater BSSIDs
   * should not crowd every other SSID out of the bounded menu. */
  uint16_t available = 0;
  if (esp_wifi_scan_get_ap_num(&available) != ESP_OK) {
    esp_wifi_clear_ap_list();
    return -1;
  }
  uint16_t limit = available < 64 ? available : 64;
  wifi_ap_record_t *records = calloc(limit ? limit : 1, sizeof(*records));
  if (!records) {
    esp_wifi_clear_ap_list();
    return -1;
  }
  uint16_t n = limit;
  int result = esp_wifi_scan_get_ap_records(&n, records) != ESP_OK;
  if (!result)
    for (unsigned i = 0; i < n; ++i) {
      pos_network_ap_t ap = {0};
      memcpy(ap.ssid, records[i].ssid, 32);
      ap.rssi = records[i].rssi;
      ap.secured = records[i].authmode != WIFI_AUTH_OPEN;
      *count = pos_wifi_scan_merge(aps, *count, capacity, &ap);
    }
  free(records);
  if (result)
    esp_wifi_clear_ap_list();
  return result ? -1 : 0;
}
static int vault_handle(nvs_handle_t *handle) {
  /* Never erase NVS to work around an error: factory/user data may exist. */
  return nvs_flash_init() != ESP_OK ||
                 nvs_open("pos_wifi", NVS_READWRITE, handle) != ESP_OK
             ? -1
             : 0;
}
int pos_network_profiles(const char *pass, pos_wifi_profiles_t *profiles) {
  nvs_handle_t handle;
  if (vault_handle(&handle))
    return -1;
  uint8_t blob[POS_WIFI_VAULT_BYTES];
  size_t length = sizeof(blob);
  esp_err_t status = nvs_get_blob(handle, "vault", blob, &length);
  nvs_close(handle);
  if (status == ESP_ERR_NVS_NOT_FOUND) {
    memset(profiles, 0, sizeof(*profiles));
    return 1;
  }
  int result = status != ESP_OK || length != sizeof(blob)
                   ? -1
                   : pos_wifi_vault_unlock(blob, pass, profiles);
  pos_secure_zero(blob, sizeof(blob));
  return result;
}
int pos_network_remember(const char *pass,
                         const pos_wifi_profiles_t *profiles) {
  if (pos_device_wifi_prepare())
    return -1; /* RF entropy for fresh salt/nonce. */
  pos_wifi_profiles_t previous = {0};
  int existing = pos_network_profiles(pass, &previous);
  pos_secure_zero(&previous, sizeof(previous));
  if (existing < 0)
    return -1; /* Never replace a vault that cannot be authenticated. */
  uint8_t blob[POS_WIFI_VAULT_BYTES], salt[16], nonce[12];
  esp_fill_random(salt, sizeof(salt));
  esp_fill_random(nonce, sizeof(nonce));
  if (pos_wifi_vault_encrypt(profiles, pass, salt, nonce, blob))
    return -1;
  nvs_handle_t handle;
  int result = -1;
  if (!vault_handle(&handle)) {
    result = nvs_set_blob(handle, "vault", blob, sizeof(blob)) != ESP_OK ||
                     nvs_commit(handle) != ESP_OK
                 ? -1
                 : 0;
    nvs_close(handle);
  }
  pos_secure_zero(blob, sizeof(blob));
  return result;
}
static atomic_bool auto_enabled, auto_paused, auto_stopping, auto_busy;
static atomic_bool auto_running;
bool pos_network_auto_enabled(void) {
  nvs_handle_t h;
  uint8_t enabled = 0;
  if (!vault_handle(&h)) {
    nvs_get_u8(h, "auto", &enabled);
    nvs_close(h);
  }
  atomic_store(&auto_enabled, enabled == 1);
  return enabled == 1;
}
int pos_network_auto_set(bool enabled) {
  nvs_handle_t h;
  if (vault_handle(&h))
    return -1;
  int bad = nvs_set_u8(h, "auto", enabled) != ESP_OK || nvs_commit(h) != ESP_OK;
  nvs_close(h);
  if (!bad)
    atomic_store(&auto_enabled, enabled);
  if (!bad && enabled && !atomic_load(&auto_running))
    pos_network_auto_start();
  return bad ? -1 : 0;
}
void pos_network_auto_pause(bool paused) {
  atomic_store(&auto_paused, paused);
  if (paused && atomic_load(&auto_busy)) {
    esp_wifi_scan_stop();
    while (atomic_load(&auto_busy))
      vTaskDelay(1);
  }
}
static void auto_task(void *unused) {
  (void)unused;
  unsigned retry = 0;
  while (!atomic_load(&auto_stopping)) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (atomic_load(&auto_paused) || !atomic_load(&auto_enabled) ||
        pos_network_status().connected)
      continue;
    if (retry) {
      --retry;
      continue;
    }
    retry = 300;
    atomic_store(&auto_busy, true);
    pos_wifi_profiles_t profiles = {0};
    pos_network_ap_t aps[12];
    unsigned count = 0;
    if (!atomic_load(&auto_paused) && !pos_network_known(&profiles) &&
        profiles.count && !pos_network_scan(aps, 12, &count)) {
      for (unsigned i = 0; i < count && !atomic_load(&auto_paused) &&
                           !atomic_load(&auto_stopping);
           ++i)
        for (unsigned j = 0; j < profiles.count; ++j)
          if (!strcmp(aps[i].ssid, profiles.networks[j].ssid)) {
            wifi_config_t cfg = {0};
            memcpy(cfg.sta.ssid, profiles.networks[j].ssid,
                   strlen(profiles.networks[j].ssid));
            memcpy(cfg.sta.password, profiles.networks[j].password,
                   strlen(profiles.networks[j].password));
            cfg.sta.pmf_cfg.capable = true;
            esp_wifi_disconnect();
            int started = esp_wifi_set_config(WIFI_IF_STA, &cfg) == ESP_OK &&
                          esp_wifi_connect() == ESP_OK;
            pos_secure_zero(&cfg, sizeof(cfg));
            for (unsigned wait = 0;
                 started && wait < 100 && !atomic_load(&auto_paused) &&
                 !atomic_load(&auto_stopping) &&
                 !pos_network_status().connected;
                 ++wait)
              vTaskDelay(pdMS_TO_TICKS(100));
            if (pos_network_status().connected)
              goto finished;
            esp_wifi_disconnect();
            break;
          }
    }
  finished:
    pos_secure_zero(&profiles, sizeof(profiles));
    atomic_store(&auto_busy, false);
  }
  auto_running = false;
  vTaskDelete(NULL);
}
void pos_network_auto_start(void) {
  if (!pos_network_auto_enabled())
    return;
  if (auto_running)
    return;
  atomic_store(&auto_stopping, false);
  auto_running = true;
  if (xTaskCreate(auto_task, "pos_wifi_auto", 6144, NULL, 1, NULL) != pdPASS)
    auto_running = false;
}
void pos_network_auto_stop(void) {
  atomic_store(&auto_stopping, true);
  pos_network_auto_pause(true);
  while (auto_running)
    vTaskDelay(1);
}
int pos_network_sync_time(void) {
  if (!pos_network_status().connected)
    return -1;
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  config.sync_cb = NULL;
  if (esp_netif_sntp_init(&config) != ESP_OK)
    return -1;
  int result = -1;
  for (unsigned i = 0; i < 150; ++i) {
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(100)) == ESP_OK) {
      result = 0;
      break;
    }
    pos_event_t event;
    if (pos_device_event(&event, 0) && event.pressed &&
        (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME)) {
      if (event.key == POS_KEY_HOME)
        pos_home_request();
      break;
    }
  }
  esp_netif_sntp_deinit();
  return result;
}
static int http_read(void *ctx, void *buffer, size_t length) {
  int n = esp_http_client_read(ctx, buffer, length);
  return n == 0 && !esp_http_client_is_complete_data_received(ctx) ? -1 : n;
}
static esp_err_t redirect_header(esp_http_client_event_t *event) {
  if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
      !strcasecmp(event->header_key, "Location")) {
    char *location = event->user_data;
    if (event->header_value && strlen(event->header_value) < 512)
      strcpy(location, event->header_value);
  }
  return ESP_OK;
}
static int https_failure(esp_http_client_handle_t client, pos_error_t *e,
                         const char *phase) {
  int tls = 0, flags = 0;
  esp_http_client_get_and_clear_last_tls_error(client, &tls, &flags);
  int socket_error = esp_http_client_get_errno(client);
  size_t heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  ESP_LOGW("pos_https", "%s: TLS=%d verify=0x%x socket=%d heap=%u largest=%u",
           phase, tls, flags, socket_error, (unsigned)heap, (unsigned)largest);
  if (e)
    snprintf(
        e->message, sizeof(e->message),
        "%s. TLS %d, verify 0x%x, socket %d. Free %u, largest %u bytes. "
        "%s",
        phase, tls, flags, socket_error, (unsigned)heap, (unsigned)largest,
        flags ? "Check UTC time and certificate trust; validation stays on."
              : "Check Wi-Fi/server availability or retry after restarting.");
  return -1;
}
static int https_once(const char *url, const char *method, const char *body,
                      const char *type, const char *root,
                      const char *destination, const char *sha,
                      pos_io_progress_t progress, void *ctx, pos_error_t *e,
                      char location[512], uint32_t limit) {
  if (!pos_https_url_valid(url) || !method ||
      (strcmp(method, "GET") && strcmp(method, "POST")) ||
      (body && strlen(body) > 4096))
    return error(e, "Use an HTTPS URL and a GET or POST request (body at most "
                    "4096 bytes).");
  if (!pos_network_status().connected)
    return error(e, "Connect Wi-Fi first.");
  if (time(NULL) < 1704067200)
    return error(
        e, "Sync time first so HTTPS certificate dates can be validated.");
  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = !strncmp(destination, "POS/Maps/", 9) ? 30000 : 12000,
      .buffer_size = 1024,
      .disable_auto_redirect = true,
      .keep_alive_enable = false,
      .event_handler = redirect_header,
      .user_data = location};
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return error(e, "Not enough memory for HTTPS.");
  size_t bytes = body ? strlen(body) : 0;
  esp_http_client_set_method(client, !strcmp(method, "POST") ? HTTP_METHOD_POST
                                                             : HTTP_METHOD_GET);
  esp_http_client_set_header(client, "Accept-Encoding", "identity");
  esp_http_client_set_header(client, "User-Agent",
                             "POS/0.6 (https://github.com/thatsockmonke/POS)");
  if (type)
    esp_http_client_set_header(client, "Content-Type", type);
  int result = -1;
  if (esp_http_client_open(client, bytes) != ESP_OK) {
    https_failure(client, e, "HTTPS connection failed");
    goto done;
  }
  size_t sent = 0;
  while (sent < bytes) {
    int n = esp_http_client_write(client, body + sent, bytes - sent);
    if (n <= 0) {
      error(e, "API request could not be sent.");
      goto done;
    }
    sent += n;
  }
  int64_t length = esp_http_client_fetch_headers(client);
  if (length < 0) {
    https_failure(client, e, "HTTPS headers failed");
    goto done;
  }
  int status = esp_http_client_get_status_code(client);
  if ((status == 301 || status == 302 || status == 303 || status == 307 ||
       status == 308) &&
      !strcmp(method, "GET") && pos_https_url_valid(location)) {
    result = 1;
    goto done;
  }
  if (status < 200 || status >= 300) {
    if (e)
      snprintf(
          e->message, sizeof(e->message), "Server returned HTTP %d. %s", status,
          status == 429 ? "Rate limited: wait before retrying."
          : status >= 500
              ? "Server unavailable: try later or select another provider."
          : status == 400 ? "Region/request rejected: choose a smaller area."
                          : "Check the selected service URL.");
    goto done;
  }
  result = pos_receive_file_limit(
      root, destination,
      length <= 0 || esp_http_client_is_chunked_response(client)
          ? UINT64_MAX
          : (uint64_t)length,
      limit, sha, http_read, client, progress, ctx, e);
done:
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return result;
}

int pos_network_https_limit(const char *url, const char *method,
                            const char *body, const char *type,
                            const char *root, const char *dest, const char *sha,
                            pos_io_progress_t progress, void *ctx,
                            pos_error_t *e, uint32_t limit) {
  if (!pos_https_url_valid(url))
    return error(e, "Use an HTTPS URL without embedded credentials.");
  char current[512], location[512];
  strcpy(current, url);
  for (unsigned hop = 0; hop < 4; ++hop) {
    location[0] = 0;
    int result = https_once(current, method, body, type, root, dest, sha,
                            progress, ctx, e, location, limit);
    if (result != 1)
      return result;
    if (progress && progress(ctx, "Following HTTPS redirect", hop + 1, 3))
      return error(e, "Download cancelled.");
    strcpy(current, location);
  }
  return error(e, "Too many HTTPS redirects. Use the final HTTPS URL.");
}

int pos_network_https(const char *url, const char *method, const char *body,
                      const char *type, const char *root, const char *dest,
                      const char *sha, pos_io_progress_t progress, void *ctx,
                      pos_error_t *e) {
  return pos_network_https_limit(url, method, body, type, root, dest, sha,
                                 progress, ctx, e, UINT32_MAX);
}

static httpd_handle_t server;
static wifi_ps_type_t share_previous_ps;
static bool share_ps_held;
static atomic_bool share_stop_requested;
static atomic_uint transfer_phase, transfer_done, transfer_total;
pos_transfer_status_t pos_network_share_activity(void) {
  return (pos_transfer_status_t){atomic_load(&transfer_phase),
                                 atomic_load(&transfer_done),
                                 atomic_load(&transfer_total)};
}
static int share_progress(void *, const char *, uint64_t, uint64_t);
static atomic_int
    deployment_state; /* 0 idle, 1 queued, 2 executing, 3 done, 4 failed */
static char deployment_url[512];
static atomic_bool deployment_enabled;
static int64_t deployment_deadline;
static struct {
  char root[POS_FS_PATH], export_file[POS_FS_PATH], key[17];
} share;
static bool authorized(httpd_req_t *request) {
  char query[512], key[32], bearer[32];
  if (deployment_enabled && esp_timer_get_time() > deployment_deadline)
    return false;
  if (httpd_req_get_hdr_value_str(request, "Authorization", bearer,
                                  sizeof(bearer)) == ESP_OK &&
      strlen(bearer) == 23 && !memcmp(bearer, "Bearer ", 7)) {
    unsigned different = 0;
    for (unsigned i = 0; i < 16; ++i)
      different |= (unsigned char)bearer[i + 7] ^ (unsigned char)share.key[i];
    return different == 0;
  }
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "key", key, sizeof(key)) != ESP_OK ||
      strlen(key) != 16)
    return false;
  unsigned different = 0;
  for (unsigned i = 0; i < 16; ++i)
    different |= (unsigned char)key[i] ^ (unsigned char)share.key[i];
  return different == 0;
}
static esp_err_t page(httpd_req_t *request) {
  if (!authorized(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                               "Session key required.");
  httpd_resp_set_type(request, "text/html");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(
      request,
      "<!doctype html><meta name=viewport "
      "content='width=device-width'><title>POS file transfer</title>"
      "<h1>Panputer file transfer</h1><p>Uploads go to Downloads. Existing "
      "files are preserved.</p>"
      "<button onclick=\"location.href='/download'+location.search\">Download "
      "selected file</button>"
      "<p><input id=f type=file><button "
      "onclick=\"upload()\">Upload</button></p><pre id=s></pre>"
      "<script>async function upload(){let "
      "file=f.files[0];if(!file)return;s.textContent='Uploading...';"
      "try{let r=await "
      "fetch('/"
      "upload'+location.search+'&name='+encodeURIComponent(file.name),{method:'"
      "POST',body:file});"
      "s.textContent=await r.text()}catch(e){s.textContent='Transfer "
      "interrupted. Reconnect and use a new filename.'}}</script>");
}
static int share_hash_progress(void *ctx, uint64_t done, uint64_t total) {
  (void)ctx;
  atomic_store(&transfer_done, done);
  atomic_store(&transfer_total, total);
  return atomic_load(&share_stop_requested) ? -1 : 0;
}
static esp_err_t send_file(httpd_req_t *request) {
  if (!authorized(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                               "Session key required.");
  if (!*share.export_file)
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                               "No file selected on Panputer.");
  FILE *f = fopen(share.export_file, "rb");
  if (!f)
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                               "File unavailable.");
  char sha[65];
  atomic_store(&transfer_done, 0);
  atomic_store(&transfer_phase, 2);
  uint32_t export_size;
  if (pos_fs_file_size(share.export_file, &export_size) ||
      (atomic_store(&transfer_phase, 2),
       pos_hash_file_progress(share.export_file, export_size, sha,
                              share_hash_progress, NULL))) {
    fclose(f);
    atomic_store(&transfer_phase, 4);
    return ESP_FAIL;
  }
  httpd_resp_set_hdr(request, "X-POS-SHA256", sha);
  char encoded[361];
  const char *name = strrchr(share.export_file, '/');
  name = name ? name + 1 : share.export_file;
  if (pos_transfer_name_valid(name)) {
    static const char hex[] = "0123456789ABCDEF";
    size_t at = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
      encoded[at++] = '%';
      encoded[at++] = hex[*p >> 4];
      encoded[at++] = hex[*p & 15];
    }
    encoded[at] = 0;
    httpd_resp_set_hdr(request, "X-POS-Filename", encoded);
  }
  httpd_resp_set_type(request, "application/octet-stream");
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "attachment; filename=\"panputer-file\"");
  char buffer[4096];
  size_t n;
  esp_err_t result = ESP_OK;
  uint32_t copied = 0;
  atomic_store(&transfer_phase, 1);
  atomic_store(&transfer_total, export_size);
  atomic_store(&transfer_done, 0);
  while (!atomic_load(&share_stop_requested) &&
         (n = fread(buffer, 1, sizeof(buffer), f))) {
    if ((result = httpd_resp_send_chunk(request, buffer, n)) != ESP_OK)
      break;
    copied += n;
    atomic_store(&transfer_done, copied);
  }
  if (ferror(f) || atomic_load(&share_stop_requested))
    result = ESP_FAIL;
  fclose(f);
  if (result == ESP_OK)
    result = httpd_resp_send_chunk(request, NULL, 0);
  atomic_store(&transfer_phase, result == ESP_OK ? 3 : 4);
  return result;
}
typedef struct {
  httpd_req_t *request;
  size_t remaining;
} upload_t;
static int upload_read(void *opaque, void *buffer, size_t length) {
  upload_t *upload = opaque;
  if (atomic_load(&share_stop_requested))
    return -1;
  if (!upload->remaining)
    return 0;
  if (length > upload->remaining)
    length = upload->remaining;
  int n = httpd_req_recv(upload->request, buffer, length);
  if (n <= 0)
    return -1;
  upload->remaining -= n;
  return n;
}
static int share_progress(void *ctx, const char *phase, uint64_t done,
                          uint64_t total) {
  (void)ctx;
  atomic_store(&transfer_phase, !strcmp(phase, "Transferring file") ? 1 : 2);
  atomic_store(&transfer_done, done);
  atomic_store(&transfer_total, total);
  return atomic_load(&share_stop_requested) ? -1 : 0;
}
static bool decode_name(const char *encoded, char decoded[121]) {
  size_t n = 0;
  while (*encoded && n < 120) {
    unsigned char c = *encoded++;
    if (c == '%') {
      if (!encoded[0] || !encoded[1])
        return false;
      char hex[3] = {encoded[0], encoded[1], 0};
      char *end;
      unsigned long value = strtoul(hex, &end, 16);
      if (*end || value == 0)
        return false;
      c = value;
      encoded += 2;
    } else if (c == '+')
      c = ' ';
    decoded[n++] = c;
  }
  decoded[n] = 0;
  return !*encoded && pos_transfer_name_valid(decoded);
}
static esp_err_t receive_file(httpd_req_t *request) {
  if (!authorized(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                               "Session key required.");
  char query[512], encoded[361], name[121], relative[160];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "name", encoded, sizeof(encoded)) !=
          ESP_OK ||
      !decode_name(encoded, name))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Use a simple filename without directories.");
  snprintf(relative, sizeof(relative), "Downloads/%s", name);
  char sha[65];
  const char *expected = NULL;
  if (httpd_req_get_hdr_value_len(request, "X-POS-SHA256")) {
    if (httpd_req_get_hdr_value_str(request, "X-POS-SHA256", sha,
                                    sizeof(sha)) != ESP_OK ||
        strlen(sha) != 64 || strspn(sha, "0123456789abcdef") != 64)
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Invalid SHA-256 header.");
    expected = sha;
  }
  atomic_store(&transfer_phase, 1);
  atomic_store(&transfer_done, 0);
  atomic_store(&transfer_total, request->content_len);
  upload_t upload = {request, request->content_len};
  pos_error_t e = {0};
  int result =
      pos_receive_file(share.root, relative, request->content_len, expected,
                       upload_read, &upload, share_progress, NULL, &e);
  atomic_store(&transfer_phase, result ? 4 : 3);
  if (result)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, e.message);
  return httpd_resp_sendstr(request, "Uploaded and verified in Downloads.");
}
static esp_err_t device_status(httpd_req_t *request) {
  if (!authorized(request))
    return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                               "Session key required.");
  uint8_t mac[6];
  char build[65], body[600], fingerprint[65] = "";
  uint32_t sequence = 0;
  pos_update_source_t *source = calloc(1, sizeof(*source));
  if (source && !pos_update_source_load(source)) {
    sequence = source->sequence;
    if (*source->key)
      pos_update_key_fingerprint(source->key, fingerprint);
  }
  free(source);
  if (esp_efuse_mac_get_default(mac) != ESP_OK)
    return ESP_FAIL;
  esp_app_get_elf_sha256(build, sizeof(build));
  esp_ota_img_states_t state;
  bool confirmed = esp_ota_get_state_partition(esp_ota_get_running_partition(),
                                               &state) == ESP_OK &&
                   state == ESP_OTA_IMG_VALID;
  char journal[POS_FS_PATH];
  struct stat st;
  bool no_update = !pos_fs_join(journal, share.root, "pos-update.pos") &&
                   pos_fs_lstat(journal, &st) && errno == ENOENT;
  snprintf(body, sizeof(body),
           "{\"format\":1,\"model\":\"m5stack-cardputer-adv\",\"device_id\":\"%"
           "02x:%02x:%02x:%02x:%02x:%02x\",\"build_id\":\"%s\",\"version\":\"%"
           "s\",\"healthy\":%s,\"ota_confirmed\":%s,\"update_pending\":%s,"
           "\"deployment\":%d,\"accepted_sequence\":%u,\"trusted_key\":\"%s\"}",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], build,
           esp_app_get_description()->version,
           pos_health_healthy() ? "true" : "false",
           confirmed ? "true" : "false", no_update ? "false" : "true",
           atomic_load(&deployment_state), (unsigned)sequence, fingerprint);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, body);
}
static esp_err_t deploy_request(httpd_req_t *request) {
  if (!deployment_enabled || !authorized(request))
    return httpd_resp_send_err(
        request, HTTPD_403_FORBIDDEN,
        "Open a developer deployment session on device.");
  if (atomic_load(&deployment_state) != 0)
    return httpd_resp_send_err(
        request, HTTPD_400_BAD_REQUEST,
        "One request per session; reopen for another deployment.");
  if (request->content_len < 9 ||
      request->content_len >= sizeof(deployment_url))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "HTTPS manifest URL required.");
  size_t at = 0;
  while (at < request->content_len) {
    int n =
        httpd_req_recv(request, deployment_url + at, request->content_len - at);
    if (n <= 0)
      return ESP_FAIL;
    at += (size_t)n;
  }
  deployment_url[at] = 0;
  if (memchr(deployment_url, 0, at) || !pos_https_url_valid(deployment_url))
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Use HTTPS without embedded credentials.");
  atomic_store(&deployment_state, 1);
  httpd_resp_set_status(request, "202 Accepted");
  return httpd_resp_sendstr(
      request, "Queued for on-device approval and signature verification.");
}
int pos_network_deploy_start(const char *root, char key[17]) {
  if (pos_network_share_start(root, NULL, key))
    return -1;
  deployment_deadline = esp_timer_get_time() + 300000000LL;
  deployment_enabled = true;
  atomic_store(&deployment_state, 0);
  httpd_uri_t route = {
      .uri = "/deploy", .method = HTTP_POST, .handler = deploy_request};
  if (httpd_register_uri_handler(server, &route) != ESP_OK) {
    pos_network_share_stop();
    return -1;
  }
  return 0;
}
bool pos_network_deploy_take(char url[512]) {
  if (atomic_load(&deployment_state) != 1)
    return false;
  strcpy(url, deployment_url);
  atomic_store(&deployment_state, 2);
  return true;
}
void pos_network_deploy_result(bool success) {
  atomic_store(&deployment_state, success ? 3 : 4);
}
int pos_network_share_start(const char *root, const char *export_file,
                            char key[17]) {
  if (server || !pos_network_status().connected ||
      strlen(root) >= sizeof(share.root))
    return -1;
  memset(&share, 0, sizeof(share));
  strcpy(share.root, root);
  if (export_file && *export_file) {
    struct stat st;
    if (!pos_fs_archive_path(export_file) || !pos_fs_user_path(export_file) ||
        pos_fs_join(share.export_file, root, export_file) ||
        pos_fs_lstat(share.export_file, &st) || !S_ISREG(st.st_mode))
      return -1;
  }
  char downloads[POS_FS_PATH];
  if (pos_fs_join(downloads, root, "Downloads") || pos_fs_mkdirs(downloads))
    return -1;
  uint32_t random[2];
  esp_fill_random(random, sizeof(random));
  snprintf(share.key, sizeof(share.key), "%08x%08x", (unsigned)random[0],
           (unsigned)random[1]);
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.max_open_sockets = 2;
  /* A browser may pre-open idle sockets. Reclaim the least-recently-used
   * session rather than letting those sockets starve a new PC connection. */
  config.lru_purge_enable = true;
  config.stack_size = 12288;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  atomic_store(&share_stop_requested, false);
  atomic_store(&transfer_phase, 0);
  /* Stay awake while explicitly hosting a transfer. Some mesh APs buffer
   * downstream traffic poorly for power-saving stations; restore on exit. */
  if (esp_wifi_get_ps(&share_previous_ps) != ESP_OK ||
      esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK)
    return -1;
  share_ps_held = true;
  if (httpd_start(&server, &config) != ESP_OK) {
    server = NULL;
    esp_wifi_set_ps(share_previous_ps);
    share_ps_held = false;
    return -1;
  }
  httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = page},
      {.uri = "/download", .method = HTTP_GET, .handler = send_file},
      {.uri = "/upload", .method = HTTP_POST, .handler = receive_file},
      {.uri = "/status", .method = HTTP_GET, .handler = device_status}};
  for (unsigned i = 0; i < 4; ++i)
    if (httpd_register_uri_handler(server, &routes[i]) != ESP_OK) {
      pos_network_share_stop();
      return -1;
    }
  strcpy(key, share.key);
  return 0;
}
void pos_network_share_stop(void) {
  atomic_store(&share_stop_requested, true);
  if (server)
    httpd_stop(server);
  server = NULL;
  if (share_ps_held) {
    esp_wifi_set_ps(share_previous_ps);
    share_ps_held = false;
  }
  deployment_enabled = false;
  atomic_store(&deployment_state, 0);
  pos_secure_zero(deployment_url, sizeof(deployment_url));
  pos_secure_zero(&share, sizeof(share));
}
