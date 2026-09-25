/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc: Console.device
*/

/*****************************************************************************/

#define SDEBUG 0
#define DEBUG 0
#include <aros/debug.h>

#include <proto/exec.h>
#include <proto/console.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <graphics/text.h>

#include <exec/resident.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <exec/initializers.h>
#include <devices/inputevent.h>
#include <devices/conunit.h>
#include <devices/newstyle.h>

/* conunit.h pulls the PUBLIC <devices/console.h>, which is the stock
 * console.device's header and deliberately not modified by this device (see
 * the note in mmakefile.src). CD_ASKTITLE is this device's own additional
 * command, so take it from the private header rather than by patching the
 * public one out from under console.device. */
#include "include/consoleng.h"
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/classusr.h>
#include <graphics/rastport.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <graphics/rastport.h>

#include "console_gcc.h"
#include "consoleif.h"

#include LC_LIBDEFS_FILE

#include <string.h>
/*****************************************************************************/

#define NEWSTYLE_DEVICE 1

/*****************************************************************************/


/*****************************************************************************/

#if NEWSTYLE_DEVICE

static const UWORD SupportedCommands[] = {
    CMD_READ,
    CMD_WRITE,
    NSCMD_DEVICEQUERY,
    0
};

#endif

/*****************************************************************************/

static int GM_UNIQUENAME(Init) (LIBBASETYPEPTR ConsoleDevice)
{
    ConsoleDevice->cb_UtilityBase =
        TaggedOpenLibrary(TAGGEDOPEN_UTILITY);
    if (!ConsoleDevice->cb_UtilityBase)
        return FALSE;

    ConsoleDevice->cb_IntuitionBase =
        TaggedOpenLibrary(TAGGEDOPEN_INTUITION);
    if (!ConsoleDevice->cb_IntuitionBase)
        return FALSE;

    ConsoleDevice->cb_KeymapBase =
        TaggedOpenLibrary(TAGGEDOPEN_KEYMAP);
    if (!ConsoleDevice->cb_KeymapBase)
    {
        CloseLibrary(ConsoleDevice->cb_IntuitionBase);
        return FALSE;
    }

    NEWLIST(&ConsoleDevice->unitList);
    NEWLIST(&ConsoleDevice->sniphooks);
    InitSemaphore(&ConsoleDevice->unitListLock);
    InitSemaphore(&ConsoleDevice->consoleTaskLock);
    InitSemaphore(&ConsoleDevice->copyBufferLock);
    InitSemaphore(&ConsoleDevice->glyphLock);
    NEWLIST(&ConsoleDevice->ownWindows);

    ConsoleDevice->copyBuffer = 0;
    ConsoleDevice->copyBufferSize = 0;

    /* Create the console classes */
    CONSOLECLASSPTR = makeConsoleClass(ConsoleDevice);
    STDCONCLASSPTR = makeStdConClass(ConsoleDevice);
    CHARMAPCLASSPTR = makeCharMapConClass(ConsoleDevice);
    SNIPMAPCLASSPTR = makeSnipMapConClass(ConsoleDevice);

    if (!CONSOLECLASSPTR || !STDCONCLASSPTR || !CHARMAPCLASSPTR
        || !SNIPMAPCLASSPTR)
        Alert(AT_DeadEnd | AN_ConsoleDev | AG_NoMemory);

    /* Create the console.device task. */
    ConsoleDevice->consoleTask =
        NewCreateTask(TASKTAG_NAME, "consoleng.device", TASKTAG_PRI,
        COTASK_PRIORITY, TASKTAG_STACKSIZE, COTASK_STACKSIZE,
        TASKTAG_TASKMSGPORT, &ConsoleDevice->commandPort, TASKTAG_PC,
        consoleTaskEntry, TASKTAG_ARG1, ConsoleDevice, TAG_DONE);

    return ConsoleDevice->consoleTask ? TRUE : FALSE;
}

/*****************************************************************************/

static int GM_UNIQUENAME(Expunge) (LIBBASETYPEPTR ConsoleDevice)
{
    /* Drop the outline engine this device opened for Unicode glyphs. */
    ConsolengGlyphUnbind(ConsoleDevice);
    CloseLibrary(ConsoleDevice->cb_IntuitionBase);
    CloseLibrary(ConsoleDevice->cb_KeymapBase);
    return TRUE;
}

/*****************************************************************************/

