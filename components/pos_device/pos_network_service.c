#include "pos_network_service.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pos_device.h"
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
  wifi_ap_record_t *records = calloc(capacity, sizeof(*records));
  if (!records)
    return -1;
  uint16_t n = capacity;
  int result = esp_wifi_scan_get_ap_records(&n, records) != ESP_OK;
  if (!result)
    for (unsigned i = 0; i < n; ++i) {
      memset(&aps[i], 0, sizeof(aps[i]));
      memcpy(aps[i].ssid, records[i].ssid, 32);
      aps[i].rssi = records[i].rssi;
      aps[i].secured = records[i].authmode != WIFI_AUTH_OPEN;
    }
  free(records);
  if (!result)
    *count = n;
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
        (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME))
      break;
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
  esp_http_client_config_t config = {.url = url,
                                     .crt_bundle_attach = esp_crt_bundle_attach,
                                     .timeout_ms = 12000,
                                     .buffer_size = 4096,
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
  if (type)
    esp_http_client_set_header(client, "Content-Type", type);
  int result = -1;
  if (esp_http_client_open(client, bytes) != ESP_OK) {
    error(e,
          "HTTPS connection failed; certificate validation remains enabled.");
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
  int status = esp_http_client_get_status_code(client);
  if ((status == 301 || status == 302 || status == 303 || status == 307 ||
       status == 308) &&
      !strcmp(method, "GET") && pos_https_url_valid(location)) {
    result = 1;
    goto done;
  }
  if (status < 200 || status >= 300) {
    error(e, "Server returned a non-success status or redirect. Use the final "
             "HTTPS URL.");
    goto done;
  }
  result =
      pos_receive_file_limit(root, destination,
                       length < 0 || esp_http_client_is_chunked_response(client)
                           ? UINT64_MAX
                           : (uint64_t)length,
                       limit, sha, http_read, client, progress, ctx, e);
done:
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return result;
}

int pos_network_https_limit(const char *url, const char *method, const char *body,
                      const char *type, const char *root, const char *dest,
                      const char *sha, pos_io_progress_t progress, void *ctx,
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

int pos_network_https(const char *url,const char *method,const char *body,const char *type,const char *root,const char *dest,const char *sha,pos_io_progress_t progress,void *ctx,pos_error_t *e) {return pos_network_https_limit(url,method,body,type,root,dest,sha,progress,ctx,e,UINT32_MAX);}

static httpd_handle_t server;
static atomic_bool share_stop_requested;
static struct {
  char root[POS_FS_PATH], export_file[POS_FS_PATH], key[17];
} share;
static bool authorized(httpd_req_t *request) {
  char query[512], key[32];
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
  httpd_resp_set_type(request, "application/octet-stream");
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "attachment; filename=\"panputer-file\"");
  char buffer[4096];
  size_t n;
  esp_err_t result = ESP_OK;
  while (!atomic_load(&share_stop_requested) &&
         (n = fread(buffer, 1, sizeof(buffer), f)))
    if ((result = httpd_resp_send_chunk(request, buffer, n)) != ESP_OK)
      break;
  if (ferror(f) || atomic_load(&share_stop_requested))
    result = ESP_FAIL;
  fclose(f);
  if (result == ESP_OK)
    result = httpd_resp_send_chunk(request, NULL, 0);
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
  (void)phase;
  (void)done;
  (void)total;
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
  upload_t upload = {request, request->content_len};
  pos_error_t e = {0};
  int result =
      pos_receive_file(share.root, relative, request->content_len, NULL,
                       upload_read, &upload, share_progress, NULL, &e);
  if (result)
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, e.message);
  return httpd_resp_sendstr(request, "Uploaded and verified in Downloads.");
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
  config.stack_size = 12288;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  atomic_store(&share_stop_requested, false);
  if (httpd_start(&server, &config) != ESP_OK) {
    server = NULL;
    return -1;
  }
  httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = page},
      {.uri = "/download", .method = HTTP_GET, .handler = send_file},
      {.uri = "/upload", .method = HTTP_POST, .handler = receive_file}};
  for (unsigned i = 0; i < 3; ++i)
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
  pos_secure_zero(&share, sizeof(share));
}
