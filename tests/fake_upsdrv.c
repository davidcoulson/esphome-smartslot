/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Stand-in for apcmicrolink.c in the host tests. */
#include <pthread.h>

#include "nutshim.h"

volatile bool fake_stale;
pthread_t fake_instcmd_thread;
int fake_cleanup_calls;
char fake_device_path[32];

static int fake_instcmd(const char *cmd, const char *extra) {
    (void) extra;
    fake_instcmd_thread = pthread_self();
    return strcmp(cmd, "beeper.toggle") == 0 ? STAT_INSTCMD_HANDLED : STAT_INSTCMD_UNKNOWN;
}
static int fake_setvar(const char *var, const char *val) { (void) var; (void) val; return STAT_SET_HANDLED; }

void upsdrv_makevartable(void) {}
void upsdrv_initups(void) { snprintf(fake_device_path, sizeof fake_device_path, "%s", device_path); }
void upsdrv_initinfo(void) {
    dstate_setinfo("ups.status", "OL");
    dstate_addcmd("beeper.toggle");
    upsh.instcmd = fake_instcmd;
    upsh.setvar = fake_setvar;
}
void upsdrv_updateinfo(void) {
    if (fake_stale) dstate_datastale(); else dstate_dataok();
}
void upsdrv_cleanup(void) { fake_cleanup_calls++; }
