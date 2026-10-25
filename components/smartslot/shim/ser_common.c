/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Serial code shared by the target and host back ends: failure accounting and the Microlink probe. */
#include <stdarg.h>
#include <stdio.h>

#include "nutshim.h"
#include "serial.h"

#define STALE_AFTER_FAILS 3

static int consecutive_fails;
uint32_t microlink_probe_ms = 1500;

void ser_comm_fail(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    consecutive_fails++;
    upslogx(LOG_WARNING, "Communications with UPS lost: %s", buf);
    if (consecutive_fails >= STALE_AFTER_FAILS) dstate_datastale();
}

void ser_comm_good(void) {
    if (consecutive_fails >= STALE_AFTER_FAILS) upslogx(LOG_NOTICE, "Communications with UPS re-established");
    consecutive_fails = 0;
}

bool microlink_probe(void) {
    static const uint8_t init = 0xFD;
    uint8_t first = 0;
    size_t got = 0;
    TYPE_FD fd = ser_open("probe");
    if (!VALID_FD(fd)) return false;
    ser_set_speed(fd, "probe", B9600);
    ser_send_buf(fd, &init, 1);
    uint64_t end = os_now_ms() + microlink_probe_ms;
    while (os_now_ms() < end) {
        uint8_t c;
        if (ser_get_char(fd, &c, 0, 100000) != 1) continue;
        if (got++ == 0) {
            first = c;
            if (first == 0x00) break; /* APC returns 0x00 when it is not speaking Microlink */
        }
        if (got >= 8) break;
    }
    ser_close(fd, "probe");
    return got >= 8 && first != 0x00;
}
