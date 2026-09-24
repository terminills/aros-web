/*
    UVWorkTest -- threadpool + async-completion smoke for the AROS libuv port.

    uv_timer proved the loop sleeps and wakes on time, but it only touches the
    timer.device path. THIS test exercises the mechanism the whole async runtime
    rides on:

        uv_queue_work -> work_cb runs on a THREADPOOL WORKER (pthread)
                      -> completion signals the loop task (exec Signal, see
                         src-aros/aros-async.c uv_async_send on loop->wq_async)
                      -> uv__io_poll drains it (src-aros/aros.c)
                      -> after_work_cb runs back ON THE LOOP THREAD

    That is the same path uv_fs_*, getaddrinfo and every threadpool consumer
    uses. PASS = every work item ran AND every completion was delivered back.

    Note the main thread is blocked inside uv_run() while work_cb executes, so
    work_cb necessarily runs on another thread -- real threading, not inline.

    Output is mirrored to stdout and the kernel debug log (serial) so it is
    observable headlessly.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>

#include <uv.h>
#include <stdio.h>

#define TLOG(...) do { printf("[UVWork] " __VA_ARGS__); printf("\n"); \
                       bug("[UVWork] " __VA_ARGS__); bug("\n"); } while (0)

#define NWORK 4

static volatile int g_work_ran  = 0;   /* incremented on worker threads */
static int          g_after_ran = 0;   /* incremented on the loop thread */
static uv_work_t    g_reqs[NWORK];

static void work_cb(uv_work_t *req)
{
    /* Runs on a libuv threadpool worker (pthread), NOT the loop thread. */
    __sync_fetch_and_add(&g_work_ran, 1);
    (void)req;
}

static void after_work_cb(uv_work_t *req, int status)
{
    /* Runs back on the loop thread, delivered via the exec-Signal async. */
    int idx = (int)(long)req->data;
    g_after_ran++;
    TLOG("after_work req %d status %d (work_ran=%d after_ran=%d)",
         idx, status, g_work_ran, g_after_ran);
}

int main(void)
{
    uv_loop_t loop;
    int i, r;

    TLOG("libuv %s - uv_queue_work threadpool/async test", uv_version_string());

    r = uv_loop_init(&loop);
    TLOG("uv_loop_init = %d", r);
    if (r != 0)
        return 20;

    for (i = 0; i < NWORK; i++) {
        g_reqs[i].data = (void *)(long)i;
        r = uv_queue_work(&loop, &g_reqs[i], work_cb, after_work_cb);
        TLOG("uv_queue_work[%d] = %d", i, r);
        if (r != 0)
            return 20;
    }

    TLOG("running loop (expect %d work + %d completions)...", NWORK, NWORK);
    r = uv_run(&loop, UV_RUN_DEFAULT);
    TLOG("uv_run returned %d (work_ran=%d after_ran=%d)",
         r, g_work_ran, g_after_ran);

    uv_loop_close(&loop);

    if (g_work_ran == NWORK && g_after_ran == NWORK) {
        TLOG("PASS: %d work items ran on the threadpool and all %d completions "
             "were delivered back to the loop thread", NWORK, NWORK);
        return 0;
    }

    TLOG("FAIL: work_ran=%d after_ran=%d (expected %d each)",
         g_work_ran, g_after_ran, NWORK);
    return 20;
}
