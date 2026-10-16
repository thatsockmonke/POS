#pragma once
#include "pos_location.h"
#include "pos_install.h"
#include <stdio.h>
#define POS_RADIO_TEXT 160
#define POS_RADIO_WIRE_MAX 188
enum {
  POS_RADIO_MSG = 1,
  POS_RADIO_PING,
  POS_RADIO_ACK,
  POS_RADIO_LOC,
  POS_RADIO_NOTE,
  POS_RADIO_STATUS
};
typedef struct {
  uint8_t type;
  uint32_t sender, destination, id;
  char text[161];
} pos_radio_packet_t;
typedef struct {
  uint32_t sender, id;
} pos_radio_seen_t;
typedef struct {
  pos_radio_packet_t packet;
  uint64_t next_ms;
  unsigned attempts;
  bool used;
} pos_radio_pending_t;
typedef struct {
  uint32_t node;
  pos_radio_seen_t seen[32];
  unsigned next_seen;
  pos_radio_pending_t pending[4];
  uint32_t sent, received, acked, failed, duplicates;
} pos_radio_state_t;
size_t pos_radio_encode(const pos_radio_packet_t *packet,
                        uint8_t wire[POS_RADIO_WIRE_MAX]);
int pos_radio_decode(const void *data, size_t length,
                     pos_radio_packet_t *packet);
/* 1 = new data, 0 = ACK/duplicate, -1 = packet for another node. */
int pos_radio_receive(pos_radio_state_t *state,
                      const pos_radio_packet_t *packet,
                      pos_radio_packet_t *ack);
int pos_radio_enqueue(pos_radio_state_t *state,
                      const pos_radio_packet_t *packet, uint64_t now_ms);
/* 1 = ready to send; -1 = retries exhausted, packet returned for outbox; 0 =
 * idle. */
int pos_radio_due(pos_radio_state_t *state, uint64_t now_ms,
                  pos_radio_packet_t *packet);
int pos_radio_store(const char *root,const pos_radio_packet_t *packet,pos_error_t *e);
int pos_range_header(FILE *stream);
int pos_range_row(FILE *stream, const char *event,
                  const pos_radio_packet_t *packet,
                  const pos_location_t *location, int rssi, float snr,
                  const pos_radio_state_t *state, uint64_t now_ms);
