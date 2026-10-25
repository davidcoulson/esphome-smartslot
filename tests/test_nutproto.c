/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutproto.h"
#include "nutshim.h"
#include "unity.h"

static char out[16384];
static size_t out_n;
static const char *fake_user = "", *fake_pass = "";
static int instcmd_ret, setvar_ret, instcmd_calls, setvar_calls, fake_logins;
static char last_cmd[64], last_param[64], last_var[64], last_val[64];
static bool last_param_null;

static bool fake_check(const char *u, const char *p) { return fake_user[0] && !strcmp(u, fake_user) && !strcmp(p, fake_pass); }
static int fake_instcmd(const char *c, const char *p) {
    instcmd_calls++;
    snprintf(last_cmd, sizeof last_cmd, "%s", c);
    last_param_null = p == NULL;
    snprintf(last_param, sizeof last_param, "%s", p ? p : "");
    return instcmd_ret;
}
static int fake_setvar(const char *v, const char *x) {
    setvar_calls++;
    snprintf(last_var, sizeof last_var, "%s", v);
    snprintf(last_val, sizeof last_val, "%s", x);
    return setvar_ret;
}
static int fake_send(void *ctx, const char *b, size_t n) {
    (void) ctx;
    if (out_n + n >= sizeof out) return -1;
    memcpy(out + out_n, b, n);
    out_n += n;
    out[out_n] = 0;
    return 0;
}
static int fake_numlogins(void) { return fake_logins; }
static const nutproto_hooks hooks = {fake_check, fake_instcmd, fake_setvar, fake_send, "esphome-smartslot upsd test", fake_numlogins};
static nutproto_session sess;

static void reset(void) {
    out_n = 0; out[0] = 0;
    fake_user = ""; fake_pass = "";
    instcmd_ret = setvar_ret = instcmd_calls = setvar_calls = fake_logins = 0;
    nutproto_fsd_clear();
    nutproto_session_init(&sess, &hooks, NULL);
}
static int handle(const char *line) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", line);
    out_n = 0; out[0] = 0;
    return nutproto_handle(&sess, buf);
}

void test_proto_list_var_escaping(void) {
    reset();
    dstate_setinfo("ups.model", "Smart-UPS 750");
    dstate_setinfo("ups.firmware", "UPS 09.3 \"ID1008\"\\x");
    dstate_dataok();
    handle("LIST VAR ups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST VAR ups\nVAR ups ups.model \"Smart-UPS 750\"\n"
                             "VAR ups ups.firmware \"UPS 09.3 \\\"ID1008\\\"\\\\x\"\nEND LIST VAR ups\n", out);
}

void test_proto_list_rw_cmd_enum(void) {
    reset();
    dstate_setinfo("a", "1"); dstate_setinfo("b", "2"); dstate_setflags("b", ST_FLAG_RW);
    dstate_addcmd("beeper.toggle"); dstate_dataok();
    handle("LIST RW ups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST RW ups\nRW ups b \"2\"\nEND LIST RW ups\n", out);
    handle("LIST CMD ups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CMD ups\nCMD ups beeper.toggle\nEND LIST CMD ups\n", out);
    handle("LIST ENUM ups a");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST ENUM ups a\nEND LIST ENUM ups a\n", out);
    handle("LIST RANGE ups a");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST RANGE ups a\nEND LIST RANGE ups a\n", out);
    handle("LIST ENUM ups nope");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", out);
}

void test_proto_big_list_var_is_complete(void) {
    reset();
    char name[16];
    for (int i = 0; i < 300; i++) { snprintf(name, sizeof name, "v.%d", i); dstate_setinfo(name, "value-%d", i); }
    dstate_dataok();
    handle("LIST VAR ups");
    TEST_ASSERT_NOT_NULL(strstr(out, "VAR ups v.299 \"value-299\"\nEND LIST VAR ups\n"));
}

