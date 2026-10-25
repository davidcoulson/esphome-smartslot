/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SMARTSLOT_UPSD_H
#define SMARTSLOT_UPSD_H
#include <stdint.h>

#include "nutproto.h"
#ifdef __cplusplus
extern "C" {
#endif

#define UPSD_MAX_CLIENTS 4
#define UPSD_LINE_MAX 512

/* 0 on success. port 0 picks an ephemeral port (tests); read it back with upsd_port(). `hooks` must outlive the server. */
int upsd_start(const nutproto_hooks *hooks, uint16_t port);
uint16_t upsd_port(void);
int upsd_num_logins(void); /* sessions that completed LOGIN */
void upsd_stop(void);      /* tests only */

#ifdef __cplusplus
}
#endif
#endif
