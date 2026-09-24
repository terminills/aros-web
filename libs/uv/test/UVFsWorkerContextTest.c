/*
 * UVFsWorkerContextTest -- compare main-task and libuv-worker DOS context.
 *
 * VS Code's CommonJS loader can synchronously read its installed sources,
 * while the AMD loader's asynchronous fs.readFile() reports ENOENT for the
 * same SYS: path.  Keep the path fixed to the staged hosted installation so
 * this probe discriminates path construction from worker Process context.
 */

#include <aros/debug.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <uv.h>

#define TLOG(...) do { printf("[UVFsContext] " __VA_ARGS__); printf("\n"); \
                       bug("[UVFsContext] " __VA_ARGS__); bug("\n"); } while (0)

static const char *const path =
    "System:Developer/VSCode/src/vs/code/electron-main/main.js";

static uv_work_t context_work;
static uv_fs_t async_open;
static int worker_dos_open;
static int worker_lock;
static int async_result = -9999;

static void log_context(const char *where)
{
    struct Task *task = FindTask(NULL);
    char cwd[512];

    cwd[0] = '\0';
    TLOG("%s task=%p type=%u process=%d cwd-ok=%d cwd='%s'",
         where,
         task,
         task->tc_Node.ln_Type,
         task->tc_Node.ln_Type == NT_PROCESS,
         GetCurrentDirName(cwd, sizeof(cwd)),
         cwd);
}

static void inspect_worker(uv_work_t *req)
{
    BPTR file;
    BPTR lock;

    (void)req;
    log_context("worker");

    lock = Lock(path, SHARED_LOCK);
    worker_lock = lock != BNULL;
    TLOG("worker DOS Lock path='%s' ok=%d ioerr=%ld",
         path, worker_lock, (long)IoErr());
    if (lock != BNULL)
        UnLock(lock);

    file = Open(path, MODE_OLDFILE);
    worker_dos_open = file != BNULL;
    TLOG("worker DOS Open path='%s' ok=%d ioerr=%ld",
         path, worker_dos_open, (long)IoErr());
    if (file != BNULL)
        Close(file);
}

static void after_inspect(uv_work_t *req, int status)
{
    (void)req;
    TLOG("worker inspection completion status=%d", status);
}

static void after_async_open(uv_fs_t *req)
{
    async_result = (int)req->result;
    TLOG("async uv_fs_open path='%s' result=%d errno=%d ioerr=%ld",
         path, async_result, errno, (long)IoErr());
    if (req->result >= 0)
        uv_fs_close(req->loop, req, (int)req->result, NULL);
    uv_fs_req_cleanup(req);
}

int main(void)
{
    uv_loop_t loop;
    uv_fs_t sync_open;
    int sync_result;
    int queue_result;

    TLOG("libuv %s worker filesystem context probe", uv_version_string());
    log_context("main");

    if (uv_loop_init(&loop) != 0) {
        TLOG("FAIL: uv_loop_init");
        return 20;
    }

    sync_result =
        uv_fs_open(&loop, &sync_open, path, O_RDONLY, 0, NULL);
    TLOG("sync uv_fs_open path='%s' call=%d result=%d errno=%d ioerr=%ld",
         path, sync_result, (int)sync_open.result, errno, (long)IoErr());
    if (sync_open.result >= 0)
        uv_fs_close(&loop, &sync_open, (int)sync_open.result, NULL);
    uv_fs_req_cleanup(&sync_open);

    queue_result = uv_queue_work(&loop, &context_work,
                                 inspect_worker, after_inspect);
    TLOG("queue worker inspection result=%d", queue_result);
    if (queue_result != 0)
        return 20;

    queue_result =
        uv_fs_open(&loop, &async_open, path, O_RDONLY, 0, after_async_open);
    TLOG("queue async uv_fs_open result=%d", queue_result);
    if (queue_result != 0)
        return 20;

    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    if (sync_result >= 0 && worker_lock && worker_dos_open &&
        async_result >= 0) {
        TLOG("PASS: main and worker filesystem contexts agree");
        return 0;
    }

    TLOG("FAIL: sync=%d worker_lock=%d worker_open=%d async=%d",
         sync_result, worker_lock, worker_dos_open, async_result);
    return 20;
}
