#include "pos_mesh.h"
#include "pos_network.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_random.h"
#include "mbedtls/aes.h"
#include "mbedtls/ccm.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/sha256.h"
#else
#include <openssl/evp.h>
#endif

/* Independently implemented from the documented v2 wire format and protobuf
 * schema; references and the exact upstream baseline are in MESHTASTIC.md. */
static void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = v >> (8 * i);
}
static uint32_t get32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static void airtime_advance(pos_mesh_airtime_t *a, uint64_t now) {
  uint64_t minute = now / 60000;
  if (minute < a->minute || minute - a->minute >= 61)
    memset(a->milliseconds, 0, sizeof(a->milliseconds));
  else
    while (a->minute < minute)
      a->milliseconds[++a->minute % 61] = 0;
  a->minute = minute;
}
bool pos_mesh_airtime_allow(pos_mesh_airtime_t *a, uint64_t now,
                            uint32_t cost) {
  airtime_advance(a, now);
  uint64_t used = cost;
  for (unsigned i = 0; i < 61; ++i)
    used += a->milliseconds[i];
  return used <= 36000;
}
void pos_mesh_airtime_charge(pos_mesh_airtime_t *a, uint64_t now,
                             uint32_t cost) {
  airtime_advance(a, now);
  uint32_t *used = &a->milliseconds[a->minute % 61];
  *used = cost > UINT32_MAX - *used ? UINT32_MAX : *used + cost;
}
static bool string_ok(const char *p, size_t n) {
  const char *end = memchr(p, 0, n);
  if (!end)
    return false;
  for (; p < end; ++p)
    if ((unsigned char)*p < 32)
      return false;
  return true;
}
static const char *const presets[] = {
    "LongFast",  "LongSlow",  NULL,      "MediumSlow", "MediumFast",
    "ShortSlow", "ShortFast", "LongMod", "ShortTurbo", "LongTurbo"};
static const char *channel_name(const pos_mesh_config_t *c, unsigned i) {
  return *c->channels[i].name ? c->channels[i].name : presets[c->preset];
}
void pos_mesh_defaults(pos_mesh_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->region = POS_MESH_US;
  c->hops = 3;
  c->relay = true;
  strcpy(c->long_name, "Panputer POS");
  strcpy(c->short_name, "POS");
  c->channels[0].enabled = true;
  c->channels[0].key_len = 1;
  c->channels[0].key[0] = 1;
}
int pos_mesh_key(const pos_mesh_config_t *c, unsigned i, uint8_t key[32]) {
  static const uint8_t public_key[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29,
                                         0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab,
                                         0xcf, 0x4e, 0x69, 0x01};
  if (!c || i >= POS_MESH_CHANNELS || !c->channels[i].enabled)
    return -1;
  const pos_mesh_channel_t *ch = &c->channels[i];
  unsigned n = ch->key_len;
  if (!n && i)
    return pos_mesh_key(c, 0, key);
  memset(key, 0, 32);
  if (n == 1) {
    if (!ch->key[0])
      return 0;
    memcpy(key, public_key, 16);
    key[15] += ch->key[0] - 1;
    return 16;
  }
  if (n != 0 && n != 16 && n != 32)
    return -1;
  memcpy(key, ch->key, n);
  return (int)n;
}
uint8_t pos_mesh_channel_hash(const pos_mesh_config_t *c, unsigned i) {
  uint8_t key[32], h = 0;
  int n = pos_mesh_key(c, i, key);
  if (n < 0)
    return 0;
  for (int j = 0; j < n; ++j)
    h ^= key[j];
  const char *name = channel_name(c, i);
  for (; *name; ++name)
    h ^= (uint8_t)*name;
  pos_secure_zero(key, sizeof(key));
  return h;
}
int pos_mesh_modem(const pos_mesh_config_t *c, pos_mesh_modem_t *m) {
  if (!c || c->preset >= 10 || !presets[c->preset])
    return -1;
  static const uint8_t sf[] = {11, 12, 0, 10, 9, 8, 7, 11, 7, 11};
  unsigned p = c->preset;
  uint32_t bw = p == 1 || p == 7 ? 125000 : p >= 8 ? 500000 : 250000;
  uint32_t start, end;
  if (c->region == POS_MESH_US) {
    start = 902000000;
    end = 928000000;
  } else if (c->region == POS_MESH_EU868) {
    start = 869400000;
    end = 869650000;
  } else
    return -1;
  unsigned slots = (end - start) / bw;
  if (!slots || c->slot > slots)
    return -1;
  uint32_t hash = 5381;
  const char *name = channel_name(c, 0);
  for (; *name; ++name)
    hash = hash * 33 + (unsigned char)*name;
  unsigned slot = c->slot ? (unsigned)c->slot - 1 : hash % slots;
  *m = (pos_mesh_modem_t){.frequency_hz = start + bw / 2 + slot * bw,
                          .bandwidth_hz = bw,
                          .sf = sf[p],
                          .cr = p == 1 || p == 7 || p == 9 ? 8 : 5,
                          .ldro = ((1u << sf[p]) * 1000 / bw) >= 16};
  return 0;
}
int pos_mesh_config_valid(const pos_mesh_config_t *c) {
  pos_mesh_modem_t m;
  if (!c || c->hops > 7 || !c->channels[0].enabled ||
      !string_ok(c->long_name, sizeof(c->long_name)) || !*c->long_name ||
      !string_ok(c->short_name, sizeof(c->short_name)) || pos_mesh_modem(c, &m))
    return -1;
  /* The cap's RF front end is specified for 868-923 MHz. Keep the occupied
   * bandwidth within it even when the region defines additional slots. */
  if (m.frequency_hz - m.bandwidth_hz / 2 < 868000000 ||
      m.frequency_hz + m.bandwidth_hz / 2 > 923000000)
    return -1;
  for (unsigned i = 0; i < POS_MESH_CHANNELS; ++i) {
    uint8_t key[32];
    if (!string_ok(c->channels[i].name, sizeof(c->channels[i].name)))
      return -1;
    int n = c->channels[i].enabled ? pos_mesh_key(c, i, key) : 0;
    pos_secure_zero(key, sizeof(key));
    if (n < 0)
      return -1;
  }
  return 0;
}

