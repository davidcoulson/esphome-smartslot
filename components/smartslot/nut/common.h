/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SmartSlot shim for the NUT header of the same name: only what the vendored apcmicrolink sources use. */
#ifndef SMARTSLOT_NUT_COMMON_H
#define SMARTSLOT_NUT_COMMON_H
#include "config.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "attribute.h"
#ifdef __cplusplus
extern "C" {
#endif

/* syslog priorities (newlib has no <syslog.h>) */
#ifndef LOG_ERR
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
#endif

#define NUT_STRARG(x) (((x) != NULL) ? (x) : "<null>")
#define NUT_UNUSED_VARIABLE(x) (void) (x)

typedef int TYPE_FD;
typedef int TYPE_FD_SER;
#define ERROR_FD (-1)
#define VALID_FD(fd) ((fd) >= 0)
#define INVALID_FD(fd) ((fd) < 0)

extern int nut_debug_level;
int vsnprintf_dynamic(char *dst, size_t size, const char *fmt_dynamic, const char *fmt_reference, va_list va);
int snprintf_dynamic(char *dst, size_t size, const char *fmt_dynamic, const char *fmt_reference, ...);
void upsdebugx(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void upslogx(int priority, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void upsdebug_hex(int level, const char *msg, const void *buf, size_t len);
void fatalx(int status, const char *fmt, ...) __attribute__((format(printf, 2, 3), noreturn));

#ifdef __cplusplus
}
#endif
#endif
