/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    bsdsocket-backed POSIX socket layer for the AROS libuv port.

    AROS bsdsocket sockets live in a SEPARATE fd namespace from posixc files, and
    are closed/read/written via CloseSocket/recv/send -- not close/read/write.
    libuv uses the generic ops, so we:
      - provide socket()/connect()/bind()/... as real functions calling the
        bsdsocket LVOs (via the __*_WB explicit-base macros) on a lazily-opened
        SocketBase owned by the CALLING TASK;
      - keep a per-task REGISTRY of which fds are sockets (set on
        socket()/accept());
      - route close/read/write for registered fds to CloseSocket/recv/send via a
        linker --wrap layer, falling through to posixc for file fds.  posixc
        mangles its entry points (close -> __posixc_close, see
        POSIXC_MANGLE_FUNCS), so the wrap is on the mangled name:
        --wrap=__posixc_close gives __wrap___posixc_close/__real___posixc_close.
        A --wrap=close would bind nothing and route silently fall through.

    bsdsocket and posixc have independent descriptor tables and both allocate
    from zero. Keep sockets in the upper half of WaitSelect's descriptor range
    so ordinary low-numbered files cannot alias them in the routing registry.
    Full descriptor-space unification remains a longer-term POSIX substrate job.

    A bsdsocket base belongs to the task that opened it: every AROSTCP entry
    runs CHECK_TASK() and returns -1 -- errno untouched -- when any other task
    calls through it.  One global SocketBase for the whole library image
    therefore only ever works for the first task that touches the network.
    With several Node runtimes in one ElectronShell the base was
    opened by GitHub Desktop's renderer runtime (Chrome_InProcRendererThread)
    and VS Code's single-instance bind() on CrBrowserMain came back with a
    stale ENOENT ("listen ENOENT ... 1.92-main.sock", hosted user-27,
    2026-09-16).  So the base, the errno wiring and the socket/AF_UNIX
    registries (bsdsocket numbers descriptors per base) are kept per task,
    keyed by exec's unique task id, looked up on every call.  A loop only
    ever touches its sockets from the task that polls it, so this is the
    natural ownership; threadpool DNS never reaches these shims.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/semaphores.h>
#include <exec/libraries.h>
#include <aros/libcall.h>

#include <sys/socket.h>          /* struct sockaddr, socklen_t */
#include <sys/types.h>
#include <netinet/in.h>          /* struct sockaddr_in (socketpair emulation) */
#include <sys/un.h>              /* struct sockaddr_un (AF_UNIX named-pipe emulation) */

/* Only the __*_WB explicit-base macros -- NOT clib's AROS_LP inline function
   definitions (which would conflict with our socket()/connect()/... below). */
#include <libraries/bsdsocket.h>
#include <defines/bsdsocket.h>

/* Drop the convenience macros (connect -> __connect_WB(SocketBase,...)) so we
   can DEFINE these names as functions; the __*_WB explicit-base macros stay. */
#undef socket
#undef connect
#undef bind
#undef listen
#undef accept
#undef send
#undef recv
#undef sendto
#undef recvfrom
#undef setsockopt
#undef getsockopt
#undef getsockname
#undef getpeername
#undef shutdown
#undef sendmsg
#undef recvmsg
#undef IoctlSocket
#undef CloseSocket
#undef WaitSelect

#include <errno.h>
/* fd_set / FD_SET / FD_ZERO / FD_SETSIZE come in via <sys/socket.h> above; do
   NOT re-include <sys/net_types.h> -- it resolves to a second copy and
   redefines fd_set/fd_mask. */
#include <sys/time.h>            /* struct timeval (tv_sec/tv_usec) for WaitSelect */
#include <sys/ioctl.h>           /* FIONBIO */
#include <fcntl.h>               /* F_GETFL / F_SETFL / O_NONBLOCK */
#include <unistd.h>              /* close/read/write for registry files */
#include <stdarg.h>
#include <stdio.h>               /* __get_default_file, snprintf */
#include <stdlib.h>              /* atol (AF_UNIX registry port parse) */
#include <strings.h>             /* bzero, used by FD_ZERO */

