/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Driver option table and the globals a NUT driver expects from main.c. */
#include <string.h>

#include "nutshim.h"

const char *upsname = "ups", *device_name = "ups";
char *device_path;
TYPE_FD upsfd = ERROR_FD, extrafd = ERROR_FD;
int exit_flag;
time_t poll_interval = 2;
struct ups_handler upsh;

static const char *const *kv_table;
static size_t kv_n;

void nutshim_setvar_table(const char *const *kv, size_t n) { kv_table = kv; kv_n = n; }
void set_exit_flag(int sig) { exit_flag = sig; }
void addvar(int vartype, const char *name, const char *desc) { (void) vartype; (void) name; (void) desc; }

char *getval(const char *var) {
    for (size_t i = 0; i + 1 < kv_n; i += 2)
        if (strcmp(kv_table[i], var) == 0) return (char *) kv_table[i + 1];
    return NULL;
}
int testvar(const char *var) { return getval(var) != NULL; }
