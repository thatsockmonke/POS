#pragma once
#include "pos_share.h"
typedef struct {
  char name[25];
  int rssi;
} pos_ble_peer_t;
typedef struct {
  bool ready, scanning, connected, secure, pairing, offered, receiving,
      complete;
  uint32_t passkey, offset;
  pos_share_offer_t offer;
  char error[128];
} pos_ble_status_t;
int pos_ble_start(const char *root, const char *name, bool receiving,
                  pos_error_t *e);
void pos_ble_stop(void);
int pos_ble_scan(pos_error_t *e);
unsigned pos_ble_peers(pos_ble_peer_t peers[8]);
int pos_ble_connect(unsigned peer, pos_error_t *e);
int pos_ble_pair(bool accept, pos_error_t *e);
void pos_ble_poll(void);
void pos_ble_poll_progress(pos_io_progress_t progress,void *ctx);
pos_ble_status_t pos_ble_status(void);
int pos_ble_accept(bool accept, pos_error_t *e);
int pos_ble_accept_progress(bool accept,pos_io_progress_t progress,void *ctx,pos_error_t *e);
int pos_ble_send(const char *relative, pos_io_progress_t progress, void *ctx,
                 pos_error_t *e);
