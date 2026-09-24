/*
    Minimal <poll.h> for the AROS libuv port.

    AROS has no poll.h yet. libuv uses the POLL* constants and struct pollfd as
    its INTERNAL event representation (uv__io_t events/pevents) even in the
    non-networking core, so internal.h needs this header regardless of sockets.
    poll() itself is only called by posix-poll.c (not compiled in the core-first
    build); its prototype is here for completeness and gets a real bsdsocket
    (WaitSelect) implementation in the networking phase.
*/
#ifndef _AROS_LIBUV_POLL_H
#define _AROS_LIBUV_POLL_H

typedef unsigned int nfds_t;

struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLLIN     0x0001
#define POLLPRI    0x0002
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020
#define POLLRDNORM 0x0040
#define POLLRDBAND 0x0080
#define POLLWRNORM 0x0100
#define POLLWRBAND 0x0200

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif /* _AROS_LIBUV_POLL_H */