/* socket-fd registry: one flag per fd (bsdsocket default dtable is small). */
#define UV_AROS_MAXSOCK 256

/*
 * Per-task socket state (see the file comment).  is_dos_pipe stays one table:
 * DOS pipe descriptors are posixc numbers, which every pthread of a process
 * shares, and a pipe is registered and used by the loop task alone.
 */
struct uv__aros_net_task {
    struct uv__aros_net_task *next;
    ULONG           task_id;        /* GetETaskID: task pointers are recycled */
    struct Library *socket_base;    /* opened lazily, on first socket call */
    int             errno_wired;    /* SBTC_ERRNOPTR -> this task's errno */
    unsigned int    loop_users;     /* loops created on this task */
    unsigned char   is_sock[UV_AROS_MAXSOCK];
    /* AF_UNIX named-pipe emulation. bsdsocket has no AF_UNIX, so a
       path-based socket (e.g. VS Code's single-instance .sock) is backed by a
       127.0.0.1 loopback TCP socket -- exactly like the anonymous socketpair()
       emulation below. is_unix[fd] marks fds created as socket(AF_UNIX,...) so
       bind()/connect() route the sockaddr_un path through the loopback + a
       path->port registry file, instead of handing AF_UNIX to bsdsocket. The
       registry file at the path holds the ephemeral loopback port; the file's
       existence + content IS the AF_UNIX name (stale file -> connect refuses ->
       the caller re-binds, matching AF_UNIX stale-socket semantics). */
    unsigned char   is_unix[UV_AROS_MAXSOCK];
};

static struct uv__aros_net_task *net_tasks = NULL;
static struct SignalSemaphore net_sem;
static int net_sem_ready = 0;
static unsigned char is_dos_pipe[UV_AROS_MAXSOCK];

static void uv__aros_net_sem_init(void);

/* The calling task's context; created on demand when create is set. */
static struct uv__aros_net_task *net_ctx(int create)
{
    ULONG id = GetETaskID(FindTask(NULL));
    struct uv__aros_net_task *ctx;

    uv__aros_net_sem_init();
    ObtainSemaphore(&net_sem);
    for (ctx = net_tasks; ctx != NULL; ctx = ctx->next)
        if (ctx->task_id == id)
            break;
    if (ctx == NULL && create) {
        ctx = AllocMem(sizeof(*ctx), MEMF_ANY | MEMF_CLEAR);
        if (ctx != NULL) {
            ctx->task_id = id;
            ctx->next = net_tasks;
            net_tasks = ctx;
        }
    }
    ReleaseSemaphore(&net_sem);
    return ctx;
}

/* Unlink and free the calling task's context.  Exec only: this also runs
   from the last per-task close of uv1.library (aros-taskbase.c). */
static void net_ctx_release(void)
{
    ULONG id = GetETaskID(FindTask(NULL));
    struct uv__aros_net_task **link;
    struct uv__aros_net_task *ctx = NULL;

    uv__aros_net_sem_init();
    ObtainSemaphore(&net_sem);
    for (link = &net_tasks; *link != NULL; link = &(*link)->next) {
        if ((*link)->task_id == id) {
            ctx = *link;
            *link = ctx->next;
            break;
        }
    }
    ReleaseSemaphore(&net_sem);

    if (ctx != NULL) {
        if (ctx->socket_base != NULL)
            CloseLibrary(ctx->socket_base);
        FreeMem(ctx, sizeof(*ctx));
    }
}

static void ux_set(int fd)
{
    struct uv__aros_net_task *ctx = net_ctx(0);
    if (ctx != NULL && fd >= 0 && fd < UV_AROS_MAXSOCK) ctx->is_unix[fd] = 1;
}
static void ux_clr(int fd)
{
    struct uv__aros_net_task *ctx = net_ctx(0);
    if (ctx != NULL && fd >= 0 && fd < UV_AROS_MAXSOCK) ctx->is_unix[fd] = 0;
}
static int ux_is(int fd)
{
    struct uv__aros_net_task *ctx = net_ctx(0);
    return (ctx != NULL && fd >= 0 && fd < UV_AROS_MAXSOCK) ? ctx->is_unix[fd] : 0;
}
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
static int ux_write_port(const char *path, unsigned short port)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    char buf[16];
    int n;
    ssize_t written;
    int saved;

    if (fd < 0)
        return -1;
    n = snprintf(buf, sizeof(buf), "%u\n", (unsigned)port);
    written = write(fd, buf, (size_t)n);
    saved = errno;
    close(fd);
    errno = saved;
    return written == n ? 0 : -1;
}
static int ux_read_port(const char *path, unsigned short *port)
{
    int fd = open(path, O_RDONLY);
    char buf[16];
    ssize_t bytes;
    long p;

    if (fd < 0)
        return -1;
    bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes <= 0)
        return -1;
    buf[bytes] = 0;
    p = atol(buf);
    if (p <= 0 || p > 65535)
        return -1;
    *port = (unsigned short)p;
    return 0;
}

