/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc: Code for CONU_STANDARD console units.
*/

#define SDEBUG 0
#define DEBUG 0
#include <aros/debug.h>

#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <exec/rawfmt.h>
#include <aros/asmcall.h>

#include <string.h>

#include "console_gcc.h"
#include "consoleif.h"

struct stdcondata
{
    struct DrawInfo *dri;
    WORD rendercursorcount;
    BOOL cursorvisible;
    BOOL windowactive;

    UWORD pens[CONUNIT_PEN_MAX];
    UWORD *penmap[CONUNIT_PEN_MAX];

    /* ObtainBestPen results for the 8 ANSI colors (-1 = not obtained) */
    LONG obtainedpens[8];
    struct ColorMap *cm;

    /* Lazily obtained xterm-256 palette pens (-1 = not yet) */
    LONG xtermpens[256];

    /* Small truecolor pen cache, round-robin eviction */
#define STDCON_TCPENS 64
    struct
    {
        ULONG rgb;              /* 0x01000000 | RRGGBB key, 0 = empty */
        LONG pen;
    } tcpens[STDCON_TCPENS];
    UWORD tcnext;

    /* MERGE (upstream 128f15fc05): single-character scratch raster reused across
       Text() calls via InitTmpRas -- a render-buffer optimisation, orthogonal to
       our UCS-4 text layout, so kept alongside the pen caches above (my conflict
       resolution dropped these two members while its usage auto-merged in). */
    PLANEPTR charScratch;
    UWORD    charScratchHeight;

    /* Libraries */
    struct Library *scd_GfxBase;
};

/* SGR 30-37/40-47: the standard ANSI palette */
static const UBYTE ansi_rgb[8][3] =
{
    {0x00, 0x00, 0x00},         /* black */
    {0xAA, 0x00, 0x00},         /* red */
    {0x00, 0xAA, 0x00},         /* green */
    {0xAA, 0xAA, 0x00},         /* yellow */
    {0x00, 0x00, 0xAA},         /* blue */
    {0xAA, 0x00, 0xAA},         /* magenta */
    {0x00, 0xAA, 0xAA},         /* cyan */
    {0xAA, 0xAA, 0xAA},         /* white */
};

/* SGR 90-97/100-107 and xterm entries 8-15: bright ANSI */
static const UBYTE ansi_bright_rgb[8][3] =
{
    {0x55, 0x55, 0x55},
    {0xFF, 0x55, 0x55},
    {0x55, 0xFF, 0x55},
    {0xFF, 0xFF, 0x55},
    {0x55, 0x55, 0xFF},
    {0xFF, 0x55, 0xFF},
    {0x55, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF},
};

/* xterm-256 palette entry -> RGB */
static void xterm_to_rgb(UWORD n, UBYTE *r, UBYTE *g, UBYTE *b)
{
    static const UBYTE lvl[6] = { 0, 95, 135, 175, 215, 255 };

    if (n < 8)
    {
        *r = ansi_rgb[n][0]; *g = ansi_rgb[n][1]; *b = ansi_rgb[n][2];
    }
    else if (n < 16)
    {
        *r = ansi_bright_rgb[n - 8][0];
        *g = ansi_bright_rgb[n - 8][1];
        *b = ansi_bright_rgb[n - 8][2];
    }
    else if (n < 232)
    {
        n -= 16;
        *r = lvl[n / 36]; *g = lvl[(n / 6) % 6]; *b = lvl[n % 6];
    }
    else
    {
        UBYTE v = 8 + (n - 232) * 10;
        *r = v; *g = v; *b = v;
    }
}

/* 0x01010101 * component: replicate an 8-bit value to 32 bits */
static LONG stdcon_bestpen(struct stdcondata *data, UBYTE r, UBYTE g,
    UBYTE b)
{
    struct Library *GfxBase = data->scd_GfxBase;

    return ObtainBestPenA(data->cm,
        (ULONG) r * 0x01010101UL,
        (ULONG) g * 0x01010101UL,
        (ULONG) b * 0x01010101UL, NULL);
}


#undef ConsoleDevice
#define ConsoleDevice ((struct ConsoleBase *)cl->cl_UserData)

/***********  StdCon::New()  **********************/

