/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SmartSlot shim for the NUT header of the same name: only what the vendored apcmicrolink sources use. */
#ifndef SMARTSLOT_NUT_MAIN_H
#define SMARTSLOT_NUT_MAIN_H
#include "common.h"
#include "dstate.h"
#include "upshandler.h"
#ifdef __cplusplus
extern "C" {
#endif

#define VAR_FLAG 1
#define VAR_VALUE 2
#define VAR_SENSITIVE 4
#define EF_EXIT_FAILURE (-1)

typedef struct upsdrv_info_s {
	const char *name;
	const char *version;
	const char *authors;
	int status;
	struct upsdrv_info_s *subdrv_info[2];
} upsdrv_info_t;
#define DRV_EXPERIMENTAL 0
#define DRV_BETA 1
#define DRV_STABLE 2

extern upsdrv_info_t upsdrv_info;
extern struct ups_handler upsh;
extern const char *upsname, *device_name;
extern char *device_path;
extern TYPE_FD upsfd, extrafd;
extern int exit_flag;
extern time_t poll_interval;

void set_exit_flag(int sig);
void addvar(int vartype, const char *name, const char *desc);
char *getval(const char *var);
int testvar(const char *var);

/* driver entry points (defined by apcmicrolink.c) */
void upsdrv_tweak_prognames(void);
void upsdrv_initups(void);
void upsdrv_initinfo(void);
void upsdrv_updateinfo(void);
void upsdrv_shutdown(void);
void upsdrv_help(void);
void upsdrv_cleanup(void);
void upsdrv_makevartable(void);

#ifdef __cplusplus
}
#endif
#endif
