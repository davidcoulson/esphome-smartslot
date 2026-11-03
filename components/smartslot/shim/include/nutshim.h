/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shim-only API (not part of NUT): what the SmartSlot glue, upsd and the tests need beyond the NUT names. */
#ifndef SMARTSLOT_NUTSHIM_H
#define SMARTSLOT_NUTSHIM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dstate.h"
#include "main.h"
#include "os.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Creates the dstate mutex; idempotent. */
void nutshim_init(void);
bool dstate_is_dataok(void);
/* dataok, and not an empty ups.status: a new session's first polls publish an empty status and zeroed numbers before
 * the UPS has sent them. Gates what upsd and ESPHome serve; the driver's stale-poll count uses dstate_is_dataok(), so a
 * slow startup is not torn down. */
bool dstate_is_servable(void);

/* Both walks hold the dstate mutex; callbacks must not block and must not call dstate_set*. Insertion order. */
typedef void (*dstate_var_fn)(const char *name, const char *value, int flags, long aux, void *ctx);
void dstate_foreach(dstate_var_fn fn, void *ctx);
typedef void (*dstate_cmd_fn)(const char *name, void *ctx);
void dstate_foreach_cmd(dstate_cmd_fn fn, void *ctx);

/* Thread-safe readers for other tasks (dstate_getinfo returns a pointer into the table and is for the driver task). */
bool dstate_copyinfo(const char *var, char *buf, size_t len); /* false when absent */
int dstate_getflags(const char *var);                         /* -1 when absent */
long dstate_getaux(const char *var);                          /* -1 when absent */

/* Flat name,value,... list read by getval()/testvar(). The array must outlive the driver. */
void nutshim_setvar_table(const char *const *kv, size_t n);

/* Called by fatalx() after data was marked stale. Default: log, wait 5 s, restart. */
extern void (*nutshim_fatal_hook)(int status);

/* Serial: pins are stored before the driver task starts (the host build ignores them). */
void nutshim_serial_config(int uart_num, int tx_gpio, int rx_gpio);
/* Drives UART_SEL as a push-pull output at `level` before anything touches the UART. */
void uartsel_init(int gpio, int level);
/* 9600 baud, send 0xFD, read for microlink_probe_ms (1500): >= 8 bytes and the first not 0x00 means a Microlink UPS. */
bool microlink_probe(void);
extern uint32_t microlink_probe_ms;

#ifndef ESP_PLATFORM
void nutshim_reset_for_test(void);
extern int uartsel_host_level;
#endif

#ifdef __cplusplus
}
#endif
#endif
