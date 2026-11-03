/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Stand-in for apcmicrolink.c in the host tests. */
#include <pthread.h>

#include "nutshim.h"

volatile bool fake_stale;
pthread_t fake_instcmd_thread;
int fake_instcmd_calls;
int fake_cleanup_calls;
int fake_empty_status_polls; /* polls that publish an empty status after initinfo, like a slow Microlink startup */
char fake_device_path[32];

static int fake_instcmd(const char *cmd, const char *extra) {
    (void) extra;
    if (strcmp(cmd, "slow") == 0) os_sleep_ms(300);
    fake_instcmd_thread = pthread_self();
    fake_instcmd_calls++;
    return strcmp(cmd, "beeper.toggle") == 0 ? STAT_INSTCMD_HANDLED : STAT_INSTCMD_UNKNOWN;
}
static int fake_setvar(const char *var, const char *val) { (void) var; (void) val; return STAT_SET_HANDLED; }

void upsdrv_makevartable(void) {}
void upsdrv_initups(void) { snprintf(fake_device_path, sizeof fake_device_path, "%s", device_path); }
void upsdrv_initinfo(void) {
    dstate_setinfo("ups.status", "%s", fake_empty_status_polls ? "" : "OL");
    dstate_addcmd("beeper.toggle");
    upsh.instcmd = fake_instcmd;
    upsh.setvar = fake_setvar;
}
void upsdrv_updateinfo(void) {
    if (fake_empty_status_polls && --fake_empty_status_polls == 0) dstate_setinfo("ups.status", "OL");
    if (fake_stale) dstate_datastale(); else dstate_dataok();
}
void upsdrv_cleanup(void) { fake_cleanup_calls++; }
