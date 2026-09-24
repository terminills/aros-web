/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * FdProbe: report what node's PlatformInit sees on fds 0..2.
 *
 * node::PlatformInit() fstat()s fds 0..2 and ABORT()s when the call fails
 * with anything but EBADF, before it prints a single line.  This tool runs
 * the same calls from the same stdio (run it with exactly the redirects the
 * failing node line had) and writes the result to FILE, so the failing fd
 * and errno are visible without a debugger.
 */
#include <proto/dos.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    FILE *out;
    BPTR fh[3];
    char name[256];
    int fd;
    if (argc < 2) {
        fputs("usage: FdProbe FILE\n", stderr);
        return 20;
    }
    out = fopen(argv[1], "w");
    if (!out)
        return 20;
    fh[0] = Input();
    fh[1] = Output();
    fh[2] = ErrorOutput();
    for (fd = 0; fd < 3; fd++) {
        struct stat st;
        int rc, err, flags;
        errno = 0;
        rc = fstat(fd, &st);
        err = errno;
        name[0] = '\0';
        if (fh[fd] && !NameFromFH(fh[fd], name, sizeof(name)))
            snprintf(name, sizeof(name), "<NameFromFH ioerr %ld>", (long)IoErr());
        flags = fcntl(fd, F_GETFL);
        fprintf(out, "fd %d: fstat rc=%d errno=%d (%s) mode=%o fcntl=%d isatty=%d "
                     "dos fh=%p interactive=%ld name='%s'\n",
                fd, rc, err, strerror(err), rc == 0 ? (unsigned)st.st_mode : 0u,
                flags, isatty(fd), (void *)fh[fd],
                fh[fd] ? (long)IsInteractive(fh[fd]) : -1L, name);
    }
    fclose(out);
    return 0;
}
