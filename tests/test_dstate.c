/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <setjmp.h>

#include "nutshim.h"
#include "unity.h"

void test_setinfo_getinfo_roundtrip(void) {
    dstate_setinfo("ups.load", "%d", 42);
    TEST_ASSERT_EQUAL_STRING("42", dstate_getinfo("ups.load"));
    TEST_ASSERT_NULL(dstate_getinfo("nope"));
    char buf[8];
    TEST_ASSERT_TRUE(dstate_copyinfo("ups.load", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("42", buf);
    TEST_ASSERT_FALSE(dstate_copyinfo("nope", buf, sizeof buf));
}

void test_flags_and_aux(void) {
    dstate_setinfo("ups.delay.shutdown", "20");
    TEST_ASSERT_EQUAL(0, dstate_getflags("ups.delay.shutdown"));
    dstate_setflags("ups.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING);
    dstate_setaux("ups.delay.shutdown", 3);
    TEST_ASSERT_EQUAL(ST_FLAG_RW | ST_FLAG_STRING, dstate_getflags("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL(3, dstate_getaux("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL(-1, dstate_getflags("nope"));
    TEST_ASSERT_EQUAL(-1, dstate_getaux("nope"));
    /* updating the value keeps flags */
    dstate_setinfo("ups.delay.shutdown", "30");
    TEST_ASSERT_EQUAL(ST_FLAG_RW | ST_FLAG_STRING, dstate_getflags("ups.delay.shutdown"));
}

void test_status_and_alarm_builders(void) {
    status_init(); status_set("OL"); status_set("CHRG"); status_commit();
    TEST_ASSERT_EQUAL_STRING("OL CHRG", dstate_getinfo("ups.status"));
    status_init(); status_set("OB"); status_commit();
    TEST_ASSERT_EQUAL_STRING("OB", dstate_getinfo("ups.status"));
    alarm_init(); alarm_commit();
    TEST_ASSERT_NULL(dstate_getinfo("ups.alarm")); /* no alarms: variable removed, as NUT does */
    alarm_init(); alarm_set("Replace battery!"); alarm_commit();
    TEST_ASSERT_EQUAL_STRING("Replace battery!", dstate_getinfo("ups.alarm"));
    alarm_init(); alarm_commit();
    TEST_ASSERT_NULL(dstate_getinfo("ups.alarm"));
    /* an active alarm adds ALARM to ups.status, as in NUT */
    status_init(); alarm_init(); status_set("OL"); alarm_set("Battery system - Disconnected"); status_commit(); alarm_commit();
    TEST_ASSERT_EQUAL_STRING("ALARM OL", dstate_getinfo("ups.status"));
    status_init(); alarm_init(); status_set("OL"); status_commit(); alarm_commit();
    TEST_ASSERT_EQUAL_STRING("OL", dstate_getinfo("ups.status"));
    TEST_ASSERT_NULL(dstate_getinfo("ups.alarm"));
}

void test_delinfo(void) {
    dstate_setinfo("battery.charge", "100");
    dstate_setinfo("battery.runtime", "16200");
    TEST_ASSERT_EQUAL(1, dstate_delinfo("battery.charge"));
    TEST_ASSERT_NULL(dstate_getinfo("battery.charge"));
    TEST_ASSERT_EQUAL_STRING("16200", dstate_getinfo("battery.runtime"));
    TEST_ASSERT_EQUAL(0, dstate_delinfo("battery.charge"));
}

void test_dataok_datastale(void) {
    TEST_ASSERT_FALSE(dstate_is_dataok());
    dstate_dataok();
    TEST_ASSERT_TRUE(dstate_is_dataok());
    dstate_datastale();
    TEST_ASSERT_FALSE(dstate_is_dataok());
    TEST_ASSERT_FALSE(dstate_is_servable());
    dstate_dataok();
    TEST_ASSERT_TRUE(dstate_is_servable()); /* no ups.status at all (the protocol tests' fixtures) */
    status_init(); /* a new session's first poll: the UPS has not sent its status yet */
    alarm_init(); alarm_set("Battery system - Disconnected"); /* an alarm alone does not make it servable */
    status_commit();
    TEST_ASSERT_TRUE(dstate_is_dataok());
    TEST_ASSERT_FALSE(dstate_is_servable());
    status_set("OL");
    status_commit();
    TEST_ASSERT_TRUE(dstate_is_servable());
    TEST_ASSERT_EQUAL_STRING("ALARM OL", dstate_getinfo("ups.status"));
    alarm_init();
}

static void collect_var(const char *name, const char *value, int flags, long aux, void *ctx) {
    (void) value; (void) flags; (void) aux;
    strcat((char *) ctx, name);
    strcat((char *) ctx, ",");
}
static void collect_cmd(const char *name, void *ctx) { strcat((char *) ctx, name); strcat((char *) ctx, ","); }

void test_foreach_order_and_cmds(void) {
    char out[64] = "";
    dstate_setinfo("b", "1"); dstate_setinfo("a", "2"); dstate_setinfo("c", "3");
    dstate_setinfo("b", "9"); /* update must not move it */
    dstate_addcmd("x"); dstate_addcmd("y"); dstate_addcmd("x"); /* duplicate ignored */
    dstate_foreach(collect_var, out);
    TEST_ASSERT_EQUAL_STRING("b,a,c,", out);
    out[0] = 0;
    dstate_foreach_cmd(collect_cmd, out);
    TEST_ASSERT_EQUAL_STRING("x,y,", out);
}

void test_long_values_truncate_not_overflow(void) {
    char big[600];
    memset(big, 'z', sizeof big - 1); big[sizeof big - 1] = 0;
    dstate_setinfo("big", "%s", big);
    TEST_ASSERT_EQUAL(255, strlen(dstate_getinfo("big")));
    char longname[100];
    memset(longname, 'n', sizeof longname - 1); longname[sizeof longname - 1] = 0;
    TEST_ASSERT_NULL(dstate_setinfo(longname, "1")); /* name too long: rejected */
}

void test_table_full_is_rejected_not_corrupting(void) {
    char name[16];
    for (int i = 0; i < 500; i++) { snprintf(name, sizeof name, "v%d", i); dstate_setinfo(name, "1"); }
    TEST_ASSERT_NOT_NULL(dstate_getinfo("v399"));
    TEST_ASSERT_NULL(dstate_getinfo("v400"));
    dstate_setinfo("v0", "2"); /* existing entries still update when full */
    TEST_ASSERT_EQUAL_STRING("2", dstate_getinfo("v0"));
}

void test_getval_testvar_addvar(void) {
    static const char *const kv[] = {"cmdsrc", "smartslot1", "baudrate", "9600"};
    nutshim_setvar_table(kv, 4);
    addvar(VAR_VALUE, "hid_fallback", "x");
    TEST_ASSERT_EQUAL_STRING("smartslot1", getval("cmdsrc"));
    TEST_ASSERT_TRUE(testvar("baudrate"));
    TEST_ASSERT_FALSE(testvar("hid_fallback"));
    TEST_ASSERT_NULL(getval("hid_fallback"));
}

static jmp_buf fatal_jmp;
static int fatal_status;
static void fatal_test_hook(int status) { fatal_status = status; longjmp(fatal_jmp, 1); }

void test_fatalx_marks_stale_and_calls_hook(void) {
    dstate_dataok();
    void (*saved)(int) = nutshim_fatal_hook;
    nutshim_fatal_hook = fatal_test_hook;
    if (setjmp(fatal_jmp) == 0) fatalx(EF_EXIT_FAILURE, "boom %d", 1);
    nutshim_fatal_hook = saved;
    TEST_ASSERT_EQUAL(EF_EXIT_FAILURE, fatal_status);
    TEST_ASSERT_FALSE(dstate_is_dataok());
}

void test_debug_and_log_helpers_do_not_crash(void) {
    nut_debug_level = 3;
    upsdebugx(1, "x %d", 1);
    upsdebugx(9, "suppressed");
    upslogx(LOG_ERR, "e"); upslogx(LOG_WARNING, "w"); upslogx(LOG_NOTICE, "n");
    upsdebug_hex(1, "hex", "\x01\xab", 2);
    char b[16];
    TEST_ASSERT_EQUAL(3, snprintf_dynamic(b, sizeof b, "%u", "%u", 123u));
    nut_debug_level = 0;
}
