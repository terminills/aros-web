/*
 * UVFsPollTest -- async filesystem completion while polling never blocks.
 *
 * An active idle handle forces uv__io_poll(loop, 0).  Sequential filesystem
 * requests must each complete on the threadpool and wake/drain the loop.  npm
 * config initialization exposed a repeat-use failure where the fourth request
 * remained queued forever after three successful completions.
 */

#include <aros/debug.h>
#include <fcntl.h>
#include <stdio.h>

#include <uv.h>

#define TLOG(...) do { printf("[UVFsPoll] " __VA_ARGS__); printf("\n"); \
                       bug("[UVFsPoll] " __VA_ARGS__); bug("\n"); } while (0)

static uv_idle_t idle_handle;
static uv_fs_t request;
static unsigned long idle_ticks;
static unsigned int completed;
static int async_result;
static int queue_result;

static const char *const paths[] = {
    "C:Node",
    "SYS:npm-missing-builtin-npmrc",
    "System:/package.json",
    "System:/node_modules"
};

static void queue_next(uv_loop_t *loop);

static void on_idle(uv_idle_t *handle)
{
    idle_ticks++;
    if (idle_ticks == 100000000UL)
    {
        TLOG("FAIL: async completion starved during zero-timeout polling");
        uv_idle_stop(handle);
    }
}

static void on_stat(uv_fs_t *req)
{
    async_result = (int)req->result;
    TLOG("async stat %u path=%s result=%d idle_ticks=%lu",
         completed + 1, paths[completed], async_result, idle_ticks);
    uv_fs_req_cleanup(req);
    completed++;

    if (completed == sizeof(paths) / sizeof(paths[0]))
    {
        uv_idle_stop(&idle_handle);
        return;
    }

    queue_next(req->loop);
}

static void queue_next(uv_loop_t *loop)
{
    TLOG("queue stat %u path=%s", completed + 1, paths[completed]);
    queue_result = uv_fs_stat(loop, &request, paths[completed], on_stat);
    TLOG("queued stat %u result=%d", completed + 1, queue_result);
    if (queue_result != 0)
    {
        TLOG("FAIL: queue stat %u path=%s result=%d",
             completed + 1, paths[completed], queue_result);
        uv_idle_stop(&idle_handle);
    }
}

int main(void)
{
    uv_loop_t loop;
    int result;

    TLOG("start sequential filesystem completion regression");
    result = uv_loop_init(&loop);
    if (result != 0)
    {
        TLOG("FAIL: loop init result=%d", result);
        return 20;
    }

    uv_idle_init(&loop, &idle_handle);
    uv_idle_start(&idle_handle, on_idle);
    queue_next(&loop);

    uv_run(&loop, UV_RUN_DEFAULT);
    uv_close((uv_handle_t *)&idle_handle, NULL);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    if (completed == sizeof(paths) / sizeof(paths[0]))
    {
        TLOG("PASS: zero-timeout poll drained %u sequential filesystem completions",
             completed);
        return 0;
    }

    TLOG("FAIL: completed=%u queue_result=%d async_result=%d idle_ticks=%lu",
         completed, queue_result, async_result, idle_ticks);
    return 20;
}
