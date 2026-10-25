/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SmartSlot shim for the NUT header of the same name: only what the vendored apcmicrolink sources use. */
#ifndef SMARTSLOT_NUT_CONFIG_H
#define SMARTSLOT_NUT_CONFIG_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 /* strptime, timegm */
#endif
#define PACKAGE_VERSION "esphome-smartslot"
#define HAVE_STDINT_H 1
#define HAVE_SYS_TIME_H 1
/* WITH_USB is deliberately never defined: no HID fallback. */
#endif