void test_proto_get_var_type_desc(void) {
    reset();
    dstate_setinfo("ups.delay.shutdown", "20"); dstate_setflags("ups.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING); dstate_setaux("ups.delay.shutdown", 8);
    dstate_setinfo("battery.charge", "100");
    dstate_dataok();
    handle("GET VAR ups battery.charge");
    TEST_ASSERT_EQUAL_STRING("VAR ups battery.charge \"100\"\n", out);
    handle("GET TYPE ups ups.delay.shutdown");
    TEST_ASSERT_EQUAL_STRING("TYPE ups ups.delay.shutdown RW STRING:8\n", out);
    handle("GET TYPE ups battery.charge");
    TEST_ASSERT_EQUAL_STRING("TYPE ups battery.charge NUMBER\n", out);
    dstate_setflags("battery.charge", ST_FLAG_RW | ST_FLAG_NUMBER);
    handle("GET TYPE ups battery.charge");
    TEST_ASSERT_EQUAL_STRING("TYPE ups battery.charge RW NUMBER\n", out);
    handle("GET DESC ups battery.charge");
    TEST_ASSERT_EQUAL_STRING("DESC ups battery.charge \"Description unavailable\"\n", out);
    handle("GET VAR ups nope");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", out);
    dstate_addcmd("beeper.toggle");
    handle("GET CMDDESC ups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("CMDDESC ups beeper.toggle \"Description unavailable\"\n", out);
    handle("GET CMDDESC ups nope");
    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", out);
    handle("GET UPSDESC ups");
    TEST_ASSERT_EQUAL_STRING("UPSDESC ups \"SmartSlot UPS\"\n", out);
    dstate_setinfo("device.model", "Smart-UPS 750");
    handle("LIST UPS");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST UPS\nUPS ups \"Smart-UPS 750\"\nEND LIST UPS\n", out);
    fake_logins = 2;
    handle("GET NUMLOGINS ups");
    TEST_ASSERT_EQUAL_STRING("NUMLOGINS ups 2\n", out);
}

void test_proto_data_stale(void) {
    reset();
    dstate_setinfo("a", "1");
    dstate_addcmd("x");
    fake_user = "u"; fake_pass = "p";
    handle("USERNAME u"); handle("PASSWORD p");
    dstate_datastale();
    handle("LIST VAR ups"); TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", out);
    handle("LIST RW ups"); TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", out);
    handle("GET VAR ups a"); TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", out);
    handle("INSTCMD ups x"); TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", out);
    handle("SET VAR ups a 1"); TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", out);
    TEST_ASSERT_EQUAL(0, instcmd_calls + setvar_calls);
    handle("LIST UPS"); TEST_ASSERT_NOT_NULL(strstr(out, "UPS ups"));
    handle("LIST CMD ups"); TEST_ASSERT_NOT_NULL(strstr(out, "CMD ups x"));
}

