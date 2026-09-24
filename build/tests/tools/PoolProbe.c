/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * PoolProbe - exercise posixc's descriptor/stdio bookkeeping across pthread
 * worker exit, the way Chromium does when its in-process utility threads shut
 * down.  A worker Task gets its own per-task posixc base whose descriptor
 * table is routed to the creator; this drives open()/fopen()/mkstemp() from
 * workers (including a worker spawned by a worker), lets them exit, and then
 * keeps allocating from the creator's table so a corrupted pool free list
 * trips the kernel's TLSF validator ("[Kernel:TLSF] free-list corruption").
 *
 *   PoolProbe [DIR] [ROUNDS]     default DIR=T:, ROUNDS=3
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *dir = "T:";
static int failures;

static void path(char *buf, size_t len, const char *name)
{
    size_t n = strlen(dir);
    const char *sep = (n && (dir[n - 1] == ':' || dir[n - 1] == '/')) ? "" : "/";
    snprintf(buf, len, "%s%s%s", dir, sep, name);
}

static void fail(const char *what)
{
    printf("  FAIL %s errno=%d (%s)\n", what, errno, strerror(errno));
    failures++;
}

/* The mix ImportantFileWriter / SQLite / ReadFileToString put through the
   descriptor table. */
static void churn(const char *tag, int n)
{
    char p[512], tmpl[512];
    int i;

    for (i = 0; i < n; i++) {
        int fd;
        FILE *f;

        path(tmpl, sizeof tmpl, "PoolProbe-XXXXXX");
        fd = mkstemp(tmpl);
        if (fd < 0) { fail("mkstemp"); continue; }
        if (write(fd, tag, strlen(tag)) < 0) fail("write");
        close(fd);
        unlink(tmpl);

        snprintf(p, sizeof p, "%s-%d", tag, i);
        path(tmpl, sizeof tmpl, p);
        f = fopen(tmpl, "w");
        if (!f) { fail("fopen"); continue; }
        fputs(tag, f);
        fclose(f);
        fd = open(tmpl, O_RDONLY);
        if (fd < 0) { fail("open"); continue; }
        close(fd);
        unlink(tmpl);
    }
}

static void *worker(void *arg)
{
    const char *tag = arg;
    struct stat st;

    churn(tag, 8);
    /* stat/fstat go through the per-task path buffers too */
    stat(dir, &st);
    return NULL;
}

static int regular_fd = -1;     /* a held regular file, for comparison */

/* One line per descriptor: does fstat() work, and do the cheaper
   descriptor-table calls (fcntl/lseek) still see it?  Separates "the fdesc
   is gone" from "the stat path is broken". */
static void fd_report(const char *who, int fd)
{
    struct stat st;
    int e_stat = 0, e_fl = 0, e_seek = 0, fl;
    off_t pos;

    errno = 0;
    if (fstat(fd, &st) != 0) e_stat = errno;
    errno = 0;
    fl = fcntl(fd, F_GETFL);
    if (fl < 0) e_fl = errno;
    errno = 0;
    pos = lseek(fd, 0, SEEK_CUR);
    if (pos < 0) e_seek = errno;
    printf("    %s fd %d: fstat=%s(%d) F_GETFL=%s(%d) lseek=%s(%d)\n", who, fd,
           e_stat ? "FAIL" : "ok", e_stat, e_fl ? "FAIL" : "ok", e_fl,
           e_seek ? "FAIL" : "ok", e_seek);
    fflush(stdout);
}

static void *idle_worker(void *arg)
{
    (void)arg;
    /* What the worker sees through its own base (routed to the creator) */
    fd_report("worker", 0);
    fd_report("worker", 1);
    if (regular_fd >= 0)
        fd_report("worker", regular_fd);
    return NULL;
}

static void std_check(const char *when)
{
    struct stat st;
    int i, bad = 0;

    for (i = 0; i <= 2; i++)
        if (fstat(i, &st) != 0)
            bad |= 1 << i;
    printf("  %s: std fds %s%s\n", when, bad ? "BROKEN mask=" : "ok",
           bad ? (bad == 1 ? "1" : bad == 2 ? "2" : bad == 4 ? "4" : "multi") : "");
    fflush(stdout);
    for (i = 0; i <= 2; i++)
        fd_report("main", i);
    if (regular_fd >= 0)
        fd_report("main", regular_fd);
}

static void *spawner(void *arg)
{
    pthread_t child;

    churn("spawner", 4);
    if (pthread_create(&child, NULL, worker, "grandchild") == 0)
        pthread_join(child, NULL);
    else
        fail("pthread_create(grandchild)");
    churn("spawner-after", 4);
    return NULL;
}

int main(int argc, char **argv)
{
    int rounds = argc > 2 ? atoi(argv[2]) : 3;
    int held[24];
    int i, r;
    char p[512], name[64];

    if (argc > 1)
        dir = argv[1];
    printf("PoolProbe: dir=<%s> rounds=%d\n", dir, rounds);

    /* Grow the creator's descriptor table a few times before any worker
       exists, like a browser that has its profile files open. */
    for (i = 0; i < 24; i++) {
        snprintf(name, sizeof name, "PoolProbe-held-%d", i);
        path(p, sizeof p, name);
        held[i] = open(p, O_RDWR | O_CREAT, 0644);
        if (held[i] < 0) fail("open(held)");
    }
    regular_fd = held[0];

    std_check("before any worker");
    {
        /* Isolate worker *exit* from worker file activity: idle workers
           touch nothing but their own CRT base. */
        pthread_t idle;
        for (i = 0; i < 3; i++) {
            printf("  idle worker %d\n", i); fflush(stdout);
            if (pthread_create(&idle, NULL, idle_worker, NULL) != 0) fail("pthread_create(idle)");
            else pthread_join(idle, NULL);
            std_check("after idle worker");
        }
    }

    for (r = 0; r < rounds; r++) {
        pthread_t a, b;

        printf("  round %d: workers start\n", r); fflush(stdout);
        if (pthread_create(&a, NULL, worker, "worker-a") != 0) fail("pthread_create(a)");
        else pthread_join(a, NULL);
        if (pthread_create(&b, NULL, spawner, NULL) != 0) fail("pthread_create(b)");
        else pthread_join(b, NULL);
        std_check("after round workers");
        printf("  round %d: workers exited, creator churns\n", r); fflush(stdout);
        churn("main", 64);
    }

    for (i = 0; i < 24; i++) {
        if (held[i] >= 0) close(held[i]);
        snprintf(name, sizeof name, "PoolProbe-held-%d", i);
        path(p, sizeof p, name);
        unlink(p);
    }
    churn("main-final", 128);

    printf("PoolProbe: %s (failures=%d)\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
