/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: consoleng.device's own Unicode glyph path, above Latin-1.
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/bullet.h>

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <diskfont/diskfont.h>
#include <diskfont/diskfonttag.h>
#include <diskfont/glyph.h>
#include <diskfont/oterrors.h>

#include <string.h>

#include "console_gcc.h"

/*
 * WHY THIS IS HERE AND NOT IN A LIBRARY.
 *
 * AROS can only draw Latin-1 from an outline font, and the reason is one line:
 * diskfont's OTAG_ReadOutlineFont() pre-renders glyph maps for codepoints
 * 0-255 and then CloseEngine()s (bullet.c:1048). ADT's own bullet.c says the
 * clamp out loud: {OT_GlyphCode, (i < 256) ? i : 0x25A1} - anything higher
 * becomes a white square. After that the TextFont carries a fixed Latin-1
 * glyph array with no live rasteriser attached, so nothing can ask for U+2500
 * later.
 *
 * An earlier (non-ADT) tree fixed that by keeping the engine alive on the font and exposing it as
 * four diskfont LVOs, with graphics.library/TextUTF8 as the consumer.
 * terminills' call for this tree is to keep it out of graphics.library and
 * handle it inside this device; if the concerns are worth separating into
 * diskfont later, that is an upstream decision and nothing here has to change
 * shape for it.
 *
 * So this device opens its OWN GlyphEngine for the font it is rendering with.
 * The engine was never the limitation - its lifetime was. Everything needed
 * already exists in this tree: OT_GlyphMap8Bits (diskfonttag.h:50), and a
 * freetype2 that takes a full 32-bit OT_GlyphCode (setinfoa.c:207, stored as
 * int glyph_code).
 *
 * ONE HONEST LIMITATION. This tree has no alpha blit at all - graphics.library
 * has BltTemplate() (1-bit mask) and nothing else; BltTemplateAlpha and
 * cybergraphics' alpha calls are additions in an earlier (non-ADT) tree. So the 8-bit alpha glyph map
 * is thresholded to a 1-bit mask and blitted with BltTemplate. Glyph SHAPES
 * are correct - which is what box drawing, arrows and braille spinners need -
 * but they are not antialiased. That is a visible quality difference from
 * that tree, not a functional one, and it costs no library changes.
 */

/* Local stand-in for diskfont's private struct OTagList. The engine only ever
   receives ->tags and ->filename, so the rest of the shape is ours. */
struct ConsolengOTag
{
    STRPTR          filename;
    struct TagItem *tags;       /* IPTR-expanded copy (64-bit) or ->data */
    ULONG          *data;       /* raw file image; indirect tags point in here */
    ULONG           size;
};

/****** helpers *************************************************************/

/* Case-insensitive ".font" test, without pulling in utility.library. */
static BOOL ConsolengIsDotFont(CONST_STRPTR s)
{
    static const char want[] = ".font";
    int i;

    for (i = 0; i < 5; i++)
    {
        char c = s[i];

        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != want[i])
            return FALSE;
    }
    return TRUE;
}

/* "FONTS:Name.font" -> "FONTS:Name.otag", the way diskfont's
   OTAG_MakeFileName does it (replace the last four chars). */
static STRPTR ConsolengOTagName(CONST_STRPTR fontname)
{
    STRPTR out;
    LONG   l, n;

    if (!fontname)
        return NULL;

    /* Accept a bare font name and put it where fonts live. */
    n = strlen(fontname);
    if (n < 6)                            /* "x.font" is the shortest useful */
        return NULL;

    l = n + sizeof("FONTS:") + 1;
    out = AllocVec(l, MEMF_ANY | MEMF_CLEAR);
    if (!out)
        return NULL;

    if (strchr(fontname, ':') || strchr(fontname, '/'))
        strcpy(out, fontname);            /* already a path */
    else
    {
        strcpy(out, "FONTS:");
        strcat(out, fontname);
    }

    /* swap the ".font" tail for ".otag" */
    n = strlen(out);
    /* Local compare: the UtilityBase macro in console_gcc.h expands to
       ConsoleDevice->cb_UtilityBase, and a static helper has no base. */
    if (n > 5 && ConsolengIsDotFont(out + n - 5))
        strcpy(out + n - 5, ".otag");
    else
        strcat(out, ".otag");

    return out;
}

