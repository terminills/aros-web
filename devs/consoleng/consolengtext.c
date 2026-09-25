/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.

    Desc: consoleng.device's local stand-in for graphics.library/TextUTF8().
*/

#include <proto/graphics.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

#include "console_gcc.h"

/*
 * WHY THIS EXISTS, AND WHAT IT DELIBERATELY DOES NOT DO.
 *
 * An earlier (non-ADT) tree renders codepoints above U+00FF through a new
 * graphics.library LVO, TextUTF8(), which in turn needs a UTF-8 glyph-map API
 * in diskfont.library (UTF8GlyphAdvance / UTF8RenderCP / ColorGlyphMap).
 * Neither exists in this tree, and adding them means changing two ROM
 * libraries that the whole system sits on. terminills' call: keep it out of
 * graphics.library - if it belongs there, that is an upstream merge decision,
 * not a side effect of porting a console.
 *
 * So this device carries its own stand-in instead, and the cost is bounded
 * because of how the callers are written: stdconclass.c and charmapconclass.c
 * already SPLIT each same-pen run, sending ASCII/Latin-1 cells through the
 * ordinary Text() path as bytes and calling here only for codepoints ABOVE
 * U+00FF. Everything a terminal needs structurally - escape-sequence
 * reassembly across writes, CSI semantics through UTF-8 input, deferred
 * autowrap, the redraw controls, UCS-4 charmap cells - is in the device and is
 * unaffected by this file.
 *
 * This file decodes the run and routes each codepoint:
 *   < 0x100  -> Text() as a single Latin-1 byte, which is exactly the factory
 *               ceiling, so this device is never worse than the stock one;
 *   >= 0x100 -> consolengglyph.c, which opens this device's OWN outline engine
 *               and rasterises the codepoint (diskfont closes its engine after
 *               pre-rendering 0-255, which is the whole reason a higher
 *               codepoint cannot be asked for later).
 *
 * The placeholder is now only the LAST resort - a bitmap font, a font with no
 * outline engine, or a codepoint the face does not cover. It is kept because
 * the callers have already filled the cell background and advance the cursor
 * by CELLS: something of the right width has to go where the glyph would have
 * been. Drawing the raw UTF-8 bytes through Text() instead would emit two to
 * four mojibake characters per cell and shear the whole line.
 */

/* Decode one UTF-8 sequence, advancing *cursor. Malformed input is tolerated
   the same way the device's own decoder does: a stray continuation byte
   consumes one cell rather than desynchronising the whole run. */
static ULONG ConsoleNGUTF8Next(const UBYTE **cursor, const UBYTE *end)
{
    const UBYTE *p = *cursor;
    UBYTE        b = *p;
    ULONG        cp;

    if (b < 0x80)
    {
        *cursor = p + 1;
        return b;
    }
    if (b < 0xC0)
    {
        *cursor = p + 1;         /* unexpected continuation byte */
        return '?';
    }
    if (b < 0xE0)
    {
        if (p + 2 > end) { *cursor = end; return '?'; }
        cp = ((ULONG)(b & 0x1F) << 6) | (p[1] & 0x3F);
        *cursor = p + 2;
        return cp;
    }
    if (b < 0xF0)
    {
        if (p + 3 > end) { *cursor = end; return '?'; }
        cp = ((ULONG)(b & 0x0F) << 12) | ((ULONG)(p[1] & 0x3F) << 6) |
             (p[2] & 0x3F);
        *cursor = p + 3;
        return cp;
    }
    if (p + 4 > end) { *cursor = end; return '?'; }
    cp = ((ULONG)(b & 0x07) << 18) | ((ULONG)(p[1] & 0x3F) << 12) |
         ((ULONG)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    *cursor = p + 4;
    return cp;
}

void ConsoleNGTextUTF8(struct Library *GfxBase,
                       struct ConsoleBase *ConsoleDevice,
                       struct RastPort *rp,
                       CONST_STRPTR string, ULONG byteLen)
{
    const UBYTE *p   = (const UBYTE *)string;
    const UBYTE *end = p + byteLen;

    if (!rp || !string || !byteLen)
        return;

    while (p < end)
    {
        const UBYTE *prev = p;
        ULONG        cp   = ConsoleNGUTF8Next(&p, end);

        if (cp < 0x100)
        {
            /* Latin-1 and below: draw it properly as a single byte. This is
               the SAME rule charmapconclass.c:1113 uses on the refresh path
               ("if (str[idx] < 0x100) ... Text()"), and matching it here
               matters for two reasons.

               First, it is the factory ceiling. diskfont pre-renders glyphs
               0-255 only (bullet.c: {OT_GlyphCode, (i < 256) ? i : 0x25A1}),
               so Latin-1 is exactly what the stock console can draw and
               exactly what this device should still draw without a live
               glyph engine. Throwing it away would make this device WORSE
               than the one it sits beside.

               Second, the write path disagrees with the refresh path:
               stdconclass.c:427 sends everything >= 0x80 here, while the
               refresh keeps < 0x100 on Text(). That asymmetry is harmless in
               that tree, whose TextUTF8 renders Latin-1 through the engine, but
               with a placeholder it made a freshly-typed 'a-umlaut' show as
               '?' and then correct itself on the next redraw. Handling it
               here removes the inconsistency. */
            UBYTE byte = (UBYTE)cp;

            Text(rp, (CONST_STRPTR)&byte, 1);
        }
        else
        {
            /* Above Latin-1: ask this device's own GlyphEngine, which exists
               precisely because diskfont closes its own at the end of
               OTAG_ReadOutlineFont (bullet.c:1048). The pen position has
               already been set by the caller, so hand the glyph path the
               current position explicitly and advance by one cell.

               Fall back to the placeholder only when there really is no glyph
               to be had - a bitmap font, a font with no outline engine, or a
               codepoint the face does not cover. */
            LONG why = ConsolengGlyphDraw(GfxBase, ConsoleDevice, rp, cp,
                                          rp->cp_x, rp->cp_y);

            if (why != CONSOLENG_GLYPH_OK)
            {
                /* Draw the REASON, not a generic '?'. See the note in
                   console_gcc.h: this device cannot use kprintf, so the
                   rendered cell is the only diagnostic channel it has. */
                UBYTE d = (why < 10) ? (UBYTE)('0' + why)
                                     : (UBYTE)('A' + (why - 10));

                Text(rp, (CONST_STRPTR)&d, 1);
            }
            else
            {
                /* BltTemplate does not move the pen; keep the caller's cell
                   arithmetic intact by advancing one character width. */
                Move(rp, rp->cp_x + rp->Font->tf_XSize, rp->cp_y);
            }
        }

        if (p <= prev)
            break;               /* never loop on a zero-width step */
    }
}