/*
 * Open a window for an opener that did not supply one.
 *
 * WHY THIS EXISTS. console.device is the odd one out among AROS devices: it
 * takes a Window through io_Data at OpenDevice() time, and consoleclass.c's
 * New() returns NULL without one. Everything that wants a console therefore
 * has to be a window-owning program.
 *
 * That is exactly what kept this device unreachable. rom/filesys/
 * console_handler already has a generic "device mode" - it takes the device
 * NAME from the mount's FileSysStartupMsg (con_handler.c:473) and states in
 * its own comment that "any device answering those can stand in for
 * console.device" - but it opens that device with io_Data = NULL, because a
 * plain device takes a unit and flags rather than a window. So the seam for
 * mounting a second console already existed and this device could not use it.
 *
 * The alternative was to teach rom/filesys/console_handler to carry a console
 * device name down its window path. That handler is load-bearing for the
 * Shell, every CLI tool and Startup-Sequence, and terminills' standing call on
 * this work is to leave the stock path alone - so the missing capability goes
 * HERE, in the new device, which is also where "consoleng handles everything
 * internally" points. A mount needs no code at all now: give it
 * Device = consoleng.device and Unit = 3 (CONU_SNIPMAP, what CON: uses).
 *
 * A caller that DOES pass a window keeps owning it; only what we opened
 * ourselves is tracked and closed again.
 *
 * THE FONT. A fresh window inherits the screen's font, and on a stock
 * install that is a bitmap font (ttcourier/topaz): no .otag, no outline
 * engine, so every codepoint above Latin-1 renders as the glyph path's
 * reason digit ('2' = CONSOLENG_GLYPH_NOOTAG). The blocks, box lines and
 * arrows a TUI is drawn with are exactly what this device exists for, so
 * the window it opens for itself gets DejaVu Sans Mono - the monospace face
 * the tree ships for this coverage - at the screen font's size. The unit's
 * cell grid is derived from the RastPort font at New(), so this has to
 * happen before the unit object is created. A caller-supplied window keeps
 * whatever font its owner chose; OSC 50 can still switch either later.
 */
#define CONSOLENG_OWNWIN_FONT   "DejaVuSansMono.font"
#define CONSOLENG_OWNWIN_MINYSIZE 12

static struct TextFont *ConsolengOpenOwnWindowFont(LIBBASETYPEPTR ConsoleDevice,
    struct Screen *scr, struct Window *win)
{
    struct Library *GfxBase = TaggedOpenLibrary(TAGGEDOPEN_GRAPHICS);
    struct Library *DiskfontBase = OpenLibrary("diskfont.library", 0);
    struct TextFont *tf = NULL;

    if (GfxBase && DiskfontBase)
    {
        struct TextAttr ta;

        ta.ta_Name  = CONSOLENG_OWNWIN_FONT;
        ta.ta_YSize = scr->Font ? scr->Font->ta_YSize : 0;
        if (ta.ta_YSize < CONSOLENG_OWNWIN_MINYSIZE)
            ta.ta_YSize = CONSOLENG_OWNWIN_MINYSIZE;
        ta.ta_Style = FS_NORMAL;
        ta.ta_Flags = 0;

        tf = OpenDiskFont(&ta);
        if (tf && (tf->tf_Flags & FPF_PROPORTIONAL))
        {
            /* the cell grid needs a fixed pitch; keep the screen font */
            CloseFont(tf);
            tf = NULL;
        }
        if (tf)
            SetFont(win->RPort, tf);
    }
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);

    return tf;
}

static struct Window *ConsolengOpenOwnWindow(LIBBASETYPEPTR ConsoleDevice,
    struct TextFont **fontp)
{
    struct Screen *scr;
    struct Window *win = NULL;

    *fontp = NULL;

    /* Lock the default public screen rather than assuming one: a console can
       legitimately be opened before Wanderer, and LockPubScreen(NULL) failing
       is a real answer ("no screen yet"), not an error to paper over. */
    scr = LockPubScreen(NULL);
    if (!scr)
        return NULL;

    {
        WORD w = scr->Width  - 100;
        WORD h = scr->Height - 100;
        struct TagItem wintags[] =
        {
            { WA_Left,          40                        },
            { WA_Top,           40                        },
            { WA_Width,         0                         },
            { WA_Height,        0                         },
            { WA_Title,         (IPTR)"AROS Console"       },
            { WA_PubScreen,     (IPTR)scr                 },
            { WA_CloseGadget,   TRUE                      },
            { WA_DragBar,       TRUE                      },
            { WA_DepthGadget,   TRUE                      },
            { WA_SizeGadget,    TRUE                      },
            { WA_SmartRefresh,  TRUE                      },
            { WA_Activate,      TRUE                      },
            { WA_SimpleRefresh, FALSE                     },
            { TAG_DONE,         0                         }
        };

        if (w < 200) w = 200;
        if (h < 120) h = 120;
        if (w > 720) w = 720;
        if (h > 480) h = 480;

        wintags[2].ti_Data = (IPTR)w;
        wintags[3].ti_Data = (IPTR)h;

        /* OpenWindowTagList, NOT OpenWindowTags. The amiga.lib VARARGS stubs
           resolve IntuitionBase from a GLOBAL that is NULL in a
           __INTUITION_NOLIBBASE__ module like this one; the A-forms take the
           base from the macro in console_gcc.h. Getting this wrong faults at
           a small negative offset off a NULL base. */
        win = OpenWindowTagList(NULL, wintags);
    }

