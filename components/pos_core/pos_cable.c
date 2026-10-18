#include "pos_cable.h"
#include <stdlib.h>
#include <string.h>
static const char hex[] = "0123456789abcdef";
static int digit(unsigned char c) {
  const char *p = strchr(hex, c);
  return c && p ? (int)(p - hex) : -1;
}
static void put(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = n >> (i * 8);
}
static uint32_t get(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
size_t pos_cable_encode(char *out, uint32_t seq, const void *data, size_t n) {
  if (!n || n > POS_CABLE_PAYLOAD)
    return 0;
  uint8_t head[4];
  put(head, seq);
  uint32_t crc = pos_crc32(pos_crc32(0, head, 4), data, n);
  size_t at = 0;
  out[at++] = 30;
  memcpy(out + at, "POS1", 4);
  at += 4;
  for (size_t i = 0; i < n + 8; ++i) {
    unsigned char b = i < 4       ? head[i]
                      : i < n + 4 ? ((const uint8_t *)data)[i - 4]
                                  : (uint8_t)(crc >> ((i - n - 4) * 8));
    out[at++] = hex[b >> 4];
    out[at++] = hex[b & 15];
  }
  out[at++] = '\n';
  return at;
}
bool pos_cable_byte(pos_cable_frame_t *f, unsigned char c) {
  if (c == 30) {
    f->reading = true;
    f->used = 0;
    return false;
  }
  if (!f->reading)
    return false;
  if (c != '\n') {
    if (f->used < sizeof(f->line))
      f->line[f->used++] = c;
    else
      f->reading = false;
    return false;
  }
  f->reading = false;
  if (f->used < 22 || (f->used & 1) || memcmp(f->line, "POS1", 4))
    return false;
  size_t bytes = (f->used - 4) / 2;
  if (bytes > sizeof(f->data) + 4)
    return false;
  uint32_t crc = 0;
  for (size_t i = 0; i < bytes; ++i) {
    int a = digit(f->line[4 + i * 2]), b = digit(f->line[5 + i * 2]);
    if (a < 0 || b < 0)
      return false;
    uint8_t v = (a << 4) | b;
    if (i < bytes - 4)
      f->data[i] = v;
    else
      crc |= (uint32_t)v << ((i - bytes + 4) * 8);
  }
  if (crc != pos_crc32(0, f->data, bytes - 4))
    return false;
  f->sequence = get(f->data);
  f->checksum = crc;
  f->length = bytes - 8;
  memmove(f->data, f->data + 4, f->length);
  return true;
}
void pos_cable_close(pos_cable_t *s) {
  pos_share_cancel(&s->receive);
  if (s->source)
    fclose(s->source);
  s->source = NULL;
}
int pos_cable_request(pos_cable_t *s, const void *data, size_t n, uint8_t *out,
                      size_t *used, pos_io_progress_t p, void *ctx,
                      pos_error_t *e) {
  const uint8_t *in = data;
  *used = 5;
  out[0] = 'K';
  put(out + 1, s->receive.offset);
  if (!n)
    goto invalid;
  if (in[0] == 'Q' && n == 1) {
    pos_cable_close(s);
    return 1;
  }
  if (!*s->selected) {
    if (in[0] == 'O' && n == 1 + POS_SHARE_OFFER_BYTES) {
      pos_share_offer_t offer;
      if (pos_share_decode(in + 1, n - 1, &offer) || offer.size > INT32_MAX)
        goto invalid;
      pos_share_cancel(&s->receive);
      int r =
          pos_share_accept_progress(&s->receive, s->root, &offer, p, ctx, e);
      if (r < 0)
        return -1;
      out[0] = r ? 'Z' : 'A';
      put(out + 1, s->receive.offset);
      return 0;
    }
    if (in[0] == 'D' && n > 5 && n <= POS_CABLE_PAYLOAD) {
      if (pos_share_write(&s->receive, get(in + 1), in + 5, n - 5, e))
        return -1;
      put(out + 1, s->receive.offset);
      return 0;
    }
    if (in[0] == 'F' && n == 1) {
      if (pos_share_finish(&s->receive, p, ctx, e))
        return -1;
      out[0] = 'Z';
      put(out + 1, s->receive.offset);
      return 0;
    }
  } else {
    if (in[0] == 'G' && n == 1) {
      if (s->source)
        fclose(s->source);
      s->source = NULL;
      if (pos_share_offer_progress(s->root, s->selected, &s->offer, p, ctx,
                                   e) ||
          s->offer.size > INT32_MAX)
        return -1;
      char path[POS_FS_PATH];
      if (pos_fs_join(path, s->root, s->selected) ||
          !(s->source = fopen(path, "rb")))
        goto invalid;
      out[0] = 'O';
      *used = 1 + POS_SHARE_OFFER_BYTES;
      return pos_share_encode(&s->offer, out + 1);
    }
    if (in[0] == 'R' && n == 5 && s->source) {
      uint32_t at = get(in + 1);
      if (at > s->offer.size || fseek(s->source, at, SEEK_SET))
        goto invalid;
      size_t take = s->offer.size - at;
      if (take > POS_CABLE_CHUNK)
        take = POS_CABLE_CHUNK;
      if (fread(out + 5, 1, take, s->source) != take)
        goto invalid;
      out[0] = 'D';
      put(out + 1, at);
      *used = take + 5;
      return 0;
    }
  }
invalid:
  if (e)
    snprintf(e->message, sizeof(e->message),
             "Invalid USB request or wrong transfer direction.");
  return -1;
}
