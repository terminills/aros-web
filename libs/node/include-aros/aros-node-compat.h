/*
 * Target-only compatibility declarations for the Node 22 static-core build.
 * Keep these out of host generators and graduate generally useful contracts
 * into libc only after their independent runtime validation.
 */
#ifndef AROS_NODE_COMPAT_H
#define AROS_NODE_COMPAT_H

/*
 * node.library is a pertaskbase module whose rellib bases live in the
 * calling Task's storage slot.  A thread node starts gets no slot of its own
 * and genmodule lets it borrow its creator's - one level only, so a thread
 * started from a thread (the CA-certificate loader started from a Worker)
 * has no base at all.  These rename node's thread creation to wrappers in
 * node_bridge.cc that bind the creator's base on the new thread first; the
 * renamed declarations come from uv.h and pthread.h themselves.  The bridge
 * is compiled without this header and calls the real functions.  Defined
 * before any include: signal.h already brings pthread.h in, and a
 * declaration seen before the rename would stay pthread_create.
 */
#define uv_thread_create aros_node_uv_thread_create
#define uv_thread_create_ex aros_node_uv_thread_create_ex
#define pthread_create aros_node_pthread_create

#include <stddef.h>
#include <signal.h>
#include <string.h>

#ifndef EHOSTDOWN
#define EHOSTDOWN 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

int posix_memalign(void **memptr, size_t alignment, size_t size);

static inline void *aros_node_memrchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;

    while (n--) {
        if (p[n] == (unsigned char)c)
            return (void *)(p + n);
    }
    return NULL;
}
#define memrchr aros_node_memrchr

static inline int aros_node_pthread_sigmask(int how,
                                            const sigset_t *set,
                                            sigset_t *oldset)
{
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}
#define pthread_sigmask aros_node_pthread_sigmask

#ifdef __cplusplus
}
#endif

#endif