static Object *stdcon_new(Class *cl, Object *o, struct opSet *msg)
{
    EnterFunc(bug("StdCon::New()\n"));
    o = (Object *) DoSuperMethodA(cl, o, (Msg) msg);
    if (o)
    {
        struct stdcondata *data = INST_DATA(cl, o);
        STACKULONG dispmid = OM_DISPOSE;
        int i;

        /* Clear for checking inside dispose() whether stuff was allocated.
           Basically this is bug-prevention.
         */
        SetMem(data, 0, sizeof(struct stdcondata));
        for (i = 0; i < CONUNIT_PEN_MAX; i ++)
        {
            data->pens[i] = i;
            data->penmap[i] = &data->pens[i];
        }

        data->scd_GfxBase = TaggedOpenLibrary(TAGGEDOPEN_GRAPHICS);
        if (data->scd_GfxBase)
        {
            /* MERGE: AllocRaster below (upstream scratch-raster) needs GfxBase
               in scope; its declaration was in the conflict side I resolved to
               ours. dispose() already has its own. */
            struct Library *GfxBase = data->scd_GfxBase;
            data->dri = GetScreenDrawInfo(CU(o)->cu_Window->WScreen);
            if (data->dri)
            {
                /* SGR 30-37/40-47 stay on the SYSTEM PENS - pens[i] == i, as
                   set above and as the stock console.device does.

                   This device used to ObtainBestPen() true ANSI RGB for these
                   eight, which is more faithful to a terminal and is the wrong
                   default here. AROS's own prompt is written against pen
                   INDICES, not against ANSI colours (workbench/s/Shell-Startup:
                   "*E[42m*E[31m%N.*E[43m*E[32m%s*E[42m*E[31m>*E[40m*E[31m "),
                   so on the stock console those numbers pick muted Workbench
                   theme colours. Resolving them to real red/green instead made
                   an ordinary Shell prompt come out vivid red-on-green - the
                   device looked broken next to the console it sits beside.
                   terminills: it should look like a normal Console by default.

                   Nothing is lost. An application that genuinely wants colour
                   asks for it explicitly with xterm-256 (SGR 38;5;n) or
                   truecolor (SGR 38;2;r;g;b), and those pens are still
                   obtained lazily on first use, below and in
                   Console_GetColorPen(). Only the eight legacy slots that
                   predate any of that are left alone. */
                data->cm = CU(o)->cu_Window->WScreen->ViewPort.ColorMap;
                for (i = 0; i < 256; i++)
                    data->xtermpens[i] = -1;
                for (i = 0; i < 8; i++)
                    data->obtainedpens[i] = -1;

                data->penmap[CONUNIT_PEN_DEFBG] =
                    &data->dri->dri_Pens[BACKGROUNDPEN];
                data->penmap[CONUNIT_PEN_DEFFG] =
                    &data->dri->dri_Pens[TEXTPEN];

                CU(o)->cu_BgPen = (BYTE)*(data->penmap[CONUNIT_PEN_DEFBG]);
                CU(o)->cu_FgPen = (BYTE)*(data->penmap[CONUNIT_PEN_DEFFG]);

                data->charScratchHeight = RASTPORT(o)->Font->tf_YSize;
                data->charScratch = AllocRaster(8, data->charScratchHeight);

                data->cursorvisible = TRUE;
                data->windowactive =
                    (CU(o)->cu_Window->Flags & WFLG_WINDOWACTIVE) != 0;
                Console_RenderCursor(o);

                ReturnPtr("StdCon::New", Object *, o);
            }
        }
        CoerceMethodA(cl, o, (Msg) &dispmid);
    }
    ReturnPtr("StdCon::New", Object *, NULL);

}

/***********  StdCon::Dispose()  **************************/

static VOID stdcon_dispose(Class *cl, Object *o, Msg msg)
{
    struct stdcondata *data = INST_DATA(cl, o);
    struct Library *GfxBase = data->scd_GfxBase;

    if (data->charScratch)
        FreeRaster(data->charScratch, 8, data->charScratchHeight);

    if (data->cm && data->scd_GfxBase)
    {
        struct Library *GfxBase = data->scd_GfxBase;
        int i;

        for (i = 0; i < 8; i++)
        {
            if (data->obtainedpens[i] != -1)
                ReleasePen(data->cm, data->obtainedpens[i]);
        }
        for (i = 0; i < 256; i++)
        {
            if (data->xtermpens[i] != -1)
                ReleasePen(data->cm, data->xtermpens[i]);
        }
        for (i = 0; i < STDCON_TCPENS; i++)
        {
            if (data->tcpens[i].rgb)
                ReleasePen(data->cm, data->tcpens[i].pen);
        }
    }

    if (data->scd_GfxBase)
        CloseLibrary(data->scd_GfxBase);

    if (data->dri)
        FreeScreenDrawInfo(CU(o)->cu_Window->WScreen, data->dri);

    /* Let superclass free its allocations */
    DoSuperMethodA(cl, o, msg);

    return;
}

VOID setabpen(struct Library *GfxBase, struct RastPort *rp, UBYTE tflags,
    UBYTE FgPen, UBYTE BgPen)
{
    UBYTE fg = FgPen, bg = BgPen;
    UBYTE style = JAM2;

    if (tflags & CON_TXTFLAGS_CONCEALED)
        fg = bg;
    else if (tflags & CON_TXTFLAGS_REVERSED)
        style |= INVERSVID;
    SetABPenDrMd(rp, fg, bg, style);
}

static void setstyle(struct Library *GfxBase, struct RastPort *rp,
    Object *o)
{
    UBYTE tflags = CU(o)->cu_TxFlags;
    setabpen(GfxBase, rp, tflags, CU(o)->cu_FgPen, CU(o)->cu_BgPen);
    SetSoftStyle(rp, tflags, CON_TXTFLAGS_MASK);
}

static void stdcon_text(struct stdcondata *data, struct RastPort *rp,
    CONST_STRPTR text, ULONG len)
{
    struct TmpRas *savedTmpRas = rp->TmpRas;
    struct TmpRas charTmpRas;
    struct Library *GfxBase = data->scd_GfxBase;
    ULONG scratchSize = RASSIZE(8, data->charScratchHeight);

    if (len == 1 && data->charScratch &&
        (!savedTmpRas || savedTmpRas->Size < scratchSize))
        rp->TmpRas = InitTmpRas(&charTmpRas, data->charScratch, scratchSize);

    Text(rp, text, len);
    rp->TmpRas = savedTmpRas;
}

