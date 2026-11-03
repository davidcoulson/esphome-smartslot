/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutdrv.h"
#include "nutshim.h"
#include "serial_host.h"
#include "unity.h"

extern int fake_empty_status_polls;
void setUp(void) { nutshim_reset_for_test(); serial_host_reset(); fake_empty_status_polls = 0; }
void tearDown(void) { nutdrv_stop(); } /* a failed test must not leave the driver task running */

void test_setinfo_getinfo_roundtrip(void);
void test_flags_and_aux(void);
void test_status_and_alarm_builders(void);
void test_delinfo(void);
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

void test_upsd_netver_over_tcp(void);
void test_upsd_fifth_client_refused_four_survive(void);
void test_upsd_overlong_line_rejected(void);
void test_upsd_line_just_under_limit_is_handled(void);
void test_upsd_logout_closes_and_numlogins_counts(void);

void test_nutdrv_waits_for_probe_then_runs(void);
void test_nutdrv_instcmd_runs_on_driver_task(void);
void test_nutdrv_detached_request_returns_before_the_driver_runs_it(void);
void test_nutdrv_five_stale_polls_return_to_no_ups(void);
void test_nutdrv_slow_status_is_stale_but_not_reprobed(void);

void test_publish_only_when_running_fresh_and_present(void);

void test_timegm_matches_libc(void);

void test_proto_primary_and_fsd(void);

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_setinfo_getinfo_roundtrip);
    RUN_TEST(test_flags_and_aux);
    RUN_TEST(test_status_and_alarm_builders);
    RUN_TEST(test_delinfo);
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
    RUN_TEST(test_upsd_netver_over_tcp);
    RUN_TEST(test_upsd_fifth_client_refused_four_survive);
    RUN_TEST(test_upsd_overlong_line_rejected);
    RUN_TEST(test_upsd_line_just_under_limit_is_handled);
    RUN_TEST(test_upsd_logout_closes_and_numlogins_counts);
    RUN_TEST(test_nutdrv_waits_for_probe_then_runs);
    RUN_TEST(test_nutdrv_instcmd_runs_on_driver_task);
    RUN_TEST(test_nutdrv_detached_request_returns_before_the_driver_runs_it);
    RUN_TEST(test_nutdrv_five_stale_polls_return_to_no_ups);
    RUN_TEST(test_nutdrv_slow_status_is_stale_but_not_reprobed);
    RUN_TEST(test_publish_only_when_running_fresh_and_present);
    RUN_TEST(test_timegm_matches_libc);
    RUN_TEST(test_proto_primary_and_fsd);
    return UNITY_END();
}
