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

/* A page-0 frame as apcmicrolink.c parses one: object 0x00, width at [2], width + 3 bytes, Fletcher-style checksum
 * in the last two. Only this passes, so a line stuck low (a run of 0x00) or noise never reaches the driver's
 * handshake, whose failure is fatalx() and a reboot. */
static bool microlink_page0_at(const uint8_t *b, size_t n) {
    if (n < 3 || b[0] != 0x00) return false;
    size_t len = (size_t) b[2] + 3;
    if (len > n) return false;
    unsigned c0 = 0, c1 = 0;
    for (size_t i = 0; i < len - 2; i++) {
        c0 = (c0 + b[i]) % 255U;
        c1 = (c1 + c0) % 255U;
    }
    uint8_t k0 = (uint8_t) (255U - (c0 + c1) % 255U);
    return b[len - 2] == k0 && b[len - 1] == (uint8_t) (255U - (c0 + k0) % 255U);
}

bool microlink_probe(void) {
    static const uint8_t init = 0xFD;
    uint8_t buf[300]; /* one 258-byte maximum frame plus some leading noise */
    size_t got = 0;
    bool found = false;
    TYPE_FD fd = ser_open("probe");
    if (!VALID_FD(fd)) return false;
    ser_set_speed(fd, "probe", B9600);
    ser_send_buf(fd, &init, 1);
    uint64_t end = os_now_ms() + microlink_probe_ms;
    while (!found && got < sizeof buf && os_now_ms() < end) {
        uint8_t c;
        if (ser_get_char(fd, &c, 0, 100000) != 1) continue;
        buf[got++] = c;
        for (size_t i = 0; i < got && !found; i++) found = microlink_page0_at(buf + i, got - i);
    }
    ser_close(fd, "probe");
    return found;
}
