/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * SqlIoProbe - replay the descriptor-level sequence SQLite's Unix VFS puts a
 * freshly created database file through (open/fstat/pread/pwrite/fsync/
 * ftruncate at page granularity) and check every size and byte against what
 * POSIX promises.  A wrong fstat() size or a pread() that returns stale
 * bytes past EOF shows up in Chromium as SQLITE_NOTADB ("file is not a
 * database") on a brand-new profile.
 *
 *   SqlIoProbe [DIR]     default DIR=T:
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAGE 4096

static int failures;

static void check(int ok, const char *what, long got, long want)
{
    printf("  %-40s %s (got %ld, want %ld)\n", what, ok ? "ok" : "FAIL", got, want);
    if (!ok) failures++;
    fflush(stdout);
}

static long fsize(int fd)
{
    struct stat st;
    if (fstat(fd, &st) != 0) return -errno;
    return (long)st.st_size;
}

static long readat(int fd, void *buf, size_t n, off_t off, int fill)
{
    memset(buf, fill, n);
    return (long)pread(fd, buf, n, off);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "T:";
    char path[512];
    static unsigned char page[PAGE], buf[PAGE];
    int fd, i;
    long r;

    snprintf(path, sizeof path, "%s%sSqlIoProbe-db", dir,
             (dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/') ? "" : "/");
    unlink(path);
    printf("SqlIoProbe: %s\n", path);

    for (i = 0; i < PAGE; i++) page[i] = (unsigned char)(i * 7 + 3);
    memcpy(page, "SQLite format 3", 16);

    /* 1. create */
    fd = open(path, O_RDWR | O_CREAT, 0644);
    check(fd >= 0, "open(O_RDWR|O_CREAT) new file", fd, 0);
    if (fd < 0) return 1;
    check(fsize(fd) == 0, "fstat size after create", fsize(fd), 0);
    r = readat(fd, buf, 100, 0, 0xAA);
    check(r == 0, "pread(100 @0) on empty file", r, 0);
    check(lseek(fd, 0, SEEK_CUR) == 0, "offset unchanged by pread", lseek(fd, 0, SEEK_CUR), 0);

    /* 2. first page write, as sqlite does for page 1 */
    r = pwrite(fd, page, PAGE, 0);
    check(r == PAGE, "pwrite(page @0)", r, PAGE);
    check(fsize(fd) == PAGE, "fstat size after page 1", fsize(fd), PAGE);
    r = readat(fd, buf, 100, 0, 0xAA);
    check(r == 100 && memcmp(buf, page, 100) == 0, "pread(100 @0) header bytes", r, 100);
    r = readat(fd, buf, 100, PAGE, 0xAA);
    check(r == 0, "pread(100 @EOF) returns 0", r, 0);
    r = readat(fd, buf, PAGE, PAGE - 50, 0xAA);
    check(r == 50 && memcmp(buf, page + PAGE - 50, 50) == 0, "short pread across EOF", r, 50);

    /* 3. extend by writing page 2, read whole file back */
    r = pwrite(fd, page, PAGE, PAGE);
    check(r == PAGE, "pwrite(page @PAGE)", r, PAGE);
    check(fsize(fd) == 2 * PAGE, "fstat size after page 2", fsize(fd), 2 * PAGE);
    check(fsync(fd) == 0, "fsync", 0, 0);
    r = readat(fd, buf, PAGE, PAGE, 0xAA);
    check(r == PAGE && memcmp(buf, page, PAGE) == 0, "pread(page @PAGE) bytes", r, PAGE);

    /* 4. truncate back to one page (journal rollback path) */
    check(ftruncate(fd, PAGE) == 0, "ftruncate(PAGE)", 0, 0);
    check(fsize(fd) == PAGE, "fstat size after ftruncate", fsize(fd), PAGE);
    r = readat(fd, buf, 100, PAGE, 0xAA);
    check(r == 0, "pread past truncated EOF", r, 0);
    check(ftruncate(fd, 0) == 0, "ftruncate(0)", 0, 0);
    check(fsize(fd) == 0, "fstat size after ftruncate(0)", fsize(fd), 0);
    r = readat(fd, buf, 100, 0, 0xAA);
    check(r == 0, "pread on truncated-empty file", r, 0);

    /* 5. second descriptor on the same file sees the same size */
    {
        int fd2 = open(path, O_RDWR | O_CREAT, 0644);
        check(fd2 >= 0, "second open(O_RDWR|O_CREAT)", fd2, 0);
        if (fd2 >= 0) {
            check(fsize(fd2) == 0, "fstat size via 2nd fd (empty)", fsize(fd2), 0);
            r = pwrite(fd, page, PAGE, 0);
            check(r == PAGE, "pwrite via 1st fd", r, PAGE);
            check(fsize(fd2) == PAGE, "fstat size via 2nd fd after write", fsize(fd2), PAGE);
            r = readat(fd2, buf, 100, 0, 0xAA);
            check(r == 100 && memcmp(buf, page, 100) == 0, "pread via 2nd fd", r, 100);
            close(fd2);
        }
    }
    close(fd);

    /* 6. reopen after close: size must persist; O_CREAT must not truncate */
    fd = open(path, O_RDWR | O_CREAT, 0644);
    check(fd >= 0, "reopen existing", fd, 0);
    if (fd >= 0) {
        struct stat st;
        check(fsize(fd) == PAGE, "fstat size after reopen", fsize(fd), PAGE);
        r = readat(fd, buf, 100, 0, 0xAA);
        check(r == 100 && memcmp(buf, page, 100) == 0, "pread after reopen", r, 100);
        r = stat(path, &st);
        check(r == 0 && st.st_size == PAGE, "stat() size by path", r == 0 ? (long)st.st_size : -1, PAGE);
        close(fd);
    }
    unlink(path);

    /* 7. identity: SQLite keys its lock/descriptor bookkeeping by
       (st_dev, st_ino) from fstat(), so two different files opened O_RDWR
       must not report the same inode, and fstat() must agree with stat(). */
    {
        char path2[512];
        struct stat sa, sb, sp;
        int fa, fb;

        snprintf(path2, sizeof path2, "%s-2", path);
        fa = open(path, O_RDWR | O_CREAT, 0644);
        fb = open(path2, O_RDWR | O_CREAT, 0644);
        check(fa >= 0 && fb >= 0, "open two files O_RDWR|O_CREAT", fa >= 0 && fb >= 0, 1);
        if (fa >= 0 && fb >= 0) {
            int ra = fstat(fa, &sa), rb = fstat(fb, &sb), rp = stat(path, &sp);
            printf("    A fstat=%d dev=%ld ino=%#lx  B fstat=%d dev=%ld ino=%#lx  A-by-path stat=%d ino=%#lx\n",
                   ra, (long)sa.st_dev, (unsigned long)sa.st_ino,
                   rb, (long)sb.st_dev, (unsigned long)sb.st_ino,
                   rp, (unsigned long)sp.st_ino);
            check(ra == 0 && rb == 0, "fstat both O_RDWR files", ra == 0 && rb == 0, 1);
            check(sa.st_ino != sb.st_ino || sa.st_dev != sb.st_dev,
                  "distinct (dev,ino) for distinct files", sa.st_ino == sb.st_ino, 0);
            check(rp == 0 && sp.st_ino == sa.st_ino, "fstat ino == stat-by-path ino",
                  sp.st_ino == sa.st_ino, 1);
            /* the same again with a read-only descriptor, SQLite's -journal case */
            {
                int fr = open(path, O_RDONLY);
                struct stat sr;
                if (fr >= 0 && fstat(fr, &sr) == 0) {
                    printf("    A O_RDONLY fstat ino=%#lx\n", (unsigned long)sr.st_ino);
                    check(sr.st_ino == sa.st_ino, "O_RDONLY ino == O_RDWR ino", sr.st_ino == sa.st_ino, 1);
                    close(fr);
                } else
                    check(0, "open/fstat O_RDONLY", fr, 0);
            }
        }
        if (fa >= 0) close(fa);
        if (fb >= 0) close(fb);
        unlink(path);
        unlink(path2);
    }

    /* 8. offsets beyond EOF.  SQLite (built without pread) does
       lseek()+read() at offsets past the end of a file all the time: the
       change-counter probe at offset 24 of an empty database in NORMAL
       locking mode, the next-journal-header magic check past the end of the
       rollback journal.  POSIX: the seek succeeds and does not grow the
       file, the read returns 0, only a write there extends the file with a
       zero-filled gap. */
    fd = open(path, O_RDWR | O_CREAT, 0644);
    check(fd >= 0, "open new file for past-EOF checks", fd, 0);
    if (fd >= 0) {
        r = lseek(fd, 24, SEEK_SET);
        check(r == 24, "lseek(24) on empty file", r, 24);
        check(fsize(fd) == 0, "size still 0 after lseek past EOF", fsize(fd), 0);
        r = lseek(fd, 0, SEEK_CUR);
        check(r == 24, "lseek(0,SEEK_CUR) reports 24", r, 24);
        r = read(fd, buf, 16);
        check(r == 0, "read() past EOF returns 0", r, 0);
        check(fsize(fd) == 0, "size still 0 after read past EOF", fsize(fd), 0);
        r = readat(fd, buf, 8, 5120, 0xAA);
        check(r == 0, "pread(8 @5120) on empty file", r, 0);
        check(fsize(fd) == 0, "size still 0 after pread past EOF", fsize(fd), 0);
        r = lseek(fd, 0, SEEK_CUR);
        check(r == 24, "offset unchanged by pread past EOF", r, 24);

        /* write at the parked offset: bytes 0..23 must read as zeros */
        r = write(fd, "SQLite format 3", 16);
        check(r == 16, "write(16) at parked offset 24", r, 16);
        check(fsize(fd) == 40, "size 40 after write at 24", fsize(fd), 40);
        r = readat(fd, buf, 40, 0, 0xAA);
        {
            int zeros = 1;
            for (i = 0; i < 24; i++) if (buf[i]) zeros = 0;
            check(r == 40 && zeros && memcmp(buf + 24, "SQLite format 3", 16) == 0,
                  "gap reads as zeros, data at 24", r, 40);
        }
        r = lseek(fd, 0, SEEK_CUR);
        check(r == 40, "offset 40 after the write", r, 40);

        /* pwrite well past EOF extends with a zero gap, offset unchanged */
        r = pwrite(fd, page, 4, 100);
        check(r == 4, "pwrite(4 @100)", r, 4);
        check(fsize(fd) == 104, "size 104 after pwrite past EOF", fsize(fd), 104);
        r = lseek(fd, 0, SEEK_CUR);
        check(r == 40, "offset still 40 after pwrite", r, 40);
        r = readat(fd, buf, 64, 40, 0xAA);
        {
            int zeros = 1;
            for (i = 0; i < 60; i++) if (buf[i]) zeros = 0;
            check(r == 64 && zeros && memcmp(buf + 60, page, 4) == 0,
                  "pwrite gap zero-filled", r, 64);
        }

        /* SEEK_END past EOF, then truncate underneath a parked offset */
        r = lseek(fd, 16, SEEK_END);
        check(r == 120, "lseek(16,SEEK_END)", r, 120);
        check(ftruncate(fd, 8) == 0, "ftruncate(8) with parked offset", 0, 0);
        r = lseek(fd, 0, SEEK_CUR);
        check(r == 120, "offset survives ftruncate", r, 120);
        r = read(fd, buf, 4);
        check(r == 0, "read at parked offset after truncate", r, 0);
        r = lseek(fd, 4, SEEK_SET);
        check(r == 4, "lseek back inside the file", r, 4);
        r = read(fd, buf, 4);
        check(r == 4, "read(4 @4) inside the file", r, 4);
        close(fd);
    }
    unlink(path);

    /* 9. SQLite's NORMAL locking-mode sequence on a new database: the
       shared lock is taken with fcntl() record locks (PENDING byte, then
       the SHARED range, then PENDING released) before the 16-byte change
       counter is read at offset 24 of the still-empty file. */
    fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
    check(fd >= 0, "open with SQLite's flags", fd, 0);
    if (fd >= 0) {
        struct flock lk;
        struct stat st;

        errno = 0;
        r = fstat(fd, &st);
        check(r == 0 && st.st_size == 0, "fstat new file", r == 0 ? (long)st.st_size : -errno, 0);

        memset(&lk, 0, sizeof lk);
        lk.l_type = F_RDLCK; lk.l_whence = SEEK_SET; lk.l_start = 0x40000000; lk.l_len = 1;
        errno = 0;
        r = fcntl(fd, F_SETLK, &lk);
        check(r == 0, "F_SETLK RDLCK pending byte", r == 0 ? 0 : -errno, 0);
        lk.l_start = 0x40000002; lk.l_len = 510;
        errno = 0;
        r = fcntl(fd, F_SETLK, &lk);
        check(r == 0, "F_SETLK RDLCK shared range", r == 0 ? 0 : -errno, 0);
        lk.l_type = F_UNLCK; lk.l_start = 0x40000000; lk.l_len = 1;
        errno = 0;
        r = fcntl(fd, F_SETLK, &lk);
        check(r == 0, "F_SETLK UNLCK pending byte", r == 0 ? 0 : -errno, 0);

        errno = 0;
        r = lseek(fd, 24, SEEK_SET);
        check(r == 24, "lseek(24) after record locks", r == -1 ? -errno : r, 24);
        errno = 0;
        r = read(fd, buf, 16);
        check(r == 0, "read(16 @24) on empty locked file", r == -1 ? -errno : r, 0);

        lk.l_type = F_UNLCK; lk.l_start = 0; lk.l_len = 0;
        errno = 0;
        r = fcntl(fd, F_SETLK, &lk);
        check(r == 0, "F_SETLK UNLCK all", r == 0 ? 0 : -errno, 0);
        close(fd);
    }
    unlink(path);

    printf("SqlIoProbe: %s (failures=%d)\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