/* VT/xterm autowrap is delayed: writing the rightmost cell leaves the
   cursor there and marks wrap pending.  Only the next printable character
   moves to column zero on the next line. */
static void stdcon_prepare_print(Object *o)
{
    if (ICU(o)->conFlags & CF_WRAP_PENDING)
    {
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        XCP = XCCP = CHAR_XMIN(o);
        Console_Down(o, 1);
    }
}

static void stdcon_advance_print(Object *o, ULONG cells, BOOL more)
{
    while (cells)
    {
        ULONG remaining = CHAR_XMAX(o) + 1 - XCCP;
        ULONG step = MIN(cells, remaining);

        if (step < remaining)
        {
            XCP = XCCP += step;
            cells = 0;
        }
        else
        {
            XCP = XCCP = CHAR_XMAX(o);
            cells -= step;
            ICU(o)->conFlags |= CF_WRAP_PENDING;

            if (cells || more)
                stdcon_prepare_print(o);
        }
    }
}

/* JAM2 text replaces every pixel in the first character cell, including a
 * rendered full-cell cursor.  Update cursor nesting without first drawing
 * the redundant COMPLEMENT erase. */
static void stdcon_overwritecursor(struct stdcondata *data)
{
    data->rendercursorcount--;
}

/*********  StdCon::DoCommand()  ****************************/

static VOID stdcon_docommand(Class *cl, Object *o,
    struct P_Console_DoCommand *msg)
{
    struct Window *w = CU(o)->cu_Window;
    struct RastPort *rp = w->RPort;
    IPTR *params = msg->Params;
    struct stdcondata *data = INST_DATA(cl, o);
    struct Library *GfxBase = data->scd_GfxBase;

    EnterFunc(bug("StdCon::DoCommand(o=%p, cmd=%d, params=%p)\n",
            o, msg->Command, params));

    /* Obscured (background tab): keep all cursor/mode/state logic but
       never touch the shared window's RastPort. */
    if (CON_IS_OBSCURED(o))
    {
        switch (msg->Command)
        {
        case C_ASCII:
            stdcon_prepare_print(o);
            stdcon_advance_print(o, 1, FALSE);
            break;

        case C_ASCII_STRING:
            stdcon_prepare_print(o);
            stdcon_advance_print(o, CON_IS_UTF8(o) ?
                con_utf8_cellcount((const UBYTE *) params[0], params[1]) :
                (ULONG) params[1], FALSE);
            break;

        case C_SET_FONT:
            /* no live switch while hidden */
            break;

        default:
            /* pure state logic lives in the base class */
            DoSuperMethodA(cl, o, (Msg) msg);
            break;
        }
        ReturnVoid("StdCon::DoCommand(obscured)");
    }

