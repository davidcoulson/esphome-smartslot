/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NUT dstate API on fixed-size tables, mutex-protected: the driver task writes, upsd and ESPHome read. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "nutshim.h"

#define MAX_VARS 400
#define MAX_CMDS 32
#define NAME_LEN 64
#define VALUE_LEN 256

struct var {
    char name[NAME_LEN];
    char value[VALUE_LEN];
    int flags;
    long aux;
};

static struct var vars[MAX_VARS];
static size_t nvars;
static char cmds[MAX_CMDS][NAME_LEN];
static size_t ncmds;
static os_mutex_t lock;
static volatile bool dataok;
static bool warned_full, warned_trunc;
static char status_buf[VALUE_LEN], alarm_buf[VALUE_LEN];

void nutshim_init(void) {
    if (!lock) lock = os_mutex_create();
}

#ifndef ESP_PLATFORM
void nutshim_reset_for_test(void) {
    nutshim_init();
    os_mutex_lock(lock);
    nvars = ncmds = 0;
    memset(vars, 0, sizeof vars);
    warned_full = warned_trunc = false;
    status_buf[0] = alarm_buf[0] = 0;
    os_mutex_unlock(lock);
    dataok = false;
}
#endif

static struct var *find(const char *name) {
    for (size_t i = 0; i < nvars; i++)
        if (strcmp(vars[i].name, name) == 0) return &vars[i];
    return NULL;
}

const char *dstate_setinfo(const char *name, const char *fmt, ...) {
    char tmp[VALUE_LEN];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (strlen(name) >= NAME_LEN) return NULL;
    nutshim_init();
    os_mutex_lock(lock);
    const char *ret = NULL;
    bool trunc = n >= (int) sizeof tmp && !warned_trunc, full = false;
    struct var *v = find(name);
    if (!v && nvars < MAX_VARS) {
        v = &vars[nvars++];
        strcpy(v->name, name);
        v->flags = 0;
        v->aux = 0;
    } else if (!v) {
        full = !warned_full;
        warned_full = true;
    }
    if (v) {
        memcpy(v->value, tmp, sizeof v->value);
        ret = v->value;
    }
    if (trunc) warned_trunc = true;
    os_mutex_unlock(lock);
    if (trunc) os_log(OS_LOG_WARN, "dstate", "value of %s truncated to %d bytes", name, VALUE_LEN - 1);
    if (full) os_log(OS_LOG_ERROR, "dstate", "variable table full (%d), dropping %s", MAX_VARS, name);
    return ret;
}

const char *dstate_getinfo(const char *name) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    os_mutex_unlock(lock);
    return v ? v->value : NULL;
}

int dstate_delinfo(const char *name) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    if (v) {
        memmove(v, v + 1, (size_t) (&vars[nvars] - (v + 1)) * sizeof *v);
        nvars--;
    }
    os_mutex_unlock(lock);
    return v != NULL;
}

bool dstate_copyinfo(const char *name, char *buf, size_t len) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    if (v) snprintf(buf, len, "%s", v->value);
    os_mutex_unlock(lock);
    return v != NULL;
}

int dstate_setflags(const char *name, int flags) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    if (v) v->flags = flags;
    os_mutex_unlock(lock);
    return v ? 0 : -1;
}

int dstate_setaux(const char *name, long aux) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    if (v) v->aux = aux;
    os_mutex_unlock(lock);
    return v ? 0 : -1;
}

int dstate_getflags(const char *name) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    int r = v ? v->flags : -1;
    os_mutex_unlock(lock);
    return r;
}

long dstate_getaux(const char *name) {
    nutshim_init();
    os_mutex_lock(lock);
    struct var *v = find(name);
    long r = v ? v->aux : -1;
    os_mutex_unlock(lock);
    return r;
}

void dstate_addcmd(const char *name) {
    if (strlen(name) >= NAME_LEN) return;
    nutshim_init();
    os_mutex_lock(lock);
    bool have = false;
    for (size_t i = 0; i < ncmds; i++)
        if (strcmp(cmds[i], name) == 0) have = true;
    if (!have && ncmds < MAX_CMDS) strcpy(cmds[ncmds++], name);
    else if (!have) os_log(OS_LOG_ERROR, "dstate", "command table full, dropping %s", name);
    os_mutex_unlock(lock);
}

void dstate_foreach(dstate_var_fn fn, void *ctx) {
    nutshim_init();
    os_mutex_lock(lock);
    for (size_t i = 0; i < nvars; i++) fn(vars[i].name, vars[i].value, vars[i].flags, vars[i].aux, ctx);
    os_mutex_unlock(lock);
}

void dstate_foreach_cmd(dstate_cmd_fn fn, void *ctx) {
    nutshim_init();
    os_mutex_lock(lock);
    for (size_t i = 0; i < ncmds; i++) fn(cmds[i], ctx);
    os_mutex_unlock(lock);
}

void dstate_dataok(void) { dataok = true; }
void dstate_datastale(void) { dataok = false; }
bool dstate_is_dataok(void) { return dataok; }
bool dstate_is_servable(void) {
    char st[2] = "";
    return dataok && (!dstate_copyinfo("ups.status", st, sizeof st) || st[0]);
}

static void append(char *buf, const char *s) {
    size_t l = strlen(buf);
    if (l) { if (l + 1 < VALUE_LEN) { buf[l++] = ' '; buf[l] = 0; } }
    snprintf(buf + l, VALUE_LEN - l, "%s", s);
}

void status_init(void) { status_buf[0] = 0; }
void status_set(const char *s) { append(status_buf, s); }
/* As NUT does, an active alarm adds ALARM to ups.status; but not to an empty one, which marks data not yet servable. */
void status_commit(void) { dstate_setinfo("ups.status", "%s%s", status_buf[0] && alarm_buf[0] ? "ALARM " : "", status_buf); }
void alarm_init(void) { alarm_buf[0] = 0; }
void alarm_set(const char *s) { append(alarm_buf, s); }
void alarm_commit(void) {
    if (alarm_buf[0]) dstate_setinfo("ups.alarm", "%s", alarm_buf);
    else dstate_delinfo("ups.alarm");
}
