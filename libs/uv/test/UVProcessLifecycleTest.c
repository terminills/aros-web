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
    if (nread > 0) {
        size_t room = sizeof(output) - output_length - 1;
        size_t copy = (size_t)nread < room ? (size_t)nread : room;
        memcpy(output + output_length, buf->base, copy);
        output_length += copy;
        output[output_length] = '\0';
    }

    free(buf->base);

    if (nread < 0)
        uv_close((uv_handle_t *)stream, on_close);
}

static void on_process_exit(uv_process_t *process, int64_t status, int signal)
{
    (void)signal;
    exit_count++;
    exit_status = status;
    uv_close((uv_handle_t *)process, on_close);
}

int main(void)
{
    uv_loop_t loop;
    uv_process_t process;
    uv_pipe_t child_stdout;
    uv_pipe_t child_stderr;
    uv_process_options_t options;
    uv_stdio_container_t stdio[3];
    char *args[] = { (char *)"C:Echo", (char *)"uv process probe", NULL };
    int rc;

    memset(&process, 0, sizeof(process));
    memset(&options, 0, sizeof(options));
    memset(stdio, 0, sizeof(stdio));

    rc = uv_loop_init(&loop);
    if (rc != 0) {
        fprintf(stderr, "FAIL: uv_loop_init: %s\n", uv_strerror(rc));
        return 1;
    }

    if (uv_pipe_init(&loop, &child_stdout, 0) != 0 ||
        uv_pipe_init(&loop, &child_stderr, 0) != 0) {
        fputs("FAIL: uv_pipe_init\n", stderr);
        return 1;
    }

    stdio[0].flags = UV_INHERIT_FD;
    stdio[0].data.fd = 0;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&child_stdout;
    stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[2].data.stream = (uv_stream_t *)&child_stderr;

    options.exit_cb = on_process_exit;
    options.file = args[0];
    options.args = args;
    options.stdio_count = 3;
    options.stdio = stdio;

    rc = uv_spawn(&loop, &process, &options);
    if (rc != 0) {
        fprintf(stderr, "FAIL: uv_spawn: %d (%s)\n", rc, uv_strerror(rc));
        return 1;
    }

    if (uv_read_start((uv_stream_t *)&child_stdout, alloc_buffer, on_read) != 0 ||
        uv_read_start((uv_stream_t *)&child_stderr, alloc_buffer, on_read) != 0) {
        fputs("FAIL: uv_read_start\n", stderr);
        return 1;
    }

    uv_run(&loop, UV_RUN_DEFAULT);

    if (exit_count != 1 || exit_status != 0) {
        fprintf(stderr, "FAIL: exit callback count=%d status=%lld\n",
                exit_count, (long long)exit_status);
        return 1;
    }
    if (strstr(output, "uv process probe") == NULL) {
        fprintf(stderr, "FAIL: captured output was '%s'\n", output);
        return 1;
    }
    if (close_count != 3) {
        fprintf(stderr, "FAIL: close callback count=%d\n", close_count);
        return 1;
    }

    rc = uv_loop_close(&loop);
    if (rc != 0) {
        fprintf(stderr, "FAIL: uv_loop_close: %s\n", uv_strerror(rc));
        return 1;
    }

    printf("PASS: uv_spawn captured child output and exit status: %s", output);
    return 0;
}
