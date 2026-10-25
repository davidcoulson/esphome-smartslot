/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nutproto.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nutshim.h"

#define UPS_NAME "ups"
#define MAX_ARGS 8
#define LINE_MAX_OUT 1100 /* name 64 + value 255 escaped to 510 + framing */

static int reply(nutproto_session *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int reply(nutproto_session *s, const char *fmt, ...) {
    char buf[LINE_MAX_OUT];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n >= (int) sizeof buf) n = (int) sizeof buf - 1;
    return s->h->send(s->ctx, buf, (size_t) n);
}

/* Splits on blanks; double quotes may wrap any argument; backslash escapes the next character. In place.
 * Returns the argument count, or -1 for an unterminated quote or too many arguments. */
static int tokenize(char *line, char *argv[MAX_ARGS]) {
    int argc = 0;
    char *r = line, *w = line;
    for (;;) {
        while (*r == ' ' || *r == '\t') r++;
        if (!*r) return argc;
        if (argc == MAX_ARGS) return -1;
        argv[argc++] = w;
        bool quoted = false;
        while (*r && (quoted || (*r != ' ' && *r != '\t'))) {
            if (*r == '\\' && r[1]) {
                *w++ = r[1];
                r += 2;
            } else if (*r == '"') {
                quoted = !quoted;
                r++;
            } else {
                *w++ = *r++;
            }
        }
        if (quoted) return -1;
        bool more = *r != 0;
        *w++ = 0;
        if (!more) return argc;
        r++;
    }
}

/* Appends `src` to dst (size dn) with " and \ backslash-escaped. */
static size_t escape(char *dst, size_t dn, const char *src) {
    size_t n = 0;
    for (; *src && n + 2 < dn; src++) {
        if (*src == '"' || *src == '\\') dst[n++] = '\\';
        dst[n++] = *src;
    }
    dst[n] = 0;
    return n;
}

static int var_line(char *buf, size_t n, const char *kind, const char *name, const char *value) {
    char esc[2 * 256 + 2];
    escape(esc, sizeof esc, value);
    return snprintf(buf, n, "%s %s %s \"%s\"\n", kind, UPS_NAME, name, esc);
}

struct outbuf {
    char *p;
    size_t len, cap;
    bool oom, rw_only;
};

static void out_append(struct outbuf *o, const char *s, size_t n) {
    if (o->oom) return;
    if (o->len + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 2048;
        while (cap < o->len + n + 1) cap *= 2;
        char *p = realloc(o->p, cap);
        if (!p) { o->oom = true; return; }
        o->p = p;
        o->cap = cap;
    }
    memcpy(o->p + o->len, s, n);
    o->len += n;
    o->p[o->len] = 0;
}

static void list_var_cb(const char *name, const char *value, int flags, long aux, void *ctx) {
    struct outbuf *o = ctx;
    (void) aux;
    if (o->rw_only && !(flags & ST_FLAG_RW)) return;
    char line[LINE_MAX_OUT];
    int n = var_line(line, sizeof line, o->rw_only ? "RW" : "VAR", name, value);
    if (n > 0) out_append(o, line, (size_t) (n < (int) sizeof line ? n : (int) sizeof line - 1));
}

static void list_cmd_cb(const char *name, void *ctx) {
    char line[128];
    int n = snprintf(line, sizeof line, "CMD %s %s\n", UPS_NAME, name);
    out_append(ctx, line, (size_t) n);
}

struct find { const char *name; bool found; };
static void find_cmd_cb(const char *name, void *ctx) {
    struct find *f = ctx;
    if (strcmp(name, f->name) == 0) f->found = true;
}

static const char *model(char *buf, size_t n) {
    if (!dstate_copyinfo("device.model", buf, n) && !dstate_copyinfo("ups.model", buf, n)) snprintf(buf, n, "SmartSlot UPS");
    return buf;
}

static int list_streamed(nutproto_session *s, const char *what, bool rw_only) {
    if (!dstate_is_dataok()) return reply(s, "ERR DATA-STALE\n");
    struct outbuf o = {0};
    o.rw_only = rw_only;
    dstate_foreach(list_var_cb, &o); /* format under the lock, send after releasing it */
    int r = 0;
    if (o.oom) {
        r = reply(s, "ERR DATA-STALE\n");
    } else {
        r = reply(s, "BEGIN LIST %s %s\n", what, UPS_NAME);
        if (r == 0 && o.len) r = s->h->send(s->ctx, o.p, o.len);
        if (r == 0) r = reply(s, "END LIST %s %s\n", what, UPS_NAME);
    }
    free(o.p);
    return r;
}