    switch (msg->Command)
    {
    case C_NIL:
        /* do nothing */
        break;

    case C_ASCII:
        stdcon_prepare_print(o);

        D(bug("Writing char %c at (%d, %d)\n",
                params[0], CP_X(o), CP_Y(o) + rp->Font->tf_Baseline));

        stdcon_overwritecursor(data);

        setstyle(GfxBase, rp, o);
        Move(rp, CP_X(o), CP_Y(o) + rp->Font->tf_Baseline);
        {
            UBYTE c = params[0];
            stdcon_text(data, rp, &c, 1);
        }

        stdcon_advance_print(o, 1, FALSE);

        /* Rerender the cursor */
        Console_RenderCursor(o);

        break;

    case C_ASCII_STRING:
        D(bug("Writing string %.*s at (%d, %d)\n",
                (int)params[1], (char *)params[0], CP_X(o),
                CP_Y(o) + rp->Font->tf_Baseline));

        stdcon_overwritecursor(data);

        setstyle(GfxBase, rp, o);

        if (CON_IS_UTF8(o))
        {
            /* The span is UTF-8: ASCII runs go through Text() unchanged,
               multi-byte runs render via TextUTF8() over a background
               fill. Cursor advance counts CELLS (codepoints), not bytes. */
            const UBYTE *p = (const UBYTE *) params[0];
            const UBYTE *end = p + params[1];

            while (p < end)
            {
                stdcon_prepare_print(o);
                ULONG remaining_space = CHAR_XMAX(o) + 1 - XCCP;

                if (remaining_space == 0)
                    remaining_space = 1;        /* paranoia: never stall */

                if (*p < 0x80)
                {
                    ULONG n = 0;

                    while (p + n < end && n < remaining_space && p[n] < 0x80)
                        n++;

                    Move(rp, CP_X(o), CP_Y(o) + rp->Font->tf_Baseline);
                    Text(rp, p, n);
                    p += n;
                    stdcon_advance_print(o, n, p < end);
                }
                else
                {
                    /* one codepoint at a time so wide glyphs (emoji,
                       CJK) get their two cells and zero-width marks
                       consume none */
                    const UBYTE *q = p;
                    LONG qleft = end - p;
                    ULONG cp = con_utf8_decode(&q, &qleft);
                    WORD cw = con_cp_width(cp);
                    UBYTE tflags = CU(o)->cu_TxFlags;
                    UBYTE ufg = CU(o)->cu_FgPen, ubg = CU(o)->cu_BgPen, swp;

                    if (cw == 0)
                    {
                        /* combining marks/selectors: not yet composed */
                        p = q;
                        continue;
                    }
                    if ((ULONG) cw > remaining_space)
                        cw = remaining_space;

                    if (tflags & CON_TXTFLAGS_REVERSED)
                    {
                        swp = ufg;
                        ufg = ubg;
                        ubg = swp;
                    }
                    if (tflags & CON_TXTFLAGS_CONCEALED)
                        ufg = ubg;

                    SetAPen(rp, ubg);
                    RectFill(rp, CP_X(o), CP_Y(o),
                        CP_X(o) + cw * XRSIZE - 1, CP_Y(o) + YRSIZE - 1);
                    SetAPen(rp, ufg);
                    Move(rp, CP_X(o), CP_Y(o) + rp->Font->tf_Baseline);
                    TextUTF8(rp, p, q - p);
                    setstyle(GfxBase, rp, o);
                    p = q;
                    stdcon_advance_print(o, cw, p < end);
                }
            }
        }
        else
        {
            ULONG len = params[1];
            STRPTR str = (STRPTR) params[0];

            while (len)
            {
                stdcon_prepare_print(o);
                ULONG remaining_space = CHAR_XMAX(o) + 1 - XCCP;
                ULONG line_len =
                    len < remaining_space ? len : remaining_space;

                Move(rp, CP_X(o), CP_Y(o) + rp->Font->tf_Baseline);
                stdcon_text(data, rp, str, line_len);

                len -= line_len;
                str += line_len;
                stdcon_advance_print(o, line_len, len != 0);
            }
        }

        /* Rerender the cursor */
        Console_RenderCursor(o);

        break;

    case C_FORMFEED:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        {
            /* Clear the console */

            UBYTE oldpen = rp->FgPen;
            IPTR newcurpos[2] = { 0, 0 };

            Console_UnRenderCursor(o);

            SetAPen(rp, CU(o)->cu_BgPen);
            RectFill(rp, CU(o)->cu_XROrigin, CU(o)->cu_YROrigin,
                CU(o)->cu_XRExtant, CU(o)->cu_YRExtant);

            SetAPen(rp, oldpen);

            Console_DoCommand(o, C_CURSOR_POS, 2, newcurpos);

            Console_RenderCursor(o);

            break;
        }

    case C_BELL:
        /* !!! maybe trouble with LockLayers() here !!! */
//      DisplayBeep(CU(o)->cu_Window->WScreen);
        break;

    case C_BACKSPACE:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        Console_Left(o, 1);
        Console_RenderCursor(o);
        break;

    case C_CURSOR_BACKWARD:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        Console_Left(o, params[0]);
        Console_RenderCursor(o);
        break;

    case C_CURSOR_FORWARD:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        Console_Right(o, params[0]);
        Console_RenderCursor(o);
        break;

    case C_DELETE_CHAR:        /* FIXME: can it have params!? */
        {
            UBYTE oldpen = rp->FgPen;
            Console_UnRenderCursor(o);
            SetAPen(rp, CU(o)->cu_BgPen);
            ScrollRaster(rp,
                XRSIZE,
                0,
                GFX_X(o, XCP),
                GFX_Y(o, YCP), GFX_XMAX(o), GFX_Y(o, YCP + 1));
            SetAPen(rp, oldpen);
            Console_RenderCursor(o);
        }
        break;
        Console_RenderCursor(o);
        break;

    case C_HTAB:
        {
            WORD x = XCCP, i = 0;

            while ((CU(o)->cu_TabStops[i] != (UWORD) - 1) &&
                (CU(o)->cu_TabStops[i] <= x))
            {
                i++;
            }
            if (CU(o)->cu_TabStops[i] != (UWORD) - 1)
            {
                Console_UnRenderCursor(o);
                Console_Right(o, CU(o)->cu_TabStops[i] - x);
                Console_RenderCursor(o);
            }
            break;
        }

    case C_CURSOR_HTAB:
        {
            WORD i = params[0];

            do
            {
                IPTR dummy;

                Console_DoCommand(o, C_HTAB, 0, &dummy);

            }
            while (--i > 0);
            break;
        }

    case C_CURSOR_BACKTAB:
        {
            WORD count = params[0];

            Console_UnRenderCursor(o);

            do
            {
                WORD x = XCCP, i = 0;

                while ((CU(o)->cu_TabStops[i] != (UWORD) - 1) &&
                    (CU(o)->cu_TabStops[i] < x))
                {
                    i++;
                }

                i--;

                if (i >= 0)
                    if (CU(o)->cu_TabStops[i] != (UWORD) - 1)
                    {
                        Console_Left(o, x - CU(o)->cu_TabStops[i]);
                    }

            }
            while (--count > 0);

            Console_RenderCursor(o);

            break;
        }

    case C_LINEFEED:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        D(bug("Got linefeed command\n"));
        /*Console_ClearCell(o, XCCP, YCCP); */
        Console_UnRenderCursor(o);

        Console_Down(o, 1);

        /* Check for linefeed mode (LF or LF+CR) */

        D(bug("conflags: %d\n", ICU(o)->conFlags));

        /* if (ICU(o)->conFlags & CF_LF_MODE_ON) */
        if (CHECK_MODE(o, M_LNM))
        {
            CU(o)->cu_XCP = CHAR_XMIN(o);
            CU(o)->cu_XCCP = CHAR_XMIN(o);
        }
        Console_RenderCursor(o);
        break;

    case C_CURSOR_PREV_LINE:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        CU(o)->cu_XCP = CHAR_XMIN(o);
        CU(o)->cu_XCCP = CHAR_XMIN(o);
        Console_Up(o, params[0]);
        Console_RenderCursor(o);
        break;

    case C_VTAB:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        Console_Up(o, 1);
        Console_RenderCursor(o);
        break;

    case C_CURSOR_UP:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        Console_Up(o, params[0]);
        Console_RenderCursor(o);
        break;

    case C_CURSOR_NEXT_LINE:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        CU(o)->cu_XCP = CHAR_XMIN(o);
        CU(o)->cu_XCCP = CHAR_XMIN(o);
        Console_Down(o, params[0]);
        Console_RenderCursor(o);
        break;

    case C_CURSOR_DOWN:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        Console_UnRenderCursor(o);
        Console_Down(o, params[0]);
        Console_RenderCursor(o);
        break;

    case C_CARRIAGE_RETURN:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        /* Goto start of line */

        Console_UnRenderCursor(o);
        CU(o)->cu_XCP = CHAR_XMIN(o);
        CU(o)->cu_XCCP = CHAR_XMIN(o);
        Console_RenderCursor(o);
        break;

    case C_INDEX:
        Console_Down(o, 1);
        break;

    case C_NEXT_LINE:
        D(bug("Got NEXT LINE cmd\n"));
        Console_Down(o, 1);
        Console_Left(o, XCP);
        break;

    case C_REVERSE_IDX:
        Console_Up(o, 1);
        break;

    case C_CURSOR_POS:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        {
            WORD y = ((WORD) params[0]) - 1;
            WORD x = ((WORD) params[1]) - 1;

            if (x < CHAR_XMIN(o))
            {
                x = CHAR_XMIN(o);
            }
            else if (x > CHAR_XMAX(o))
            {
                x = CHAR_XMAX(o);
            }

            if (y < CHAR_YMIN(o))
            {
                y = CHAR_YMIN(o);
            }
            else if (y > CHAR_YMAX(o))
            {
                y = CHAR_YMAX(o);
            }

            Console_UnRenderCursor(o);

            XCCP = XCP = x;
            YCCP = YCP = y;

            Console_RenderCursor(o);
            break;
        }

    case C_CURSOR_COLUMN:
        ICU(o)->conFlags &= ~CF_WRAP_PENDING;
        {
            WORD x = ((WORD) params[0]) - 1;

            if (x < CHAR_XMIN(o))
                x = CHAR_XMIN(o);
            else if (x > CHAR_XMAX(o))
                x = CHAR_XMAX(o);

            Console_UnRenderCursor(o);
            XCCP = XCP = x;
            Console_RenderCursor(o);
            break;
        }

    case C_ERASE_IN_LINE:
        {
            UBYTE oldpen = rp->FgPen;
            WORD xmin, xmax;

            Console_UnRenderCursor(o);

            switch (params[0])
            {
            case 1:
                xmin = CU(o)->cu_XROrigin;
                xmax = GFX_X(o, XCP + 1) - 1;
                break;
            case 2:
                xmin = CU(o)->cu_XROrigin;
                xmax = CU(o)->cu_XRExtant;
                break;
            case 0:
            default:
                xmin = GFX_X(o, XCP);
                xmax = CU(o)->cu_XRExtant;
                break;
            }

            SetAPen(rp, CU(o)->cu_BgPen);
            SetDrMd(rp, JAM2);

            RectFill(rp, xmin,
                CU(o)->cu_YROrigin + YCP * YRSIZE, xmax,
                CU(o)->cu_YROrigin + (YCP + 1) * YRSIZE - 1);


            SetAPen(rp, oldpen);

            Console_RenderCursor(o);

        }
        break;

    case C_ERASE_IN_DISPLAY:
        {
            UBYTE oldpen = rp->FgPen;
            WORD ymin;

            Console_UnRenderCursor(o);

            SetAPen(rp, CU(o)->cu_BgPen);
            SetDrMd(rp, JAM2);

            switch (params[0])
            {
            case 1:
                if (YCP > CHAR_YMIN(o))
                    RectFill(rp, CU(o)->cu_XROrigin,
                        CU(o)->cu_YROrigin, CU(o)->cu_XRExtant,
                        GFX_Y(o, YCP) - 1);
                RectFill(rp, CU(o)->cu_XROrigin, GFX_Y(o, YCP),
                    GFX_X(o, XCP + 1) - 1, GFX_Y(o, YCP + 1) - 1);
                break;
            case 2:
            case 3:
                RectFill(rp, CU(o)->cu_XROrigin, CU(o)->cu_YROrigin,
                    CU(o)->cu_XRExtant, CU(o)->cu_YRExtant);
                break;
            case 0:
            default:
                RectFill(rp, GFX_X(o, XCP), GFX_Y(o, YCP),
                    CU(o)->cu_XRExtant, GFX_Y(o, YCP + 1) - 1);
                ymin = GFX_Y(o, YCP + 1);
                if (ymin <= CU(o)->cu_YRExtant)
                    RectFill(rp, CU(o)->cu_XROrigin, ymin,
                        CU(o)->cu_XRExtant, CU(o)->cu_YRExtant);
                break;
            }

            SetAPen(rp, oldpen);

            Console_RenderCursor(o);

            break;
        }

    case C_INSERT_CHAR:
        {
            UBYTE oldpen = rp->FgPen;
            Console_UnRenderCursor(o);
            SetAPen(rp, CU(o)->cu_BgPen);
            ScrollRaster(rp,
                -XRSIZE,
                0,
                GFX_X(o, XCP),
                GFX_Y(o, YCP), GFX_XMAX(o), GFX_Y(o, YCP + 1));
            SetAPen(rp, oldpen);
            Console_RenderCursor(o);
        }
        break;

    case C_INSERT_LINE:
        {
            UBYTE oldpen = rp->FgPen;

            Console_UnRenderCursor(o);
            SetAPen(rp, CU(o)->cu_BgPen);

            ScrollRaster(rp,
                0,
                -YRSIZE * params[0],
                GFX_XMIN(o), GFX_Y(o, YCP), GFX_XMAX(o), GFX_YMAX(o));

            SetAPen(rp, oldpen);

            Console_RenderCursor(o);
            break;
        }

    case C_DELETE_LINE:
        {
            UBYTE oldpen = rp->FgPen;

            Console_UnRenderCursor(o);
            SetAPen(rp, CU(o)->cu_BgPen);

            ScrollRaster(rp,
                0,
                YRSIZE * params[0],
                GFX_XMIN(o), GFX_Y(o, YCP), GFX_XMAX(o), GFX_YMAX(o));

            SetAPen(rp, oldpen);

            Console_RenderCursor(o);
            break;
        }

    case C_SCROLL_UP:
        {
            UBYTE oldpen = rp->FgPen;

            D(bug("C_SCROLL_UP area (%d, %d) to (%d, %d), %d\n",
                    GFX_XMIN(o), GFX_YMIN(o), GFX_XMAX(o), GFX_YMAX(o),
                    YRSIZE * params[0]));

            Console_UnRenderCursor(o);

            SetAPen(rp, CU(o)->cu_BgPen);
/* FIXME: LockLayers problem here ? */
            ScrollRaster(rp, 0, YRSIZE * params[0], GFX_XMIN(o),
                GFX_YMIN(o), GFX_XMAX(o), GFX_YMAX(o));
            SetAPen(rp, oldpen);

            Console_RenderCursor(o);

            break;
        }

    case C_SCROLL_DOWN:
        {
            UBYTE oldpen = rp->FgPen;

            D(bug("C_SCROLL_DOWN area (%d, %d) to (%d, %d), %d\n",
                    GFX_XMIN(o), GFX_YMIN(o), GFX_XMAX(o), GFX_YMAX(o),
                    YRSIZE * params[0]));

            Console_UnRenderCursor(o);

            SetAPen(rp, CU(o)->cu_BgPen);
/* FIXME: LockLayers problem here?     */
            ScrollRaster(rp, 0, -YRSIZE * params[0], GFX_XMIN(o),
                GFX_YMIN(o), GFX_XMAX(o), GFX_YMAX(o));
            SetAPen(rp, oldpen);

            Console_RenderCursor(o);

            break;
        }

    case C_CURSOR_VISIBLE:
        if (!data->cursorvisible)
        {
            data->cursorvisible = TRUE;
            data->rendercursorcount--;
            Console_RenderCursor(o);
        }
        break;

    case C_CURSOR_INVISIBLE:
        if (data->cursorvisible)
        {
            Console_UnRenderCursor(o);
            data->cursorvisible = FALSE;
            data->rendercursorcount++;
        }
        break;

    case C_SET_TOP_OFFSET:
        Console_UnRenderCursor(o);
        CU(o)->cu_YROrigin = params[0];
        CU(o)->cu_YMax =
            (w->Height - (CU(o)->cu_YROrigin +
                w->BorderBottom)) / CU(o)->cu_YRSize - 1;
        Console_RenderCursor(o);
        Console_NewWindowSize(o);
        break;

    case C_SET_PAGE_LENGTH:
        Console_UnRenderCursor(o);
        CU(o)->cu_YMax = params[0];
        // FIXME: Need to set something that prevents NewWindowSize to
        // change YMax
        Console_RenderCursor(o);
        Console_NewWindowSize(o);
        break;

    case C_WINDOW_STATUS_REQUEST:
        {
            UBYTE reply[32];
            NewRawDoFmt("\x9b" "1;1;%d;%d r", RAWFMTFUNC_STRING, reply,
                CU(o)->cu_YMax + 1, CU(o)->cu_XMax + 1);
            con_inject((struct ConsoleBase *)cl->cl_UserData, CU(o), reply,
                -1);
            break;
        }

    case C_DEVICE_STATUS_REPORT:
        {
            UBYTE reply[32];
            NewRawDoFmt("\x9b" "%d;%dR", RAWFMTFUNC_STRING, reply,
                CU(o)->cu_YCP + 1, CU(o)->cu_XCP + 1);
            con_inject((struct ConsoleBase *)cl->cl_UserData, CU(o), reply,
                -1);
            break;
        }

    default:
        DoSuperMethodA(cl, o, (Msg) msg);
        break;
    }