static void ConsolengFreeOTag(struct ConsolengOTag *ot)
{
    if (!ot)
        return;
#if (__WORDSIZE == 64)
    if (ot->tags && (ULONG *)ot->tags != ot->data)
        FreeVec(ot->tags);
#endif
    if (ot->data)
        FreeVec(ot->data);
    if (ot->filename)
        FreeVec(ot->filename);
    FreeVec(ot);
}

/* Read and validate a .otag file, expanding its tag list. This mirrors
   diskfont's OTAG_GetFile, which needs no diskfont-private state - the
   indirection fixup is relative to the raw buffer, which is why data and tags
   are kept separately on 64-bit. */
static struct ConsolengOTag *ConsolengReadOTag(CONST_STRPTR fontname)
{
    struct ConsolengOTag *ot;
    struct FileInfoBlock *fib;
    BPTR                  fh;
    LONG                  size = 0;
    BOOL                  ok;
    ULONG                *srctag;
    struct TagItem       *ti;
    ULONG                 count;

    ot = AllocVec(sizeof(*ot), MEMF_ANY | MEMF_CLEAR);
    if (!ot)
        return NULL;

    ot->filename = ConsolengOTagName(fontname);
    if (!ot->filename)
    {
        ConsolengFreeOTag(ot);
        return NULL;
    }

    fh = Open(ot->filename, MODE_OLDFILE);
    if (!fh)
    {
        ConsolengFreeOTag(ot);
        return NULL;
    }

    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib)
    {
        if (ExamineFH(fh, fib))
            size = fib->fib_Size;
        FreeDosObject(DOS_FIB, fib);
    }

    if (size < (LONG)(2 * sizeof(ULONG)))
    {
        Close(fh);
        ConsolengFreeOTag(ot);
        return NULL;
    }

    ot->data = AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!ot->data)
    {
        Close(fh);
        ConsolengFreeOTag(ot);
        return NULL;
    }
    ot->size = size;

    ok = (Read(fh, ot->data, size) == size);
    Close(fh);

    /* The file is big-endian and self-describing; reject anything else rather
       than walking a bad tag list. */
    if (!ok || AROS_LONG2BE(ot->data[0]) != OT_FileIdent
            || AROS_LONG2BE(ot->data[1]) != (ULONG)size)
    {
        ConsolengFreeOTag(ot);
        return NULL;
    }

    count = 1;
    for (srctag = ot->data; srctag[0] != TAG_DONE; srctag += 2)
        count++;

#if (__WORDSIZE == 64)
    ot->tags = AllocVec(sizeof(struct TagItem) * count, MEMF_ANY);
    if (!ot->tags)
    {
        ConsolengFreeOTag(ot);
        return NULL;
    }
#else
    ot->tags = (struct TagItem *)ot->data;
#endif

    ti = ot->tags;
    for (srctag = ot->data;; srctag += 2)
    {
        ti->ti_Tag = AROS_LONG2BE(srctag[0]);

        /* Stop at TAG_DONE without touching its data word: some generators
           omit it, and reading past it would corrupt the attached data. */
        if (ti->ti_Tag == TAG_DONE)
            break;

        ti->ti_Data = AROS_LONG2BE(srctag[1]);
        if (ti->ti_Tag & OT_Indirect)
            ti->ti_Data = (IPTR)ot->data + ti->ti_Data;

        ti++;
    }

    return ot;
}

/****** engine binding ******************************************************/

void ConsolengGlyphUnbind(struct ConsoleBase *ConsoleDevice)
{
    if (ConsoleDevice->glyphEngine)
    {
        struct Library *BulletBase = ConsoleDevice->glyphEngineBase;

        if (BulletBase)
            CloseEngine(ConsoleDevice->glyphEngine);
        ConsoleDevice->glyphEngine = NULL;
    }
    if (ConsoleDevice->glyphEngineBase)
    {
        CloseLibrary(ConsoleDevice->glyphEngineBase);
        ConsoleDevice->glyphEngineBase = NULL;
    }
    if (ConsoleDevice->glyphOTag)
    {
        ConsolengFreeOTag((struct ConsolengOTag *)ConsoleDevice->glyphOTag);
        ConsoleDevice->glyphOTag = NULL;
    }
    if (ConsoleDevice->glyphMask)
    {
        FreeVec(ConsoleDevice->glyphMask);
        ConsoleDevice->glyphMask = NULL;
        ConsoleDevice->glyphMaskSize = 0;
    }
    ConsoleDevice->glyphFont = NULL;
    ConsoleDevice->glyphFailed = FALSE;
    ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_OK;
}

