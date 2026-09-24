/*
    Minimal <sys/un.h> shim for the AROS libuv port.

    AROS bsdsocket has no AF_UNIX domain, but libuv's src/unix/pipe.c needs
    struct sockaddr_un to COMPILE. Named-pipe (filesystem-path) operations fail
    at runtime -- socket(AF_UNIX) is unsupported by bsdsocket -- and libuv
    surfaces that as an ordinary error. The anonymous, socketpair-backed pipe
    path (Node child_process stdio) works via the AF_INET-127.0.0.1-loopback
    socketpair emulation in aros-net.c, so a real uv_pipe over a passed fd is
    functional even though bind-to-path is not.

    Layout matches the glibc sockaddr_un ABI (108-byte sun_path).
*/
#ifndef _AROS_UV_SYS_UN_H
#define _AROS_UV_SYS_UN_H

#include <sys/socket.h>   /* sa_family_t */

#define UNIX_PATH_MAX 108

struct sockaddr_un {
    sa_family_t sun_family;                 /* AF_UNIX */
    char        sun_path[UNIX_PATH_MAX];    /* pathname */
};

#endif /* _AROS_UV_SYS_UN_H */
