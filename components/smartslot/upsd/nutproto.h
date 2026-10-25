/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NUT network protocol (upsd side, protocol 1.3): one parsed line in, replies out through a send hook. */
#ifndef SMARTSLOT_NUTPROTO_H
#define SMARTSLOT_NUTPROTO_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool (*check_user)(const char *user, const char *pass); /* false when no user configured */
    int (*instcmd)(const char *cmd, const char *param);     /* STAT_INSTCMD_*; param may be NULL */
    int (*setvar)(const char *var, const char *val);        /* STAT_SET_* */
    int (*send)(void *ctx, const char *buf, size_t len);    /* 0 ok, -1 drop client */
    const char *version;                                    /* for VER */
    int (*numlogins)(void);
} nutproto_hooks;

typedef struct {
    char user[33];
    char pass[65];
    bool logged_in;
    const nutproto_hooks *h;
    void *ctx;
} nutproto_session;

void nutproto_session_init(nutproto_session *s, const nutproto_hooks *h, void *ctx);
/* `line` has no CR/LF and is modified in place. Returns 0 = keep the connection, 1 = close it. */
int nutproto_handle(nutproto_session *s, char *line);

#ifdef __cplusplus
}
#endif
#endif
