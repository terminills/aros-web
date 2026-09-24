/*
    AROS core-first stubs for the libuv port.

    Networking helpers that require the (not-yet-built) POSIX socket layer
    return the "unknown / unsupported" value, which libuv handles gracefully.
    Replace with real bsdsocket-backed implementations when networking lands.
*/
#include "aros-compat.h"
#include <string.h>   /* memset */
#include <errno.h>    /* errno, ENOSYS */

/* IPv6 scope-id resolution. libuv treats id 0 as "unknown interface,
   silently ignored" (see uv_inet6_addr in uv-common.c), so 0 is a safe
   core-first result until the socket layer can query interface indices. */
unsigned int if_nametoindex(const char *ifname)
{
    (void)ifname;
    return 0;
}

/* Resource/priority core-first stubs. These feed peripheral libuv APIs
   (uv_getrusage, uv_os_setpriority) that the event loop itself never calls;
   real implementations can arrive with the process/exec integration. */
int getpriority(int which, int who)
{
    (void)which; (void)who;
    return 0;   /* normal priority */
}

int setpriority(int which, int who, int prio)
{
    (void)which; (void)who; (void)prio;
    return 0;
}

int getrusage(int who, struct rusage *usage)
{
    (void)who;
    if (usage)
        memset(usage, 0, sizeof(*usage));
    return 0;
}

int getpagesize(void)
{
    return 4096;
}

/* --- vector I/O: loop the single-buffer posixc ops over the iovec array.
   Stops at a short transfer (the caller retries the remainder), matching how
   libuv's fs.c consumes these. --- */
#include <sys/uio.h>
#include <unistd.h>

long preadv(int fd, const struct iovec *iov, int iovcnt, long offset)
{
    long total = 0; int i;
    for (i = 0; i < iovcnt; i++) {
        long r = (long)pread(fd, iov[i].iov_base, iov[i].iov_len, offset + total);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

long pwritev(int fd, const struct iovec *iov, int iovcnt, long offset)
{
    long total = 0; int i;
    for (i = 0; i < iovcnt; i++) {
        long r = (long)pwrite(fd, iov[i].iov_base, iov[i].iov_len, offset + total);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

long readv(int fd, const struct iovec *iov, int iovcnt)
{
    long total = 0; int i;
    for (i = 0; i < iovcnt; i++) {
        long r = (long)read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

long writev(int fd, const struct iovec *iov, int iovcnt)
{
    long total = 0; int i;
    for (i = 0; i < iovcnt; i++) {
        long r = (long)write(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

/* AROS has no symlink-preserving chown; uv_fs_lchown is not on the core-first
   path, so this only needs to link. Report unsupported. */
int lchown(const char *path, uid_t owner, gid_t group)
{
    (void)path; (void)owner; (void)group;
    errno = ENOSYS;
    return -1;
}

/* poll(): real WaitSelect-backed implementation is the networking phase. Not on
   the fs read/write path, so a link-only stub for now. */
#include <poll.h>
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    (void)fds; (void)nfds; (void)timeout;
    errno = ENOSYS;
    return -1;
}

/* getifaddrs(): AROS/bsdsocket has no interface enumeration. libuv's tcp.c uses
   it only for IPv6 link-local scope-id resolution and early-returns when it
   fails, so reporting "unavailable" (empty list) degrades gracefully -- IPv4 and
   non-link-local IPv6 binds are unaffected. Real enumeration can arrive with the
   uv_interface_addresses pass. */
#include <ifaddrs.h>
int getifaddrs(struct ifaddrs **ifap)
{
    if (ifap) *ifap = NULL;
    errno = ENOSYS;
    return -1;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    (void)ifa;
}

/* in6addr_any: netinet/in.h declares it extern, but its definition lives in the
   network-stack link lib that uv.library does not link. src/unix/udp.c
   references it for the IPv6 wildcard bind. Provide the all-zeros (::) value via
   the header's own initializer macro. */
#include <netinet/in.h>
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