static void reg_add(int fd)
{
    struct uv__aros_net_task *ctx = net_ctx(0);
    if (ctx != NULL && fd >= 0 && fd < UV_AROS_MAXSOCK) ctx->is_sock[fd] = 1;
}
static void reg_del(int fd)
{
    struct uv__aros_net_task *ctx = net_ctx(0);
    if (ctx != NULL && fd >= 0 && fd < UV_AROS_MAXSOCK) ctx->is_sock[fd] = 0;
}
int uv__aros_is_socket(int fd)
{
    struct uv__aros_net_task *ctx = net_ctx(0);
    return (ctx != NULL && fd >= 0 && fd < UV_AROS_MAXSOCK) ? ctx->is_sock[fd] : 0;
}
void uv__aros_register_dos_pipe(int fd) {
    if (fd >= 0 && fd < UV_AROS_MAXSOCK)
        is_dos_pipe[fd] = 1;
}
void uv__aros_unregister_dos_pipe(int fd) {
    if (fd >= 0 && fd < UV_AROS_MAXSOCK)
        is_dos_pipe[fd] = 0;
}
int uv__aros_is_dos_pipe(int fd) {
    return (fd >= 0 && fd < UV_AROS_MAXSOCK) ? is_dos_pipe[fd] : 0;
}

static void uv__aros_net_sem_init(void)
{
    if (net_sem_ready)
        return;

    Forbid();
    if (!net_sem_ready) {
        InitSemaphore(&net_sem);
        net_sem_ready = 1;
    }
    Permit();
}

void uv__aros_net_loop_init(void)
{
    struct uv__aros_net_task *ctx = net_ctx(1);

    if (ctx != NULL)
        ctx->loop_users++;
}

/* The last loop of this task goes: drop its bsdsocket base with it.  A task
   that only ever ran socket calls without a loop of its own keeps its
   context until its last uv1.library close (uv__aros_net_task_release). */
void uv__aros_net_loop_delete(void)
{
    struct uv__aros_net_task *ctx = net_ctx(0);

    if (ctx == NULL)
        return;
    if (ctx->loop_users > 0)
        ctx->loop_users--;
    if (ctx->loop_users == 0)
        net_ctx_release();
}

void uv__aros_net_task_release(void)
{
    net_ctx_release();
}

int uv__aros_dos_pipe_ready(int fd)
{
    long fh;

    if (!uv__aros_is_dos_pipe(fd))
        return 0;
    if (__get_default_file(fd, &fh) != 0)
        return 0;
    return WaitForChar((BPTR)fh, 0) != 0;
}

static int promote_socket(struct Library *sb, int fd)
{
    int target;
    int saved;

    for (target = FD_SETSIZE - 1; target >= FD_SETSIZE / 2; target--) {
        int promoted;

        if (target == fd || uv__aros_is_socket(target))
            continue;
        promoted = __Dup2Socket_WB(sb, fd, target);
        if (promoted >= 0) {
            __CloseSocket_WB(sb, fd);
            reg_add(promoted);
            return promoted;
        }
    }

    saved = errno;
    reg_add(fd);
    errno = saved;
    return fd;
}

/* The calling task's bsdsocket base: opened on first use, with bsdsocket's
   errno wired to this task's C errno (stdc keeps one errno cell per task). */
struct Library *uv__aros_socketbase(void)
{
    struct uv__aros_net_task *ctx = net_ctx(1);