    if (win)
        *fontp = ConsolengOpenOwnWindowFont(ConsoleDevice, scr, win);

    UnlockPubScreen(NULL, scr);

    return win;
}

static int GM_UNIQUENAME(Open)
    (LIBBASETYPEPTR ConsoleDevice,
    struct IOStdReq *ioreq, ULONG unitnum, ULONG flags)
{
    BOOL success = FALSE;
    struct Window *ownwin = NULL;
    struct TextFont *ownfont = NULL;

    /* Keep compiler happy */
    flags = 0;

    EnterFunc(bug("OpenConsole()\n"));

    if (((LONG) unitnum) == CONU_LIBRARY)
        /* unitnum is ULONG while CONU_LIBRARY is -1 :-(   */
    {
        D(bug("Opening CONU_LIBRARY unit\n"));
        ioreq->io_Device = (struct Device *)ConsoleDevice;

        /* Set io_Unit so that CloseDevice knows this is a CONU_LIBRARY unit.
         * WB1.3 Setmap sets io_Unit to CONU_LIBRARY (-1) before closing
         * console.device */
        ioreq->io_Unit = (struct Unit *)CONU_LIBRARY;
        success = TRUE;
    }
    else
    {
        Class *classptr = NULL; /* Keep compiler happy */
#ifndef __mc68000
        /* AOS programs don't always initialize mn_Length. */
        if (ioreq->io_Message.mn_Length < sizeof(struct IOStdReq))
        {
            D(bug("console.device/open: IORequest structure passed to"
                " OpenDevice is too small!\n"));
            goto open_fail;
        }
#endif
        struct TagItem conunit_tags[] = {
            {A_Console_Window, 0},
            {TAG_DONE, 0}
        };

        /* Init tags */

        /* No window supplied: open one ourselves. This is the plain-device
           open the con-handler's device mode performs (io_Data = NULL), and
           without it every such open fails in consoleclass.c's New(). */
        if (ioreq->io_Data == NULL)
        {
            ownwin = ConsolengOpenOwnWindow(ConsoleDevice, &ownfont);
            if (!ownwin)
            {
                D(bug("consoleng/open: no window supplied and none could be"
                    " opened\n"));
                goto open_fail;
            }
        }

        conunit_tags[0].ti_Data =
            (IPTR)(ownwin ? ownwin : (struct Window *)ioreq->io_Data);

        /* Select class of which to create console object */
        switch (unitnum)
        {
        case CONU_STANDARD:
            D(bug("Opening CONU_STANDARD console\n"));
            classptr = STDCONCLASSPTR;
            break;

        case CONU_CHARMAP:
            classptr = CHARMAPCLASSPTR;
            break;

        case CONU_SNIPMAP:
            classptr = SNIPMAPCLASSPTR;
            break;

        default:
            goto open_fail;
        }

        /* Create console object */
        ioreq->io_Unit =
            (struct Unit *)NewObjectA(classptr, NULL, conunit_tags);
        if (ioreq->io_Unit)
        {
            struct opAddTail add_msg;
            success = TRUE;

            /* Add the newly created unit to console's list of units */
            ObtainSemaphore(&ConsoleDevice->unitListLock);

            add_msg.MethodID = OM_ADDTAIL;
            add_msg.opat_List = (struct List *)&ConsoleDevice->unitList;
            DoMethodA((Object *) ioreq->io_Unit, (Msg) &add_msg);

            /* Remember a window we opened ourselves, so Close() can close it
               again. Tracked per UNIT, not per device, because several units
               can be open at once and only some of them self-opened. If the
               bookkeeping cannot be allocated, close the window now rather
               than leak it - the unit still works with a caller's window and
               this path only ever applies to one we would have owned. */
            if (ownwin)
            {
                struct ConsolengOwnWin *ow =
                    AllocMem(sizeof(struct ConsolengOwnWin),
                             MEMF_CLEAR | MEMF_PUBLIC);

                if (ow)
                {
                    ow->unit = ioreq->io_Unit;
                    ow->win  = ownwin;
                    ow->font = ownfont;
                    AddTail((struct List *)&ConsoleDevice->ownWindows,
                            (struct Node *)&ow->node);
                    ownwin = NULL;      /* handed over to the list */
                    ownfont = NULL;
                }
            }

            ReleaseSemaphore(&ConsoleDevice->unitListLock);
        } /* if (console unit created) */
    } /* if (not CONU_LIBRARY) */

