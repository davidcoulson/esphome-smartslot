/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <pthread.h>

#include "nutdrv.h"
#include "nutshim.h"
#include "serial_host.h"
#include "unity.h"

extern volatile bool fake_stale;
extern pthread_t fake_instcmd_thread;
extern int fake_instcmd_calls;
extern int fake_cleanup_calls;
extern int fake_empty_status_polls;
extern char fake_device_path[32];

static const nutdrv_config cfg = {"smartslot1", 9600, -1, 0};

static void wait_state(nutdrv_state_t want, uint32_t ms) {
    for (uint32_t t = 0; t < ms && nutdrv_state() != want; t++) os_sleep_ms(1);
}
static void reply_ok(void) { static const uint8_t ok[19] = {0x00, 0x0a, 0x10, 0x89, 0x00, 0x12, 0x11, 0x09, 0x00, 0x01, 0x00, 0x46, 0x00, 0xf8, 0x02, 0xfe, 0x04, 0x4b, 0x9f}; serial_host_queue_rx(ok, sizeof ok); }

static void begin(void) {
    fake_stale = false;
    fake_cleanup_calls = 0;
    poll_interval = 0;
    microlink_probe_ms = 100;
    nutdrv_probe_retry_ms = 50;
    TEST_ASSERT_EQUAL(0, nutdrv_start(&cfg));
}

void test_nutdrv_waits_for_probe_then_runs(void) {
    begin();
    wait_state(NUTDRV_NO_UPS, 1000);
    os_sleep_ms(200);
    TEST_ASSERT_EQUAL(NUTDRV_NO_UPS, nutdrv_state());
    TEST_ASSERT_FALSE(dstate_is_dataok());
    reply_ok();
    wait_state(NUTDRV_RUNNING, 3000);
    TEST_ASSERT_EQUAL(NUTDRV_RUNNING, nutdrv_state());
    os_sleep_ms(50);
    TEST_ASSERT_EQUAL_STRING("OL", dstate_getinfo("ups.status"));
    TEST_ASSERT_TRUE(dstate_is_dataok());
    TEST_ASSERT_EQUAL_STRING("smartslot", fake_device_path);
    TEST_ASSERT_EQUAL_STRING("smartslot1", getval("cmdsrc"));
    TEST_ASSERT_EQUAL_STRING("9600", getval("baudrate"));
    nutdrv_stop();
}

void test_nutdrv_instcmd_runs_on_driver_task(void) {
    reply_ok();
    begin();
    wait_state(NUTDRV_RUNNING, 3000);
    TEST_ASSERT_EQUAL(STAT_INSTCMD_HANDLED, nutdrv_instcmd("beeper.toggle", NULL, 2000));
    TEST_ASSERT_FALSE(pthread_equal(fake_instcmd_thread, pthread_self()));
    TEST_ASSERT_EQUAL(STAT_INSTCMD_UNKNOWN, nutdrv_instcmd("nope", NULL, 2000));
    TEST_ASSERT_EQUAL(STAT_SET_HANDLED, nutdrv_setvar("ups.delay.shutdown", "20", 2000));
    for (int i = 0; i < 20; i++) TEST_ASSERT_EQUAL(STAT_INSTCMD_HANDLED, nutdrv_instcmd("beeper.toggle", NULL, 2000)); /* slots are reused */
    nutdrv_stop();
    TEST_ASSERT_EQUAL(STAT_INSTCMD_FAILED, nutdrv_instcmd("beeper.toggle", NULL, 100)); /* no driver: fails at once */
}

void test_nutdrv_detached_request_returns_before_the_driver_runs_it(void) {
    reply_ok();
    begin();
    wait_state(NUTDRV_RUNNING, 3000);
    fake_instcmd_calls = 0;
    uint64_t t0 = os_now_ms();
    TEST_ASSERT_EQUAL(STAT_INSTCMD_HANDLED, nutdrv_instcmd("slow", NULL, 0)); /* a 300 ms command: OK means queued */
    TEST_ASSERT_LESS_THAN_UINT64(100, os_now_ms() - t0);
    TEST_ASSERT_EQUAL(0, fake_instcmd_calls);
    TEST_ASSERT_EQUAL(STAT_SET_HANDLED, nutdrv_setvar("ups.delay.shutdown", "20", 0));
    /* a timed request behind a detached one waits its turn and gets its own result, not the detached one's */
    TEST_ASSERT_EQUAL(STAT_INSTCMD_UNKNOWN, nutdrv_instcmd("nope", NULL, 2000));
    TEST_ASSERT_EQUAL(2, fake_instcmd_calls);
    os_sleep_ms(100);
    TEST_ASSERT_EQUAL(2, fake_instcmd_calls); /* the slow one ran once, on the driver task, and its slot was freed */
    TEST_ASSERT_EQUAL(STAT_INSTCMD_HANDLED, nutdrv_instcmd("beeper.toggle", NULL, 0)); /* a slot is free again */
    nutdrv_stop();
}

void test_nutdrv_five_stale_polls_return_to_no_ups(void) {
    reply_ok();
    begin();
    wait_state(NUTDRV_RUNNING, 3000);
    fake_stale = true;
    wait_state(NUTDRV_NO_UPS, 2000);
    /* the driver sets NO_UPS before it marks data stale and calls cleanup */
    for (int t = 0; t < 1000 && fake_cleanup_calls < 1; t++) os_sleep_ms(1);
    TEST_ASSERT_EQUAL(NUTDRV_NO_UPS, nutdrv_state());
    TEST_ASSERT_FALSE(dstate_is_dataok());
    TEST_ASSERT_TRUE(fake_cleanup_calls >= 1);
    fake_instcmd_calls = 0;
    TEST_ASSERT_EQUAL(STAT_INSTCMD_FAILED, nutdrv_instcmd("beeper.toggle", NULL, 0)); /* refused while probing... */
    /* the UPS comes back */
    fake_stale = false;
    reply_ok();
    wait_state(NUTDRV_RUNNING, 3000);
    TEST_ASSERT_EQUAL(NUTDRV_RUNNING, nutdrv_state());
    os_sleep_ms(100);
    TEST_ASSERT_EQUAL(0, fake_instcmd_calls); /* ...and nothing accepted before the session ended runs on the new one */
    nutdrv_stop();
}

void test_nutdrv_slow_status_is_stale_but_not_reprobed(void) {
    fake_empty_status_polls = 8; /* more polls than the five stale ones that force a re-probe */
    reply_ok();
    begin();
    wait_state(NUTDRV_RUNNING, 3000);
    char buf[8];
    TEST_ASSERT_FALSE(dstate_is_servable());
    TEST_ASSERT_FALSE(nutshim_value_for_publish("ups.status", buf, sizeof buf));
    for (int t = 0; t < 2000 && !dstate_is_servable(); t++) os_sleep_ms(1);
    TEST_ASSERT_TRUE(dstate_is_servable());
    TEST_ASSERT_EQUAL(NUTDRV_RUNNING, nutdrv_state());
    TEST_ASSERT_EQUAL(0, fake_cleanup_calls); /* the session was never torn down */
    nutdrv_stop();
}