typedef struct {
  const uint8_t *p, *end;
} reader_t;
typedef struct {
  unsigned field, type;
  uint64_t value;
  const uint8_t *p;
  size_t n;
} field_t;
static int varint(reader_t *r, uint64_t *v) {
  *v = 0;
  for (unsigned i = 0; i < 10; ++i) {
    if (r->p == r->end)
      return -1;
    unsigned b = *r->p++;
    if (i == 9 && b > 1)
      return -1;
    *v |= (uint64_t)(b & 127) << (7 * i);
    if (!(b & 128))
      return 0;
  }
  return -1;
}
static int field(reader_t *r, field_t *f) {
  if (r->p == r->end)
    return 0;
  uint64_t tag, v = 0;
  if (varint(r, &tag) || tag < 8 || tag > UINT32_MAX)
    return -1;
  memset(f, 0, sizeof(*f));
  f->field = tag >> 3;
  f->type = tag & 7;
  if (!f->type) {
    if (varint(r, &v))
      return -1;
    f->value = v;
    return 1;
  }
  size_t n;
  if (f->type == 1)
    n = 8;
  else if (f->type == 5)
    n = 4;
  else if (f->type == 2) {
    if (varint(r, &v) || v > (uint64_t)(r->end - r->p))
      return -1;
    n = v;
  } else
    return -1;
  if (n > (size_t)(r->end - r->p))
    return -1;
  f->p = r->p;
  f->n = n;
  if (f->type == 5)
    f->value = get32(r->p);
  r->p += n;
  return 1;
}
static size_t vint(uint8_t *p, uint64_t v) {
  size_t n = 0;
  do {
    p[n++] = (v & 127) | (v > 127 ? 128 : 0);
    v >>= 7;
  } while (v);
  return n;
}
static size_t bytes(uint8_t *p, unsigned tag, const void *data, size_t n) {
  size_t k = vint(p, tag * 8 + 2);
  k += vint(p + k, n);
  memcpy(p + k, data, n);
  return k + n;
}
static size_t number(uint8_t *p, unsigned tag, uint64_t v) {
  size_t n = vint(p, tag * 8);
  return n + vint(p + n, v);
}
static size_t fixed(uint8_t *p, unsigned tag, uint32_t v) {
  size_t n = vint(p, tag * 8 + 5);
  put32(p + n, v);
  return n + 4;
}
size_t pos_mesh_data_encode(const pos_mesh_data_t *d, uint8_t *p, size_t cap) {
  uint8_t b[POS_MESH_WIRE + 32];
  size_t n = 0;
  if (!d || !d->port || d->port > 65535 || d->length > POS_MESH_PAYLOAD)
    return 0;
  n += number(b + n, 1, d->port);
  if (d->length)
    n += bytes(b + n, 2, d->payload, d->length);
  if (d->want_response)
    n += number(b + n, 3, 1);
  if (d->request_id)
    n += fixed(b + n, 6, d->request_id);
  if (d->reply_id)
    n += fixed(b + n, 7, d->reply_id);
  if (n > cap)
    return 0;
  memcpy(p, b, n);
  return n;
}
int pos_mesh_data_decode(const void *data, size_t n, pos_mesh_data_t *d) {
  if (!data || !n || n > 239)
    return -1;
  memset(d, 0, sizeof(*d));
  reader_t r = {data, (const uint8_t *)data + n};
  field_t f;
  int more;
  unsigned seen = 0;
  while ((more = field(&r, &f)) > 0) {
    if (f.field <= 9) {
      unsigned bit = 1u << f.field;
      if (seen & bit)
        return -1;
      seen |= bit;
    }
    switch (f.field) {
    case 1:
      if (f.type || !f.value || f.value > 65535)
        return -1;
      d->port = f.value;
      break;
    case 2:
      if (f.type != 2 || f.n > sizeof(d->payload))
        return -1;
      memcpy(d->payload, f.p, f.n);
      d->length = f.n;
      break;
    case 3:
      if (f.type || f.value > 1)
        return -1;
      d->want_response = f.value;
      break;
    case 6:
    case 7:
      if (f.type != 5)
        return -1;
      if (f.field == 6)
        d->request_id = f.value;
      else
        d->reply_id = f.value;
      break;
    default:
      break;
    }
  }
  return more < 0 || !d->port ? -1 : 0;
}
static int base64(const char *text, uint8_t *out, size_t cap) {
  uint32_t value = 0;
  unsigned bits = 0, padding = 0;
  size_t n = 0, digits = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    if (*p == '=') {
      if (++padding > 2)
        return -1;
      continue;
    }
    if (padding)
      return -1;
    unsigned v;
    if (*p >= 'A' && *p <= 'Z')
      v = *p - 'A';
    else if (*p >= 'a' && *p <= 'z')
      v = *p - 'a' + 26;
    else if (*p >= '0' && *p <= '9')
      v = *p - '0' + 52;
    else if (*p == '+' || *p == '-')
      v = 62;
    else if (*p == '/' || *p == '_')
      v = 63;
    else
      return -1;
    value = (value << 6) | v;
    bits += 6;
    ++digits;
    if (bits >= 8) {
      bits -= 8;
      if (n == cap)
        return -1;
      out[n++] = value >> bits;
    }
  }
  if (digits % 4 == 1 || (bits && (value & ((1u << bits) - 1))) ||
      (padding && (digits + padding) % 4))
    return -1;
  return (int)n;
}
int pos_mesh_key_parse(const char *text, pos_mesh_channel_t *out) {
  pos_mesh_channel_t copy = *out;
  uint8_t key[32] = {0};
  size_t n = strlen(text);
  int length;
  if ((n == 32 || n == 64) && strspn(text, "0123456789abcdefABCDEF") == n) {
    length = (int)n / 2;
    for (int i = 0; i < length; ++i) {
      char b[3] = {text[i * 2], text[i * 2 + 1], 0};
      unsigned v = 0;
      for (unsigned j = 0; j < 2; ++j) {
        unsigned x = (unsigned char)b[j];
        v = v * 16 + (x <= '9' ? x - '0' : (x | 32) - 'a' + 10);
      }
      key[i] = v;
    }
  } else
    length = base64(text, key, sizeof(key));
  if (length != 1 && length != 16 && length != 32) {
    pos_secure_zero(key, 32);
    return -1;
  }
  memcpy(copy.key, key, 32);
  copy.key_len = length;
  copy.enabled = true;
  *out = copy;
  pos_secure_zero(key, 32);
  return 0;
}
int pos_mesh_import_url(const char *text, pos_mesh_config_t *c) {
  static const char prefix[] = "https://meshtastic.org/e/#";
  if (strncmp(text, prefix, sizeof(prefix) - 1))
    return -1;
  uint8_t payload[1024];
  int n = base64(text + sizeof(prefix) - 1, payload, sizeof(payload));
  if (n <= 0)
    return -1;
  pos_mesh_config_t copy = *c;
  memset(copy.channels, 0, sizeof(copy.channels));
  reader_t r = {payload, payload + n};
  field_t f;
  int more;
  unsigned count = 0;
  bool lora_seen = false;
  while ((more = field(&r, &f)) > 0) {
    if (f.field == 1) {
      if (f.type != 2 || count == 8)
        return -1;
      pos_mesh_channel_t *ch = &copy.channels[count++];
      ch->enabled = true;
      reader_t sub = {f.p, f.p + f.n};
      field_t q;
      int next;
      while ((next = field(&sub, &q)) > 0) {
        if (q.field == 2) {
          if (q.type != 2 || q.n > 32)
            return -1;
          memcpy(ch->key, q.p, q.n);
          ch->key_len = q.n;
          if (q.n > 1 && q.n < 16)
            ch->key_len = 16;
          else if (q.n > 16 && q.n < 32)
            ch->key_len = 32;
        } else if (q.field == 3) {
          if (q.type != 2 || q.n >= sizeof(ch->name) || memchr(q.p, 0, q.n))
            return -1;
          memcpy(ch->name, q.p, q.n);
          ch->name[q.n] = 0;
        }
      }
      if (next < 0)
        return -1;
    } else if (f.field == 2) {
      if (f.type != 2 || lora_seen)
        return -1;
      lora_seen = true;
      reader_t sub = {f.p, f.p + f.n};
      field_t q;
      int next;
      bool use_preset = false;
      unsigned coding = 0;
      while ((next = field(&sub, &q)) > 0) {
        if (q.field == 6 || q.field == 14) {
          if (q.type != 5 || (q.value & 0x7fffffff))
            return -1;
        } else if (q.field == 1 || q.field == 2 || q.field == 5 ||
                   q.field == 7 || q.field == 8 || q.field == 11) {
          if (q.type || q.value > 65535)
            return -1;
          if (q.field == 1)
            use_preset = q.value == 1;
          if (q.field == 2) {
            if (q.value >= 10)
              return -1;
            copy.preset = q.value;
          }
          if (q.field == 5)
            coding = q.value;
          if (q.field == 7 && q.value) {
            if (q.value != 1 && q.value != 3)
              return -1;
            copy.region = q.value;
          }
          if (q.field == 8) {
            if (q.value > 7)
              return -1;
            copy.hops = q.value;
          }
          if (q.field == 11)
            copy.slot = q.value;
        }
      }
      pos_mesh_modem_t m;
      if (next < 0 || !use_preset || pos_mesh_modem(&copy, &m) ||
          (coding && coding != m.cr))
        return -1;
    }
  }
  if (more < 0 || !count || pos_mesh_config_valid(&copy))
    return -1;
  *c = copy;
  pos_secure_zero(payload, sizeof(payload));
  return 0;
}
int pos_mesh_header(const void *wire, size_t n, pos_mesh_header_t *h) {
  if (!wire || n < 18 || n > 255)
    return -1;
  const uint8_t *p = wire;
  *h = (pos_mesh_header_t){.to = get32(p),
                           .from = get32(p + 4),
                           .id = get32(p + 8),
                           .flags = p[12],
                           .channel = p[13],
                           .next_hop = p[14],
                           .relay = p[15]};
  return !h->to || h->from < 4 || h->from == UINT32_MAX || !h->id ? -1 : 0;
}
static void nonce(uint8_t p[16], uint32_t from, uint32_t id, uint32_t extra) {
  memset(p, 0, 16);
  put32(p, id);
  put32(p + 4, extra);
  put32(p + 8, from);
}
int pos_mesh_ctr(const uint8_t *key, size_t k, uint32_t from, uint32_t id,
                 uint8_t *data, size_t n) {
  if (n > 239 || (k != 0 && k != 16 && k != 32))
    return -1;
  if (!k)
    return 0;
  uint8_t iv[16];
  nonce(iv, from, id, 0);
  int bad;
#ifdef ESP_PLATFORM
  uint8_t stream[16] = {0};
  size_t off = 0;
  mbedtls_aes_context a;
  mbedtls_aes_init(&a);
  bad = mbedtls_aes_setkey_enc(&a, key, k * 8) ||
        mbedtls_aes_crypt_ctr(&a, n, &off, iv, stream, data, data);
  mbedtls_aes_free(&a);
  pos_secure_zero(stream, sizeof(stream));
#else
  EVP_CIPHER_CTX *a = EVP_CIPHER_CTX_new();
  int out = 0, tail = 0;
  bad = !a ||
        EVP_EncryptInit_ex(a, k == 16 ? EVP_aes_128_ctr() : EVP_aes_256_ctr(),
                           NULL, key, iv) != 1 ||
        EVP_EncryptUpdate(a, data, &out, data, (int)n) != 1 ||
        EVP_EncryptFinal_ex(a, data + out, &tail) != 1;
  EVP_CIPHER_CTX_free(a);
#endif
  pos_secure_zero(iv, sizeof(iv));
  return bad ? -1 : 0;
}
#ifdef ESP_PLATFORM
static int rng(void *ctx, unsigned char *p, size_t n) {
  (void)ctx;
  esp_fill_random(p, n);
  return 0;
}
#endif
static int curve(const uint8_t private_key[32], const uint8_t *peer,
                 uint8_t out[32]) {
  int bad = 1;
#ifdef ESP_PLATFORM
  mbedtls_ecp_group g;
  mbedtls_ecp_point q, result;
  mbedtls_mpi d, z;
  mbedtls_ecp_group_init(&g);
  mbedtls_ecp_point_init(&q);
  mbedtls_ecp_point_init(&result);
  mbedtls_mpi_init(&d);
  mbedtls_mpi_init(&z);
  uint8_t clamped[32];
  memcpy(clamped, private_key, 32);
  clamped[0] &= 248;
  clamped[31] &= 127;
  clamped[31] |= 64;
  if (mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_CURVE25519) ||
      mbedtls_mpi_read_binary_le(&d, clamped, 32))
    goto done;
  if (!peer) {
    if (!mbedtls_ecp_mul(&g, &result, &d, &g.G, rng, NULL) &&
        !mbedtls_mpi_write_binary_le(&result.MBEDTLS_PRIVATE(X), out, 32))
      bad = 0;
  } else if (!mbedtls_mpi_read_binary_le(&q.MBEDTLS_PRIVATE(X), peer, 32) &&
             !mbedtls_mpi_lset(&q.MBEDTLS_PRIVATE(Z), 1) &&
             !mbedtls_ecdh_compute_shared(&g, &z, &q, &d, rng, NULL) &&
             !mbedtls_mpi_write_binary_le(&z, out, 32))
    bad = 0;
