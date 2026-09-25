#ifndef DEVICES_CONSOLENG_H
#define DEVICES_CONSOLENG_H

/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc: consoleng.device's own additions to the console protocol.
*/

/* This is deliberately NOT a copy of <devices/console.h>. The stock
 * console.device owns that header and consoleng.device does not modify it, so
 * this file holds only what this device adds on top. It also must not reuse
 * the DEVICES_CONSOLE_H guard: <devices/conunit.h> already pulls the public
 * header in, so a duplicate guard silently guards this file out and the
 * additions vanish - which is exactly how the first build of this device
 * failed, with CD_ASKTITLE "undeclared" despite being right here.
 */

#include <devices/console.h>

/* Copy the unit title (as set by OSC 0/2) into io_Data. Ink-style TUIs set and
 * read the terminal title; the stock console has no equivalent command. */
#ifndef CD_ASKTITLE
#define CD_ASKTITLE             (CMD_NONSTD + 4)
#endif

#endif /* DEVICES_CONSOLENG_H */
