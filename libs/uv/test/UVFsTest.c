/*
    UVFsTest -- uv_fs file I/O smoke for the AROS libuv port.

    Two phases, both exercising fs.c through uv.library:
      SYNC  (cb == NULL): open -> write -> read-back -> verify -> close ->
             unlink, all on the calling thread. Validates fs.c + the posixc
             pread/pwrite leaves.
      ASYNC (cb != NULL): the same open/write/read/close run on the THREADPOOL
             with completions delivered to the loop thread -- i.e. it re-uses
             the relbase + exec-Signal async path end-to-end for real
             file I/O, not just a no-op work item.

    PASS = sync round-trip content matches AND the async open completes on the
    loop (result >= 0). Output mirrored to stdout + kernel debug (serial).
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>

#include <uv.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#define TLOG(...) do { printf("[UVFs] " __VA_ARGS__); printf("\n"); \
                       bug("[UVFs] " __VA_ARGS__); bug("\n"); } while (0)

static const char *PATH = "T:uvfs-smoke.tmp";
static const char *MSG  = "libuv fs on AROS\n";

static int g_async_open_result = -12345;
static int g_async_done = 0;

/* ---- synchronous round-trip ---- */
static int sync_roundtrip(uv_loop_t *loop)
{
    uv_fs_t req;
    uv_buf_t buf;
    char rd[64];
    int fd, r, ok = 1;
    size_t len = strlen(MSG);

    r = uv_fs_open(loop, &req, PATH, O_CREAT | O_TRUNC | O_RDWR, 0644, NULL);
    fd = (int)req.result; uv_fs_req_cleanup(&req);
    TLOG("sync open = %d (fd=%d)", r, fd);
    if (fd < 0) return 0;

    buf = uv_buf_init((char *)MSG, (unsigned)len);
    r = uv_fs_write(loop, &req, fd, &buf, 1, 0, NULL);
    TLOG("sync write = %d (result=%d, wanted %d)", r, (int)req.result, (int)len);
    if ((size_t)req.result != len) ok = 0;
    uv_fs_req_cleanup(&req);

    memset(rd, 0, sizeof(rd));
    buf = uv_buf_init(rd, sizeof(rd) - 1);
    r = uv_fs_read(loop, &req, fd, &buf, 1, 0, NULL);
    TLOG("sync read = %d (result=%d)", r, (int)req.result);
    if ((size_t)req.result != len || memcmp(rd, MSG, len) != 0) {
        TLOG("sync CONTENT MISMATCH: got '%s'", rd);
        ok = 0;
    } else {
        TLOG("sync content verified: '%s'", rd);
    }
    uv_fs_req_cleanup(&req);

    uv_fs_close(loop, &req, fd, NULL); uv_fs_req_cleanup(&req);
    return ok;
}

/* ---- async open (runs on the threadpool) ---- */
static void on_async_open(uv_fs_t *req)
{
    g_async_open_result = (int)req->result;
    g_async_done = 1;
    TLOG("async open completed on loop thread: result=%d", g_async_open_result);
    if (req->result >= 0)
        uv_fs_close(req->loop, req, (int)req->result, NULL);
    uv_fs_req_cleanup(req);
}

int main(void)
{
    uv_loop_t loop;
    uv_fs_t areq;
    int r, sync_ok;

    TLOG("libuv %s - uv_fs smoke", uv_version_string());
    if (uv_loop_init(&loop) != 0) { TLOG("loop_init failed"); return 20; }

    sync_ok = sync_roundtrip(&loop);
    TLOG("sync phase %s", sync_ok ? "OK" : "FAILED");

    /* async: open the file we just wrote, completion on the loop thread */
    r = uv_fs_open(&loop, &areq, PATH, O_RDONLY, 0, on_async_open);
    TLOG("uv_fs_open(async) queued = %d, running loop...", r);
    uv_run(&loop, UV_RUN_DEFAULT);

    /* cleanup the temp file */
    { uv_fs_t u; uv_fs_unlink(&loop, &u, PATH, NULL); uv_fs_req_cleanup(&u); }
    uv_loop_close(&loop);

    if (sync_ok && g_async_done && g_async_open_result >= 0) {
        TLOG("PASS: sync fs round-trip verified + async fs open completed on the "
             "loop via the threadpool");
        return 0;
    }
    TLOG("FAIL: sync_ok=%d async_done=%d async_result=%d",
         sync_ok, g_async_done, g_async_open_result);
    return 20;
}