done:
  pos_secure_zero(clamped, sizeof(clamped));
  mbedtls_ecp_group_free(&g);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_point_free(&result);
  mbedtls_mpi_free(&d);
  mbedtls_mpi_free(&z);
#else
  EVP_PKEY *key =
      EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, private_key, 32);
  EVP_PKEY *remote = NULL;
  EVP_PKEY_CTX *ctx = NULL;
  size_t n = 32;
  if (!key)
    goto done;
  if (!peer)
    bad = EVP_PKEY_get_raw_public_key(key, out, &n) != 1 || n != 32;
  else {
    remote = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer, 32);
    ctx = EVP_PKEY_CTX_new(key, NULL);
    bad = !ctx || !remote || EVP_PKEY_derive_init(ctx) != 1 ||
          EVP_PKEY_derive_set_peer(ctx, remote) != 1 ||
          EVP_PKEY_derive(ctx, out, &n) != 1 || n != 32;
  }
done:
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(remote);
  EVP_PKEY_free(key);
#endif
  unsigned any = 0;
  if (!bad)
    for (unsigned i = 0; i < 32; ++i)
      any |= out[i];
  if (bad || !any) {
    pos_secure_zero(out, 32);
    return -1;
  }
  return 0;
}
int pos_mesh_public(const uint8_t key[32], uint8_t out[32]) {
  return curve(key, NULL, out);
}
int pos_mesh_pki(bool encrypt, const uint8_t private_key[32],
                 const uint8_t peer_key[32], uint32_t from, uint32_t id,
                 uint32_t extra, const uint8_t *input, size_t n, uint8_t *out) {
  if ((!encrypt && n < 12) || n > (encrypt ? 227u : 239u))
    return -1;
  uint8_t shared[32] = {0}, key[32] = {0}, iv[16];
  int bad = 1;
  if (curve(private_key, peer_key, shared))
    goto done;
  if (!encrypt) {
    n -= 12;
    extra = get32(input + n + 8);
  }
  nonce(iv, from, id, extra);
#ifdef ESP_PLATFORM
  mbedtls_ccm_context c;
  mbedtls_ccm_init(&c);
  if (!mbedtls_sha256(shared, 32, key, 0) &&
      !mbedtls_ccm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 256)) {
    bad = encrypt ? mbedtls_ccm_encrypt_and_tag(&c, n, iv, 13, NULL, 0, input,
                                                out, out + n, 8)
                  : mbedtls_ccm_auth_decrypt(&c, n, iv, 13, NULL, 0, input, out,
                                             input + n, 8);
  }
  mbedtls_ccm_free(&c);