    if (!success)
        goto open_fail;

    return TRUE;

  open_fail:

    /* Still set means the unit was never created, or its bookkeeping failed,
       so nothing else can be holding this window. */
    if (ownwin)
        CloseWindow(ownwin);
    if (ownfont)
    {
        struct Library *GfxBase = TaggedOpenLibrary(TAGGEDOPEN_GRAPHICS);
        if (GfxBase)
            CloseFont(ownfont);
    }

    ioreq->io_Error = IOERR_OPENFAIL;

    return FALSE;
}

/*****************************************************************************/

static int GM_UNIQUENAME(Close)
    (LIBBASETYPEPTR ConsoleDevice, struct IORequest *ioreq)
{
    if (ioreq->io_Unit && ioreq->io_Unit != (struct Unit *)CONU_LIBRARY)
    {
        ULONG mid = OM_REMOVE;
        struct ConsolengOwnWin *ow;
        struct Window *ownwin = NULL;
        struct TextFont *ownfont = NULL;

        /* Remove the consoe from the console list */
        ObtainSemaphore(&ConsoleDevice->unitListLock);
        DoMethodA((Object *) ioreq->io_Unit, (Msg) &mid);

        /* Unlink any window this device opened for THIS unit. A window the
           caller supplied is not on this list and is left alone. */
        ForeachNode(&ConsoleDevice->ownWindows, ow)
        {
            if (ow->unit == ioreq->io_Unit)
            {
                Remove((struct Node *)&ow->node);
                ownwin = ow->win;
                ownfont = ow->font;
                FreeMem(ow, sizeof(struct ConsolengOwnWin));
                break;
            }
        }

        ReleaseSemaphore(&ConsoleDevice->unitListLock);

        DisposeObject((Object *) ioreq->io_Unit);

        /* Only after the unit is gone: it renders into this window, so
           closing the window first would leave it drawing into freed
           layers. */
        if (ownwin)
            CloseWindow(ownwin);
        /* ...and the font only once nothing renders with it any more. */
        if (ownfont)
        {
            struct Library *GfxBase = TaggedOpenLibrary(TAGGEDOPEN_GRAPHICS);
            if (GfxBase)
                CloseFont(ownfont);
        }
    }

    return TRUE;
}

/*****************************************************************************/

ADD2INITLIB(GM_UNIQUENAME(Init), 0)
ADD2EXPUNGELIB(GM_UNIQUENAME(Expunge), 0)
ADD2OPENDEV(GM_UNIQUENAME(Open), 0) ADD2CLOSEDEV(GM_UNIQUENAME(Close), 0)
/*****************************************************************************/
    AROS_LH1(void, beginio,
    AROS_LHA(struct IOStdReq *, ioreq, A1),
    struct ConsoleBase *, ConsoleDevice, 5, Consoleng)
{
    AROS_LIBFUNC_INIT

    LONG error = 0;
    BOOL done_quick = TRUE;

    /* WaitIO will look into this */
    ioreq->io_Message.mn_Node.ln_Type = NT_MESSAGE;

    EnterFunc(bug("BeginIO(ioreq=%p)\n", ioreq));

