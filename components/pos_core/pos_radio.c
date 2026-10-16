#include "pos_radio.h"
#include "pos_files.h"
#include "pos_fs.h"
#include <string.h>
#include <unistd.h>
static void put(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = value >> (i * 8);
}
static uint32_t get(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static bool valid(const pos_radio_packet_t *p) {
  if (!p || p->type < POS_RADIO_MSG || p->type > POS_RADIO_STATUS ||
      !p->sender || !p->id || !memchr(p->text, 0, sizeof(p->text)))
    return false;
  for (const unsigned char *c = (const unsigned char *)p->text; *c; ++c)
    if (*c < 32 || *c >= 127)
      return false;
  return true;
}
int pos_radio_store(const char *root, const pos_radio_packet_t *packet,
                    pos_error_t *e) {
  char relative[128], full[POS_FS_PATH], part[POS_FS_PATH];
  uint8_t wire[POS_RADIO_WIRE_MAX];
  size_t length = pos_radio_encode(packet, wire);
  snprintf(relative, sizeof(relative), "POS/Users/Owner/Radio/outbox/%08x.posr",
           (unsigned)packet->id);
  if (!length || !pos_files_mutable(root, relative) ||
      pos_fs_join(full, root, relative) ||
      snprintf(part, sizeof(part), "%s.part", full) >= (int)sizeof(part))
    goto failed;
  if (pos_fs_write_new(part, wire, length))
    goto failed;
  uint32_t crc;
  if (pos_fs_crc_file(part, length, &crc) ||
      crc != pos_crc32(0, wire, length) || pos_fs_publish_new(part, full))
    goto failed;
  return 0;
failed:
  if (e)
    snprintf(e->message, sizeof(e->message),
             "Message could not be committed; existing outbox/partial files "
             "preserved.");
  return -1;
}
size_t pos_radio_encode(const pos_radio_packet_t *p,
                        uint8_t wire[POS_RADIO_WIRE_MAX]) {
  if (!valid(p))
    return 0;
  size_t n = strlen(p->text);
  memcpy(wire, "POSR", 4);
  wire[4] = 1;
  wire[5] = p->type;
  put(wire + 6, p->sender);
  put(wire + 10, p->destination);
  put(wire + 14, p->id);
  wire[18] = n;
  memcpy(wire + 19, p->text, n);
  put(wire + 19 + n, pos_crc32(0, wire, 19 + n));
  return n + 23;
}
int pos_radio_decode(const void *data, size_t length, pos_radio_packet_t *p) {
  const uint8_t *w = data;
  if (length < 23 || memcmp(w, "POSR", 4) || w[4] != 1 || w[18] > 160 ||
      length != 23u + w[18] ||
      get(w + 19 + w[18]) != pos_crc32(0, w, 19 + w[18]))
    return -1;
  memset(p, 0, sizeof(*p));
  p->type = w[5];
  p->sender = get(w + 6);
  p->destination = get(w + 10);
  p->id = get(w + 14);
  memcpy(p->text, w + 19, w[18]);
  return valid(p) ? 0 : -1;
}
int pos_radio_receive(pos_radio_state_t *s, const pos_radio_packet_t *p,
                      pos_radio_packet_t *ack) {
  memset(ack, 0, sizeof(*ack));
  if (!valid(p) || p->sender == s->node ||
      (p->destination && p->destination != s->node))
    return -1;
  if (p->type == POS_RADIO_ACK) {
    for (unsigned i = 0; i < 4; ++i)
      if (s->pending[i].used && s->pending[i].packet.id == p->id &&
          s->pending[i].packet.destination == p->sender) {
        s->pending[i].used = false;
        ++s->acked;
      }
    return 0;
  }
  /* Broadcasts deliberately have no ACK storm. Unicast retries are ACKed even
   * when duplicate. */
  if (p->destination) {
    ack->type = POS_RADIO_ACK;
    ack->sender = s->node;
    ack->destination = p->sender;
    ack->id = p->id;
  }
  for (unsigned i = 0; i < 32; ++i)
    if (s->seen[i].sender == p->sender && s->seen[i].id == p->id) {
      ++s->duplicates;
      return 0;
    }
  s->seen[s->next_seen++ % 32] = (pos_radio_seen_t){p->sender, p->id};
  ++s->received;
  return 1;
}
int pos_radio_enqueue(pos_radio_state_t *s, const pos_radio_packet_t *p,
                      uint64_t now) {
  if (!valid(p) || p->sender != s->node || p->type == POS_RADIO_ACK)
    return -1;
  for (unsigned i = 0; i < 4; ++i)
    if (s->pending[i].used && s->pending[i].packet.id == p->id)
      return -1;
  for (unsigned i = 0; i < 4; ++i)
    if (!s->pending[i].used) {
      s->pending[i] =
          (pos_radio_pending_t){.packet = *p, .next_ms = now, .used = true};
      return 0;
    }
  return -1;
}
int pos_radio_due(pos_radio_state_t *s, uint64_t now, pos_radio_packet_t *p) {
  for (unsigned i = 0; i < 4; ++i) {
    pos_radio_pending_t *slot = &s->pending[i];
    if (!slot->used || slot->next_ms > now)
      continue;
    *p = slot->packet;
    if (slot->attempts == 4) {
      slot->used = false;
      ++s->failed;
      return -1;
    }
    ++slot->attempts;
    ++s->sent;
    slot->next_ms = now + 15000u * slot->attempts;
    if (!p->destination)
      slot->used = false;
    return 1;
  }
  return 0;
}
int pos_range_header(FILE *f) {
  return fprintf(f, "event,monotonic_ms,gps_utc,fix,latitude,longitude,"
                    "altitude_m,satellites,hdop,peer,message_id,type,rssi_dbm,"
                    "snr_db,sent,received,acked,failed\n") < 0
             ? -1
             : 0;
}
int pos_range_row(FILE *f, const char *event, const pos_radio_packet_t *p,
                  const pos_location_t *g, int rssi, float snr,
                  const pos_radio_state_t *s, uint64_t now) {
  if (strcmp(event, "TX") && strcmp(event, "RX") && strcmp(event, "ACK") &&
      strcmp(event, "TIMEOUT") && strcmp(event, "SAMPLE"))
    return -1;
  if (fprintf(f, "%s,%llu,%lld,%u,", event, (unsigned long long)now,
              (long long)g->utc, g->fix) < 0)
    return -1;
  if (g->fix) {
    if (fprintf(f, "%.7f,%.7f,%.2f,%u,%.2f,", g->latitude, g->longitude,
                g->altitude_m, g->satellites, g->hdop) < 0)
      return -1;
  } else if (fputs(",,,,,", f) == EOF)
    return -1;
  if (fprintf(
          f, "%08x,%08x,%u,",
          (unsigned)(p ? ((!strcmp(event, "TX") || !strcmp(event, "TIMEOUT"))
                              ? p->destination
                              : p->sender)
                       : 0),
          (unsigned)(p ? p->id : 0), p ? p->type : 0) < 0)
    return -1;
  if (!strcmp(event, "RX") || !strcmp(event, "ACK")) {
    if (fprintf(f, "%d,%.2f,", rssi, (double)snr) < 0)
      return -1;
  } else if (fputs(",,", f) == EOF)
    return -1;
  return fprintf(f, "%u,%u,%u,%u\n", (unsigned)s->sent, (unsigned)s->received,
                 (unsigned)s->acked, (unsigned)s->failed) < 0 ||
                 fflush(f) || fsync(fileno(f))
             ? -1
             : 0;
}
