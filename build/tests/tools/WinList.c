/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * WinList - dump every screen and window Intuition knows about, with
 * position, size, flags and depth order (front to back), so "the console
 * window is not in the screenshot" can be split into never-opened /
 * opened-behind / opened-off-screen without guessing from pixels.
 *
 * The lists are copied out under LockIBase() and printed AFTER the lock is
 * dropped: a Write() that blocks while IBase is held stalls input.device
 * and every window on the machine (first version of this tool did exactly
 * that, and it looked like AROS hanging).
 *
 *   WinList
 */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <stdio.h>
#include <string.h>

#define MAXENT 64

struct ent
{
    char  title[80];
    WORD  w, h, x, y;
    ULONG flags;
    int   kind;     /* 0 screen, 1 window */
    int   depth;
    int   front, ibactive;
};

int main(void)
{
    struct IntuitionBase *ib;
    struct Screen *s;
    static struct ent ents[MAXENT];
    int n = 0, i;
    ULONG lock;

    ib = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);
    if (!ib)
    {
        printf("WinList: intuition.library would not open\n");
        return 20;
    }

    lock = LockIBase(0);
    for (s = ib->FirstScreen; s && n < MAXENT; s = s->NextScreen)
    {
        struct Window *w;
        int depth = 0;
        struct ent *e = &ents[n++];

        strncpy(e->title, s->Title ? (char *)s->Title : "(untitled)", sizeof(e->title) - 1);
        e->w = s->Width; e->h = s->Height; e->x = s->LeftEdge; e->y = s->TopEdge;
        e->flags = s->Flags; e->kind = 0; e->front = (s == ib->FirstScreen);

        /* FirstWindow is the front-most window; NextWindow walks back */
        for (w = s->FirstWindow; w && n < MAXENT; w = w->NextWindow, depth++)
        {
            e = &ents[n++];
            strncpy(e->title, w->Title ? (char *)w->Title : "(untitled)", sizeof(e->title) - 1);
            e->w = w->Width; e->h = w->Height; e->x = w->LeftEdge; e->y = w->TopEdge;
            e->flags = w->Flags; e->kind = 1; e->depth = depth;
            e->ibactive = (w == ib->ActiveWindow);
        }
    }
    UnlockIBase(lock);

    for (i = 0; i < n; i++)
    {
        struct ent *e = &ents[i];
        if (e->kind == 0)
            printf("WinList: screen \"%s\" %dx%d at %d,%d flags=0x%lx%s\n",
                   e->title, e->w, e->h, e->x, e->y, (unsigned long)e->flags,
                   e->front ? " FRONT" : "");
        else
            printf("WinList:   [%d] \"%s\" %dx%d at %d,%d flags=0x%lx%s%s%s\n",
                   e->depth, e->title, e->w, e->h, e->x, e->y,
                   (unsigned long)e->flags,
                   (e->flags & WFLG_WINDOWACTIVE) ? " ACTIVE" : "",
                   (e->flags & WFLG_BACKDROP) ? " BACKDROP" : "",
                   e->ibactive ? " (IB active)" : "");
    }
    if (n == MAXENT)
        printf("WinList: (list truncated at %d entries)\n", MAXENT);

    CloseLibrary((struct Library *)ib);
    return 0;
}
