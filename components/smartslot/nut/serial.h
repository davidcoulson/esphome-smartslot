/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SmartSlot shim for the NUT header of the same name: only what the vendored apcmicrolink sources use. */
#ifndef SMARTSLOT_NUT_SERIAL_H
#define SMARTSLOT_NUT_SERIAL_H
#include <sys/types.h>
#include <unistd.h>
#include "common.h"
#ifdef ESP_PLATFORM
/* <termios.h> is not available on target: only the speeds the driver can ask for. */
typedef unsigned int speed_t;
#define B1200 1200U
#define B2400 2400U
#define B4800 4800U
#define B9600 9600U
#define B19200 19200U
#define B38400 38400U
#define B57600 57600U
#define B115200 115200U
#else
#include <termios.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
TYPE_FD_SER ser_open(const char *port);
int ser_close(TYPE_FD_SER fd, const char *port);
int ser_set_speed(TYPE_FD_SER fd, const char *port, speed_t speed);
int ser_set_dtr(TYPE_FD_SER fd, int state);
ssize_t ser_send_buf(TYPE_FD_SER fd, const void *buf, size_t buflen);
ssize_t ser_get_char(TYPE_FD_SER fd, void *ch, time_t d_sec, useconds_t d_usec);
int ser_flush_io(TYPE_FD_SER fd);
void ser_comm_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ser_comm_good(void);
#ifdef __cplusplus
}
#endif
#endif
