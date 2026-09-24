/*
    AROS compat glue for the libuv port (core-first phase).

    Declares the handful of POSIX / networking helpers that libuv references
    but AROS's headers don't yet provide. Force-included into every libuv
    translation unit via -include (see make.opt), so upstream libuv sources
    stay unpatched. As the POSIX socket layer lands, these prototypes map to
    real bsdsocket calls (and the core-first stubs in aros-compat.c go away).
*/
#ifndef UV_AROS_COMPAT_H
#define UV_AROS_COMPAT_H

#include <sys/resource.h> /* struct rusage, RUSAGE_* */
#include <sys/types.h>   /* ssize_t, off_t, uid_t, gid_t */

/* Real posixc leaves this port added (pread/pwrite/mkdtemp) but which AROS
   headers don't declare where fs.c looks -> provide the prototypes. */
ssize_t pread(int fd, void *buf, size_t nbytes, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t nbytes, off_t offset);
char *mkdtemp(char *tmpl);

/* AROS lacks lchown() (chown a symlink itself); libuv uv_fs_lchown -> stub. */
int lchown(const char *path, uid_t owner, gid_t group);

/* AROS net/if.h does not declare this; core-first stub in aros-compat.c. */
unsigned int if_nametoindex(const char *ifname);

/* bsdsocket has no socketpair() (Amiga sockets have no AF_UNIX). libuv uses it
   only for uv_socketpair()/pipe IPC, never on the TCP data path. Stub in
   aros-net.c returns ENOSYS for now; the AF_INET-127.0.0.1-loopback emulation
   lands with the pipe/process phase. */
int socketpair(int domain, int type, int protocol, int sv[2]);

/* --- sys/resource.h bits AROS lists as NOTIMPL --- */
#ifndef PRIO_PROCESS
#define PRIO_PROCESS    0
#endif
int getpriority(int which, int who);
int setpriority(int which, int who, int prio);
int getrusage(int who, struct rusage *usage);

/* AROS lacks getpagesize(); stub returns the standard 4 KiB page. */
int getpagesize(void);

/* Vector I/O for libuv's fs.c multi-buffer path. AROS posixc has pread/pwrite
   but not the vector variants; these loop over the single-buffer ops. */
struct iovec;   /* from <sys/uio.h>, included in aros-compat.c */
long preadv(int fd, const struct iovec *iov, int iovcnt, long offset);
long pwritev(int fd, const struct iovec *iov, int iovcnt, long offset);
long readv(int fd, const struct iovec *iov, int iovcnt);
long writev(int fd, const struct iovec *iov, int iovcnt);

/* AROS libpthread lacks pthread_condattr_setclock(). libuv uses it only to
   request a monotonic condvar clock; no-op for core-first (condvars use the
   default clock). Revisit if timed-wait drift matters. */
#define pthread_condattr_setclock(attr, clockid) (0)

/* AROS libpthread lacks pthread_atfork(). libuv registers a threadpool
   reset-after-fork handler; core-first does not fork, so no-op. */
#define pthread_atfork(prepare, parent, child) (0)

/* sysconf() constant AROS lacks (glibc value); posixc sysconf returns -1 for
   it, which libuv tolerates by falling back to a 1-CPU count. */
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 84
#endif

#endif /* UV_AROS_COMPAT_H */
