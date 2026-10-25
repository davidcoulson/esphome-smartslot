/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host stand-in for the UART: bytes queued with serial_host_queue_rx become readable after the next send (the UPS
 * answering), or echo straight back when loopback is on. */
#include <string.h>

#include "nutshim.h"
#include "serial.h"
#include "serial_host.h"

#define RING 1024

static uint8_t rx[RING], reply[RING];
static size_t rx_n, reply_n;
static bool loopback;
int uartsel_host_level = -1;
uint8_t serial_host_tx[RING];
size_t serial_host_tx_n;
static os_mutex_t m;

static void lock(void) { if (!m) m = os_mutex_create(); os_mutex_lock(m); }

void serial_host_reset(void) {
    lock();
    rx_n = reply_n = serial_host_tx_n = 0;
    loopback = false;
    uartsel_host_level = -1;
    os_mutex_unlock(m);
}
void serial_host_set_loopback(bool on) { lock(); loopback = on; os_mutex_unlock(m); }
void serial_host_queue_rx(const uint8_t *buf, size_t n) {
    lock();
    for (size_t i = 0; i < n && reply_n < RING; i++) reply[reply_n++] = buf[i];
    os_mutex_unlock(m);
}

void nutshim_serial_config(int uart_num, int tx_gpio, int rx_gpio) { (void) uart_num; (void) tx_gpio; (void) rx_gpio; }
void uartsel_init(int gpio, int level) { (void) gpio; uartsel_host_level = level; }

TYPE_FD_SER ser_open(const char *port) { (void) port; return 3; }
int ser_close(TYPE_FD_SER fd, const char *port) { (void) fd; (void) port; return 0; }
int ser_set_speed(TYPE_FD_SER fd, const char *port, speed_t speed) { (void) fd; (void) port; (void) speed; return 0; }
int ser_set_dtr(TYPE_FD_SER fd, int state) { (void) fd; (void) state; return 0; }

ssize_t ser_send_buf(TYPE_FD_SER fd, const void *buf, size_t len) {
    (void) fd;
    lock();
    for (size_t i = 0; i < len && serial_host_tx_n < RING; i++) serial_host_tx[serial_host_tx_n++] = ((const uint8_t *) buf)[i];
    if (loopback)
        for (size_t i = 0; i < len && rx_n < RING; i++) rx[rx_n++] = ((const uint8_t *) buf)[i];
    for (size_t i = 0; i < reply_n && rx_n < RING; i++) rx[rx_n++] = reply[i];
    reply_n = 0;
    os_mutex_unlock(m);
    return (ssize_t) len;
}

ssize_t ser_get_char(TYPE_FD_SER fd, void *ch, time_t d_sec, useconds_t d_usec) {
    (void) fd;
    uint64_t end = os_now_ms() + (uint64_t) d_sec * 1000 + d_usec / 1000;
    for (;;) {
        lock();
        if (rx_n) {
            *(uint8_t *) ch = rx[0];
            memmove(rx, rx + 1, --rx_n);
            os_mutex_unlock(m);
            return 1;
        }
        os_mutex_unlock(m);
        if (os_now_ms() >= end) return 0;
        os_sleep_ms(1);
    }
}

int ser_flush_io(TYPE_FD_SER fd) { (void) fd; lock(); rx_n = 0; os_mutex_unlock(m); return 0; }
