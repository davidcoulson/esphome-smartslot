/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nutshim.h"
#include "unity.h"
#include "upsd.h"

static bool check(const char *u, const char *p) { return !strcmp(u, "u") && !strcmp(p, "p"); }
static int noinst(const char *c, const char *p) { (void) c; (void) p; return STAT_INSTCMD_HANDLED; }
static int noset(const char *c, const char *p) { (void) c; (void) p; return STAT_SET_HANDLED; }
static const nutproto_hooks hooks = {check, noinst, noset, NULL, "ver", upsd_num_logins};
static nutproto_hooks run_hooks;

static void start_server(void) {
    run_hooks = hooks;
    run_hooks.send = (int (*)(void *, const char *, size_t)) 0; /* upsd installs its own send */
    TEST_ASSERT_EQUAL(0, upsd_start(&run_hooks, 0));
}

static int dial(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(upsd_port());
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    TEST_ASSERT_EQUAL(0, connect(fd, (struct sockaddr *) &a, sizeof a));
    return fd;
}

/* Reads until `want` bytes or EOF/timeout; returns the count. */
static size_t slurp(int fd, char *buf, size_t cap, size_t want) {
    size_t n = 0;
    while (n < want && n + 1 < cap) {
        ssize_t r = recv(fd, buf + n, cap - 1 - n, 0);
        if (r <= 0) break;
        n += (size_t) r;
    }
    buf[n] = 0;
    return n;
}

static void ask(int fd, const char *q, const char *expect) {
    char buf[256];
    send(fd, q, strlen(q), 0);
    slurp(fd, buf, sizeof buf, strlen(expect));
    TEST_ASSERT_EQUAL_STRING(expect, buf);
}

void test_upsd_netver_over_tcp(void) {
    start_server();
    int fd = dial();
    ask(fd, "NETVER\n", "1.3\n");
    ask(fd, "NETVER\r\nVER\n", "1.3\nver\n"); /* CRLF and two lines in one segment */
    close(fd);
    upsd_stop();
}

void test_upsd_fifth_client_refused_four_survive(void) {
    start_server();
    int c[4];
    for (int i = 0; i < 4; i++) { c[i] = dial(); ask(c[i], "NETVER\n", "1.3\n"); }
    int fifth = dial();
    char buf[16];
    TEST_ASSERT_EQUAL(0, (int) slurp(fifth, buf, sizeof buf, 1)); /* EOF, no greeting */
    close(fifth);
    for (int i = 0; i < 4; i++) { ask(c[i], "NETVER\n", "1.3\n"); close(c[i]); }
    upsd_stop();
}

void test_upsd_overlong_line_rejected(void) {
    start_server();
    int fd = dial();
    char big[600];
    memset(big, 'A', sizeof big);
    send(fd, big, sizeof big, 0);
    char buf[64];
    slurp(fd, buf, sizeof buf, 21);
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", buf);
    TEST_ASSERT_EQUAL(0, (int) slurp(fd, buf, sizeof buf, 1)); /* then EOF */
    close(fd);
    int ok = dial(); /* the server is still alive and the slot was freed */
    ask(ok, "NETVER\n", "1.3\n");
    close(ok);
    upsd_stop();
}

void test_upsd_line_just_under_limit_is_handled(void) {
    start_server();
    int fd = dial();
    char big[512];
    memset(big, 'A', sizeof big);
    big[510] = '\n'; /* 510 chars + newline */
    send(fd, big, 511, 0);
    char buf[64];
    slurp(fd, buf, sizeof buf, 22);
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-COMMAND\n", buf);
    close(fd);
    upsd_stop();
}

void test_upsd_logout_closes_and_numlogins_counts(void) {
    start_server();
    dstate_dataok();
    int a = dial(), b = dial();
    ask(a, "USERNAME u\n", "OK\n");
    ask(a, "PASSWORD p\n", "OK\n");
    ask(a, "LOGIN ups\n", "OK\n");
    ask(a, "GET NUMLOGINS ups\n", "NUMLOGINS ups 1\n");
    ask(b, "GET NUMLOGINS ups\n", "NUMLOGINS ups 1\n");
    ask(a, "LOGOUT\n", "OK Goodbye\n");
    char buf[8];
    TEST_ASSERT_EQUAL(0, (int) slurp(a, buf, sizeof buf, 1));
    close(a);
    ask(b, "GET NUMLOGINS ups\n", "NUMLOGINS ups 0\n");
    close(b);
    upsd_stop();
}