    if (ctx == NULL)
        return NULL;
    if (ctx->socket_base == NULL)
        ctx->socket_base = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (ctx->socket_base != NULL && !ctx->errno_wired) {
        struct TagItem tags[] = {
            { SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (IPTR)&errno },
            { TAG_END, 0 }
        };
        __SocketBaseTagList_WB(ctx->socket_base, tags);
        ctx->errno_wired = 1;
    }
    return ctx->socket_base;
}

#define BASE (uv__aros_socketbase())

/* A shim must never return -1 with errno untouched: the caller (libuv's
   UV__ERR(errno)) would report whatever the previous call left behind. */
static int no_base(void) { errno = ENOSYS; return -1; }

int socket(int domain, int type, int protocol)
{
    struct Library *sb = BASE;
    int fd;
    int posix_flags = 0;
    /* AF_UNIX has no bsdsocket domain -- emulate over 127.0.0.1 TCP.
       Create the socket as AF_INET/STREAM and tag it so bind()/connect() route
       the sockaddr_un path through the loopback + path->port registry. */
    int unix_dom = (domain == AF_UNIX);
    if (unix_dom) { domain = AF_INET; protocol = 0; }

#ifdef SOCK_NONBLOCK
    posix_flags |= type & SOCK_NONBLOCK;
#endif
#ifdef SOCK_CLOEXEC
    posix_flags |= type & SOCK_CLOEXEC;
#endif

    if (sb == NULL) { errno = ENOSYS; return -1; }
    fd = __socket_WB(sb, domain, type, protocol);
    /*
     * libuv probes atomic SOCK_NONBLOCK|SOCK_CLOEXEC creation first and
     * retries the plain socket plus fcntl when the platform reports EINVAL.
     * AROSTCP reports EPROTONOSUPPORT for those POSIX type bits instead.
     * Normalize only that flagged probe so libuv reaches the existing AROS
     * fcntl adapter without hiding genuine protocol failures.
     */
    if (fd < 0 && posix_flags != 0 && errno == EPROTONOSUPPORT)
        errno = EINVAL;
    if (fd >= 0) {
        fd = promote_socket(sb, fd);
        if (fd >= 0 && unix_dom)
            ux_set(fd);
    }
    return fd;
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    struct Library *sb = BASE;
    int fd;
    if (sb == NULL) { errno = ENOSYS; return -1; }
    fd = __accept_WB(sb, s, addr, addrlen);
    if (fd >= 0)
        fd = promote_socket(sb, fd);
    return fd;
}

int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    struct Library *sb = BASE;
    /* AF_UNIX path -> read the loopback port from the registry file
       and connect to 127.0.0.1:<port>. A missing/stale file (owner gone) makes
       connect refuse, which is exactly the AF_UNIX stale-socket contract that
       lets a single-instance client (VS Code claimInstance) re-bind. */
    if (ux_is(s) && name && name->sa_family == AF_UNIX) {
        const struct sockaddr_un *un = (const struct sockaddr_un *)name;
        struct sockaddr_in a;
        unsigned short port;
        if (sb == NULL) { errno = ENOSYS; return -1; }
        if (ux_read_port(un->sun_path, &port) < 0) { errno = ENOENT; return -1; }
        bzero(&a, sizeof(a));
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = 0x0100007fUL;   /* 127.0.0.1 */
        a.sin_port        = htons(port);
        return __connect_WB(sb, s, (struct sockaddr *)&a, sizeof(a));
    }
    return sb ? __connect_WB(sb, s, (struct sockaddr *)name, namelen) : no_base();
}

int bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    struct Library *sb = BASE;
    /* AF_UNIX path -> bind a 127.0.0.1 ephemeral-port listener and
       publish the port in a registry file at the path. listen()/accept() then
       run unmodified on the AF_INET fd (real accept, multi-client). */
    if (ux_is(s) && name && name->sa_family == AF_UNIX) {
        const struct sockaddr_un *un = (const struct sockaddr_un *)name;
        struct sockaddr_in a;
        socklen_t alen = sizeof(a);
        if (sb == NULL) { errno = ENOSYS; return -1; }
        bzero(&a, sizeof(a));
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = 0x0100007fUL;   /* 127.0.0.1 */
        a.sin_port        = 0;              /* ephemeral */
        if (__bind_WB(sb, s, (struct sockaddr *)&a, sizeof(a)) < 0)
            return -1;
        if (__getsockname_WB(sb, s, (struct sockaddr *)&a, &alen) < 0)
            return -1;
        if (ux_write_port(un->sun_path, ntohs(a.sin_port)) < 0)
            return -1;
        return 0;
    }
    return sb ? __bind_WB(sb, s, (struct sockaddr *)name, namelen) : no_base();
}