void test_proto_instcmd_auth(void) {
    reset();
    dstate_dataok();
    handle("USERNAME a"); handle("PASSWORD b");
    handle("INSTCMD ups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", out);
    TEST_ASSERT_EQUAL(0, instcmd_calls);
    fake_user = "a"; fake_pass = "secret";
    handle("INSTCMD ups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", out);
    handle("PASSWORD secret");
    handle("INSTCMD ups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    TEST_ASSERT_EQUAL_STRING("beeper.toggle", last_cmd);
    TEST_ASSERT_TRUE(last_param_null);
    handle("INSTCMD ups load.off.delay 30");
    TEST_ASSERT_EQUAL_STRING("30", last_param);
    handle("INSTCMD other x"); TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", out);
    handle("INSTCMD ups"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
}

void test_proto_result_mapping(void) {
    reset();
    dstate_dataok();
    fake_user = "a"; fake_pass = "b";
    handle("USERNAME a"); handle("PASSWORD b");
    instcmd_ret = STAT_INSTCMD_UNKNOWN; handle("INSTCMD ups c"); TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", out);
    instcmd_ret = STAT_INSTCMD_INVALID; handle("INSTCMD ups c"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    instcmd_ret = STAT_INSTCMD_FAILED; handle("INSTCMD ups c"); TEST_ASSERT_EQUAL_STRING("ERR INSTCMD-FAILED\n", out);
    setvar_ret = STAT_SET_UNKNOWN; handle("SET VAR ups v \"1\""); TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", out);
    setvar_ret = STAT_SET_INVALID; handle("SET VAR ups v \"1\""); TEST_ASSERT_EQUAL_STRING("ERR INVALID-VALUE\n", out);
    setvar_ret = STAT_SET_FAILED; handle("SET VAR ups v \"1\""); TEST_ASSERT_EQUAL_STRING("ERR SET-FAILED\n", out);
}

void test_proto_set_var_unquotes(void) {
    reset();
    dstate_dataok();
    fake_user = "a"; fake_pass = "b";
    handle("USERNAME a"); handle("PASSWORD b");
    handle("SET VAR ups ups.delay.shutdown \"20\"");
    TEST_ASSERT_EQUAL_STRING("OK\n", out);
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", last_var);
    TEST_ASSERT_EQUAL_STRING("20", last_val);
    handle("SET VAR ups ups.id \"a \\\"b\\\" \\\\c\"");
    TEST_ASSERT_EQUAL_STRING("a \"b\" \\c", last_val);
    handle("SET VAR ups x"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    handle("SET FOO ups x y"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
}

void test_proto_login_logout_misc(void) {
    reset();
    handle("LOGIN ups"); TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", out);
    TEST_ASSERT_FALSE(sess.logged_in);
    fake_user = "a"; fake_pass = "b";
    handle("USERNAME a"); TEST_ASSERT_EQUAL_STRING("OK\n", out);
    handle("PASSWORD b"); TEST_ASSERT_EQUAL_STRING("OK\n", out);
    handle("LOGIN other"); TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", out);
    handle("LOGIN ups"); TEST_ASSERT_EQUAL_STRING("OK\n", out);
    TEST_ASSERT_TRUE(sess.logged_in);
    handle("STARTTLS"); TEST_ASSERT_EQUAL_STRING("ERR FEATURE-NOT-CONFIGURED\n", out);
    handle("FOO"); TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-COMMAND\n", out);
    handle("GET VAR other x"); TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", out);
    handle("GET"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    handle("LIST"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    handle("LIST FOO ups"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    handle("GET FOO ups x"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    TEST_ASSERT_EQUAL(0, handle("")); TEST_ASSERT_EQUAL(0, (int) out_n);
    TEST_ASSERT_EQUAL(0, handle("LIST UPS \"unterminated")); /* error reply, connection kept */
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    handle("a b c d e f g h i"); TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    TEST_ASSERT_EQUAL(1, handle("LOGOUT"));
    TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", out);
    TEST_ASSERT_FALSE(sess.logged_in);
}

void test_proto_ver_netver_and_send_failure(void) {
    reset();
    handle("VER"); TEST_ASSERT_EQUAL_STRING("esphome-smartslot upsd test\n", out);
    handle("netver"); TEST_ASSERT_EQUAL_STRING("1.3\n", out);
    dstate_setinfo("a", "1"); dstate_dataok();
    static char tiny_out;
    (void) tiny_out;
    out_n = sizeof out - 4; /* fake_send now fails */
    char l[] = "NETVER";
    TEST_ASSERT_EQUAL(1, nutproto_handle(&sess, l)); /* send failure asks for the connection to be closed */
}

void test_proto_primary_and_fsd(void) {
    reset();
    dstate_setinfo("ups.status", "OL");
    dstate_dataok();
    handle("PRIMARY ups");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", out);
    handle("FSD ups");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", out);
    fake_user = "u"; fake_pass = "p";
    handle("USERNAME u"); handle("PASSWORD p");
    handle("PRIMARY nope");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", out);
    handle("MASTER ups");
    TEST_ASSERT_EQUAL_STRING("OK PRIMARY-GRANTED\n", out);
    handle("PRIMARY");
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", out);
    handle("GET VAR ups ups.status");
    TEST_ASSERT_EQUAL_STRING("VAR ups ups.status \"OL\"\n", out);
    handle("FSD ups");
    TEST_ASSERT_EQUAL_STRING("OK FSD-SET\n", out);
    handle("GET VAR ups ups.status");
    TEST_ASSERT_EQUAL_STRING("VAR ups ups.status \"OL FSD\"\n", out);
    handle("LIST VAR ups");
    TEST_ASSERT_NOT_NULL(strstr(out, "VAR ups ups.status \"OL FSD\"\n"));
    nutproto_fsd_clear();
}
