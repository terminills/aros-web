/*
    UVTcpTest -- uv_tcp + uv_stream loopback echo smoke for the AROS libuv port.

    Exercises the HIGH-LEVEL uv_tcp handle API un-stubbed in commit e744823045
    (real src/unix/tcp.c + stream.c over the aros-net.c bsdsocket layer and the
    WaitSelect poll backend):

      server: uv_tcp_init -> uv_tcp_bind(127.0.0.1:ephemeral) -> uv_listen
      client: uv_tcp_init -> uv_tcp_connect -> uv_write(token)
      server: uv_accept -> uv_read_start -> echo the bytes back with uv_write
      client: uv_read_start -> verify the echo == the sent token -> close all

    PASS = the client reads back the exact token it sent. Requires AROSTCP
    (bsdsocket.library) up with a loopback interface -- same runtime prereq as
    the uv_poll selftest (UVNetTest). Output to stdout + serial.

    Byte order: the program has no bsdsocket SocketBase (only uv.library does),
    so it must NOT call htons/ntohs (those are bsdsocket LVOs). The sockaddr_in
    is built by hand -- 127.0.0.1 as its network-order little-endian word, and
    the ephemeral port from getsockname (already network order) is reused
    verbatim for connect. All actual socket ops happen INSIDE uv.library.
*/
#include <proto/exec.h>
#include <aros/debug.h>

#include <uv.h>

#include <sys/socket.h>          /* AF_INET, struct sockaddr */
#include <netinet/in.h>          /* struct sockaddr_in */
#include <string.h>
#include <stdio.h>

#define TLOG(...) do { printf("[UVTcp] " __VA_ARGS__); printf("\n"); \
                       bug("[UVTcp] " __VA_ARGS__); bug("\n"); } while (0)

/* 127.0.0.1 as stored in network byte order on a little-endian host (AROS
   x86_64): bytes { 0x7f, 0x00, 0x00, 0x01 } == 0x0100007f. */
#define LOOPBACK_NET_ORDER 0x0100007fUL

static const char TOKEN[] = "libuv-tcp-aros-echo";

static uv_tcp_t     g_server;    /* listening socket                */
static uv_tcp_t     g_conn;      /* server side of accepted conn    */
static uv_tcp_t     g_client;    /* client socket                   */
static uv_connect_t g_connect;
static uv_write_t   g_cwrite;    /* client -> server                */
static uv_write_t   g_swrite;    /* server echo -> client           */

static char g_server_rx[128];
static char g_client_rx[128];
static int  g_conn_active = 0;
static int  g_pass = 0;
static int  g_fail = 0;

static void alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf)
{
    (void)suggested;
    if (h == (uv_handle_t *)&g_conn) { buf->base = g_server_rx; buf->len = sizeof(g_server_rx); }
    else                             { buf->base = g_client_rx; buf->len = sizeof(g_client_rx); }
}

static void on_close(uv_handle_t *h) { (void)h; }

static void close_all(void)
{
    if (!uv_is_closing((uv_handle_t *)&g_client)) uv_close((uv_handle_t *)&g_client, on_close);
    if (g_conn_active && !uv_is_closing((uv_handle_t *)&g_conn))
        uv_close((uv_handle_t *)&g_conn, on_close);
    if (!uv_is_closing((uv_handle_t *)&g_server)) uv_close((uv_handle_t *)&g_server, on_close);
}

/* client received the echo -> verify */
static void on_client_read(uv_stream_t *s, ssize_t nread, const uv_buf_t *buf)
{
    (void)s; (void)buf;
    if (nread <= 0) { TLOG("client read err/eof nread=%d", (int)nread); g_fail = 1; close_all(); return; }
    TLOG("client got %d bytes: '%.*s'", (int)nread, (int)nread, g_client_rx);
    if ((size_t)nread == strlen(TOKEN) && memcmp(g_client_rx, TOKEN, (size_t)nread) == 0) {
        TLOG("echo matches the sent token");
        g_pass = 1;
    } else {
        TLOG("echo MISMATCH");
        g_fail = 1;
    }
    close_all();
}

static void on_server_write(uv_write_t *req, int status)
{
    (void)req;
    TLOG("server echo write status=%d", status);
    if (status != 0) { g_fail = 1; close_all(); }
}

