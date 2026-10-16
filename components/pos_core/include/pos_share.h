#pragma once
#include "pos_files.h"
#include <stdio.h>
#define POS_SHARE_NAME 80
typedef struct {
  char name[81], sha256[65];
  uint32_t size;
  uint64_t id;
} pos_share_offer_t;
typedef struct {
  pos_share_offer_t offer;
  FILE *file;
  char part[POS_FS_PATH], metadata[POS_FS_PATH], destination[POS_FS_PATH];
  uint32_t offset;
} pos_share_receive_t;
int pos_share_offer(const char *root, const char *relative,
                    pos_share_offer_t *offer, pos_error_t *e);
int pos_share_offer_progress(const char *root,const char *relative,pos_share_offer_t *offer,pos_io_progress_t progress,void *ctx,pos_error_t *e);
/* 0 opened/resumed, 1 identical destination already verified, -1 refused. */
int pos_share_accept(pos_share_receive_t *receive, const char *root,
                     const pos_share_offer_t *offer, pos_error_t *e);
int pos_share_write(pos_share_receive_t *receive, uint32_t offset,
                    const void *data, size_t length, pos_error_t *e);
int pos_share_finish(pos_share_receive_t *receive, pos_io_progress_t progress,
                     void *ctx, pos_error_t *e);
void pos_share_cancel(pos_share_receive_t *receive);
int pos_share_accept_progress(pos_share_receive_t *receive,const char *root,const pos_share_offer_t *offer,pos_io_progress_t progress,void *ctx,pos_error_t *e);
/* Stable, bounded binary offer format, independent of BLE/Wi-Fi transports. */
#define POS_SHARE_OFFER_BYTES 158
int pos_share_encode(const pos_share_offer_t *offer,
                     uint8_t wire[POS_SHARE_OFFER_BYTES]);
int pos_share_decode(const void *wire, size_t length, pos_share_offer_t *offer);
typedef struct {uint8_t wire[POS_SHARE_OFFER_BYTES];unsigned used,sequence;} pos_share_assembly_t;
/* BLE MTU-23-compatible offer fragments: 'O', sequence, up to 18 bytes. */
int pos_share_assemble(pos_share_assembly_t *assembly,const void *frame,size_t length,pos_share_offer_t *offer);