/* Bind an engine to font. Caller holds glyphLock. Returns FALSE and latches
   glyphFailed if this font has no outline engine, so a bitmap font costs one
   failed attempt rather than a file open per glyph. */
static LONG ConsolengGlyphBind(struct ConsoleBase *ConsoleDevice,
                               struct TextFont *font)
{
    struct ConsolengOTag *ot;
    STRPTR                enginename;
    STRPTR                enginelib;
    struct GlyphEngine   *ge;
    struct Library       *BulletBase;

    if (ConsoleDevice->glyphFont == font && ConsoleDevice->glyphEngine)
        return CONSOLENG_GLYPH_OK;
    if (ConsoleDevice->glyphFont == font && ConsoleDevice->glyphFailed)
        return ConsoleDevice->glyphWhy;

    /* Different font (or first use): drop whatever we had. */
    ConsolengGlyphUnbind(ConsoleDevice);
    ConsoleDevice->glyphFont = font;

    if (!font || !font->tf_Message.mn_Node.ln_Name)
    {
        ConsoleDevice->glyphFailed = TRUE;
        ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_NOFONT;
        return CONSOLENG_GLYPH_NOFONT;
    }

    ot = ConsolengReadOTag(font->tf_Message.mn_Node.ln_Name);
    if (!ot)
    {
        /* No .otag means a bitmap font: Latin-1 is all it ever had. */
        ConsoleDevice->glyphFailed = TRUE;
        ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_NOOTAG;
        return CONSOLENG_GLYPH_NOOTAG;
    }

    enginename = (STRPTR)GetTagData(OT_Engine, (IPTR)NULL, ot->tags);
    if (!enginename)
    {
        ConsolengFreeOTag(ot);
        ConsoleDevice->glyphFailed = TRUE;
        ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_NOENGINENAME;
        return CONSOLENG_GLYPH_NOENGINENAME;
    }

    enginelib = AllocVec(strlen(enginename) + sizeof(".library") + 1, MEMF_ANY);
    if (!enginelib)
    {
        ConsolengFreeOTag(ot);
        ConsoleDevice->glyphFailed = TRUE;
        ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_NOMEM;
        return CONSOLENG_GLYPH_NOMEM;
    }
    strcpy(enginelib, enginename);
    strcat(enginelib, ".library");

    BulletBase = OpenLibrary(enginelib, 0);
    FreeVec(enginelib);

    if (!BulletBase)
    {
        ConsolengFreeOTag(ot);
        ConsoleDevice->glyphFailed = TRUE;
        ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_NOLIB;
        return CONSOLENG_GLYPH_NOLIB;
    }

    ge = OpenEngine();
    if (!ge)
    {
        CloseLibrary(BulletBase);
        ConsolengFreeOTag(ot);
        ConsoleDevice->glyphFailed = TRUE;
        ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_NOOPENENGINE;
        return CONSOLENG_GLYPH_NOOPENENGINE;
    }

    {
        struct TagItem maintags[] =
        {
            { OT_OTagList, (IPTR)ot->tags     },
            { OT_OTagPath, (IPTR)ot->filename },
            { TAG_DONE,    0                  }
        };
        /* Same derivation diskfont uses: point height from the font's YSize,
           DPI from the otag's YSizeFactor. */
        LONG ysf   = GetTagData(OT_YSizeFactor, 0x10001, ot->tags);
        LONG low   = ysf & 0xFFFF;
        LONG high  = (ysf >> 16) & 0xFFFF;
        LONG ydpi;
        struct TagItem sizetags[] =
        {
            { OT_PointHeight, 0 },
            { OT_DeviceDPI,   0 },
            { OT_DotSize,     0 },
            { TAG_DONE,       0 }
        };

        if (!low)
            low = high = 1;
        ydpi = 72 * high / low;

        sizetags[0].ti_Data = ((ULONG)font->tf_YSize) << 16;
        sizetags[1].ti_Data = (ydpi << 16) | ydpi;
        sizetags[2].ti_Data = (100 << 16) | 100;

        if (SetInfoA(ge, maintags) != OTERR_Success
            || SetInfoA(ge, sizetags) != OTERR_Success)
        {
            CloseEngine(ge);
            CloseLibrary(BulletBase);
            ConsolengFreeOTag(ot);
            ConsoleDevice->glyphFailed = TRUE;
            ConsoleDevice->glyphWhy = CONSOLENG_GLYPH_SETINFO;
            return CONSOLENG_GLYPH_SETINFO;
        }
    }