int listen(int s, int backlog)
{ struct Library *sb = BASE; return sb ? __listen_WB(sb, s, backlog) : no_base(); }

ssize_t send(int s, const void *buf, size_t len, int flags)
{ struct Library *sb = BASE; return sb ? __send_WB(sb, s, (void *)buf, len, flags) : no_base(); }

ssize_t recv(int s, void *buf, size_t len, int flags)
{ struct Library *sb = BASE; return sb ? __recv_WB(sb, s, buf, len, flags) : no_base(); }

ssize_t sendto(int s, const void *buf, size_t len, int flags,
               const struct sockaddr *to, socklen_t tolen)
{ struct Library *sb = BASE; return sb ? __sendto_WB(sb, s, (void *)buf, len, flags,
                                                 (struct sockaddr *)to, tolen) : no_base(); }

ssize_t recvfrom(int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen)
{ struct Library *sb = BASE; return sb ? __recvfrom_WB(sb, s, buf, len, flags, from, fromlen) : no_base(); }

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{ struct Library *sb = BASE; return sb ? __setsockopt_WB(sb, s, level, optname, (void *)optval, optlen) : no_base(); }

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{ struct Library *sb = BASE; return sb ? __getsockopt_WB(sb, s, level, optname, optval, optlen) : no_base(); }

int getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{ struct Library *sb = BASE; return sb ? __getsockname_WB(sb, s, name, namelen) : no_base(); }

int getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{ struct Library *sb = BASE; return sb ? __getpeername_WB(sb, s, name, namelen) : no_base(); }

/* uv__try_write (fd passing) and udp reach these; through the bsdsocket
   macros they would name a process-global SocketBase that no longer exists. */
ssize_t sendmsg(int s, const struct msghdr *msg, int flags)
{ struct Library *sb = BASE; return sb ? __sendmsg_WB(sb, s, (struct msghdr *)msg, flags) : no_base(); }

ssize_t recvmsg(int s, struct msghdr *msg, int flags)
{ struct Library *sb = BASE; return sb ? __recvmsg_WB(sb, s, msg, flags) : no_base(); }

/*
 * shutdown(): libuv half-closes a stream's write side with shutdown(SHUT_WR)
 * (uv__drain, after uv_shutdown()); node's spawnSync does exactly that on the
 * child's stdin pipe once the input is written.  Without this function the
 * call was the bsdsocket LVO macro (uv/aros.h left `shutdown` defined) on
 * uv1's SocketBase, which is NULL until the first socket is created:
 * every spawnSync in a process that had not touched the network jumped
 * through -0x70 (a C:Node page fault in uv__drain).  Sockets go to bsdsocket.  A DOS descriptor cannot be
 * half-closed, so the descriptor number is re-pointed at NIL: for the
 * direction being shut: the PIPE: end is Closed (the reader on the other
 * side sees EOF once the last writer is gone) while the number stays valid
 * for the stream's later uv__close().
 */
int shutdown(int s, int how)
{
    int nullfd;
    int mode;

    if (uv__aros_is_socket(s)) {
        struct Library *sb = BASE;
        return sb ? __shutdown_WB(sb, s, how) : no_base();
    }

    if (how == SHUT_RD)
        mode = O_RDONLY;
    else if (how == SHUT_WR)
        mode = O_WRONLY;
    else if (how == SHUT_RDWR)
        mode = O_RDWR;
    else {
        errno = EINVAL;
        return -1;
    }

    nullfd = open("NIL:", mode);
    if (nullfd < 0)
        return -1;
    if (dup2(nullfd, s) < 0) {
        int saved = errno;
        close(nullfd);
        errno = saved;
        return -1;
    }
    close(nullfd);
    return 0;
}