    ReturnVoid("StdCon::DoCommand");
}

/*********  StdCon::RenderCursor()  ****************************/
static VOID stdcon_drawcursor(Object *o, struct stdcondata *data)
{
    static const UWORD inactive_pattern[] = { 0xAAAA, 0x5555 };
    struct RastPort *rp = RASTPORT(o);
    const UWORD *oldpattern = rp->AreaPtrn;
    BYTE oldpatternsize = rp->AreaPtSz;
    struct Library *GfxBase = data->scd_GfxBase;

    SetDrMd(rp, COMPLEMENT);
    if (!data->windowactive)
    {
        rp->AreaPtrn = inactive_pattern;
        rp->AreaPtSz = 1;
    }
    RectFill(rp, CP_X(o), CP_Y(o), CP_X(o) + XRSIZE - 1,
        CP_Y(o) + YRSIZE - 1);
    if (!data->windowactive)
    {
        rp->AreaPtrn = oldpattern;
        rp->AreaPtSz = oldpatternsize;
    }
    SetDrMd(rp, JAM2);
}

static VOID stdcon_rendercursor(Class *cl, Object *o,
    struct P_Console_RenderCursor *msg)
{
    struct stdcondata *data = INST_DATA(cl, o);

    /* SetAPen(rp, data->dri->dri_Pens[FILLPEN]); */

