/*
    <sys/statfs.h> redirect for the AROS libuv port.
    AROS declares struct statfs + statfs() in <sys/mount.h>, not <sys/statfs.h>
    (which libuv fs.c includes). Point fs.c at the real definition.
*/
#ifndef _AROS_LIBUV_SYS_STATFS_H
#define _AROS_LIBUV_SYS_STATFS_H
#include <sys/mount.h>
#endif
