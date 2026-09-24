/*
    uv_aros_net_selftest -- a library-internal networking smoke for the AROS
    libuv port, exported as an LVO.

    Runs ENTIRELY inside uv.library so every socket is created under the
    library's own bsdsocket SocketBase (AROS bsdsocket fds are private to the
    opener's base -- a caller's own OpenLibrary would give a separate,
    invisible fd table) and every fd is registered by our socket() wrapper, so
    uv_poll's non-blocking setup (fcntl -> IoctlSocket) and the WaitSelect
    backend both see it.

    Flow: TCP loopback pair (listen 127.0.0.1:ephemeral -> connect -> accept),
    watch the accepted fd for UV_READABLE via uv_poll, send a token from the
    client, run the loop, and verify the poll callback fires and recv() returns
    the token. Returns 0 on PASS, a small positive code on FAIL (so the caller/
    serial log can tell where it stopped). Requires AROSTCP (bsdsocket.library)
    to be running with a loopback interface.

    Socket ops here resolve to the definitions in src-aros/aros-net.c (declared
    with their POSIX prototypes below); this file stays free of the bsdsocket
    library headers so it can include uv.h without the AROS_LP macro clashes
    that aros-net.c has to #undef.
*/
#include <proto/exec.h>
#include <aros/debug.h>

#include <uv.h>

#include <sys/socket.h>          /* AF_INET, SOCK_STREAM, struct sockaddr */
#include <netinet/in.h>          /* struct sockaddr_in */
#include <string.h>

/* Those headers #define socket/bind/... as convenience macros onto __*_WB(
   SocketBase, ...). Drop them so the names below are plain functions resolving
   to aros-net.c's definitions (which run under the library's own SocketBase);
   the types (struct sockaddr_in, socklen_t, AF_INET) stay. */
/* socket/bind/listen/connect/accept/getsockname are convenience MACROS here;
   drop them so the names are plain functions from aros-net.c. send/recv are
   real prototypes in <sys/socket.h> already (leave them alone -- redeclaring
   conflicts), and resolve to aros-net.c at link. */
#undef socket
#undef bind
#undef listen
#undef connect
#undef accept
#undef getsockname
#undef close

/* Implemented in src-aros/aros-net.c (bsdsocket-backed). */
extern int socket(int domain, int type, int protocol);
extern int bind(int s, const struct sockaddr *name, socklen_t namelen);
extern int listen(int s, int backlog);
extern int connect(int s, const struct sockaddr *name, socklen_t namelen);
extern int accept(int s, struct sockaddr *addr, socklen_t *addrlen);
extern int getsockname(int s, struct sockaddr *name, socklen_t *namelen);
/* close() on a socket fd is routed to CloseSocket by aros-net.c's __wrap_close. */
extern int close(int fd);

#define NLOG(...) do { bug("[UVNet] " __VA_ARGS__); bug("\n"); } while (0)

static const char TOKEN[] = "libuv-net-ok";
#define TOKLEN ((int)sizeof(TOKEN) - 1)

static int  g_got = 0;         /* poll callback fired */
static int  g_match = 0;       /* recv content matched TOKEN */
static int  g_conn = -1;       /* accepted server-side fd (read here) */

static void on_readable(uv_poll_t *handle, int status, int events)
{
    char rd[64];
    ssize_t n;

    if (status < 0) {
        NLOG("poll cb status=%d", status);
        uv_poll_stop(handle);
        uv_close((uv_handle_t *)handle, NULL);
        return;
    }

    g_got = 1;
    if (events & UV_READABLE) {
        n = recv(g_conn, rd, sizeof(rd) - 1, 0);
        NLOG("readable: recv=%ld", (long)n);
        if (n == TOKLEN && memcmp(rd, TOKEN, TOKLEN) == 0)
            g_match = 1;
    }

    /* One shot: stop watching and let the loop drain to completion. */
    uv_poll_stop(handle);
    uv_close((uv_handle_t *)handle, NULL);
}

int uv_aros_net_selftest(void)
{
    uv_loop_t loop;
    uv_poll_t poll;
    struct sockaddr_in sa;
    socklen_t slen;
    int lst = -1, cli = -1;
    int rc = 0;

    g_got = g_match = 0;
    g_conn = -1;

    lst = socket(AF_INET, SOCK_STREAM, 0);
    if (lst < 0) { NLOG("socket(listen) failed -- is AROSTCP running?"); return 1; }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = 0;                     /* ephemeral port */
    sa.sin_addr.s_addr = 0x0100007fUL;     /* 127.0.0.1 in network byte order (LE store) */

    if (bind(lst, (struct sockaddr *)&sa, sizeof(sa)) < 0) { NLOG("bind failed"); rc = 2; goto out; }
    if (listen(lst, 1) < 0)                                { NLOG("listen failed"); rc = 3; goto out; }

    /* Recover the actually-bound port (already in network order) for connect. */
    slen = sizeof(sa);
    if (getsockname(lst, (struct sockaddr *)&sa, &slen) < 0) { NLOG("getsockname failed"); rc = 4; goto out; }

    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (cli < 0) { NLOG("socket(client) failed"); rc = 5; goto out; }

    /* Blocking connect on loopback completes against the listen backlog. */
    if (connect(cli, (struct sockaddr *)&sa, sizeof(sa)) < 0) { NLOG("connect failed"); rc = 6; goto out; }

    g_conn = accept(lst, NULL, NULL);
    if (g_conn < 0) { NLOG("accept failed"); rc = 7; goto out; }

    if (uv_loop_init(&loop) != 0) { NLOG("uv_loop_init failed"); rc = 8; goto out; }
    if (uv_poll_init_socket(&loop, &poll, g_conn) != 0) { NLOG("uv_poll_init_socket failed"); rc = 9; goto out_loop; }
    if (uv_poll_start(&poll, UV_READABLE, on_readable) != 0) { NLOG("uv_poll_start failed"); rc = 10; goto out_loop; }

    if (send(cli, TOKEN, TOKLEN, 0) != TOKLEN) { NLOG("send short/failed"); rc = 11; goto out_loop; }

    NLOG("running loop, waiting for readiness on fd %d...", g_conn);
    uv_run(&loop, UV_RUN_DEFAULT);

    if (!(g_got && g_match)) { NLOG("FAIL: got=%d match=%d", g_got, g_match); rc = 12; }

out_loop:
    uv_loop_close(&loop);
out:
    if (g_conn >= 0) close(g_conn);
    if (cli >= 0)    close(cli);
    if (lst >= 0)    close(lst);

    if (rc == 0)
        NLOG("PASS: uv_poll readiness dispatched over bsdsocket loopback; token verified");
    return rc;
}
