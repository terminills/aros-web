/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#ifndef CONSOLENG_CONCLIP_TEXT_H
#define CONSOLENG_CONCLIP_TEXT_H

/* ConClip's whole-clip text requests (ConClip 42.4), as ConClip.c defines
 * them; ConClip has no public header. */
#include <exec/ports.h>

struct SGWork;

#define CONCLIP_READ_TEXT        0x100
#define CONCLIP_WRITE_TEXT       0x101
#define CONCLIP_WRITE_MAX        (1024UL * 1024UL)
#define CONCLIP_TEXT_VERSION     1
#define CONCLIP_TEXT_UNSUPPORTED 0
#define CONCLIP_TEXT_OK          1
#define CONCLIP_TEXT_ERROR       2

struct ConClipTextMessage
{
    struct Message  msg;
    struct SGWork  *sgw;
    WORD            code;
    ULONG           version;
    ULONG           status;
    UBYTE          *data;
    ULONG           length;
    ULONG           capacity;
};

#endif
