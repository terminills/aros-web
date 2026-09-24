/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * SeekProbe - what a DOS handle says when asked to seek past EOF, through
 * dos.library/Seek() and through dos64.library/Seek64().
 *
 *   SeekProbe [FILE ...]        default T:SeekProbe-file
 *
 * posixc's lseek() parks the offset past EOF only when the handler
 * answers ERROR_SEEK_ERROR (219).  On emul.handler SqlIoProbe saw errno
 * 1209 (IoErr 209, ERROR_ACTION_NOT_KNOWN) instead, which the 32-bit
 * Seek() never produces - so this shows which layer changes the code.
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/dos64.h>

#include <stdio.h>

struct Library *DOS64Base;

static int failures;

static void report(const char *what, QUAD r, LONG err, LONG wanterr)
{
    int ok = (r == -1 && err == wanterr);

    printf("  %-44s r=%lld IoErr=%ld (want -1/%ld) %s\n", what, (long long)r,
           (long)err, (long)wanterr, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void probe(const char *path)
{
    BPTR fh;
    QUAD r;
    LONG err;

    printf("SeekProbe: %s\n", path);

    fh = Open(path, MODE_NEWFILE);
    if (!fh)
    {
        printf("  Open failed, IoErr %ld FAIL\n", (long)IoErr());
        failures++;
        return;
    }

    /* 32-bit dos.library path */
    SetIoErr(0);
    r = Seek(fh, 24, OFFSET_BEGINNING);
    err = IoErr();
    report("Seek(24, BEGINNING) on empty file", r, err, ERROR_SEEK_ERROR);

    SetIoErr(0);
    r = Seek(fh, 16, OFFSET_END);
    err = IoErr();
    report("Seek(+16, END) on empty file", r, err, ERROR_SEEK_ERROR);

    /* dos64.library path, the one posixc uses when IsFileSystem64() */
    if (DOS64Base)
    {
        printf("  IsFileSystem64=%ld\n", (long)IsFileSystem64(fh));

        SetIoErr(0);
        r = Seek64(fh, OFFSET_BEGINNING, 24);
        err = IoErr();
        report("Seek64(24, BEGINNING) on empty file", r, err, ERROR_SEEK_ERROR);

        SetIoErr(0);
        r = Seek64(fh, OFFSET_END, 16);
        err = IoErr();
        report("Seek64(+16, END) on empty file", r, err, ERROR_SEEK_ERROR);
    }
    else
        printf("  dos64.library not available\n");

    Close(fh);
    DeleteFile(path);
}

int main(int argc, char **argv)
{
    int i;

    DOS64Base = OpenLibrary("dos64.library", 0);

    if (argc > 1)
        for (i = 1; i < argc; i++)
            probe(argv[i]);
    else
        probe("T:SeekProbe-file");

    if (DOS64Base)
        CloseLibrary(DOS64Base);
    printf("SeekProbe: failures=%d\n", failures);
    return failures ? 1 : 0;
}
