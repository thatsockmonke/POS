#include "pos_console.h"
#include "pos_core.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/* One USB console, polled by the input supervisor. Never wait for a sequence:
 * physical keyboard scanning and the recovery shortcut must keep running. */
static unsigned sequence;
static uint64_t started;
static int pending = -1;
static atomic_bool claimed;
static atomic_flag reader = ATOMIC_FLAG_INIT;
static bool discard;
bool pos_console_claim(void) {
  if (atomic_flag_test_and_set(&reader))
    return false;
  bool ok = !atomic_exchange(&claimed, true);
  if (ok) {
    sequence = 0;
    pending = -1;
    discard = false;
  }
  atomic_flag_clear(&reader);
  return ok;
}
void pos_console_release(void) {
  sequence = 0;
  pending = -1;
  atomic_store(&claimed, false);
}
static uint64_t milliseconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
int pos_console_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK))
    return -1;
  sequence = 0;
  pending = -1;
  return 0;
}
static int poll_key(int fd) {
  /* A lone Escape is emitted after 100 ms; incomplete ANSI sequences expire
   * without leaking their bytes into text fields or selecting menu items. */
  if (sequence && milliseconds() - started >= 100) {
    unsigned previous = sequence;
    sequence = 0;
    if (previous == 1)
      return 27;
  }
  for (unsigned count = 0; count < 32; ++count) {
    unsigned char byte;
    if (pending >= 0) {
      byte = (unsigned char)pending;
      pending = -1;
    } else if (read(fd, &byte, 1) != 1)
      return -1;
    /* Late/retried cable frames never become keyboard shortcuts after exit. */
    if (byte == 30) {
      discard = true;
      sequence = 0;
    }
    if (discard) {
      if (byte == '\n')
        discard = false;
      continue;
    }
    if (byte == 0 || byte == 29) {
      sequence = 0;
      return byte; /* The global Home shortcut cannot be consumed by ANSI. */
    }
    if (!sequence) {
      if (byte != 27)
        return byte;
      sequence = 1;
      started = milliseconds();
    } else if (sequence == 1) {
      if (byte == '[' || byte == 'O')
        sequence = 2; /* ANSI CSI or application-mode SS3 arrows. */
      else {
        sequence = 0;
        pending = byte;
        return 27;
      }
    } else if (byte >= 0x40 && byte <= 0x7e) {
      sequence = 0;
      switch (byte) {
      case 'A':
        return POS_KEY_UP;
      case 'B':
        return POS_KEY_DOWN;
      case 'C':
        return POS_KEY_RIGHT;
      case 'D':
        return POS_KEY_LEFT;
      default:
        break; /* Ignore unsupported terminal keys. */
      }
    }
  }
  return -1;
}
int pos_console_poll(int fd) {
  if (atomic_load(&claimed) || atomic_flag_test_and_set(&reader))
    return -1;
  int result = atomic_load(&claimed) ? -1 : poll_key(fd);
  atomic_flag_clear(&reader);
  return result;
}
