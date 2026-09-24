/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * StatProbe - show whether posixc gives distinct (st_dev, st_ino) identities
 * to distinct files, the way SQLite's unix VFS relies on: it keys its inode
 * lock table and its recycled-fd list on the pair returned by fstat().
 *
 *   StatProbe [DIR]        default DIR=T:
 *
 * Creates DIR/StatProbe-a and DIR/StatProbe-b with the flag combinations
 * SQLite/Chromium use, then prints stat()-by-path and fstat()-by-fd for each.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void show(const char *what, const char *path, int fd)
{
    struct stat s;
    int rc;

    errno = 0;
    rc = fd >= 0 ? fstat(fd, &s) : stat(path, &s);
    if (rc != 0) {
        printf("  %-22s %-22s FAILED errno=%d (%s)\n", what, path, errno, strerror(errno));
        return;
    }
    printf("  %-22s %-22s dev=%#llx ino=%#llx size=%lld mode=%o\n", what, path,
           (unsigned long long)s.st_dev, (unsigned long long)s.st_ino,
           (long long)s.st_size, (unsigned)s.st_mode);
}

static int run(const char *dir, const char *tag, int flags)
{
    char pa[512], pb[512];
    int fa, fb, distinct;
    struct stat sa, sb;

    snprintf(pa, sizeof pa, "%s%sStatProbe-a", dir, dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/' ? "" : "/");
    snprintf(pb, sizeof pb, "%s%sStatProbe-b", dir, dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/' ? "" : "/");

    printf("%s: flags=%#x\n", tag, flags);
    errno = 0;
    fa = open(pa, flags, 0644);
    if (fa < 0) { printf("  open(%s): FAILED errno=%d (%s)\n", pa, errno, strerror(errno)); return 1; }
    errno = 0;
    fb = open(pb, flags, 0644);
    if (fb < 0) { printf("  open(%s): FAILED errno=%d (%s)\n", pb, errno, strerror(errno)); close(fa); return 1; }
    if (write(fa, "aaaa", 4) != 4 || write(fb, "bbbbbbbb", 8) != 8)
        printf("  write: FAILED errno=%d (%s)\n", errno, strerror(errno));

    show("fstat(fd a)", pa, fa);
    show("fstat(fd b)", pb, fb);
    show("stat(path a)", pa, -1);
    show("stat(path b)", pb, -1);

    distinct = fstat(fa, &sa) == 0 && fstat(fb, &sb) == 0 &&
               (sa.st_dev != sb.st_dev || sa.st_ino != sb.st_ino);
    printf("  fstat identities %s\n", distinct ? "DISTINCT" : "*** IDENTICAL ***");
    close(fa);
    close(fb);
    unlink(pa);
    unlink(pb);
    return distinct ? 0 : 2;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "T:";
    int rc = 0;

    printf("StatProbe: dir=<%s>\n", dir);
    rc |= run(dir, "O_RDWR|O_CREAT (sqlite db)", O_RDWR | O_CREAT);
    rc |= run(dir, "O_RDWR|O_CREAT|O_TRUNC", O_RDWR | O_CREAT | O_TRUNC);
    rc |= run(dir, "O_RDONLY after create", O_RDONLY | O_CREAT);
    printf("StatProbe: %s\n", rc ? "FAIL - files share an identity" : "ok");
    return rc;
}
