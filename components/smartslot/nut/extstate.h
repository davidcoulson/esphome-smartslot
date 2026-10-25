/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SmartSlot shim for the NUT header of the same name: only what the vendored apcmicrolink sources use. */
#ifndef SMARTSLOT_NUT_EXTSTATE_H
#define SMARTSLOT_NUT_EXTSTATE_H
#define ST_FLAG_NONE      0x0000
#define ST_FLAG_RW        0x0001
#define ST_FLAG_STRING    0x0002 /* not STRING implies NUMBER */
#define ST_FLAG_NUMBER    0x0004
#define ST_FLAG_IMMUTABLE 0x0008
#endif
