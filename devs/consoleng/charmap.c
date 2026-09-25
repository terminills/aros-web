/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.

    Desc: Code for CONU_CHARMAP console units.
*/

#include <proto/exec.h>
#include <proto/utility.h>

#include <string.h>

#include "console_gcc.h"
#include "charmap.h"

struct charmap_line *charmap_dispose_line(struct charmap_line *line)
{
    struct charmap_line *next = NULL;
    if (line)
    {
        next = line->next;
        if (line->capacity)
        {
            if (line->text)
                FreeMem(line->text, line->capacity * sizeof(ULONG));
            if (line->fgpen)
                FreeMem(line->fgpen, line->capacity);
            if (line->bgpen)
                FreeMem(line->bgpen, line->capacity);
            if (line->flags)    /* was leaked before the UCS-4 rework */
                FreeMem(line->flags, line->capacity);
        }
        FreeMem(line, sizeof(struct charmap_line));
    }
    return next;
}

VOID charmap_dispose_lines(struct charmap_line *line)
{
    while ((line = charmap_dispose_line(line)));
}

struct charmap_line *charmap_newline(struct charmap_line *next,
    struct charmap_line *prev)
{
    struct charmap_line *newline =
        (struct charmap_line *)AllocMem(sizeof(struct charmap_line),
        MEMF_ANY);
    newline->next = next;
    newline->prev = prev;
    if (next)
        next->prev = newline;
    if (prev)
        prev->next = newline;
    newline->text = 0;
    newline->fgpen = 0;
    newline->bgpen = 0;
    newline->flags = 0;
    newline->size = 0;
    newline->capacity = 0;
    return newline;
}


VOID charmap_resize(struct ConsoleBase *ConsoleDevice, struct charmap_line *line, ULONG newsize)
{
    ULONG *oldtext = line->text;
    BYTE *oldfgpen = line->fgpen;
    BYTE *oldbgpen = line->bgpen;
    BYTE *oldflags = line->flags;
    ULONG oldsize = line->size;
    ULONG oldcapacity = line->capacity ? line->capacity : line->size;

    if (newsize && newsize <= line->capacity)
    {
        if (newsize > oldsize)
        {
            ULONG extra = newsize - oldsize;

            SetMem(line->text + oldsize, 0, extra * sizeof(ULONG));
            SetMem(line->fgpen + oldsize, 0, extra);
            SetMem(line->bgpen + oldsize, 0, extra);
            SetMem(line->flags + oldsize, 0, extra);
        }
        else if (newsize < oldsize)
        {
            ULONG removed = oldsize - newsize;

            SetMem(line->text + newsize, 0, removed * sizeof(ULONG));
            SetMem(line->fgpen + newsize, 0, removed);
            SetMem(line->bgpen + newsize, 0, removed);
            SetMem(line->flags + newsize, 0, removed);
        }
        line->size = newsize;
        return;
    }

    if (newsize)
    {
        ULONG growth = line->capacity / 2;
        ULONG newcapacity;
        ULONG copycells;
        ULONG *newtext;
        BYTE *newfgpen;
        BYTE *newbgpen;
        BYTE *newflags;

        if (growth < 8)
            growth = 8;
        newcapacity = line->capacity + growth;
        if (newcapacity < newsize || newcapacity < line->capacity)
            newcapacity = newsize;

        newtext = (ULONG *)AllocMem(newcapacity * sizeof(ULONG), MEMF_ANY);
        newfgpen = (BYTE *)AllocMem(newcapacity, MEMF_ANY);
        newbgpen = (BYTE *)AllocMem(newcapacity, MEMF_ANY);
        newflags = (BYTE *)AllocMem(newcapacity, MEMF_ANY);

        if (!newtext || !newfgpen || !newbgpen || !newflags)
        {
            if (newtext)
                FreeMem(newtext, newcapacity * sizeof(ULONG));
            if (newfgpen)
                FreeMem(newfgpen, newcapacity);
            if (newbgpen)
                FreeMem(newbgpen, newcapacity);
            if (newflags)
                FreeMem(newflags, newcapacity);
            return;
        }

        SetMem(newtext, 0, newcapacity * sizeof(ULONG));
        SetMem(newfgpen, 0, newcapacity);
        SetMem(newbgpen, 0, newcapacity);
        SetMem(newflags, 0, newcapacity);

        copycells = oldsize < newsize ? oldsize : newsize;
        if (oldtext && copycells)
            memcpy(newtext, oldtext, copycells * sizeof(ULONG));
        if (oldfgpen && copycells)
            memcpy(newfgpen, oldfgpen, copycells);
        if (oldbgpen && copycells)
            memcpy(newbgpen, oldbgpen, copycells);
        if (oldflags && copycells)
            memcpy(newflags, oldflags, copycells);

        line->text = newtext;
        line->fgpen = newfgpen;
        line->bgpen = newbgpen;
        line->flags = newflags;
        line->size = newsize;
        line->capacity = newcapacity;
    }
    else
    {
        line->text = 0;
        line->fgpen = 0;
        line->bgpen = 0;
        line->flags = 0;
        line->size = 0;
        line->capacity = 0;
    }

    if (oldtext)
        FreeMem(oldtext, oldcapacity * sizeof(ULONG));
    if (oldfgpen)
        FreeMem(oldfgpen, oldcapacity);
    if (oldbgpen)
        FreeMem(oldbgpen, oldcapacity);
    if (oldflags)
        FreeMem(oldflags, oldcapacity);
}