    data->rendercursorcount++;

    if (data->cursorvisible && (data->rendercursorcount == 1)
        && !CON_IS_OBSCURED(o))
    {
        stdcon_drawcursor(o, data);
    }
}

/*********  StdCon::UnRenderCursor()  ****************************/
static VOID stdcon_unrendercursor(Class *cl, Object *o,
    struct P_Console_UnRenderCursor *msg)
{
    struct stdcondata *data = INST_DATA(cl, o);

    data->rendercursorcount--;

    /* SetAPen(rp, data->dri->dri_Pens[FILLPEN]); */

    if (data->cursorvisible && (data->rendercursorcount == 0)
        && !CON_IS_OBSCURED(o))
    {
        stdcon_drawcursor(o, data);
    }
}

static VOID stdcon_setactive(Class *cl, Object *o,
    struct P_Console_HandleGadgets *msg)
{
    struct stdcondata *data = INST_DATA(cl, o);
    BOOL active = msg->Event->ie_Class == IECLASS_ACTIVEWINDOW;

    if (active == data->windowactive)
        return;

    if (data->cursorvisible && data->rendercursorcount == 1)
        stdcon_drawcursor(o, data);
    data->windowactive = active;
    if (data->cursorvisible && data->rendercursorcount == 1)
        stdcon_drawcursor(o, data);
}

