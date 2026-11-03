/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
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
    /* the page-0 frame an SMT750 (firmware 9.2) sends after 0xFD, captured on the bench */
    static const uint8_t smt750[19] = {0x00, 0x0a, 0x10, 0x89, 0x00, 0x12, 0x11, 0x09, 0x00, 0x01, 0x00, 0x46, 0x00, 0xf8, 0x02, 0xfe, 0x04, 0x4b, 0x9f};
    uint8_t noisy[23] = {0x13, 0x80, 0xff, 0x00};
    memcpy(noisy + 4, smt750, sizeof smt750);
    uint8_t bad_sum[19];
    memcpy(bad_sum, smt750, sizeof smt750);
    bad_sum[18] ^= 1;
    static const uint8_t zeros[32] = {0}; /* line stuck low: every byte reads 0x00 */
    static const uint8_t old_ok[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t width1[4] = {0x00, 0x7f, 0x01, 0x7f}; /* checksum-valid, too short to carry page 0 */
    static const uint8_t width2[5] = {0x00, 0x7f, 0x02, 0x7d, 0x01};
    uint8_t long_noise[300 + sizeof smt750];
    memset(long_noise, 0x13, 300);
    memcpy(long_noise + 300, smt750, sizeof smt750);
    probe_with(smt750, sizeof smt750, true);
    serial_host_reset();
    probe_with(noisy, sizeof noisy, true);
    serial_host_reset();
    probe_with(smt750, sizeof smt750 - 1, false); /* truncated */
    serial_host_reset();
    probe_with(bad_sum, sizeof bad_sum, false);
    serial_host_reset();
    probe_with(zeros, sizeof zeros, false);
    serial_host_reset();
    probe_with(old_ok, sizeof old_ok, false);
    serial_host_reset();
    probe_with(width1, sizeof width1, false);
    serial_host_reset();
    probe_with(width2, sizeof width2, false);
    serial_host_reset();
    probe_with(long_noise, sizeof long_noise, true); /* more noise than the buffer holds */
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
