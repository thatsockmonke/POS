#include "esp_app_desc.h"
#include "esp_timer.h"
#include "pos_cable.h"
#include "pos_console.h"
#include "pos_device.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {
  pos_cable_frame_t frame;
  pos_cable_t files;
  uint8_t reply[POS_CABLE_PAYLOAD];
  char output[POS_CABLE_LINE];
  uint32_t last, checksum, current;
  size_t reply_size;
  int64_t draw;
} cable_t;
static void send(cable_t *s, uint32_t sequence, const void *data, size_t n) {
  size_t bytes = pos_cable_encode(s->output, sequence, data, n), at = 0;
  while (at < bytes) {
    ssize_t sent = write(STDOUT_FILENO, s->output + at, bytes - at);
    if (sent <= 0)
      break;
    at += (size_t)sent;
  }
}
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  cable_t *s = ctx;
  int64_t now = esp_timer_get_time();
  if (now - s->draw < 200000 && done != total)
    return pos_home_pending();
  s->draw = now;
  uint8_t status[9] = {'V'};
  for (unsigned i = 0; i < 4; ++i) {
    status[1 + i] = done >> (8 * i);
    status[5 + i] = total >> (8 * i);
  }
  send(s, s->current, status, sizeof(status));
  return pos_ui_progress(NULL, phase, "USB cable", done, total);
}
void pos_ui_cable(const char *root, const char *selected) {
  cable_t *s = calloc(1, sizeof(*s));
  if (!s) {
    pos_ui_notice("USB unavailable", "Not enough memory.");
    return;
  }
  bool owned = false;
  for (unsigned i = 0; i < 10 && !(owned = pos_console_claim()); ++i) {
    pos_event_t event;
    pos_device_event(&event, 10);
  }
  if (!owned) {
    free(s);
    pos_ui_notice("USB busy", "Close the other cable session.");
    return;
  }
  if (pos_console_nonblocking(STDIN_FILENO)) {
    pos_console_release();
    free(s);
    pos_ui_notice("USB unavailable", "Nonblocking console input is required.");
    return;
  }
  s->files.root = root;
  s->files.selected = selected ? selected : "";
  pos_audio_feedback_hold(true);
  pos_ui_message("USB cable transfer",
                 "Connect USB; share with Linux.\nPC: POS Transfer > USB "
                 "cable\nLeave this screen open.\nEsc / G0: close session");
  int64_t hello = 0;
  bool leave = false;
  while (!leave && !pos_home_pending()) {
    int64_t now = esp_timer_get_time();
    if (now - hello >= 1000000) {
      char banner[80];
      int n = snprintf(banner, sizeof(banner), "H%c%cm5stack-cardputer-adv%c%s",
                       1, *s->files.selected ? 'S' : 'R', 0,
                       esp_app_get_description()->version);
      send(s, 0, banner, (size_t)n);
      hello = now;
    }
    unsigned char buffer[256];
    ssize_t got = read(STDIN_FILENO, buffer, sizeof(buffer));
    for (ssize_t i = 0; i < got; ++i) {
      if (!pos_cable_byte(&s->frame, buffer[i]))
        continue;
      s->current = s->frame.sequence;
      if (!s->current)
        continue;
      if (s->last == s->current && s->checksum == s->frame.checksum) {
        send(s, s->last, s->reply, s->reply_size);
        continue;
      }
      pos_error_t error = {0};
      bool accept = true;
      if (s->frame.length == 1 + POS_SHARE_OFFER_BYTES &&
          s->frame.data[0] == 'O' && !*s->files.selected) {
        pos_share_offer_t offer;
        if (!pos_share_decode(s->frame.data + 1, POS_SHARE_OFFER_BYTES,
                              &offer)) {
          char label[160];
          snprintf(label, sizeof(label),
                   "File: %s\nSize: %u bytes\nSave in Downloads.", offer.name,
                   (unsigned)offer.size);
          const char *items[] = {"Decline", "Accept into Downloads"};
          pos_ui_notice("Incoming USB file", label);
          accept = pos_ui_menu("Receive USB file?", items, 2) == 1;
        }
      }
      int result =
          accept
              ? pos_cable_request(&s->files, s->frame.data, s->frame.length,
                                  s->reply, &s->reply_size, progress, s, &error)
              : -1;
      if (result < 0) {
        pos_share_cancel(&s->files.receive);
        s->reply[0] = 'E';
        snprintf((char *)s->reply + 1, sizeof(s->reply) - 1, "%s",
                 !accept          ? "File declined on Cardputer."
                 : *error.message ? error.message
                                  : "USB operation failed; partial retained.");
        s->reply_size = strlen((char *)s->reply + 1) + 1;
      }
      s->last = s->current;
      s->checksum = s->frame.checksum;
      send(s, s->last, s->reply, s->reply_size);
      if (result < 0)
        pos_ui_message("USB transfer stopped", (char *)s->reply + 1);
      if (result > 0) {
        leave = true;
        break;
      }
      if (!result && s->reply[0] == 'Z')
        pos_ui_message("USB transfer verified",
                       "Saved in Downloads.\nPC may send another file.\nEsc / "
                       "G0: close session");
      else if (!result && s->reply[0] == 'K')
        progress(s, "Receiving USB file", s->files.receive.offset,
                 s->files.receive.offer.size);
      else if (!result && s->reply[0] == 'D') {
        uint32_t at = s->frame.data[1] | (uint32_t)s->frame.data[2] << 8 |
                      (uint32_t)s->frame.data[3] << 16 |
                      (uint32_t)s->frame.data[4] << 24;
        progress(s, "Sending USB file", at + s->reply_size - 5,
                 s->files.offer.size);
      }
    }
    pos_event_t event;
    if (pos_device_event(&event, got > 0 ? 0 : 10) && event.pressed &&
        (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME))
      break;
  }
  pos_cable_close(&s->files);
  pos_console_release();
  pos_audio_feedback_hold(false);
  free(s);
}
