/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * SqlNormalProbe - drive the real SQLite amalgamation (Chromium's copy,
 * built with Chromium's AROS configuration: no pread, no WAL, no mmap)
 * through the sequence sql::Database uses for a freshly created profile
 * database, in both locking modes, and dump the file after every step.
 *
 * Chromium's profile stores that open with exclusive_locking=false
 * (History, Cookies, Reporting and NEL, Shared Dictionary) end up as
 * 24 zero bytes on T: and fail with SQLITE_NOTADB, while the EXCLUSIVE
 * ones are fine.  This probe replays that difference in a single task with
 * SQLite's own OS trace enabled, so the exact VFS call that damages the
 * file shows up.
 *
 *   SqlNormalProbe [DIR] [NORMAL|EXCLUSIVE|BOTH]     default /T BOTH
 *
 * Linked with -nix like Chromium, so DIR is a unix-style path (/T is T:).
 * An AmigaDOS "T:" would be made absolute by SQLite as "/System:/T:..."
 * and land in the SYS: root instead.
 *
 * Build: tools/build.sh (needs the amalgamation, see SQLITE_AMALGAMATION).
 */
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sqlite3.h"

extern int sqlite3OSTrace;

static int failures;

static void dump(const char *path, const char *when)
{
    struct stat st;
    unsigned char hdr[32];
    FILE *f;
    size_t n = 0;

    if (stat(path, &st) != 0) {
        printf("  [%s] stat(%s): errno %d\n", when, path, errno);
        return;
    }
    f = fopen(path, "rb");
    if (f) {
        n = fread(hdr, 1, sizeof hdr, f);
        fclose(f);
    }
    printf("  [%s] size=%ld head=", when, (long)st.st_size);
    for (size_t i = 0; i < n; i++)
        printf("%02x", hdr[i]);
    printf("%s\n", n >= 16 && memcmp(hdr, "SQLite format 3", 16) == 0 ? " (sqlite)" : "");
    fflush(stdout);
}

static int exec(sqlite3 *db, const char *sql, const char *path, int must)
{
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    printf("  %-48s -> %d%s%s", sql, rc, err ? " " : "", err ? err : "");
    if (rc != SQLITE_OK)
        printf(" [xerr=%d os_errno=%d]", sqlite3_extended_errcode(db), sqlite3_system_errno(db));
    printf("\n");
    if (err) sqlite3_free(err);
    if (rc != SQLITE_OK && must) failures++;
    fflush(stdout);
    dump(path, "after");
    return rc;
}

static int run(const char *dir, int exclusive)
{
    char path[512];
    sqlite3 *db = NULL;
    int rc;

    snprintf(path, sizeof path, "%s%sSqlNormalProbe-%s.db", dir,
             (dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/') ? "" : "/",
             exclusive ? "excl" : "normal");
    unlink(path);
    {
        char jpath[560];
        snprintf(jpath, sizeof jpath, "%s-journal", path);
        unlink(jpath);
    }
    printf("== %s locking: %s\n", exclusive ? "EXCLUSIVE" : "NORMAL", path);

    rc = sqlite3_open_v2(path, &db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                         SQLITE_OPEN_EXRESCODE | SQLITE_OPEN_PRIVATECACHE, NULL);
    printf("  sqlite3_open_v2 -> %d\n", rc);
    if (rc != SQLITE_OK) {
        int fd;
        printf("  errmsg: %s os_errno=%d\n", db ? sqlite3_errmsg(db) : "(no db)",
               db ? sqlite3_system_errno(db) : 0);
        /* what does the plain syscall say about the same path? */
        errno = 0;
        fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        printf("  open(%s, O_RDWR|O_CREAT|O_CLOEXEC) -> %d errno=%d\n", path, fd, errno);
        if (fd >= 0) { close(fd); unlink(path); }
        if (db) sqlite3_close(db);
        failures++;
        return 1;
    }
    dump(path, "opened");

    if (!exclusive)
        exec(db, "PRAGMA locking_mode=NORMAL", path, 1);

    /* sql::Database forces the header/schema parse right after open */
    rc = sqlite3_table_column_metadata(db, "main", "sqlite_schema", NULL,
                                       NULL, NULL, NULL, NULL, NULL);
    printf("  sqlite3_table_column_metadata -> %d (%s)\n", rc, sqlite3_errmsg(db));
    dump(path, "after metadata");

    exec(db, "PRAGMA page_size=4096", path, 0);
    exec(db, "PRAGMA journal_mode=TRUNCATE", path, 1);
    exec(db, "PRAGMA cache_size=500", path, 0);
    exec(db, "PRAGMA mmap_size=0", path, 0);
    exec(db, "PRAGMA auto_vacuum", path, 0);
    exec(db, "BEGIN TRANSACTION", path, 1);
    exec(db, "CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR)", path, 1);
    exec(db, "INSERT INTO meta VALUES('version','1')", path, 1);
    exec(db, "COMMIT", path, 1);
    exec(db, "SELECT value FROM meta WHERE key='version'", path, 1);
    exec(db, "BEGIN TRANSACTION", path, 1);
    exec(db, "CREATE TABLE urls(id INTEGER PRIMARY KEY AUTOINCREMENT, url LONGVARCHAR)", path, 1);
    exec(db, "INSERT INTO urls(url) VALUES('http://example.org/')", path, 1);
    exec(db, "COMMIT", path, 1);
    exec(db, "SELECT count(*) FROM urls", path, 1);
    exec(db, "PRAGMA integrity_check", path, 1);

    rc = sqlite3_close(db);
    printf("  sqlite3_close -> %d\n", rc);
    dump(path, "closed");

    /* reopen the way the next browser start would */
    rc = sqlite3_open_v2(path, &db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                         SQLITE_OPEN_EXRESCODE | SQLITE_OPEN_PRIVATECACHE, NULL);
    printf("  reopen -> %d\n", rc);
    if (rc == SQLITE_OK) {
        if (!exclusive)
            exec(db, "PRAGMA locking_mode=NORMAL", path, 1);
        exec(db, "SELECT count(*) FROM urls", path, 1);
        sqlite3_close(db);
    } else
        failures++;
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/T";
    const char *mode = argc > 2 ? argv[2] : "BOTH";

    sqlite3OSTrace = getenv("SQLITE_OSTRACE") ? atoi(getenv("SQLITE_OSTRACE")) : 1;
    sqlite3_initialize();
    printf("SqlNormalProbe: sqlite %s dir=%s mode=%s\n", sqlite3_libversion(), dir, mode);

    if (strcmp(mode, "NORMAL") == 0 || strcmp(mode, "BOTH") == 0)
        run(dir, 0);
    if (strcmp(mode, "EXCLUSIVE") == 0 || strcmp(mode, "BOTH") == 0)
        run(dir, 1);

    printf("SqlNormalProbe: failures=%d\n", failures);
    return failures ? 1 : 0;
}
