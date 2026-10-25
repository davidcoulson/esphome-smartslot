/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutshim.h"
#include "serial.h"
#include "serial_host.h"
#include "unity.h"

void test_serial_loopback_and_timeout(void) {
    serial_host_set_loopback(true);
    TYPE_FD fd = ser_open("loop");
    TEST_ASSERT_TRUE(VALID_FD(fd));
    TEST_ASSERT_EQUAL(3, ser_send_buf(fd, "\xfd\x00\x01", 3));
    unsigned char c;
    TEST_ASSERT_EQUAL(1, ser_get_char(fd, &c, 0, 1000));
    TEST_ASSERT_EQUAL_HEX8(0xfd, c);
    ser_flush_io(fd);
    TEST_ASSERT_EQUAL(0, ser_get_char(fd, &c, 0, 1000));
    ser_close(fd, "loop");
}

static void probe_with(const uint8_t *reply, size_t n, bool expect) {
    microlink_probe_ms = 300;
    serial_host_queue_rx(reply, n);
    TEST_ASSERT_EQUAL(expect, microlink_probe());
    TEST_ASSERT_EQUAL_HEX8(0xfd, serial_host_tx[0]); /* INIT byte was sent */
}

void test_microlink_probe_cases(void) {
    static const uint8_t ok[8] = {0x01, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t zero_led[8] = {0x00, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t short_reply[7] = {1, 2, 3, 4, 5, 6, 7};
    probe_with(ok, sizeof ok, true);
    serial_host_reset();
    probe_with(zero_led, sizeof zero_led, false);
    serial_host_reset();
    probe_with(short_reply, sizeof short_reply, false);
    serial_host_reset();
    probe_with(NULL, 0, false); /* nobody answers */
}

void test_uartsel_init_records_level(void) {
    uartsel_init(53, 1);
    TEST_ASSERT_EQUAL(1, uartsel_host_level);
    uartsel_init(53, 0);
    TEST_ASSERT_EQUAL(0, uartsel_host_level);
}

void test_comm_fail_goes_stale_after_three(void) {
    dstate_dataok();
    ser_comm_good();
    ser_comm_fail("one"); ser_comm_fail("two");
    TEST_ASSERT_TRUE(dstate_is_dataok());
    ser_comm_fail("three");
    TEST_ASSERT_FALSE(dstate_is_dataok());
    ser_comm_good(); /* logs recovery, resets the counter */
    dstate_dataok();
    ser_comm_fail("again");
    TEST_ASSERT_TRUE(dstate_is_dataok());
}
