/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * ErrnoProbe - check that errno is per-thread in an absolutely linked
 * pthread program (the way Chromium is linked).
 *
 * Chromium's LevelDB env reported "GetChildren::3" (FILE_ERROR_EXISTS, i.e.
 * EEXIST) for an empty, existing directory: its FileEnumerator reads errno
 * after readdir() returns NULL, and the EEXIST came from another thread's
 * mkdir() of an existing directory.  Every thread shared the main Task's
 * StdCBase->_errno.
 *
 *   ErrnoProbe [DIR] [ROUNDS]            default /T 200
 *
 * Section 1: N workers each pin a distinct errno value, sleep, and check it
 *            survived while the others were pinning theirs.  Also reports
 *            whether &errno differs per thread.
 * Section 2: workers hammer mkdir(DIR/ErrnoProbe-dir) (EEXIST) while the
 *            main thread runs opendir/readdir over DIR/ErrnoProbe-dir and
 *            checks errno stays 0 at end-of-directory, as
 *            base::FileEnumerator does.
 *
 * Linked with -nix like Chromium ("/T" is T:).  Prints ok/FAIL per check
 * and a final "ErrnoProbe: failures=N".
 */
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NWORKERS 6

static int failures;
static int rounds = 200;
static char dirpath[512];
static volatile int stop_hammer;

static void check(int ok, const char *what)
{
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
    fflush(stdout);
}

/* ---- section 1: pinned values survive other threads' assignments ---- */

struct pin_arg
{
    int id;
    int survived;
    int *addr;
};

static void *pin_thread(void *p)
{
    struct pin_arg *a = p;
    int i;

    a->addr = &errno;
    a->survived = 1;
    for (i = 0; i < rounds; i++)
    {
        errno = 1000 + a->id;
        usleep(1000);
        if (errno != 1000 + a->id)
            a->survived = 0;
        /* a real error too, not just an assignment */
        if (mkdir(dirpath, 0777) == 0 || errno != EEXIST)
            a->survived = 0;
    }
    return NULL;
}

static void section_pin(void)
{
    pthread_t th[NWORKERS];
    struct pin_arg arg[NWORKERS];
    int i, j, distinct = 1, all = 1;
    int *mainaddr = &errno;

    printf("== 1: pinned errno values across %d threads\n", NWORKERS);
    errno = 7;
    for (i = 0; i < NWORKERS; i++)
    {
        arg[i].id = i;
        arg[i].survived = 0;
        arg[i].addr = NULL;
        pthread_create(&th[i], NULL, pin_thread, &arg[i]);
    }
    for (i = 0; i < NWORKERS; i++)
        pthread_join(th[i], NULL);

    for (i = 0; i < NWORKERS; i++)
    {
        all &= arg[i].survived;
        if (arg[i].addr == mainaddr)
            distinct = 0;
        for (j = 0; j < i; j++)
            if (arg[i].addr == arg[j].addr)
                distinct = 0;
    }
    printf("  main &errno=%p", mainaddr);
    for (i = 0; i < NWORKERS; i++)
        printf(" t%d=%p", i, arg[i].addr);
    printf("\n");
    check(distinct, "&errno distinct per thread");
    check(all, "each thread keeps its own errno value");
    check(errno == 7, "main errno untouched by workers");
}

/* ---- section 2: readdir end-of-directory errno vs concurrent EEXIST ---- */

static void *hammer_thread(void *p)
{
    (void)p;
    while (!stop_hammer)
    {
        if (mkdir(dirpath, 0777) == 0)
            break;   /* should never happen, dir exists */
        usleep(200);
    }
    return NULL;
}

static void section_readdir(void)
{
    pthread_t th[NWORKERS];
    int i, r, bad_opendir = 0, bad_eod = 0, entries = 0;
    char sub[600];

    printf("== 2: readdir end-of-directory errno under concurrent mkdir EEXIST\n");
    snprintf(sub, sizeof sub, "%s/entry", dirpath);
    {
        FILE *f = fopen(sub, "w");
        if (f) fclose(f);
    }

    stop_hammer = 0;
    for (i = 0; i < NWORKERS; i++)
        pthread_create(&th[i], NULL, hammer_thread, NULL);

    for (r = 0; r < rounds; r++)
    {
        DIR *d;
        struct dirent *de;
        int seen = 0;

        errno = 0;
        d = opendir(dirpath);
        if (!d)
        {
            if (!bad_opendir)
                printf("  opendir failed: errno %d\n", errno);
            bad_opendir++;
            continue;
        }
        for (;;)
        {
            errno = 0;
            de = readdir(d);
            if (!de)
                break;
            if (strcmp(de->d_name, "entry") == 0)
                seen = 1;
        }
        if (errno != 0)
        {
            if (!bad_eod)
                printf("  round %d: errno %d after readdir() NULL\n", r, errno);
            bad_eod++;
        }
        closedir(d);
        entries += seen;
    }

    stop_hammer = 1;
    for (i = 0; i < NWORKERS; i++)
        pthread_join(th[i], NULL);

    printf("  rounds=%d opendir-fail=%d eod-errno=%d entry-seen=%d\n",
           rounds, bad_opendir, bad_eod, entries);
    check(bad_opendir == 0, "opendir succeeds every round");
    check(bad_eod == 0, "errno is 0 at end of directory every round");
    check(entries == rounds, "the entry is listed every round");
    unlink(sub);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/T";

    if (argc > 2)
        rounds = atoi(argv[2]);
    snprintf(dirpath, sizeof dirpath, "%s%sErrnoProbe-dir", dir,
             dir[strlen(dir) - 1] == '/' ? "" : "/");
    printf("ErrnoProbe: dir=%s rounds=%d\n", dirpath, rounds);

    if (mkdir(dirpath, 0777) != 0 && errno != EEXIST)
    {
        printf("mkdir(%s): errno %d\n", dirpath, errno);
        return 1;
    }
    check(mkdir(dirpath, 0777) != 0 && errno == EEXIST, "mkdir of existing dir gives EEXIST");

    section_pin();
    section_readdir();

    rmdir(dirpath);
    printf("ErrnoProbe: failures=%d\n", failures);
    return failures ? 1 : 0;
}