/* socketpair(): bsdsocket has no AF_UNIX socketpair, so emulate it over an
   AF_INET 127.0.0.1 loopback pair -- listen on an ephemeral port, connect a
   client, accept; the connected TCP pair is bidirectional and stands in for the
   AF_UNIX pipe (this is how Windows and other AF_UNIX-less platforms do it).
   Both ends are created under uv.library's SocketBase by the wrappers above and
   registered, so uv_poll/WaitSelect can watch them. Backs uv_socketpair() and
   the anonymous uv_pipe path (Node child_process stdio). */
int socketpair(int domain, int type, int protocol, int sv[2])
{
    struct sockaddr_in a;
    socklen_t alen;
    int lst = -1, cli = -1, acc = -1, saved;
    (void)domain; (void)type; (void)protocol;

    lst = socket(AF_INET, SOCK_STREAM, 0);
    if (lst < 0) goto fail;

    bzero(&a, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = 0x0100007fUL;   /* 127.0.0.1, network order on LE host */
    a.sin_port        = 0;              /* ephemeral */
    if (bind(lst, (struct sockaddr *)&a, sizeof(a)) < 0)    goto fail;
    if (listen(lst, 1) < 0)                                 goto fail;

    alen = sizeof(a);
    if (getsockname(lst, (struct sockaddr *)&a, &alen) < 0) goto fail;

    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (cli < 0) goto fail;
    if (connect(cli, (struct sockaddr *)&a, sizeof(a)) < 0) goto fail;

    alen = sizeof(a);
    acc = accept(lst, (struct sockaddr *)&a, &alen);
    if (acc < 0) goto fail;

    reg_del(lst);
    __CloseSocket_WB(BASE, lst);
    sv[0] = cli;
    sv[1] = acc;
    return 0;

fail:
    saved = errno;
    if (acc >= 0) { reg_del(acc); __CloseSocket_WB(BASE, acc); }
    if (cli >= 0) { reg_del(cli); __CloseSocket_WB(BASE, cli); }
    if (lst >= 0) { reg_del(lst); __CloseSocket_WB(BASE, lst); }
    errno = saved;
    return -1;
}

/* --- close/read/write routing via linker --wrap ---
   aros/features.h renames every posixc function to __posixc_<name>
   (POSIXC_MANGLE_FUNCS), so the symbols libuv actually references - and
   therefore the ones the mmakefile wraps - are the mangled spellings.
   Wrapping the plain names matched nothing: socket reads/writes went to
   posixc.library's fd table and close() leaked the bsdsocket socket. */
#ifndef POSIXC_MANGLE_FUNCS
#error "aros-net.c expects mangled posixc symbols; update the --wrap names in mmakefile.src"
#endif
#define UV_WRAP(name) __wrap___posixc_##name
#define UV_REAL(name) __real___posixc_##name

extern int     UV_REAL(close)(int fd);
extern ssize_t UV_REAL(read)(int fd, void *buf, size_t n);
extern ssize_t UV_REAL(write)(int fd, const void *buf, size_t n);

int UV_WRAP(close)(int fd)
{
    if (uv__aros_is_socket(fd)) {
        struct Library *sb = BASE;
        int r = sb ? __CloseSocket_WB(sb, fd) : no_base();
        reg_del(fd);
        ux_clr(fd);   /* clear AF_UNIX tag (libuv unlinks the .sock path) */
        return r;
    }
    uv__aros_unregister_dos_pipe(fd);
    return UV_REAL(close)(fd);
}

ssize_t UV_WRAP(read)(int fd, void *buf, size_t n)
{
    if (uv__aros_is_socket(fd)) {
        struct Library *sb = BASE;
        return sb ? __recv_WB(sb, fd, buf, n, 0) : no_base();
    }
    /*
     * DOS PIPE: reads are blocking.  The event backend dispatches them only
     * after ACTION_WAIT_CHAR says data or EOF is ready, but uv__read() may
     * immediately try a second read after filling its first buffer.  Preserve
     * libuv's nonblocking stream contract for that second read.
     */
    if (uv__aros_is_dos_pipe(fd) && !uv__aros_dos_pipe_ready(fd)) {
        errno = EAGAIN;
        return -1;
    }
    return UV_REAL(read)(fd, buf, n);
}

ssize_t UV_WRAP(write)(int fd, const void *buf, size_t n)
{
    if (uv__aros_is_socket(fd)) {
        struct Library *sb = BASE;
        return sb ? __send_WB(sb, fd, (void *)buf, n, 0) : no_base();
    }
    return UV_REAL(write)(fd, buf, n);
}

/* --- fcntl routing via linker --wrap ---
   libuv's uv__nonblock_fcntl() toggles O_NONBLOCK with F_GETFL/F_SETFL, which
   operate on posixc fds -- meaningless for a bsdsocket fd. Route socket fds to
   IoctlSocket(FIONBIO); non-socket fds fall through to posixc __real_fcntl.
   fcntl() is variadic and declared plain (not via POSIXCFUNC), so unlike
   close/read/write it keeps its unmangled name. */
extern int __real_fcntl(int fd, int cmd, ...);

int __wrap_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg;

    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);

    if (uv__aros_is_socket(fd)) {
        if (cmd == F_GETFL)
            return 0;                       /* report "no flags set" */
        if (cmd == F_SETFL) {
            int nb = (arg & O_NONBLOCK) ? 1 : 0;
            struct Library *sb = BASE;
            if (sb)
                __IoctlSocket_WB(sb, fd, FIONBIO, (char *)&nb);
            return 0;                       /* keep uv_poll_init happy regardless */
        }
        return 0;                           /* other cmds: no-op success */
    }
    return __real_fcntl(fd, cmd, arg);
}

