/*
    Minimal <ifaddrs.h> shim for the AROS libuv port.

    AROS/bsdsocket has no getifaddrs(). libuv's src/unix/tcp.c includes this
    header and uses getifaddrs()/freeifaddrs() only for IPv6 link-local scope-id
    resolution, and it early-returns when getifaddrs() fails -- so the stub in
    aros-compat.c (reports ENOSYS, empty list) degrades gracefully: IPv4 and
    non-link-local IPv6 binds are unaffected. Structure layout follows the glibc
    ifaddrs ABI so libuv's field accesses (ifa_next/ifa_addr) compile.
*/
#ifndef _AROS_UV_IFADDRS_H
#define _AROS_UV_IFADDRS_H

#include <sys/socket.h>

struct ifaddrs {
    struct ifaddrs  *ifa_next;      /* next item in the list */
    char            *ifa_name;      /* interface name */
    unsigned int     ifa_flags;     /* IFF_* flags */
    struct sockaddr *ifa_addr;      /* interface address */
    struct sockaddr *ifa_netmask;   /* netmask */
    union {
        struct sockaddr *ifu_broadaddr;
        struct sockaddr *ifu_dstaddr;
    } ifa_ifu;
    void            *ifa_data;      /* address-family-specific data */
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr

int  getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#endif /* _AROS_UV_IFADDRS_H */
