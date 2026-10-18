#pragma once
#include <stdbool.h>
/* Input supervisor must never wait for USB input while scanning physical keys.
 */
int pos_console_nonblocking(int fd);
/* Explicit file session exclusively owns stdin; physical keys remain active. */
bool pos_console_claim(void);
void pos_console_release(void);
int pos_console_poll(
    int fd); /* byte, POS_KEY arrow, or -1 when absent/error; nonblocking */
