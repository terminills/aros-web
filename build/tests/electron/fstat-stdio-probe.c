/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: What does posixc fstat() say about NIL: stdio?

    node's PlatformInit fstat()s fds 0-2 and ABORT()s on any failure other
    than EBADF (src/node.cc, "Make sure file descriptors 0-2 are valid").
    A process started with `Run >NIL: <NIL:` (the guest input channel) gets
    NIL: handles for all three, so this records errno per fd, plus what a
    fresh open("/dev/null") and a PIPE: end report, into
    SYS:Developer/Chromium/fstat-stdio-probe.log (stdio may be NIL:, so
    nothing is printed).

    Build: x86_64-aros-gcc --sysroot=<ADT Developer> -o fstat-stdio-probe
    Run (guest input channel): SYS:Developer/Chromium/fstat-stdio-probe
*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <proto/dos.h>

static FILE *out;

static void probe(const char *label, int fd)
{
    struct stat st;
    int rc;

    errno = 0;
    SetIoErr(0);
    rc = fstat(fd, &st);
    fprintf(out, "%s fd=%d fstat=%d errno=%d (%s) IoErr=%ld mode=0%o\n",
            label, fd, rc, errno, strerror(errno), (long)IoErr(),
            rc == 0 ? (unsigned)st.st_mode : 0u);
}

int main(void)
{
    int fd;

    out = fopen("SYS:Developer/Chromium/fstat-stdio-probe.log", "a");
    if (!out)
        return 20;

    fprintf(out, "--- fstat-stdio-probe\n");
    probe("stdin ", 0);
    probe("stdout", 1);
    probe("stderr", 2);

    fd = open("/dev/null", O_RDWR);
    fprintf(out, "open(/dev/null)=%d errno=%d\n", fd, fd < 0 ? errno : 0);
    if (fd >= 0)
    {
        probe("devnull", fd);
        close(fd);
    }

    fd = open("NIL:", O_RDWR);
    fprintf(out, "open(NIL:)=%d errno=%d\n", fd, fd < 0 ? errno : 0);
    if (fd >= 0)
    {
        probe("nil    ", fd);
        close(fd);
    }

    fclose(out);
    return 0;
}