/**************************
**  StdCon::ClearCell()  **
**************************/
static VOID stdcon_clearcell(Class *cl, Object *o,
    struct P_Console_ClearCell *msg)
{
    struct RastPort *rp = RASTPORT(o);
    struct stdcondata *data = INST_DATA(cl, o);
    struct Library *GfxBase = data->scd_GfxBase;

    SetAPen(rp, data->dri->dri_Pens[BACKGROUNDPEN]);
    SetDrMd(rp, JAM1);
    RectFill(rp, GFX_X(o, msg->X), GFX_Y(o, msg->Y), GFX_X(o,
            msg->X) + XRSIZE - 1, GFX_Y(o, msg->Y) + YRSIZE - 1);
}

/*******************************
**  StdCon::NewWindowSize()  **
*******************************/
static VOID stdcon_newwindowsize(Class *cl, Object *o,
    struct P_Console_NewWindowSize *msg)
{
    struct RastPort *rp = RASTPORT(o);
    struct stdcondata *data = INST_DATA(cl, o);
    struct Library *GfxBase = data->scd_GfxBase;
    WORD old_xmax = CHAR_XMAX(o);
    WORD old_ymax = CHAR_YMAX(o);
    WORD old_xcp = XCP;
    WORD old_ycp = YCP;

    WORD x1, y1, x2, y2;

    DoSuperMethodA(cl, o, (Msg) msg);

    if (CHAR_XMAX(o) < old_xmax)
    {
        x1 = GFX_XMAX(o) + 1;
        y1 = GFX_YMIN(o);
        x2 = GFX_XMAX(o);
        y2 = GFX_YMAX(o) - 1;

        if ((x2 >= x1) && (y2 >= y1))
        {
            SetAPen(rp, 0);
            SetDrMd(rp, JAM2), RectFill(rp, x1, y1, x2, y2);
        }
    }