    /* NOTE for whoever picks this up: an OT_WidthList probe here - to prove a
       face exists, since SetInfoA(OT_OTagList) returns Success even when
       scantags() opened none - CRASHED the engine (page fault, CR2=0x21, with
       ".library" sitting in RBX). That is consistent with there being no face
       for GetWidthList to walk, but a crash is not proof of anything and a
       probe that faults is worse than no probe, so it is removed. Diagnose
       this from the freetype2 side instead: scantags() only opens a face when
       it sees OT_Spec1_FontFile, so the question is whether that tag survives
       this file's tag expansion with a usable string.
       See setinfoa.c:36 (OT_Spec1_FontFile) and glyph.c:333 (GetGlyph). */
    ConsoleDevice->glyphEngineBase = BulletBase;
    ConsoleDevice->glyphEngine     = ge;
    ConsoleDevice->glyphOTag       = ot;
    ConsoleDevice->glyphFailed     = FALSE;

    return CONSOLENG_GLYPH_OK;
}

/****** drawing *************************************************************/

/* Threshold the engine's 8-bit alpha map into a 1-bit planar mask that
   BltTemplate can use. This tree has no alpha blit, so this is the cost of
   staying inside the device. Returns the mask modulo in bytes, or 0. */
static UWORD ConsolengMakeMask(struct ConsoleBase *ConsoleDevice,
                               struct GlyphMap *gm)
{
    UWORD  rowbytes = (gm->glm_BlackWidth + 15) >> 4;    /* words, like BltTemplate wants */
    ULONG  need;
    UBYTE *src;
    UBYTE *dst;
    UWORD  y, x;

    rowbytes <<= 1;
    need = (ULONG)rowbytes * gm->glm_BMRows;
    if (!need)
        return 0;

    if (ConsoleDevice->glyphMaskSize < need)
    {
        if (ConsoleDevice->glyphMask)
            FreeVec(ConsoleDevice->glyphMask);
        ConsoleDevice->glyphMask = AllocVec(need, MEMF_ANY);
        ConsoleDevice->glyphMaskSize = ConsoleDevice->glyphMask ? need : 0;
        if (!ConsoleDevice->glyphMask)
            return 0;
    }

    memset(ConsoleDevice->glyphMask, 0, need);
    src = (UBYTE *)gm->glm_BitMap;
    dst = ConsoleDevice->glyphMask;

    for (y = 0; y < gm->glm_BMRows; y++)
    {
        UBYTE *srow = src + (ULONG)y * gm->glm_BMModulo + gm->glm_BlackLeft;
        UBYTE *drow = dst + (ULONG)y * rowbytes;

        for (x = 0; x < gm->glm_BlackWidth; x++)
        {
            if (srow[x] >= 0x80)                  /* 50% coverage -> set */
                drow[x >> 3] |= 0x80 >> (x & 7);
        }
    }

    return rowbytes;
}

/* Draw one codepoint at (x, base_y) using the device's own engine.
   Returns TRUE if a glyph was drawn, FALSE if the caller should fall back. */
LONG ConsolengGlyphDraw(struct Library *GfxBase,
                        struct ConsoleBase *ConsoleDevice,
                        struct RastPort *rp, ULONG cp, LONG x, LONG base_y)
{
    LONG drawn = CONSOLENG_GLYPH_NOFONT;

    if (!rp || !rp->Font)
        return CONSOLENG_GLYPH_NOFONT;

    ObtainSemaphore(&ConsoleDevice->glyphLock);

