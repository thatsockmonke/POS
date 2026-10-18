#include "pos_mesh.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Only native tests: deterministic randomness, never linked into firmware. */
void esp_fill_random(void *p, size_t n) {
  unsigned char *b = p;
  while (n--)
    *b++ = (unsigned char)rand();
}
void pos_secure_zero(void *p, size_t n) {
  volatile unsigned char *v = p;
  while (n--)
    *v++ = 0;
}
static size_t unhex(const char *p, uint8_t *out, size_t cap) {
  size_t n = strlen(p);
  assert(!(n & 1) && n / 2 <= cap);
  for (size_t i = 0; i < n / 2; ++i) {
    char b[3] = {p[2 * i], p[2 * i + 1], 0};
    out[i] = strtoul(b, NULL, 16);
  }
  return n / 2;
}
static void hex(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; ++i)
    printf("%02x", p[i]);
  puts("");
}
typedef struct {
  uint32_t id, random;
  unsigned received, acked, failed;
  bool disk_fail;
} host_t;
static uint32_t next(void *p) { return ++((host_t *)p)->id; }
static uint32_t random_word(void *p) { return ++((host_t *)p)->random; }
static int sink(void *p, pos_mesh_event_t e, const pos_mesh_message_t *m) {
  host_t *h = p;
  if (e == POS_MESH_RX && m->data.port == POS_MESH_TEXT) {
    if (h->disk_fail)
      return -1;
    ++h->received;
  }
  if (e == POS_MESH_ACKED)
    ++h->acked;
  if (e == POS_MESH_TIMEOUT || e == POS_MESH_NAK)
    ++h->failed;
  return 0;
}
static pos_mesh_t *make(uint32_t id, host_t *h) {
  pos_mesh_t *s = calloc(1, sizeof(*s));
  assert(s);
  pos_mesh_config_t c;
  pos_mesh_defaults(&c);
  uint8_t key[32];
  memset(key, id & 255, 32);
  assert(!pos_mesh_init(s, id, &c, key));
  s->context = h;
  s->random = random_word;
  s->next_id = next;
  s->sink = sink;
  return s;
}
static void known(pos_mesh_t *a, const pos_mesh_t *b) {
  a->nodes[0].id = b->node;
  a->nodes[0].has_key = true;
  memcpy(a->nodes[0].public_key, b->public_key, 32);
}
static size_t take(pos_mesh_t *s, uint8_t w[255], uint64_t now) {
  const uint8_t *p = NULL;
  size_t n = 0;
  int ready = pos_mesh_due(s, now, &p, &n);
  assert(ready == 1);
  memcpy(w, p, n);
  pos_mesh_tx_result(s, true, now);
  return n;
}
static unsigned queued(const pos_mesh_t *s) {
  unsigned n = 0;
  for (unsigned i = 0; i < POS_MESH_QUEUE; ++i)
    n += s->queue[i].used;
  return n;
}
static void engine(const char *mode) {
  host_t ah = {.id = 100}, bh = {.id = 200}, ch = {.id = 300};
  pos_mesh_t *a = make(0x11111111, &ah), *b = make(0x22222222, &bh),
             *c = make(0x33333333, &ch);
  known(a, c);
  known(c, a);
  uint8_t w[255], relay[255], ack[255];
  size_t n, nr, na;
  if (!strcmp(mode, "mesh")) {
    assert(!pos_mesh_send(a, 0, c->node, POS_MESH_TEXT, "private hello", 13,
                          false, 0));
    n = take(a, w, 1000);
    assert(!pos_mesh_receive(b, w, n, -90, 3, 1000));
    nr = take(b, relay, 2000);
    assert(nr == n);
    assert((relay[12] & 7) == 2 && relay[15] == 0x22);
    assert(!memcmp(w + 16, relay + 16, n - 16));
    assert(pos_mesh_receive(c, relay, nr, -95, 1, 2000) == 1 &&
           ch.received == 1);
    na = take(c, ack, 3000);
    assert(!pos_mesh_receive(b, ack, na, -90, 3, 3000));
    nr = take(b, relay, 4000);
    assert(pos_mesh_receive(a, relay, nr, -95, 1, 4000) == 1 && ah.acked == 1);
    assert(!queued(a));
    assert(!pos_mesh_receive(c, w, n, -90, 3, 5000) && ch.received == 1);
    assert(queued(c)); /* lost ACK: duplicate DM is ACKed again */
  } else if (!strcmp(mode, "storage")) {
    assert(!pos_mesh_send(a, 0, c->node, 1, "retain before ack", 17, false, 0));
    n = take(a, w, 1000);
    ch.disk_fail = true;
    assert(pos_mesh_receive(c, w, n, -80, 5, 1000) < 0);
    assert(!queued(c) && ch.received == 0);
    ch.disk_fail = false;
    assert(pos_mesh_receive(c, w, n, -80, 5, 2000) == 1);
    assert(queued(c) && ch.received == 1);
  } else if (!strcmp(mode, "retry")) {
    assert(!pos_mesh_send(a, 0, c->node, 1, "lost", 4, false, 0));
    for (unsigned i = 0; i < 4; ++i) {
      n = take(a, w, 1000 + i * 100000);
      assert(n);
    }
    const uint8_t *p;
    assert(pos_mesh_due(a, 500000, &p, &n) < 0 && ah.failed == 1 && !queued(a));
  } else if (!strcmp(mode, "ownership")) {
    assert(!pos_mesh_send(a, 0, c->node, 1, "DM", 2, false, 0));
    n = take(a, w, 1000);
    /* Our DM heard relayed does not count as a destination acknowledgement. */
    w[12] = (w[12] & 0xf8) | 2;
    w[15] = 0x22;
    assert(!pos_mesh_receive(a, w, n, -80, 5, 2000) && queued(a) &&
           ah.acked == 0);
    /* An unrelated node's ACK never completes the DM. */
    pos_mesh_message_t fake = {
        .header = {.to = a->node, .from = b->node, .id = 999},
        .data = {
            .port = 5, .request_id = 101, .length = 2, .payload = {24, 0}}};
    n = pos_mesh_encode(b, &fake, w);
    assert(n);
    assert(pos_mesh_receive(a, w, n, -80, 5, 3000) == 1 && queued(a) &&
           ah.acked == 0);
    pos_mesh_cancel(a);
    assert(!queued(a));
    /* Unknown and changed public keys fail closed, no plaintext fallback. */
    assert(pos_mesh_send(b, 0, c->node, 1, "unknown", 7, false, 0));
    a->nodes[0].key_changed = true;
    assert(pos_mesh_send(a, 0, c->node, 1, "changed", 7, false, 0));
  } else if (!strcmp(mode, "flood")) {
    assert(!pos_mesh_send(a, 0, UINT32_MAX, 1, "broadcast", 9, false, 0));
    n = take(a, w, 1000);
    assert(pos_mesh_receive(b, w, n, -80, 5, 1000) == 1 && bh.received == 1 &&
           queued(b));
    memcpy(relay, w, n);
    relay[12] = (w[12] & 0xf8) | 2;
    relay[15] = 0x33;
    assert(!pos_mesh_receive(b, relay, n, -80, 5, 1200) && !queued(b));
    /* zero-hop frames are delivered but cannot grow the mesh */
    w[8]++;
    w[12] &= 0xf8; /* changing id invalidates encrypted data: opaque relay still
                      prohibited */
    assert(pos_mesh_receive(b, w, n, -80, 5, 2000) <= 0 && !queued(b));
  } else if (!strcmp(mode, "discovery")) {
    assert(!pos_mesh_announce(a, UINT32_MAX, false, 0));
    n = take(a, w, 1000);
    assert(pos_mesh_receive(b, w, n, -80, 5, 1000) == 1);
    bool found = false;
    for (unsigned i = 0; i < 32; ++i)
      if (b->nodes[i].id == a->node) {
        assert(b->nodes[i].has_key &&
               !memcmp(b->nodes[i].public_key, a->public_key, 32));
        found = true;
      }
    assert(found);
    assert(!pos_mesh_position(a, 0, UINT32_MAX, 340505000, -1182475000,
                              1780000000, 2000));
    n = take(a, w, 3000);
    assert(pos_mesh_receive(b, w, n, -80, 5, 3000) == 1);
    found = false;
    for (unsigned i = 0; i < 32; ++i)
      if (b->nodes[i].id == a->node) {
        assert(b->nodes[i].has_position &&
               b->nodes[i].longitude == -1182475000);
        found = true;
      }
    assert(found);
  } else if (!strcmp(mode, "bounds")) {
    for (unsigned i = 0; i < 12; ++i)
      assert(!pos_mesh_send(a, 0, UINT32_MAX, 1, "queue", 5, false, 0));
    assert(pos_mesh_send(a, 0, UINT32_MAX, 1, "full", 4, false, 0));
    pos_mesh_cancel(a);
    const uint8_t *p;
    assert(!pos_mesh_due(a, 1000, &p, &n));
    uint8_t malformed[] = {8, 1, 18, 255};
    pos_mesh_data_t d;
    assert(pos_mesh_data_decode(malformed, sizeof(malformed), &d));
    uint8_t overflow[] = {8,    1,    0x80, 0x80, 0x80, 0x80,
                          0x80, 0x80, 0x80, 0x80, 0x80, 2};
    assert(pos_mesh_data_decode(overflow, sizeof(overflow), &d));
    uint8_t dup[] = {8, 1, 8, 4};
    assert(pos_mesh_data_decode(dup, sizeof(dup), &d));
    for (unsigned i = 0; i < 2000; ++i) {
      size_t length = i % 256;
      for (size_t j = 0; j < length; ++j)
        w[j] = (i * 31 + j * 17) ^ (i >> 3);
      (void)pos_mesh_receive(a, w, length, -100, 0, i * 1000);
    }
  } else
    assert(!"unknown engine mode");
  free(a);
  free(b);
  free(c);
  puts("PASS");
}
int main(int argc, char **argv) {
  assert(argc >= 2);
  if (!strcmp(argv[1], "engine")) {
    assert(argc == 3);
    engine(argv[2]);
    return 0;
  }
  uint8_t key[32] = {0}, peer[32] = {0}, data[255] = {0}, out[255] = {0};

  if (!strcmp(argv[1], "export")) {
    pos_mesh_config_t c;
    char url[1024];
    pos_mesh_defaults(&c);
    if (argc == 3 && pos_mesh_import_url(argv[2], &c))
      return 1;
    if (pos_mesh_export_url(&c, url, sizeof(url)))
      return 1;
    puts(url);
    return 0;
  }
  if (!strcmp(argv[1], "envelope")) {
    assert(argc == 3);
    size_t n = unhex(argv[2], data, sizeof(data));
    uint8_t envelope[400];
    n = pos_mesh_envelope(data, n, "LongFast", "!12345678", envelope,
                          sizeof(envelope));
    if (!n)
      return 1;
    hex(envelope, n);
    return 0;
  }
  if (!strcmp(argv[1], "unenvelope")) {
    assert(argc == 4);
    uint8_t envelope[400];
    size_t n = unhex(argv[2], envelope, sizeof(envelope)), length = 0;
    if (pos_mesh_unenvelope(envelope, n, "LongFast", argv[3], out, &length))
      return 1;
    hex(out, length);
    return 0;
  }
  if (!strcmp(argv[1], "trace")) {
    assert(argc == 4);
    pos_mesh_data_t d = {.port = POS_MESH_TRACEROUTE};
    d.length = unhex(argv[2], d.payload, sizeof(d.payload));
    if (pos_mesh_trace_route(&d, 0x11223344, atoi(argv[3])))
      return 1;
    hex(d.payload, d.length);
    return 0;
  }
  if (!strcmp(argv[1], "import")) {
    assert(argc == 3);
    pos_mesh_config_t c;
    pos_mesh_defaults(&c);
    pos_mesh_config_t old = c;
    int r = pos_mesh_import_url(argv[2], &c);
    if (r) {
      assert(!memcmp(&old, &c, sizeof(c)));
      return 1;
    }
    printf("%u %u %u %s %s\n", c.region, c.preset, c.hops, c.channels[0].name,
           c.channels[1].name);
    hex(c.channels[1].key, c.channels[1].key_len);
    return 0;
  }
  if (!strcmp(argv[1], "key")) {
    assert(argc == 3);
    pos_mesh_channel_t c = {0};
    if (pos_mesh_key_parse(argv[2], &c))
      return 1;
    hex(c.key, c.key_len);
    return 0;
  }
  if (!strcmp(argv[1], "airtime")) {
    pos_mesh_airtime_t a = {0};
    assert(pos_mesh_airtime_allow(&a, 59000, 36000));
    pos_mesh_airtime_charge(&a, 59000, 35000);
    assert(pos_mesh_airtime_allow(&a, 60000, 1000));
    assert(!pos_mesh_airtime_allow(&a, 60000, 1001));
    assert(!pos_mesh_airtime_allow(&a, 60 * 60000, 1001));
    assert(pos_mesh_airtime_allow(&a, 61 * 60000, 36000));
    puts("PASS");
    return 0;
  }
  if (!strcmp(argv[1], "public")) {
    assert(argc == 3);
    assert(unhex(argv[2], key, 32) == 32);
    assert(!pos_mesh_public(key, out));
    hex(out, 32);
    return 0;
  }
  if (!strcmp(argv[1], "ctr")) {
    assert(argc == 6);
    size_t k = unhex(argv[2], key, 32), n = unhex(argv[5], data, 255);
    int r = pos_mesh_ctr(key, k, strtoul(argv[3], NULL, 0),
                         strtoul(argv[4], NULL, 0), data, n);
    if (r)
      return 1;
    hex(data, n);
    return 0;
  }
  if (!strcmp(argv[1], "pki")) {
    assert(argc == 9);
    unhex(argv[3], key, 32);
    unhex(argv[4], peer, 32);
    size_t n = unhex(argv[8], data, 255);
    bool enc = !strcmp(argv[2], "encrypt");
    int r = pos_mesh_pki(enc, key, peer, strtoul(argv[5], NULL, 0),
                         strtoul(argv[6], NULL, 0), strtoul(argv[7], NULL, 0),
                         data, n, out);
    if (r)
      return 1;
    hex(out, enc ? n + 12 : n - 12);
    return 0;
  }
  if (!strcmp(argv[1], "decode")) {
    assert(argc == 6);
    unhex(argv[2], key, 32);
    unhex(argv[3], peer, 32);
    size_t n = unhex(argv[5], data, 255);
    pos_mesh_config_t c;
    pos_mesh_defaults(&c);
    pos_mesh_t *s = calloc(1, sizeof(*s));
    assert(!pos_mesh_init(s, strtoul(argv[4], NULL, 0), &c, key));
    s->nodes[0].id = 0x12345678;
    s->nodes[0].has_key = true;
    memcpy(s->nodes[0].public_key, peer, 32);
    pos_mesh_message_t m;
    int r = pos_mesh_decode(s, data, n, &m);
    free(s);
    if (r)
      return 1;
    printf("%u %u %u\n", m.data.port, m.data.request_id, m.pki);
    hex(m.data.payload, m.data.length);
    return 0;
  }
  if (!strcmp(argv[1], "modem")) {
    assert(argc == 5);
    pos_mesh_config_t c;
    pos_mesh_defaults(&c);
    c.region = atoi(argv[2]);
    c.preset = atoi(argv[3]);
    c.slot = atoi(argv[4]);
    pos_mesh_modem_t m;
    if (pos_mesh_config_valid(&c) || pos_mesh_modem(&c, &m))
      return 1;
    printf("%u %u %u %u %u %u\n", m.frequency_hz, m.bandwidth_hz, m.sf, m.cr,
           m.ldro, pos_mesh_channel_hash(&c, 0));
    return 0;
  }
  return 1;
}
