#include "nvs.h"
#include "pos_fs.h"
#include "pos_mesh_service.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char base[512];
static uint64_t clock_ms;
static bool fail_write;
static unsigned revoked;
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
int pos_device_wifi_prepare(void) { return 0; }
void pos_expansion_revoke_transmit(void) { ++revoked; }
uint32_t esp_random(void) {
  static uint32_t x = 0x12345678;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}
void esp_fill_random(void *p, size_t n) {
  unsigned char *b = p;
  while (n--)
    *b++ = esp_random();
}
int nvs_open(const char *name, int mode, nvs_handle_t *h) {
  (void)mode;
  assert(!strcmp(name, "pos_mesh"));
  *h = 1;
  return ESP_OK;
}
static void filename(const char *name, char path[600]) {
  assert(strlen(name) < 20 &&
         strspn(name, "abcdefghijklmnopqrstuvwxyz_") == strlen(name));
  snprintf(path, 600, "%s/%s.nvs", base, name);
}
int nvs_get_blob(nvs_handle_t h, const char *name, void *p, size_t *n) {
  (void)h;
  char path[600];
  filename(name, path);
  FILE *f = fopen(path, "rb");
  if (!f)
    return errno == ENOENT ? ESP_ERR_NVS_NOT_FOUND : -1;
  size_t size = fread(p, 1, *n, f);
  int bad = ferror(f) || fgetc(f) != EOF;
  fclose(f);
  *n = size;
  return bad ? -1 : ESP_OK;
}
int nvs_set_blob(nvs_handle_t h, const char *name, const void *p, size_t n) {
  (void)h;
  if (fail_write)
    return -1;
  char path[600], part[610];
  filename(name, path);
  snprintf(part, sizeof(part), "%s.part", path);
  FILE *f = fopen(part, "wb");
  if (!f)
    return -1;
  bool bad = fwrite(p, 1, n, f) != n || fflush(f) || fsync(fileno(f));
  if (fclose(f))
    bad = true;
  return bad || rename(part, path) ? -1 : ESP_OK;
}
int nvs_get_u32(nvs_handle_t h, const char *name, uint32_t *p) {
  size_t n = 4;
  int r = nvs_get_blob(h, name, p, &n);
  return !r && n != 4 ? -1 : r;
}
int nvs_set_u32(nvs_handle_t h, const char *name, uint32_t p) {
  return nvs_set_blob(h, name, &p, 4);
}
int nvs_commit(nvs_handle_t h) {
  (void)h;
  return fail_write ? -1 : ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
int main(int argc, char **argv) {
  assert(argc == 4);
  assert(strlen(argv[2]) < sizeof(base));
  strcpy(base, argv[2]);
  assert(!pos_meshtastic_init(argv[3], 0x12345678));
  pos_mesh_config_t c;
  assert(!pos_meshtastic_settings(&c));
  pos_error_t e = {0};
  if (!strcmp(argv[1], "prepare")) {
    assert(!pos_meshtastic_configure(&c, true, &e) && revoked);
    assert(pos_meshtastic_status().enabled && pos_meshtastic_status().ready);
  } else if (!strcmp(argv[1], "send")) {
    assert(!pos_meshtastic_send(0, UINT32_MAX, 1, "persist this message", 20,
                                false, &e));
    assert(pos_meshtastic_status().queued == 1);
  } else if (!strcmp(argv[1], "retry")) {
    assert(!pos_meshtastic_retry(&e));
    clock_ms = 2000;
    uint8_t wire[255];
    size_t n = 0;
    assert(pos_meshtastic_due(wire, &n) == 1);
    pos_mesh_header_t h;
    assert(!pos_mesh_header(wire, n, &h));
    printf("%u\n", (unsigned)h.id);
    pos_meshtastic_tx_result(true);
  } else if (!strcmp(argv[1], "failed-config")) {
    fail_write = true;
    c.hops = 6;
    assert(pos_meshtastic_configure(&c, true, &e));
    pos_mesh_config_t loaded;
    assert(!pos_meshtastic_settings(&loaded));
    assert(loaded.hops == 3);
  } else if (!strcmp(argv[1], "disabled")) {
    assert(!pos_meshtastic_configure(&c, false, &e));
    assert(!pos_meshtastic_status().enabled && !pos_meshtastic_status().ready);
  } else if (!strcmp(argv[1], "read")) {
    assert(pos_meshtastic_status().enabled && c.hops == 3);
  } else if (!strcmp(argv[1], "corrupt")) {
    assert(!pos_meshtastic_status().enabled && !pos_meshtastic_status().ready);
    assert(strstr(pos_meshtastic_status().error, "invalid"));
  } else if (!strcmp(argv[1], "reservation-fail")) {
    fail_write = true;
    assert(
        pos_meshtastic_send(0, UINT32_MAX, 1, "no nonce reuse", 14, false, &e));
    assert(!pos_meshtastic_status().queued);
  } else if (!strcmp(argv[1], "receive-node") ||
             !strcmp(argv[1], "keychange")) {
    uint8_t private_key[32], wire[255];
    memset(private_key, !strcmp(argv[1], "keychange") ? 34 : 33, 32);
    pos_mesh_t *p = calloc(1, sizeof(*p));
    assert(p);
    assert(!pos_mesh_init(p, 0x22222222, &c, private_key));
    pos_mesh_message_t m = {
        .header = {.from = p->node, .to = UINT32_MAX, .id = 123, .flags = 0x63},
        .data = {.port = POS_MESH_USER}};
    m.data.payload[0] = 66;
    m.data.payload[1] = 32;
    memcpy(m.data.payload + 2, p->public_key, 32);
    m.data.length = 34;
    size_t n = pos_mesh_encode(p, &m, wire);
    assert(n);
    pos_meshtastic_receive(wire, n, -80, 5);
    assert(pos_meshtastic_status().nodes == 1);
    free(p);
  } else if (!strcmp(argv[1], "peer-check") ||
             !strcmp(argv[1], "peer-changed") || !strcmp(argv[1], "forget")) {
    pos_mesh_node_t n;
    bool found = false;
    for (unsigned i = 0; i < 32; ++i)
      if (!pos_meshtastic_node(i, &n) && n.id == 0x22222222) {
        found = true;
        assert(n.has_key);
        assert(n.key_changed == !strcmp(argv[1], "peer-changed"));
      }
    assert(found);
    if (!strcmp(argv[1], "forget")) {
      assert(!pos_meshtastic_forget(0x22222222, &e));
      assert(!pos_meshtastic_status().nodes);
    }
  } else if (!strcmp(argv[1], "peer-absent")) {
    assert(!pos_meshtastic_status().nodes);
  } else
    return 1;
  pos_meshtastic_stop();
  return 0;
}