    drawn = ConsolengGlyphBind(ConsoleDevice, rp->Font);
    if (drawn == CONSOLENG_GLYPH_OK)
    {
        drawn = CONSOLENG_GLYPH_NOGLYPH;
        struct Library     *BulletBase = ConsoleDevice->glyphEngineBase;
        struct GlyphEngine *ge         = ConsoleDevice->glyphEngine;
        struct GlyphMap    *gm         = NULL;
        ULONG               obtrc      = OTERR_Success;

        struct TagItem settag[] =
        {
            { OT_GlyphCode, cp },
            { TAG_DONE,     0  }
        };
        struct TagItem gettag[] =
        {
            { OT_GlyphMap8Bits, (IPTR)&gm },
            { TAG_DONE,         0         }
        };

        /* Deliberately NOT gated on the return codes. ADT's own
           diskfont/bullet.c:481 does exactly these two calls and ignores rc
           except on its first iteration - freetype2 can return a non-Success
           code while still having filled the glyph map, and requiring
           OTERR_Success here threw away perfectly good glyphs (every codepoint
           above Latin-1 came back as NOGLYPH, including Greek and Cyrillic
           that the face definitely has). Trust the out-parameter, not the
           status. */
        /* The codepoint set is the last unexamined link. gm coming back NULL
           could mean the engine rejected this OT_GlyphCode rather than that it
           has no glyph, and those want different fixes - so report them
           apart. */
        if (SetInfoA(ge, settag) != OTERR_Success)
        {
            drawn = CONSOLENG_GLYPH_SETCODE;
            goto done;
        }

        obtrc = ObtainInfoA(ge, gettag);

        /* Split the old single NOGLYPH verdict into the distinct things that
           can actually go wrong here. Guessing cost two build/run cycles on
           this query; distinguishing them costs nothing and says which of
           "engine returned nothing", "returned a map with no bitmap",
           "returned an empty black box" or "mask build failed" is happening. */
        if (!gm)
        {
            /* gm NULL on its own says nothing useful - ObtainInfoA's return
               code is the engine's OWN diagnosis and it separates the two
               hypotheses that need OPPOSITE fixes:

                 OTERR_Failure (-1) is returned at obtaininfoa.c:86, BEFORE the
                 tag loop runs, when engine->face_established is FALSE - so the
                 face never opened (bad .otag path, FT_New_Face refused the
                 file) or SetInstance() failed on the point size / DPI. Nothing
                 about this codepoint is implicated at all.

                 OTERR_UnknownGlyph (12) comes from GetGlyph() and means the
                 opposite: the face IS open and instanced, and either
                 FT_Get_Char_Index returned 0 for this codepoint or the glyph
                 has a zero-width black box. That is the engine working
                 correctly on a character the face does not cover.

               Collapsing both into a single NOGLYPH is what made the last two
               build/run cycles inconclusive. */
            if (obtrc == (ULONG)OTERR_Failure)
                drawn = CONSOLENG_GLYPH_OBT_NOFACE;
            else if (obtrc == (ULONG)OTERR_UnknownGlyph)
                drawn = CONSOLENG_GLYPH_OBT_NOCP;
            else
                drawn = CONSOLENG_GLYPH_NOGLYPH;
        }
        else if (!gm->glm_BitMap)
            drawn = CONSOLENG_GLYPH_GM_NOBITMAP;
        else if (!gm->glm_BlackWidth || !gm->glm_BMRows)
            drawn = CONSOLENG_GLYPH_GM_EMPTY;
        else
        {
            UWORD mod = ConsolengMakeMask(ConsoleDevice, gm);

            if (!mod)
                drawn = CONSOLENG_GLYPH_MASKFAIL;
            else
            {
                BltTemplate((PLANEPTR)ConsoleDevice->glyphMask, 0, mod, rp,
                            x + gm->glm_X0, base_y - gm->glm_Y0,
                            gm->glm_BlackWidth, gm->glm_BMRows);
                drawn = CONSOLENG_GLYPH_OK;
            }
        }

        if (gm)
        {
            /* ObtainInfoA and ReleaseInfoA disagree about what ti_Data MEANS
               for the very same tag, so the obtain array must NOT be reused
               here:

                 obtaininfoa.c:109  gm_p = (struct GlyphMap **)otagdata;
                                    *gm_p = GetGlyph(...);      <- &gm
                 releaseinfoa.c:46  GMap = (struct GlyphMap *)otagdata;
                                    FreeVec(GMap->glm_BitMap);
                                    FreeVec(GMap);              <- gm

               Passing the obtain array (which holds &gm) to ReleaseInfoA makes
               it treat the ADDRESS OF THIS STACK VARIABLE as a GlyphMap, read
               a "glm_BitMap" out of the stack next to it and FreeVec that.
               That is what faulted in Exec_115_FreeVec+0x20 immediately after
               the first real glyph (U+03C0) drew correctly.

               ADT's own diskfont does it the right way at bullet.c:359 - a
               separate releasetags array holding (IPTR)gm[i], the value. */
            struct TagItem releasetag[] =
            {
                { OT_GlyphMap8Bits, (IPTR)gm },
                { TAG_DONE,         0        }
            };

            ReleaseInfoA(ge, releasetag);
        }
    }

done:
    ReleaseSemaphore(&ConsoleDevice->glyphLock);

    return drawn;
}
