/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutshim.h"
#include "unity.h"

void setUp(void) { nutshim_reset_for_test(); }
void tearDown(void) {}

void test_setinfo_getinfo_roundtrip(void);
void test_flags_and_aux(void);
void test_status_and_alarm_builders(void);
void test_dataok_datastale(void);
void test_foreach_order_and_cmds(void);
void test_long_values_truncate_not_overflow(void);
void test_table_full_is_rejected_not_corrupting(void);
void test_getval_testvar_addvar(void);
void test_fatalx_marks_stale_and_calls_hook(void);
void test_debug_and_log_helpers_do_not_crash(void);

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_setinfo_getinfo_roundtrip);
    RUN_TEST(test_flags_and_aux);
    RUN_TEST(test_status_and_alarm_builders);
    RUN_TEST(test_dataok_datastale);
    RUN_TEST(test_foreach_order_and_cmds);
    RUN_TEST(test_long_values_truncate_not_overflow);
    RUN_TEST(test_table_full_is_rejected_not_corrupting);
    RUN_TEST(test_getval_testvar_addvar);
    RUN_TEST(test_fatalx_marks_stale_and_calls_hook);
    RUN_TEST(test_debug_and_log_helpers_do_not_crash);
    return UNITY_END();
}
