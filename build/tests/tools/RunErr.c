/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * RunErr FILE COMMAND [ARGS...]
 *
 * Run a shell command with its error stream (pr_CES, posixc fd 2) on FILE.
 * The Shell's ">" only redirects the output stream, so a program's stderr -
 * node's NODE_DEBUG_NATIVE output, for one - lands on the console and is
 * lost to a scripted run.  SystemTags(SYS_Error) gives the child its own
 * error stream; input and output are inherited as usual.
 */
#include <proto/dos.h>
#include <dos/dostags.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    char cmd[2048];
    BPTR err;
    LONG rc;
    int i;

    if (argc < 3) {
        fputs("usage: RunErr FILE COMMAND [ARGS...]\n", stderr);
        return 20;
    }

    cmd[0] = '\0';
    for (i = 2; i < argc; i++) {
        if (strlen(cmd) + strlen(argv[i]) + 4 >= sizeof(cmd))
            break;
        if (i > 2)
            strcat(cmd, " ");
        /* re-quote arguments that carry spaces so the Shell sees one item */
        if (strchr(argv[i], ' ')) {
            strcat(cmd, "\"");
            strcat(cmd, argv[i]);
            strcat(cmd, "\"");
        } else
            strcat(cmd, argv[i]);
    }

    err = Open((CONST_STRPTR)argv[1], MODE_NEWFILE);
    if (!err) {
        fprintf(stderr, "RunErr: cannot open %s\n", argv[1]);
        return 20;
    }

    rc = SystemTags((CONST_STRPTR)cmd,
                    SYS_Error, (IPTR)err,
                    TAG_DONE);
    Close(err);
    return rc;
}
