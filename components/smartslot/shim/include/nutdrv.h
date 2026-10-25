/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SMARTSLOT_NUTDRV_H
#define SMARTSLOT_NUTDRV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *cmdsrc;
    uint32_t baudrate;
    int uartsel_gpio; /* < 0: leave UART_SEL alone (host tests) */
    int uartsel_level;
} nutdrv_config;

typedef enum { NUTDRV_NO_UPS, NUTDRV_STARTING, NUTDRV_RUNNING } nutdrv_state_t;

/* 0 on success. Drives UART_SEL, then starts the "nutdrv" task. `cfg` strings must outlive the task. */
int nutdrv_start(const nutdrv_config *cfg);
/* Run an INSTCMD / SET on the driver task (the driver is single-threaded). STAT_*_FAILED on timeout, no driver, or no
 * free request slot. A request that timed out before the driver picked it up is never executed. */
int nutdrv_instcmd(const char *cmd, const char *param, uint32_t timeout_ms);
int nutdrv_setvar(const char *var, const char *val, uint32_t timeout_ms);
nutdrv_state_t nutdrv_state(void);
/* Copies the value only while the driver is RUNNING, data is fresh and the variable exists; else false (publish nothing). */
bool nutshim_value_for_publish(const char *var, char *buf, size_t len);
/* Tests only: ask the task to finish and wait for it. */
void nutdrv_stop(void);

extern uint32_t nutdrv_probe_retry_ms; /* default 30000 */

#ifdef __cplusplus
}
#endif
#endif