#else
  unsigned kn = 0;
  int count = 0;
  EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
  if (c && EVP_Digest(shared, 32, key, &kn, EVP_sha256(), NULL) == 1 &&
      kn == 32) {
    if (encrypt)
      bad = EVP_EncryptInit_ex(c, EVP_aes_256_ccm(), NULL, NULL, NULL) != 1 ||
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_IVLEN, 13, NULL) != 1 ||
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_TAG, 8, NULL) != 1 ||
            EVP_EncryptInit_ex(c, NULL, NULL, key, iv) != 1 ||
            EVP_EncryptUpdate(c, NULL, &count, NULL, (int)n) != 1 ||
            EVP_EncryptUpdate(c, out, &count, input, (int)n) != 1 ||
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_GET_TAG, 8, out + n) != 1;
    else
      bad = EVP_DecryptInit_ex(c, EVP_aes_256_ccm(), NULL, NULL, NULL) != 1 ||
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_IVLEN, 13, NULL) != 1 ||
            EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_CCM_SET_TAG, 8,
                                (void *)(input + n)) != 1 ||
            EVP_DecryptInit_ex(c, NULL, NULL, key, iv) != 1 ||
            EVP_DecryptUpdate(c, NULL, &count, NULL, (int)n) != 1 ||
            EVP_DecryptUpdate(c, out, &count, input, (int)n) != 1;
  }
  EVP_CIPHER_CTX_free(c);