    if (CHAR_YMAX(o) < old_ymax)
    {
        x1 = GFX_XMIN(o);
        y1 = GFX_YMAX(o) + 1;
        x2 = WINDOW(o)->Width - WINDOW(o)->BorderRight - 1;
        y2 = WINDOW(o)->Height - WINDOW(o)->BorderBottom - 1;

        if ((x2 >= x1) && (y2 >= y1))
        {
            SetAPen(rp, 0);
            SetDrMd(rp, JAM2), RectFill(rp, x1, y1, x2, y2);
        }
    }

    if ((old_xcp != XCP) || (old_ycp != YCP))
    {
        SetAPen(rp, 0);
        SetDrMd(rp, JAM2);
        RectFill(rp, GFX_XMIN(o), GFX_YMIN(o), GFX_XMAX(o), GFX_YMAX(o));
        data->rendercursorcount--;
        Console_RenderCursor(o);
    }
    return;
}

IPTR stdcon_getpencolor(Class *cl, Object *o, struct P_Console_GetColorPen *msg)
{
    struct stdcondata *data = INST_DATA(cl, o);
    ULONG idx = msg->ColorIdx;

    if (CONPEN_ISRGB(idx) && data->cm)
    {
        /* 24-bit truecolor - small round-robin pen cache */
        UWORD i;
        LONG pen;

        for (i = 0; i < STDCON_TCPENS; i++)
        {
            if (data->tcpens[i].rgb == idx)
            {
                *msg->PenPtr = (UBYTE) data->tcpens[i].pen;
                return TRUE;
            }
        }

        pen = stdcon_bestpen(data, (idx >> 16) & 0xFF, (idx >> 8) & 0xFF,
            idx & 0xFF);
        if (pen == -1)
        {
            *msg->PenPtr = (BYTE)*(data->penmap[CONUNIT_PEN_DEFFG]);
            return TRUE;
        }

        i = data->tcnext;
        data->tcnext = (data->tcnext + 1) % STDCON_TCPENS;
        if (data->tcpens[i].rgb)
        {
            struct Library *GfxBase = data->scd_GfxBase;
            ReleasePen(data->cm, data->tcpens[i].pen);
        }
        data->tcpens[i].rgb = idx;
        data->tcpens[i].pen = pen;
        *msg->PenPtr = (UBYTE) pen;
        return TRUE;
    }

    if (CONPEN_ISXTERM(idx) && data->cm)
    {
        UWORD n = CONPEN_XTERMIDX(idx);

        if (data->xtermpens[n] == -1)
        {
            UBYTE r, g, b;

            xterm_to_rgb(n, &r, &g, &b);
            data->xtermpens[n] = stdcon_bestpen(data, r, g, b);
        }
        if (data->xtermpens[n] != -1)
            *msg->PenPtr = (UBYTE) data->xtermpens[n];
        else
            *msg->PenPtr = (BYTE)*(data->penmap[CONUNIT_PEN_DEFFG]);
        return TRUE;
    }

    if (idx < CONUNIT_PEN_MAX)
        *msg->PenPtr = (BYTE)*(data->penmap[idx]);
    return TRUE;
}

AROS_UFH3S(IPTR, dispatch_stdconclass,
    AROS_UFHA(Class *, cl, A0),
    AROS_UFHA(Object *, o, A2), AROS_UFHA(Msg, msg, A1))
{
    AROS_USERFUNC_INIT

    IPTR retval = 0UL;

    switch (msg->MethodID)
    {
    case M_Console_GetColorPen:
        retval = (IPTR) stdcon_getpencolor(cl, o, (struct P_Console_GetColorPen *)msg);
        break;

    case OM_NEW:
        retval = (IPTR) stdcon_new(cl, o, (struct opSet *)msg);
        break;

    case OM_DISPOSE:
        stdcon_dispose(cl, o, msg);
        break;

    case M_Console_DoCommand:
        stdcon_docommand(cl, o, (struct P_Console_DoCommand *)msg);
        break;

    case M_Console_RenderCursor:
        stdcon_rendercursor(cl, o, (struct P_Console_RenderCursor *)msg);
        break;

    case M_Console_UnRenderCursor:
        stdcon_unrendercursor(cl, o,
            (struct P_Console_UnRenderCursor *)msg);
        break;

    case M_Console_ClearCell:
        stdcon_clearcell(cl, o, (struct P_Console_ClearCell *)msg);
        break;

    case M_Console_NewWindowSize:
        stdcon_newwindowsize(cl, o, (struct P_Console_NewWindowSize *)msg);
        break;

    case M_Console_HandleGadgets:
        stdcon_setactive(cl, o,
            (struct P_Console_HandleGadgets *)msg);
        break;

    default:
        retval = DoSuperMethodA(cl, o, msg);
        break;
    }

    return retval;

    AROS_USERFUNC_EXIT
}

#undef ConsoleDevice

Class *makeStdConClass(struct ConsoleBase *ConsoleDevice)
{
    Class *cl;

    cl = MakeClass(NULL, NULL, CONSOLECLASSPTR, sizeof(struct stdcondata),
        0UL);
    if (cl)
    {
        cl->cl_Dispatcher.h_Entry = (APTR) dispatch_stdconclass;
        cl->cl_Dispatcher.h_SubEntry = NULL;

        cl->cl_UserData = (IPTR) ConsoleDevice;

        return (cl);
    }
    return NULL;
}
