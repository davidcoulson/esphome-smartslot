/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutdrv.h"
#include "nutshim.h"
#include "serial_host.h"
#include "unity.h"

extern volatile bool fake_stale;

static void wait_state(nutdrv_state_t want) {
    for (int t = 0; t < 3000 && nutdrv_state() != want; t++) os_sleep_ms(1);
}

void test_publish_only_when_running_fresh_and_present(void) {
    static const uint8_t ok[19] = {0x00, 0x0a, 0x10, 0x89, 0x00, 0x12, 0x11, 0x09, 0x00, 0x01, 0x00, 0x46, 0x00, 0xf8, 0x02, 0xfe, 0x04, 0x4b, 0x9f};
    static const nutdrv_config cfg = {"smartslot1", 9600, -1, 0};
    char buf[16];
    fake_stale = false;
    poll_interval = 0;
    microlink_probe_ms = 100;
    nutdrv_probe_retry_ms = 50;
    TEST_ASSERT_FALSE(nutshim_value_for_publish("ups.status", buf, sizeof buf)); /* no driver yet */
    serial_host_queue_rx(ok, sizeof ok);
    TEST_ASSERT_EQUAL(0, nutdrv_start(&cfg));
    wait_state(NUTDRV_RUNNING);
    os_sleep_ms(50);
    TEST_ASSERT_TRUE(nutshim_value_for_publish("ups.status", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("OL", buf);
    TEST_ASSERT_FALSE(nutshim_value_for_publish("no.such.var", buf, sizeof buf));
    TEST_ASSERT_EQUAL(0, nutshim_lookup("no.such.var", buf, sizeof buf)); /* served, but absent: goes unknown */
    fake_stale = true;
    os_sleep_ms(50);
    TEST_ASSERT_FALSE(nutshim_value_for_publish("ups.status", buf, sizeof buf));
    TEST_ASSERT_EQUAL(-1, nutshim_lookup("no.such.var", buf, sizeof buf)); /* stale: keep the last value */
    nutdrv_stop();
}

void test_parse_number_accepts_only_whole_finite_numbers(void) {
    float v = 1;
    TEST_ASSERT_TRUE(nutshim_parse_number("12.5", &v));
    TEST_ASSERT_EQUAL_FLOAT(12.5f, v);
    TEST_ASSERT_TRUE(nutshim_parse_number("-3", &v));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, v);
    TEST_ASSERT_FALSE(nutshim_parse_number("12abc", &v));
    TEST_ASSERT_FALSE(nutshim_parse_number("nan", &v));
    TEST_ASSERT_FALSE(nutshim_parse_number("inf", &v));
    TEST_ASSERT_FALSE(nutshim_parse_number("", &v));
    TEST_ASSERT_FALSE(nutshim_parse_number("OL", &v));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, v); /* a rejected string leaves out alone */
}
