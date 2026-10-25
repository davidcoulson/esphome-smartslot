/* SPDX-License-Identifier: GPL-2.0-or-later */
/* newlib lacks timegm(), which the vendored apc_common.c calls. UTC broken-down time to epoch (Hinnant's days_from_civil). */
#include <time.h>

time_t timegm(struct tm *tm) {
    long y = tm->tm_year + 1900L, m = tm->tm_mon + 1;
    long mm = m, d = tm->tm_mday;
    if (mm < 1 || mm > 12) { /* normalise out-of-range months */
        y += (mm - 1) / 12 - ((mm - 1) % 12 < 0);
        mm = ((mm - 1) % 12 + 12) % 12 + 1;
    }
    y -= mm <= 2;
    long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + doe - 719468;
    return (time_t) (days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec);
}
