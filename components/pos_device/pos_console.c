#include "pos_console.h"
#include <fcntl.h>
#include <unistd.h>

int pos_console_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
int pos_console_poll(int fd) {
  unsigned char byte;
  return read(fd, &byte, 1) == 1 ? byte : -1;
}
