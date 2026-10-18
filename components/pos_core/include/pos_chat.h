#pragma once
#include "pos_mesh.h"
#define POS_CHAT_MESSAGES 16
typedef struct {
  uint32_t id, from, to;
  char text[POS_MESH_PAYLOAD + 1], status[20];
  bool incoming, unread;
} pos_chat_message_t;
typedef struct {
  char alias[40], quick[161];
  bool favourite;
  uint32_t read_offset;
} pos_chat_contact_t;
/* Readable SD journal; bounded 64-KiB tail / 16 messages per thread. An unread
 * badge counts the visible retained tail; older data remains in Files. */
int pos_chat_contact(const char *root, uint32_t peer, unsigned channel,
                     pos_chat_contact_t *, bool save);
int pos_chat_read(const char *root, uint32_t self, uint32_t peer,
                  unsigned channel,
                  pos_chat_message_t messages[POS_CHAT_MESSAGES],
                  unsigned *count, uint32_t *end_offset, unsigned *unread);
