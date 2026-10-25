/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <time.h>
#include "unity.h"

#define timegm shim_timegm
#include "../components/smartslot/shim/timegm.c"
#undef timegm

void test_timegm_matches_libc(void) {
    struct tm t[] = {{.tm_year = 70, .tm_mon = 0, .tm_mday = 1},
                     {.tm_year = 126, .tm_mon = 9, .tm_mday = 25, .tm_hour = 13, .tm_min = 7, .tm_sec = 9},
                     {.tm_year = 124, .tm_mon = 1, .tm_mday = 29, .tm_hour = 23, .tm_min = 59, .tm_sec = 59},
                     {.tm_year = 100, .tm_mon = 11, .tm_mday = 31}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        struct tm a = t[i], b = t[i];
        TEST_ASSERT_EQUAL_INT64(timegm(&a), shim_timegm(&b));
    }
}
