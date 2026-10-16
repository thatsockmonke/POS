#pragma once
/* Input supervisor must never wait for USB input while scanning physical keys.
 */
int pos_console_nonblocking(int fd);
int pos_console_poll(
    int fd); /* byte, POS_KEY arrow, or -1 when absent/error; nonblocking */