/* server received the token -> echo it straight back */
static void on_server_read(uv_stream_t *s, ssize_t nread, const uv_buf_t *buf)
{
    uv_buf_t out;
    (void)s; (void)buf;
    if (nread <= 0) { TLOG("server read err/eof nread=%d", (int)nread); g_fail = 1; close_all(); return; }
    TLOG("server got %d bytes: '%.*s' -- echoing", (int)nread, (int)nread, g_server_rx);
    out = uv_buf_init(g_server_rx, (unsigned)nread);
    uv_write(&g_swrite, (uv_stream_t *)&g_conn, &out, 1, on_server_write);
}

static void on_client_write(uv_write_t *req, int status)
{
    (void)req;
    TLOG("client write status=%d", status);
    if (status != 0) { g_fail = 1; close_all(); return; }
    uv_read_start((uv_stream_t *)&g_client, alloc_cb, on_client_read);
}

static void on_connect(uv_connect_t *req, int status)
{
    uv_buf_t out;
    (void)req;
    TLOG("client connect status=%d", status);
    if (status != 0) { g_fail = 1; close_all(); return; }
    out = uv_buf_init((char *)TOKEN, (unsigned)strlen(TOKEN));
    uv_write(&g_cwrite, (uv_stream_t *)&g_client, &out, 1, on_client_write);
}

static void on_new_connection(uv_stream_t *server, int status)
{
    TLOG("server on_connection status=%d", status);
    if (status != 0) { g_fail = 1; close_all(); return; }
    uv_tcp_init(server->loop, &g_conn);
    g_conn_active = 1;
    if (uv_accept(server, (uv_stream_t *)&g_conn) == 0) {
        uv_read_start((uv_stream_t *)&g_conn, alloc_cb, on_server_read);
    } else {
        TLOG("uv_accept FAILED");
        g_fail = 1;
        close_all();
    }
}

int main(void)
{
    uv_loop_t loop;
    struct sockaddr_in bindaddr, connaddr, name;
    int namelen = sizeof(name);
    int r, hport;

    TLOG("libuv %s - uv_tcp loopback echo smoke", uv_version_string());
    if (uv_loop_init(&loop) != 0) { TLOG("loop_init failed"); return 20; }

    /* --- server: listen on 127.0.0.1:0 (ephemeral) --- */
    uv_tcp_init(&loop, &g_server);
    memset(&bindaddr, 0, sizeof(bindaddr));
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_port   = 0;                       /* ephemeral (0 is order-agnostic) */
    bindaddr.sin_addr.s_addr = LOOPBACK_NET_ORDER;
    r = uv_tcp_bind(&g_server, (const struct sockaddr *)&bindaddr, 0);
    TLOG("uv_tcp_bind = %d", r);
    if (r != 0) { TLOG("FAIL: bind"); return 21; }
    r = uv_listen((uv_stream_t *)&g_server, 8, on_new_connection);
    TLOG("uv_listen = %d", r);
    if (r != 0) { TLOG("FAIL: listen"); return 22; }

    /* discover the ephemeral port (network order; swap only to print it) */
    memset(&name, 0, sizeof(name));
    r = uv_tcp_getsockname(&g_server, (struct sockaddr *)&name, &namelen);
    hport = (int)(((name.sin_port >> 8) & 0xff) | ((name.sin_port & 0xff) << 8));
    TLOG("uv_tcp_getsockname = %d, port = %d", r, hport);
    if (r != 0 || name.sin_port == 0) { TLOG("FAIL: getsockname/port"); return 23; }

    /* --- client: connect to 127.0.0.1:<that port> --- */
    uv_tcp_init(&loop, &g_client);
    memset(&connaddr, 0, sizeof(connaddr));
    connaddr.sin_family = AF_INET;
    connaddr.sin_port   = name.sin_port;           /* network order -> network order */
    connaddr.sin_addr.s_addr = LOOPBACK_NET_ORDER;
    r = uv_tcp_connect(&g_connect, &g_client, (const struct sockaddr *)&connaddr, on_connect);
    TLOG("uv_tcp_connect queued = %d, running loop...", r);
    if (r != 0) { TLOG("FAIL: connect queue"); return 24; }

    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    if (g_pass && !g_fail) {
        TLOG("PASS: uv_tcp loopback echo round-trip verified over bsdsocket");
        return 0;
    }
    TLOG("FAIL: pass=%d fail=%d", g_pass, g_fail);
    return 20;
}
