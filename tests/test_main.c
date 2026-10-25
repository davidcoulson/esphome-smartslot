/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutshim.h"
#include "serial_host.h"
#include "unity.h"

void setUp(void) { nutshim_reset_for_test(); serial_host_reset(); }
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
void test_serial_loopback_and_timeout(void);
void test_microlink_probe_cases(void);
void test_uartsel_init_records_level(void);
void test_comm_fail_goes_stale_after_three(void);

void test_proto_list_var_escaping(void);
void test_proto_list_rw_cmd_enum(void);
void test_proto_big_list_var_is_complete(void);
void test_proto_get_var_type_desc(void);
void test_proto_data_stale(void);
void test_proto_instcmd_auth(void);
void test_proto_result_mapping(void);
void test_proto_set_var_unquotes(void);
void test_proto_login_logout_misc(void);
void test_proto_ver_netver_and_send_failure(void);

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
    RUN_TEST(test_serial_loopback_and_timeout);
    RUN_TEST(test_microlink_probe_cases);
    RUN_TEST(test_uartsel_init_records_level);
    RUN_TEST(test_comm_fail_goes_stale_after_three);
    RUN_TEST(test_proto_list_var_escaping);
    RUN_TEST(test_proto_list_rw_cmd_enum);
    RUN_TEST(test_proto_big_list_var_is_complete);
    RUN_TEST(test_proto_get_var_type_desc);
    RUN_TEST(test_proto_data_stale);
    RUN_TEST(test_proto_instcmd_auth);
    RUN_TEST(test_proto_result_mapping);
    RUN_TEST(test_proto_set_var_unquotes);
    RUN_TEST(test_proto_login_logout_misc);
    RUN_TEST(test_proto_ver_netver_and_send_failure);
    return UNITY_END();
}