    switch (ioreq->io_Command)
    {
#if NEWSTYLE_DEVICE
    case NSCMD_DEVICEQUERY:
        if (ioreq->io_Length < ((LONG) OFFSET(NSDeviceQueryResult,
                    SupportedCommands)) + sizeof(UWORD *))
        {
            ioreq->io_Error = IOERR_BADLENGTH;
        }
        else
        {
            struct NSDeviceQueryResult *d;

            d = (struct NSDeviceQueryResult *)ioreq->io_Data;

            d->DevQueryFormat = 0;
            d->SizeAvailable = sizeof(struct NSDeviceQueryResult);
            d->DeviceType = NSDEVTYPE_CONSOLE;
            d->DeviceSubType = 0;
            d->SupportedCommands = (UWORD *) SupportedCommands;

            ioreq->io_Actual = sizeof(struct NSDeviceQueryResult);
        }
        break;
#endif

    case CMD_WRITE:
        {
            ULONG towrite;
            D(bug("CMD_WRITE %p,%d\n", ioreq->io_Data, ioreq->io_Length));
#if DEBUG
            {
                char *str;
                int i;
                str = ioreq->io_Data;
                for (i = 0; i < ioreq->io_Length; i++)
                {
                    kprintf("%c", *str++);
                }
                kprintf("\n");
            }
#endif
            if (ioreq->io_Length == -1)
            {
                towrite = strlen((STRPTR) ioreq->io_Data);
            }
            else
            {
                towrite = ioreq->io_Length;
            }

            ioreq->io_Actual =
                writeToConsole((struct ConUnit *)ioreq->io_Unit,
                ioreq->io_Data, towrite, ConsoleDevice);

            break;
        }

    case CMD_READ:
        D(bug("CMD_READ %p,%d\n", ioreq->io_Data, ioreq->io_Length));
#if DEBUG
        {
            char *str;
            int i;
            str = ioreq->io_Data;
            for (i = 0; i < ioreq->io_Length; i++)
            {
                kprintf("%c", *str++);
            }
            kprintf("\n");
        }
#endif
        done_quick = FALSE;

        break;

    case CD_ASKKEYMAP:
        /* FIXME: Returns always default keymap */
        if (ioreq->io_Length < sizeof(struct KeyMap))
            error = IOERR_BADLENGTH;
        else
            CopyMem(AskKeyMapDefault(), ioreq->io_Data,
                sizeof(struct KeyMap));
        break;
    case CD_ASKTITLE:
        if (ioreq->io_Length < 1)
            error = IOERR_BADLENGTH;
        else
        {
            struct intConUnit *icu = (struct intConUnit *) ioreq->io_Unit;
            LONG n = 0;

            while (icu->unitTitle[n] && n < ioreq->io_Length - 1
                && n < CON_TITLE_SIZE - 1)
            {
                ((UBYTE *) ioreq->io_Data)[n] = icu->unitTitle[n];
                n++;
            }
            ((UBYTE *) ioreq->io_Data)[n] = '\0';
            ioreq->io_Actual = n;
        }
        break;
    case CD_SETKEYMAP:
        D(bug("CD_SETKEYMAP\n"));
        error = IOERR_NOCMD;
        break;
    case CD_ASKDEFAULTKEYMAP:
        if (ioreq->io_Length < sizeof(struct KeyMap))
            error = IOERR_BADLENGTH;
        else
            CopyMem(AskKeyMapDefault(), ioreq->io_Data,
                sizeof(struct KeyMap));
        break;
    case CD_SETDEFAULTKEYMAP:
        D(bug("CD_SETDEFAULTKEYMAP\n"));
        error = IOERR_NOCMD;
        break;

    default:
        D(bug("IOERR_NOCMD %d\n", ioreq->io_Command));
        error = IOERR_NOCMD;
        break;
    } /* switch (ioreq->io_Command) */

    if (!done_quick)
    {
        /* Mark IO request to be done non-quick */
        ioreq->io_Flags &= ~IOF_QUICK;
        /* Send to input device task */
        PutMsg(ConsoleDevice->commandPort, &ioreq->io_Message);
    }
    else
    {

        /* If the quick bit is not set but the IO request was done quick,
         * reply the message to tell we're through
         */
        ioreq->io_Error = error;
        if (!(ioreq->io_Flags & IOF_QUICK))
            ReplyMsg(&ioreq->io_Message);
    }

    ReturnVoid("BeginIO");

    AROS_LIBFUNC_EXIT
}

/*****************************************************************************/

AROS_LH1(LONG, abortio,
    AROS_LHA(struct IORequest *, ioreq, A1),
    struct ConsoleBase *, ConsoleDevice, 6, Consoleng)
{
    AROS_LIBFUNC_INIT

    LONG ret = -1;

    ObtainSemaphore(&ConsoleDevice->consoleTaskLock);

    /* The ioreq can either be in the ConsoleDevice->commandPort MsgPort,
       or be in the ConsoleDevice->readRequests List, or be already done.

       In the first two cases ln_Type will be NT_MESSAGE (= it can be
       aborted), in the last case ln_Type will be NT_REPLYMSG (cannot
       abort, because already done)

       The consoleTaskLock Semaphore hopefully makes sure that there are no
       other/"in-between" cases.

     */

    if (ioreq->io_Message.mn_Node.ln_Type != NT_REPLYMSG)
    {
        ioreq->io_Error = IOERR_ABORTED;
        Remove(&ioreq->io_Message.mn_Node);
        ReplyMsg(&ioreq->io_Message);

        ret = 0;
    }

    ReleaseSemaphore(&ConsoleDevice->consoleTaskLock);

    return ret;

    AROS_LIBFUNC_EXIT
}

/*****************************************************************************/
