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

#ifndef ESP_PLATFORM
void nutshim_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
