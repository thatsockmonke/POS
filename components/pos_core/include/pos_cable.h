#pragma once
#include "pos_share.h"
#define POS_CABLE_CHUNK 512
#define POS_CABLE_PAYLOAD (POS_CABLE_CHUNK + 5)
#define POS_CABLE_LINE (22 + POS_CABLE_PAYLOAD * 2)
typedef struct {
  char line[POS_CABLE_LINE];
  unsigned used;
  bool reading;
  uint32_t sequence, checksum;
  uint8_t data[POS_CABLE_PAYLOAD + 4];
  size_t length;
} pos_cable_frame_t;
/* Noise/logs, invalid CRCs and oversized frames are discarded. */
bool pos_cable_byte(pos_cable_frame_t *, unsigned char);
size_t pos_cable_encode(char *, uint32_t, const void *, size_t);
typedef struct {
  pos_share_receive_t receive;
  pos_share_offer_t offer;
  FILE *source;
  const char *root, *selected;
} pos_cable_t;
/* Caller owns approval and transport; output never exceeds PAYLOAD. */
int pos_cable_request(pos_cable_t *, const void *, size_t, uint8_t *, size_t *,
                      pos_io_progress_t, void *, pos_error_t *);
void pos_cable_close(pos_cable_t *);