static int handle_list(nutproto_session *s, int argc, char **argv) {
    if (argc < 2) return reply(s, "ERR INVALID-ARGUMENT\n");
    if (strcasecmp(argv[1], "UPS") == 0) {
        char m[128];
        model(m, sizeof m);
        char esc[300];
        escape(esc, sizeof esc, m);
        int r = reply(s, "BEGIN LIST UPS\n");
        if (r == 0) r = reply(s, "UPS %s \"%s\"\n", UPS_NAME, esc);
        if (r == 0) r = reply(s, "END LIST UPS\n");
        return r;
    }
    if (argc < 3) return reply(s, "ERR INVALID-ARGUMENT\n");
    if (strcmp(argv[2], UPS_NAME) != 0) return reply(s, "ERR UNKNOWN-UPS\n");
    if (strcasecmp(argv[1], "VAR") == 0) return list_streamed(s, "VAR", false);
    if (strcasecmp(argv[1], "RW") == 0) return list_streamed(s, "RW", true);
    if (strcasecmp(argv[1], "CMD") == 0) {
        struct outbuf o = {0};
        dstate_foreach_cmd(list_cmd_cb, &o);
        int r = reply(s, "BEGIN LIST CMD %s\n", UPS_NAME);
        if (r == 0 && o.len) r = s->h->send(s->ctx, o.p, o.len);
        if (r == 0) r = reply(s, "END LIST CMD %s\n", UPS_NAME);
        free(o.p);
        return r;
    }
    bool is_enum = strcasecmp(argv[1], "ENUM") == 0;
    if (is_enum || strcasecmp(argv[1], "RANGE") == 0) {
        if (argc < 4) return reply(s, "ERR INVALID-ARGUMENT\n");
        if (dstate_getflags(argv[3]) < 0) return reply(s, "ERR VAR-NOT-SUPPORTED\n");
        const char *w = is_enum ? "ENUM" : "RANGE";
        int r = reply(s, "BEGIN LIST %s %s %s\n", w, UPS_NAME, argv[3]);
        if (r == 0) r = reply(s, "END LIST %s %s %s\n", w, UPS_NAME, argv[3]);
        return r;
    }
    return reply(s, "ERR INVALID-ARGUMENT\n");
}

static int handle_get(nutproto_session *s, int argc, char **argv) {
    if (argc < 3) return reply(s, "ERR INVALID-ARGUMENT\n");
    const char *what = argv[1];
    if (strcmp(argv[2], UPS_NAME) != 0) return reply(s, "ERR UNKNOWN-UPS\n");
    if (strcasecmp(what, "NUMLOGINS") == 0)
        return reply(s, "NUMLOGINS %s %d\n", UPS_NAME, s->h->numlogins ? s->h->numlogins() : 0);
    if (strcasecmp(what, "UPSDESC") == 0) {
        char m[128], esc[300];
        escape(esc, sizeof esc, model(m, sizeof m));
        return reply(s, "UPSDESC %s \"%s\"\n", UPS_NAME, esc);
    }
    if (argc < 4) return reply(s, "ERR INVALID-ARGUMENT\n");
    const char *name = argv[3];
    if (strcasecmp(what, "CMDDESC") == 0) {
        struct find f = {name, false};
        dstate_foreach_cmd(find_cmd_cb, &f);
        if (!f.found) return reply(s, "ERR CMD-NOT-SUPPORTED\n");
        return reply(s, "CMDDESC %s %s \"Description unavailable\"\n", UPS_NAME, name);
    }
    if (strcasecmp(what, "VAR") == 0) {
        if (!dstate_is_dataok()) return reply(s, "ERR DATA-STALE\n");
        char val[256], line[LINE_MAX_OUT];
        if (!dstate_copyinfo(name, val, sizeof val)) return reply(s, "ERR VAR-NOT-SUPPORTED\n");
        int n = var_line(line, sizeof line, "VAR", name, val);
        return s->h->send(s->ctx, line, (size_t) (n < (int) sizeof line ? n : (int) sizeof line - 1));
    }
    if (strcasecmp(what, "TYPE") == 0 || strcasecmp(what, "DESC") == 0) {
        int flags = dstate_getflags(name);
        if (flags < 0) return reply(s, "ERR VAR-NOT-SUPPORTED\n");
        if (strcasecmp(what, "DESC") == 0) return reply(s, "DESC %s %s \"Description unavailable\"\n", UPS_NAME, name);
        char type[32];
        if (flags & ST_FLAG_STRING) snprintf(type, sizeof type, "STRING:%ld", dstate_getaux(name));
        else snprintf(type, sizeof type, "NUMBER");
        return reply(s, "TYPE %s %s %s%s\n", UPS_NAME, name, (flags & ST_FLAG_RW) ? "RW " : "", type);
    }
    return reply(s, "ERR INVALID-ARGUMENT\n");
}

