/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * TtyProbe - the posixc calls libuv's tty backend makes on fd 0/1/2, one at
 * a time with errno after each, so a node-level "setRawMode EINVAL" can be
 * pinned to isatty / tcgetattr / tcsetattr / ioctl instead of guessed.
 *
 *   TtyProbe
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static void one(int fd)
{
    struct termios t, raw;
    struct winsize ws;
    int rc;

    errno = 0;
    rc = isatty(fd);
    printf("TtyProbe: fd %d isatty=%d errno=%d\n", fd, rc, errno);

    memset(&t, 0, sizeof(t));
    errno = 0;
    rc = tcgetattr(fd, &t);
    printf("TtyProbe: fd %d tcgetattr=%d errno=%d c_lflag=0x%lx\n",
           fd, rc, errno, (unsigned long)t.c_lflag);
    if (rc != 0)
        return;

    raw = t;
    raw.c_lflag &= ~ICANON;
    errno = 0;
    rc = tcsetattr(fd, TCSADRAIN, &raw);
    printf("TtyProbe: fd %d tcsetattr(TCSADRAIN, ~ICANON)=%d errno=%d (%s)\n",
           fd, rc, errno, strerror(errno));

    errno = 0;
    rc = tcsetattr(fd, TCSANOW, &t);
    printf("TtyProbe: fd %d tcsetattr(TCSANOW, restore)=%d errno=%d (%s)\n",
           fd, rc, errno, strerror(errno));

    memset(&ws, 0, sizeof(ws));
    errno = 0;
    rc = ioctl(fd, TIOCGWINSZ, &ws);
    printf("TtyProbe: fd %d ioctl(TIOCGWINSZ)=%d errno=%d %ux%u\n",
           fd, rc, errno, ws.ws_col, ws.ws_row);
}

int main(void)
{
    one(0);
    one(1);
    one(2);
    return 0;
}
