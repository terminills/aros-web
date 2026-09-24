/*
    UVFsWatchFileTest -- real uv_fs_poll support for Node fs.watchFile().

    Starts polling an existing file, mutates its size from a timer, and
    requires libuv's changed-stat callback before a bounded watchdog expires.
    This exercises the same timer + asynchronous uv_fs_stat path used by
    Node's StatWatcher rather than treating filesystem watching as absent.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>

#include <uv.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define TLOG(...) do { printf("[UVFsWatchFile] " __VA_ARGS__); printf("\n"); \
                       bug("[UVFsWatchFile] " __VA_ARGS__); bug("\n"); } while (0)

static const char *PATH = "T:uv-fs-poll-watch.tmp";
static uv_loop_t loop;
static uv_fs_poll_t watcher;
static uv_timer_t mutation_timer;
static uv_timer_t watchdog_timer;
static int changed;
static int failed;

static void close_if_open(uv_handle_t *handle)
{
    if (!uv_is_closing(handle))
        uv_close(handle, NULL);
}

static int write_contents(const char *contents)
{
    uv_fs_t req;
    uv_buf_t buffer;
    int fd;
    int result;
    size_t length = strlen(contents);

    fd = uv_fs_open(&loop, &req, PATH, O_CREAT | O_TRUNC | O_WRONLY,
                    0644, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0)
        return fd;

    buffer = uv_buf_init((char *)contents, (unsigned int)length);
    result = uv_fs_write(&loop, &req, fd, &buffer, 1, 0, NULL);
    uv_fs_req_cleanup(&req);

    uv_fs_close(&loop, &req, fd, NULL);
    uv_fs_req_cleanup(&req);

    if (result < 0)
        return result;
    return (size_t)result == length ? 0 : UV_EIO;
}

static void on_mutation(uv_timer_t *timer)
{
    int result = write_contents("changed-content-is-longer\n");

    TLOG("mutation write = %d", result);
    close_if_open((uv_handle_t *)timer);
    if (result != 0) {
        failed = 1;
        uv_fs_poll_stop(&watcher);
        close_if_open((uv_handle_t *)&watcher);
        uv_timer_stop(&watchdog_timer);
        close_if_open((uv_handle_t *)&watchdog_timer);
    }
}

static void on_change(uv_fs_poll_t *handle,
                      int status,
                      const uv_stat_t *previous,
                      const uv_stat_t *current)
{
    TLOG("poll callback status=%d previous_size=%lld current_size=%lld",
         status,
         (long long)previous->st_size,
         (long long)current->st_size);

    if (status == 0 && previous->st_size != current->st_size)
        changed = 1;
    else
        failed = 1;

    uv_fs_poll_stop(handle);
    close_if_open((uv_handle_t *)handle);
    uv_timer_stop(&watchdog_timer);
    close_if_open((uv_handle_t *)&watchdog_timer);
}

static void on_watchdog(uv_timer_t *timer)
{
    TLOG("watchdog expired before changed-stat callback");
    failed = 1;
    uv_fs_poll_stop(&watcher);
    close_if_open((uv_handle_t *)&watcher);
    close_if_open((uv_handle_t *)timer);
}

int main(void)
{
    uv_fs_t req;
    char path[128];
    size_t path_size = sizeof(path);
    int result;

    TLOG("libuv %s - real uv_fs_poll smoke", uv_version_string());

    result = uv_loop_init(&loop);
    if (result != 0) {
        TLOG("uv_loop_init failed: %d", result);
        return 20;
    }

    result = write_contents("a\n");
    TLOG("initial write = %d", result);
    if (result != 0)
        return 20;

    result = uv_fs_poll_init(&loop, &watcher);
    TLOG("uv_fs_poll_init = %d", result);
    if (result != 0)
        return 20;

    result = uv_fs_poll_start(&watcher, on_change, PATH, 20);
    TLOG("uv_fs_poll_start = %d", result);
    if (result != 0)
        return 20;

    result = uv_fs_poll_getpath(&watcher, path, &path_size);
    TLOG("uv_fs_poll_getpath = %d path='%s' size=%lu",
         result, result == 0 ? path : "", (unsigned long)path_size);
    if (result != 0 || strcmp(path, PATH) != 0)
        failed = 1;

    uv_timer_init(&loop, &mutation_timer);
    uv_timer_start(&mutation_timer, on_mutation, 100, 0);
    uv_timer_init(&loop, &watchdog_timer);
    uv_timer_start(&watchdog_timer, on_watchdog, 3000, 0);

    result = uv_run(&loop, UV_RUN_DEFAULT);
    TLOG("uv_run returned %d changed=%d failed=%d", result, changed, failed);

    uv_fs_unlink(&loop, &req, PATH, NULL);
    uv_fs_req_cleanup(&req);

    result = uv_loop_close(&loop);
    TLOG("uv_loop_close = %d", result);
    if (result != 0)
        failed = 1;

    if (changed && !failed) {
        TLOG("PASS: real filesystem mutation delivered by uv_fs_poll");
        return 0;
    }

    TLOG("FAIL: changed=%d failed=%d", changed, failed);
    return 20;
}