static bool authorized(nutproto_session *s) {
    return s->h->check_user && s->h->check_user(s->user, s->pass);
}

static int handle_instcmd(nutproto_session *s, int argc, char **argv) {
    if (argc < 3 || argc > 4) return reply(s, "ERR INVALID-ARGUMENT\n");
    if (strcmp(argv[1], UPS_NAME) != 0) return reply(s, "ERR UNKNOWN-UPS\n");
    if (!authorized(s)) return reply(s, "ERR ACCESS-DENIED\n");
    if (!dstate_is_dataok()) return reply(s, "ERR DATA-STALE\n");
    switch (s->h->instcmd(argv[2], argc == 4 ? argv[3] : NULL)) {
        case STAT_INSTCMD_HANDLED: return reply(s, "OK\n");
        case STAT_INSTCMD_UNKNOWN: return reply(s, "ERR CMD-NOT-SUPPORTED\n");
        case STAT_INSTCMD_INVALID:
        case STAT_INSTCMD_CONVERSION_FAILED: return reply(s, "ERR INVALID-ARGUMENT\n");
        default: return reply(s, "ERR INSTCMD-FAILED\n");
    }
}

static int handle_set(nutproto_session *s, int argc, char **argv) {
    if (argc != 5 || strcasecmp(argv[1], "VAR") != 0) return reply(s, "ERR INVALID-ARGUMENT\n");
    if (strcmp(argv[2], UPS_NAME) != 0) return reply(s, "ERR UNKNOWN-UPS\n");
    if (!authorized(s)) return reply(s, "ERR ACCESS-DENIED\n");
    if (!dstate_is_dataok()) return reply(s, "ERR DATA-STALE\n");
    switch (s->h->setvar(argv[3], argv[4])) {
        case STAT_SET_HANDLED: return reply(s, "OK\n");
        case STAT_SET_UNKNOWN: return reply(s, "ERR VAR-NOT-SUPPORTED\n");
        case STAT_SET_INVALID:
        case STAT_SET_CONVERSION_FAILED: return reply(s, "ERR INVALID-VALUE\n");
        default: return reply(s, "ERR SET-FAILED\n");
    }
}

void nutproto_session_init(nutproto_session *s, const nutproto_hooks *h, void *ctx) {
    memset(s, 0, sizeof *s);
    s->h = h;
    s->ctx = ctx;
}

int nutproto_handle(nutproto_session *s, char *line) {
    char *argv[MAX_ARGS];
    int argc = tokenize(line, argv);
    int r;
    if (argc == 0) return 0;
    if (argc < 0) return reply(s, "ERR INVALID-ARGUMENT\n") < 0;
    const char *cmd = argv[0];
    if (strcasecmp(cmd, "VER") == 0) r = reply(s, "%s\n", s->h->version);
    else if (strcasecmp(cmd, "NETVER") == 0) r = reply(s, "1.3\n");
    else if (strcasecmp(cmd, "LIST") == 0) r = handle_list(s, argc, argv);
    else if (strcasecmp(cmd, "GET") == 0) r = handle_get(s, argc, argv);
    else if (strcasecmp(cmd, "INSTCMD") == 0) r = handle_instcmd(s, argc, argv);
    else if (strcasecmp(cmd, "SET") == 0) r = handle_set(s, argc, argv);
    else if (strcasecmp(cmd, "USERNAME") == 0 || strcasecmp(cmd, "PASSWORD") == 0) {
        if (argc != 2) r = reply(s, "ERR INVALID-ARGUMENT\n");
        else {
            bool user = strcasecmp(cmd, "USERNAME") == 0;
            char *dst = user ? s->user : s->pass;
            size_t cap = user ? sizeof s->user : sizeof s->pass;
            snprintf(dst, cap, "%s", argv[1]);
            r = reply(s, "OK\n");
        }
    } else if (strcasecmp(cmd, "LOGIN") == 0) {
        if (argc != 2) r = reply(s, "ERR INVALID-ARGUMENT\n");
        else if (strcmp(argv[1], UPS_NAME) != 0) r = reply(s, "ERR UNKNOWN-UPS\n");
        else if (!authorized(s)) r = reply(s, "ERR ACCESS-DENIED\n");
        else { s->logged_in = true; r = reply(s, "OK\n"); }
    } else if (strcasecmp(cmd, "LOGOUT") == 0) {
        s->logged_in = false;
        reply(s, "OK Goodbye\n");
        return 1;
    } else if (strcasecmp(cmd, "STARTTLS") == 0) r = reply(s, "ERR FEATURE-NOT-CONFIGURED\n");
    else r = reply(s, "ERR UNKNOWN-COMMAND\n");
    return r < 0;
}
