/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * DirProbe - exercise the posixc directory/file calls the way Skia's
 * SkOSFile::Iter + SkStream::MakeFromFile do, so a "0 fonts loaded"
 * result can be attributed to opendir/readdir/stat/fopen individually.
 *
 *   DirProbe [DIR] [SUFFIX]      default DIR=FONTS:TrueType SUFFIX=.ttf
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "FONTS:TrueType";
    const char *suffix = argc > 2 ? argv[2] : ".ttf";
    size_t sl = strlen(suffix);
    struct stat ds;
    DIR *d;
    struct dirent *e;
    int entries = 0, matched = 0, statok = 0, opened = 0;

    printf("DirProbe: dir=<%s> suffix=<%s>\n", dir, suffix);

    errno = 0;
    if (stat(dir, &ds) == 0)
        printf("stat(dir): mode=%o isdir=%d\n", (unsigned)ds.st_mode, S_ISDIR(ds.st_mode));
    else
        printf("stat(dir): FAILED errno=%d (%s)\n", errno, strerror(errno));

    errno = 0;
    d = opendir(dir);
    if (!d) {
        printf("opendir: FAILED errno=%d (%s)\n", errno, strerror(errno));
        return 10;
    }
    printf("opendir: ok\n");

    while ((e = readdir(d)) != NULL) {
        char path[1024];
        struct stat s;
        size_t nl = strlen(e->d_name);
        int m;

        entries++;
        m = nl >= sl && memcmp(suffix, e->d_name + nl - sl, sl) == 0;
        if (entries <= 5 || m)
            printf("  entry %d: <%s> d_type=%d match=%d\n", entries, e->d_name, (int)e->d_type, m);
        if (!m)
            continue;
        matched++;

        /* Skia: path + "/" + name unless path already ends in "/" or "\\" */
        snprintf(path, sizeof path, "%s%s%s", dir,
                 (dir[strlen(dir) - 1] == '/' || dir[strlen(dir) - 1] == '\\') ? "" : "/",
                 e->d_name);

        errno = 0;
        if (stat(path, &s) != 0) {
            printf("    stat(<%s>): FAILED errno=%d (%s)\n", path, errno, strerror(errno));
            continue;
        }
        statok++;
        if (s.st_mode & S_IFDIR) {
            printf("    stat(<%s>): reports DIRECTORY (mode=%o)\n", path, (unsigned)s.st_mode);
            continue;
        }

        errno = 0;
        FILE *f = fopen(path, "rb");
        if (!f) {
            printf("    fopen(<%s>): FAILED errno=%d (%s)\n", path, errno, strerror(errno));
            continue;
        }
        unsigned char hdr[4] = {0};
        size_t n = fread(hdr, 1, 4, f);
        long len = 0;
        if (fseek(f, 0, SEEK_END) == 0) len = ftell(f);
        fclose(f);
        opened++;
        if (opened <= 3)
            printf("    fopen(<%s>): ok size=%ld hdr=%02x%02x%02x%02x read=%zu\n",
                   path, len, hdr[0], hdr[1], hdr[2], hdr[3], n);
    }
    closedir(d);

    printf("DirProbe: entries=%d matched=%d statok=%d opened=%d\n",
           entries, matched, statok, opened);
    return 0;
}
