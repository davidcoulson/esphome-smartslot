/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SERIAL_HOST_H
#define SERIAL_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void serial_host_reset(void);
void serial_host_set_loopback(bool on);
void serial_host_queue_rx(const uint8_t *buf, size_t n); /* readable after the next send */
extern uint8_t serial_host_tx[1024];
extern size_t serial_host_tx_n;
#endif