/* --- readiness wait over bsdsocket (drives libuv's uv__io_poll on AROS) ---
   fds[i]/wanted[i] carry per-fd interest (bit0 = readable, bit1 = writable);
   revents[i] receives the ready mask (same bits). asyncsig is an exec signal bit
   to wake on in addition to fd readiness (or < 0 for none); timeout_ms < 0 waits
   forever. Returns the ready-fd count (>= 0) or -1; *async_fired is set when the
   async signal woke the wait. WaitSelect unifies fd readiness, the timeout, and
   the loop's exec async-signal in a single blocking call. */
int uv__aros_waitselect(const int *fds, const int *wanted, int *revents,
                        int nfds, int timeout_ms, int asyncsig, int *async_fired)
{
    struct Library *sb = BASE;
    fd_set rd, wr;
    struct timeval tv;
    struct timeval *tvp;
    ULONG sigmask;
    int i, maxfd, n;

    *async_fired = 0;
    if (sb == NULL)
        return -1;

    FD_ZERO(&rd);
    FD_ZERO(&wr);
    maxfd = -1;
    for (i = 0; i < nfds; i++) {
        int fd = fds[i];
        revents[i] = 0;
        if (fd < 0 || fd >= FD_SETSIZE)
            continue;
        if (wanted[i] & 1) FD_SET(fd, &rd);
        if (wanted[i] & 2) FD_SET(fd, &wr);
        if (fd > maxfd) maxfd = fd;
    }

    sigmask = (asyncsig >= 0) ? (1UL << (ULONG)asyncsig) : 0;

    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    } else {
        tvp = NULL;
    }

    n = __WaitSelect_WB(sb, maxfd + 1, &rd, &wr,
                        (fd_set *)0, tvp, &sigmask);

    /*
     * AROSTCP may return zero immediately despite a positive timeout.  Do not
     * let a network-bound event loop busy-spin and starve the stack task that
     * must refill the receive queue.  A true zero-timeout poll stays nonblocking.
     */
    if (n == 0 && timeout_ms > 0)
        Delay(1);

    if (asyncsig >= 0 && (sigmask & (1UL << (ULONG)asyncsig)))
        *async_fired = 1;

    if (n > 0) {
        for (i = 0; i < nfds; i++) {
            int fd = fds[i];
            if (fd < 0 || fd >= FD_SETSIZE)
                continue;
            if (FD_ISSET(fd, &rd)) revents[i] |= 1;
            if (FD_ISSET(fd, &wr)) revents[i] |= 2;
        }
    }
    return n;
}