#endif
  if (!bad && encrypt)
    put32(out + n + 8, extra);
done:
  pos_secure_zero(shared, 32);
  pos_secure_zero(key, 32);
  pos_secure_zero(iv, sizeof(iv));
  if (bad)
    pos_secure_zero(out, encrypt ? n + 12 : n);
  return bad ? -1 : 0;
}

static pos_mesh_node_t *node(pos_mesh_t *s, uint32_t id, bool create) {
  pos_mesh_node_t *empty = NULL;
  for (unsigned i = 0; i < POS_MESH_NODES; ++i) {
    if (s->nodes[i].id == id)
      return &s->nodes[i];
    if (!s->nodes[i].id)
      empty = &s->nodes[i];
  }
  /* Do not evict a pinned identity to accept unsolicited node advertisements.
   */
  if (!create || !empty)
    return NULL;
  empty->id = id;
  return empty;
}
int pos_mesh_init(pos_mesh_t *s, uint32_t id, const pos_mesh_config_t *c,
                  const uint8_t key[32]) {
  if (!s || id < 4 || id == UINT32_MAX || pos_mesh_config_valid(c))
    return -1;
  memset(s, 0, sizeof(*s));
  s->node = id;
  s->config = *c;
  s->active_tx = -1;
  memcpy(s->private_key, key, 32);
  return pos_mesh_public(key, s->public_key);
}
size_t pos_mesh_encode(pos_mesh_t *s, const pos_mesh_message_t *m, uint8_t *w) {
  const pos_mesh_header_t *h = &m->header;
  if (!h->to || h->from < 4 || h->from == UINT32_MAX || !h->id ||
      m->channel_index >= POS_MESH_CHANNELS)
    return 0;
  uint8_t p[239];
  size_t n = pos_mesh_data_encode(&m->data, p, m->pki ? 227 : 239);
  if (!n)
    return 0;
  uint8_t hash;
  if (m->pki) {
    pos_mesh_node_t *peer = node(s, h->to, false);
    if (!peer || !peer->has_key || peer->key_changed || !s->random ||
        h->to == UINT32_MAX ||
        pos_mesh_pki(true, s->private_key, peer->public_key, h->from, h->id,
                     s->random(s->context), p, n, w + 16))
      return 0;
    n += 12;
    hash = 0;
  } else {
    uint8_t key[32];
    int k = pos_mesh_key(&s->config, m->channel_index, key);
    int bad =
        k < 0 || pos_mesh_ctr(key, k < 0 ? 0 : (size_t)k, h->from, h->id, p, n);
    pos_secure_zero(key, sizeof(key));
    if (bad)
      return 0;
    memcpy(w + 16, p, n);
    hash = pos_mesh_channel_hash(&s->config, m->channel_index);
  }
  put32(w, h->to);
  put32(w + 4, h->from);
  put32(w + 8, h->id);
  w[12] = h->flags;
  w[13] = hash;
  w[14] = h->next_hop;
  w[15] = h->relay;
  pos_secure_zero(p, sizeof(p));
  return n + 16;
}
int pos_mesh_decode(pos_mesh_t *s, const void *wire, size_t n,
                    pos_mesh_message_t *m) {
  memset(m, 0, sizeof(*m));
  if (pos_mesh_header(wire, n, &m->header))
    return -1;
  const uint8_t *w = wire;
  uint8_t plain[239];
  size_t body = n - 16;
  if (m->header.channel == 0 && m->header.to == s->node) {
    pos_mesh_node_t *peer = node(s, m->header.from, false);
    if (peer && peer->has_key && !peer->key_changed && body >= 14 &&
        !pos_mesh_pki(false, s->private_key, peer->public_key, m->header.from,
                      m->header.id, 0, w + 16, body, plain) &&
        !pos_mesh_data_decode(plain, body - 12, &m->data)) {
      m->pki = true;
      pos_secure_zero(plain, sizeof(plain));
      return 0;
    }
  }
  for (unsigned i = 0; i < POS_MESH_CHANNELS; ++i) {
    if (!s->config.channels[i].enabled ||
        pos_mesh_channel_hash(&s->config, i) != m->header.channel)
      continue;
    uint8_t key[32];
    int k = pos_mesh_key(&s->config, i, key);
    memcpy(plain, w + 16, body);
    int bad = k < 0 || pos_mesh_ctr(key, k < 0 ? 0 : (size_t)k, m->header.from,
                                    m->header.id, plain, body);
    pos_secure_zero(key, 32);
    if (!bad && !pos_mesh_data_decode(plain, body, &m->data)) {
      m->channel_index = i;
      pos_secure_zero(plain, sizeof(plain));
      return 0;
    }
  }
  pos_secure_zero(plain, sizeof(plain));
  return -1;
}
static uint32_t random_value(pos_mesh_t *s) {
  return s->random ? s->random(s->context) : 0;
}
static int event(pos_mesh_t *s, pos_mesh_event_t e,
                 const pos_mesh_message_t *m) {
  if (e != POS_MESH_RX) {
    s->stats.last_id = m->header.id;
    s->stats.last_event = e;
  }
  return s->sink ? s->sink(s->context, e, m) : 0;
}
static pos_mesh_tx_t *queue(pos_mesh_t *s, const uint8_t *wire, size_t n,
                            bool own, unsigned priority, uint64_t now) {
  for (unsigned i = 0; i < POS_MESH_QUEUE; ++i)
    if (!s->queue[i].used) {
      pos_mesh_tx_t *q = &s->queue[i];
      memset(q, 0, sizeof(*q));
      memcpy(q->wire, wire, n);
      q->length = n;
      q->used = true;
      q->own = own;
      q->priority = priority;
      q->reliable = own && (wire[12] & 8);
      q->due_ms = now + 100 + random_value(s) % 600;
      q->expires_ms = now + 30 * 60 * 1000;
      return q;
    }
  ++s->stats.queue_full;
  return NULL;
}
static int send_message(pos_mesh_t *s, pos_mesh_message_t *m, uint64_t now,
                        unsigned priority) {
  if (!s->next_id || !(m->header.id = s->next_id(s->context)))
    return -1;
  m->header.from = s->node;
  m->header.relay = s->node & 255;
  m->header.flags |= s->config.hops | (s->config.hops << 5);
  uint8_t w[255];
  size_t n = pos_mesh_encode(s, m, w);
  if (!n || !queue(s, w, n, true, priority, now))
    return -1;
  return 0;
}
int pos_mesh_send(pos_mesh_t *s, unsigned ch, uint32_t to, unsigned port,
                  const void *p, size_t n, bool response, uint64_t now) {
  if (!to || ch >= 8 || !s->config.channels[ch].enabled ||
      n > POS_MESH_PAYLOAD || (!p && n))
    return -1;
  pos_mesh_message_t m = {
      .header = {.to = to, .flags = to != UINT32_MAX ? 8 : 0},
      .data = {.port = port, .length = n, .want_response = response},
      .channel_index = ch};
  if (n)
    memcpy(m.data.payload, p, n);
  m.pki = to != UINT32_MAX && port != POS_MESH_USER &&
          port != POS_MESH_POSITION && port != POS_MESH_ROUTING;
  return send_message(s, &m, now, 1);
}
int pos_mesh_announce(pos_mesh_t *s, uint32_t to, bool discover, uint64_t now) {
  uint8_t p[120];
  size_t n = 0;
  char id[10];
  snprintf(id, sizeof(id), "!%08x", (unsigned)s->node);
  n += bytes(p + n, 1, id, strlen(id));
  n += bytes(p + n, 2, s->config.long_name, strlen(s->config.long_name));
  n += bytes(p + n, 3, s->config.short_name, strlen(s->config.short_name));
  n += bytes(p + n, 8, s->public_key, 32);
  return pos_mesh_send(s, 0, to, POS_MESH_USER, p, n, discover, now);
}
int pos_mesh_position(pos_mesh_t *s, unsigned ch, uint32_t to, int32_t lat,
                      int32_t lon, uint32_t utc, uint64_t now) {
  if (lat < -900000000 || lat > 900000000 || lon < -1800000000 ||
      lon > 1800000000)
    return -1;
  uint8_t p[24];
  size_t n = 0;
  n += fixed(p + n, 1, (uint32_t)lat);
  n += fixed(p + n, 2, (uint32_t)lon);
  if (utc)
    n += fixed(p + n, 4, utc);
  n += number(p + n, 5, 2); /* INTERNAL GPS */
  return pos_mesh_send(s, ch, to, POS_MESH_POSITION, p, n, false, now);
}
static int update_node(pos_mesh_t *s, const pos_mesh_message_t *m, uint64_t now,
                       int rssi, int snr) {
  pos_mesh_node_t *n = node(s, m->header.from, true);
  if (!n)
    return 0;
  n->seen_ms = now;
  n->rssi = rssi;
  n->snr = snr;
  n->hops = (m->header.flags >> 5) - (m->header.flags & 7);
  if (n->hops < 0)
    n->hops = 0;
  if (m->data.port != POS_MESH_USER && m->data.port != POS_MESH_POSITION)
    return 0;
  reader_t r = {m->data.payload, m->data.payload + m->data.length};
  field_t f;
  int more;
  pos_mesh_node_t copy = *n;
  unsigned coords = 0;
  while ((more = field(&r, &f)) > 0) {
    if (m->data.port == POS_MESH_USER) {
      if (f.field == 2 || f.field == 3) {
        char *v = f.field == 2 ? copy.long_name : copy.short_name;
        size_t cap =
            f.field == 2 ? sizeof(copy.long_name) : sizeof(copy.short_name);
        if (f.type != 2 || f.n >= cap || memchr(f.p, 0, f.n))
          return -1;
        memcpy(v, f.p, f.n);
        v[f.n] = 0;
        if (!string_ok(v, cap))
          return -1;
      } else if (f.field == 8) {
        if (f.type != 2 || f.n != 32)
          return -1;
        if (copy.has_key && memcmp(copy.public_key, f.p, 32))
          copy.key_changed = true;
        else {
          memcpy(copy.public_key, f.p, 32);
          copy.has_key = true;
        }
      }
    } else {
      if (f.field == 1 || f.field == 2) {
        if (f.type != 5)
          return -1;
        if (f.field == 1) {
          copy.latitude = (int32_t)f.value;
          coords |= 1;
        } else {
          copy.longitude = (int32_t)f.value;
          coords |= 2;
        }
      } else if (f.field == 4) {
        if (f.type != 5)
          return -1;
        copy.position_time = f.value;
      }
    }
  }
  if (more < 0)
    return -1;
  if (m->data.port == POS_MESH_POSITION) {
    if (coords != 3 || copy.latitude < -900000000 ||
        copy.latitude > 900000000 || copy.longitude < -1800000000 ||
        copy.longitude > 1800000000)
      return -1;
    copy.has_position = true;
  }
  *n = copy;
  return 0;
}
static unsigned routing_error(const pos_mesh_data_t *d) {
  reader_t r = {d->payload, d->payload + d->length};
  field_t f;
  int more;
  unsigned error = 0;
  while ((more = field(&r, &f)) > 0) {
    if (f.field == 3) {
      if (f.type || f.value > UINT_MAX)
        return UINT_MAX;
      error = f.value;
    } else if (f.field == 1 || f.field == 2)
      return UINT_MAX;
  }
  return more < 0 ? UINT_MAX : error;
}
static void acknowledge(pos_mesh_t *s, const pos_mesh_message_t *rx,
                        uint64_t now) {
  if (!(rx->header.flags & 8) || rx->header.to != s->node)
    return;
  if (rx->data.request_id || rx->data.reply_id) {
    /* ACK an ACK only for direct/selected next-hop receipt, never recursively.
     */
    if (!(rx->header.flags & 7) || rx->header.next_hop)
      return;
  }
  pos_mesh_message_t m = {
      .header = {.to = rx->header.from},
      .data = {.port = POS_MESH_ROUTING, .request_id = rx->header.id},
      .channel_index = rx->channel_index};
  m.data.payload[0] = 24;
  m.data.payload[1] = 0;
  m.data.length = 2;
  (void)send_message(s, &m, now, 0);
}
int pos_mesh_receive(pos_mesh_t *s, const void *wire, size_t length, int rssi,
                     int snr, uint64_t now) {
  pos_mesh_header_t h;
  if (pos_mesh_header(wire, length, &h)) {
    ++s->stats.rejected;
    return -1;
  }
  const uint8_t *w = wire;
  if (h.from == s->node) {
    /* Hearing our broadcast forwarded confirms a relay, never DM delivery. */
    for (unsigned i = 0; i < POS_MESH_QUEUE; ++i) {
      pos_mesh_tx_t *q = &s->queue[i];
      if (q->used && q->own && get32(q->wire) == UINT32_MAX &&
          get32(q->wire + 8) == h.id && q->length == length &&
          !memcmp(q->wire + 16, w + 16, length - 16)) {
        q->used = false;
        s->stats.last_id = h.id;
        s->stats.last_event = POS_MESH_RELAYED;
      }
    }
    return 0;
  }
  bool duplicate = false;
  pos_mesh_seen_t *old = NULL;
  for (unsigned i = 0; i < 128; ++i)
    if (s->seen[i].from == h.from && s->seen[i].id == h.id &&
        now >= s->seen[i].at_ms && now - s->seen[i].at_ms < 15 * 60 * 1000) {
      duplicate = true;
      old = &s->seen[i];
      break;
    }
  if (duplicate)
    ++s->stats.duplicates;
  pos_mesh_message_t m;
  bool decoded = !pos_mesh_decode(s, wire, length, &m);
  bool ours = h.to == s->node || h.to == UINT32_MAX;
  if (decoded && ours) {
    if (!duplicate) {
      if (update_node(s, &m, now, rssi, snr)) {
        ++s->stats.rejected;
        return -1;
      }
      if (event(s, POS_MESH_RX, &m))
        return -1;
      ++s->stats.received;
    }
    if (h.to == s->node && m.data.request_id) {
      unsigned err =
          m.data.port == POS_MESH_ROUTING ? routing_error(&m.data) : 0;
      if (err != UINT_MAX)
        for (unsigned i = 0; i < POS_MESH_QUEUE; ++i) {
          pos_mesh_tx_t *q = &s->queue[i];
          if (q->used && q->own && q->reliable && get32(q->wire) == h.from &&
              get32(q->wire + 8) == m.data.request_id) {
            q->used = false;
            pos_mesh_message_t original = {.header = {.id = m.data.request_id,
                                                      .to = h.from,
                                                      .from = s->node}};
            if (!err)
              ++s->stats.acked;
            else
              ++s->stats.failed;
            event(s, err ? POS_MESH_NAK : POS_MESH_ACKED, &original);
          }
        }
    }
    acknowledge(s, &m, now);
    if (!duplicate && m.data.want_response &&
        (m.data.port == POS_MESH_USER || m.data.port == POS_MESH_REPLY) &&
        (!s->last_reply_ms || now - s->last_reply_ms >= 60000)) {
      if (m.data.port == POS_MESH_USER)
        (void)pos_mesh_announce(s, h.from, false, now);
      else
        (void)pos_mesh_send(s, m.channel_index, h.from, POS_MESH_REPLY, "POS",
                            3, false, now);
      s->last_reply_ms = now ? now : 1;
    }
  }
  if (!duplicate)
    s->seen[s->seen_next++ % 128] =
        (pos_mesh_seen_t){h.from, h.id, now, h.flags & 7};
  /* CLIENT-style delayed flooding, including opaque encrypted traffic. A
   * selected next-hop is respected; all outgoing DMs use flooding fallback. */
  for (unsigned i = 0; i < POS_MESH_QUEUE; ++i) {
    pos_mesh_tx_t *q = &s->queue[i];
    if (q->used && !q->own && get32(q->wire + 4) == h.from &&
        get32(q->wire + 8) == h.id && (h.flags & 7) <= (q->wire[12] & 7))
      q->used = false;
  }
  if (s->config.relay && h.to != s->node && (h.flags & 7) > 0 &&
      (!h.next_hop || h.next_hop == (s->node & 255)) &&
      (!duplicate || (old && (h.flags & 7) > old->hops))) {
    uint8_t relay[255];
    memcpy(relay, wire, length);
    relay[12] = (h.flags & 0xf8) | ((h.flags & 7) - 1);
    relay[15] = s->node & 255;
    if (relay[14])
      relay[14] = 0;
    (void)queue(s, relay, length, false, 2, now);
    if (old)
      old->hops = h.flags & 7;
  }
  return decoded && ours && !duplicate ? 1 : 0;
}
int pos_mesh_due(pos_mesh_t *s, uint64_t now, const uint8_t **wire, size_t *n) {
  if (s->active_tx >= 0 && s->queue[s->active_tx].used &&
      now < s->queue[s->active_tx].expires_ms) {
    *wire = s->queue[s->active_tx].wire;
    *n = s->queue[s->active_tx].length;
    return 1;
  }
  s->active_tx = -1;
  int best = -1;
  bool expired = false;
  for (unsigned i = 0; i < POS_MESH_QUEUE; ++i) {
    pos_mesh_tx_t *q = &s->queue[i];
    if (!q->used)
      continue;
    if (now >= q->expires_ms || (q->attempts >= 4 && now >= q->due_ms)) {
      if (q->own) {
        pos_mesh_message_t m = {.header = {.id = get32(q->wire + 8),
                                           .from = s->node,
                                           .to = get32(q->wire)}};
        ++s->stats.failed;
        event(s, POS_MESH_TIMEOUT, &m);
      }
      q->used = false;
      expired = true;
      continue;
    }
    if (now >= q->due_ms &&
        (best < 0 || q->priority < s->queue[best].priority ||
         (q->priority == s->queue[best].priority &&
          q->due_ms < s->queue[best].due_ms)))
      best = i;
  }
  if (best < 0)
    return expired ? -1 : 0;
  *wire = s->queue[best].wire;
  *n = s->queue[best].length;
  s->active_tx = best;
  return 1;
}
void pos_mesh_tx_result(pos_mesh_t *s, bool success, uint64_t now) {
  int active = s->active_tx;
  s->active_tx = -1;
  if (active >= 0 && active < POS_MESH_QUEUE) {
    pos_mesh_tx_t *q = &s->queue[active];
    if (!q->used)
      return;
    ++q->attempts;
    if (!success) {
      q->due_ms = now + 3000 + random_value(s) % 5000;
      return;
    }
    pos_mesh_message_t m = {.header = {.id = get32(q->wire + 8),
                                       .from = get32(q->wire + 4),
                                       .to = get32(q->wire)}};
    if (q->own) {
      ++s->stats.sent;
      event(s, POS_MESH_SENT, &m);
    } else {
      ++s->stats.relayed;
      event(s, POS_MESH_FORWARDED, &m);
    }
    if (!q->reliable)
      q->used = false;
    else
      q->due_ms =
          now + 45000 + random_value(s) % 15000 + (uint64_t)q->attempts * 15000;
    return;
  }
}
void pos_mesh_cancel(pos_mesh_t *s) {
  memset(s->queue, 0, sizeof(s->queue));
  s->active_tx = -1;
}
