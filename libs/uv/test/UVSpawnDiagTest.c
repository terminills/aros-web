/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    UVSpawnDiagTest: UVProcessLifecycleTest with every step's return value
    printed, plus node's stdio shape (READABLE|WRITABLE on every 'pipe'
    slot) as a second run.  For bringing up child_process on AROS: the
    lifecycle test only says which call failed, not why.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

static int close_count;
static int exit_count;
static int64_t exit_status = -1;
static char output[1024];
static size_t output_length;

static void on_close(uv_handle_t *handle)
{
    (void)handle;
    close_count++;
    printf("  close_cb #%d\n", close_count);
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    (void)handle;
    (void)suggested;
    buf->base = malloc(256);
    buf->len = buf->base != NULL ? 256 : 0;
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    printf("  read_cb fd=%d nread=%ld\n", stream->io_watcher.fd, (long)nread);
    if (nread > 0) {
        size_t room = sizeof(output) - output_length - 1;
        size_t copy = (size_t)nread < room ? (size_t)nread : room;
        memcpy(output + output_length, buf->base, copy);
        output_length += copy;
        output[output_length] = '\0';
    }

    free(buf->base);

    if (nread < 0) {
        printf("  read_cb: %s -> close\n", uv_strerror((int)nread));
        uv_close((uv_handle_t *)stream, on_close);
    }
}

static void on_process_exit(uv_process_t *process, int64_t status, int signal)
{
    (void)signal;
    exit_count++;
    exit_status = status;
    printf("  exit_cb status=%lld\n", (long long)status);
    uv_close((uv_handle_t *)process, on_close);
}

static int run_shape(const char *label, unsigned in_flags, unsigned out_flags)
{
    uv_loop_t loop;
    uv_process_t process;
    uv_pipe_t child_stdin;
    uv_pipe_t child_stdout;
    uv_pipe_t child_stderr;
    uv_process_options_t options;
    uv_stdio_container_t stdio[3];
    char *args[] = { (char *)"C:Echo", (char *)"uv process probe", NULL };
    int rc;

    printf("== %s\n", label);
    close_count = exit_count = 0;
    output_length = 0;
    output[0] = '\0';

    memset(&process, 0, sizeof(process));
    memset(&options, 0, sizeof(options));
    memset(stdio, 0, sizeof(stdio));

    rc = uv_loop_init(&loop);
    printf("  uv_loop_init=%d\n", rc);
    if (rc != 0)
        return 1;

    rc = uv_pipe_init(&loop, &child_stdin, 0);
    rc |= uv_pipe_init(&loop, &child_stdout, 0);
    rc |= uv_pipe_init(&loop, &child_stderr, 0);
    printf("  uv_pipe_init=%d\n", rc);

    if (in_flags) {
        stdio[0].flags = in_flags;
        stdio[0].data.stream = (uv_stream_t *)&child_stdin;
    } else {
        stdio[0].flags = UV_INHERIT_FD;
        stdio[0].data.fd = 0;
    }
    stdio[1].flags = out_flags;
    stdio[1].data.stream = (uv_stream_t *)&child_stdout;
    stdio[2].flags = out_flags;
    stdio[2].data.stream = (uv_stream_t *)&child_stderr;

    options.exit_cb = on_process_exit;
    options.file = args[0];
    options.args = args;
    options.stdio_count = 3;
    options.stdio = stdio;

    rc = uv_spawn(&loop, &process, &options);
    printf("  uv_spawn=%d (%s) pid=%d\n", rc, rc ? uv_strerror(rc) : "ok", (int)process.pid);
    if (rc != 0)
        return 1;
    printf("  stdout fd=%d flags=0x%x  stderr fd=%d flags=0x%x\n",
           child_stdout.io_watcher.fd,
           child_stdout.flags,
           child_stderr.io_watcher.fd, child_stderr.flags);

    rc = uv_read_start((uv_stream_t *)&child_stdout, alloc_buffer, on_read);
    printf("  uv_read_start(stdout)=%d (%s)\n", rc, rc ? uv_strerror(rc) : "ok");
    rc = uv_read_start((uv_stream_t *)&child_stderr, alloc_buffer, on_read);
    printf("  uv_read_start(stderr)=%d (%s)\n", rc, rc ? uv_strerror(rc) : "ok");
    if (in_flags)
        uv_close((uv_handle_t *)&child_stdin, on_close);

    rc = uv_run(&loop, UV_RUN_DEFAULT);
    printf("  uv_run=%d exit_count=%d status=%lld close_count=%d output='%s'\n",
           rc, exit_count, (long long)exit_status, close_count, output);

    rc = uv_loop_close(&loop);
    printf("  uv_loop_close=%d (%s)\n", rc, rc ? uv_strerror(rc) : "ok");
    return 0;
}

int main(void)
{
    run_shape("codex shape: stdin inherit, out WRITABLE_PIPE",
              0, UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    run_shape("node shape: every slot READABLE|WRITABLE",
              UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE,
              UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
    printf("UVSPAWNDIAG_DONE\n");
    return 0;
}
