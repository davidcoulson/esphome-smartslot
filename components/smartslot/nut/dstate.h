/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SmartSlot shim for the NUT header of the same name: only what the vendored apcmicrolink sources use. */
#ifndef SMARTSLOT_NUT_DSTATE_H
#define SMARTSLOT_NUT_DSTATE_H
#include <time.h>
#include "extstate.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct timespec st_tree_timespec_t;
static inline void state_get_timestamp(st_tree_timespec_t *ts) { clock_gettime(CLOCK_MONOTONIC, ts); }
static inline double difftime_st_tree_timespec(st_tree_timespec_t a, st_tree_timespec_t b) {
	return (double) (a.tv_sec - b.tv_sec) + (double) (a.tv_nsec - b.tv_nsec) / 1e9;
}
const char *dstate_setinfo(const char *var, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
const char *dstate_getinfo(const char *var);
int dstate_delinfo(const char *var);
void dstate_addcmd(const char *cmdname);
int dstate_setflags(const char *var, int flags);
int dstate_setaux(const char *var, long aux);
void dstate_dataok(void);
void dstate_datastale(void);
void status_init(void);
void status_set(const char *buf);
void status_commit(void);
void alarm_init(void);
void alarm_set(const char *buf);
void alarm_commit(void);
#ifdef __cplusplus
}
#endif
#endif
