/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdio.h>

#include "nutshim.h"

int nut_debug_level;

static void nutlog_v(int os_level, const char *fmt, va_list ap) {
    char buf[256];
    vsnprintf(buf, sizeof buf, fmt, ap);
    os_log(os_level, "nut", "%s", buf);
}

void upsdebugx(int level, const char *fmt, ...) {
    if (level > nut_debug_level) return;
    va_list ap;
    va_start(ap, fmt);
    nutlog_v(OS_LOG_DEBUG, fmt, ap);
    va_end(ap);
}

void upslogx(int priority, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    nutlog_v(priority <= LOG_ERR ? OS_LOG_ERROR : priority == LOG_WARNING ? OS_LOG_WARN : OS_LOG_INFO, fmt, ap);
    va_end(ap);
}

void upsdebug_hex(int level, const char *msg, const void *buf, size_t len) {
    if (level > nut_debug_level) return;
    char line[256];
    int n = snprintf(line, sizeof line, "%s:", msg);
    for (size_t i = 0; i < len && n + 4 < (int) sizeof line; i++) n += snprintf(line + n, sizeof line - n, " %02x", ((const uint8_t *) buf)[i]);
    os_log(OS_LOG_DEBUG, "nut", "%s", line);
}

int vsnprintf_dynamic(char *dst, size_t size, const char *fmt_dynamic, const char *fmt_reference, va_list va) {
    (void) fmt_reference; /* NUT only uses it to silence -Wformat-nonliteral */
    return vsnprintf(dst, size, fmt_dynamic, va);
}

int snprintf_dynamic(char *dst, size_t size, const char *fmt_dynamic, const char *fmt_reference, ...) {
    va_list ap;
    va_start(ap, fmt_reference);
    int r = vsnprintf_dynamic(dst, size, fmt_dynamic, fmt_reference, ap);
    va_end(ap);
    return r;
}

static void default_fatal(int status) {
    (void) status;
    os_log(OS_LOG_ERROR, "nut", "driver fatal: restarting in 5 s");
    os_sleep_ms(5000);
    os_restart();
}
void (*nutshim_fatal_hook)(int status) = default_fatal;

void fatalx(int status, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    nutlog_v(OS_LOG_ERROR, fmt, ap);
    va_end(ap);
    dstate_datastale(); /* never serve old numbers while the card is going down */
    nutshim_fatal_hook(status);
    for (;;) os_restart();
}
