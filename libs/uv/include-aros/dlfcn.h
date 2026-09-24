/*
    Minimal <dlfcn.h> for the AROS libuv port.

    AROS has no dlopen/dlsym. libuv's fs.c is the only consumer, and its single
    dl* use -- dlsym(RTLD_DEFAULT, "mkostemp") -- is guarded by #ifdef
    RTLD_DEFAULT. We deliberately do NOT define RTLD_DEFAULT, so that block
    compiles out and fs.c falls back to plain mkdtemp/open (which AROS has). No
    dlopen/dlsym implementation is needed. Real dynamic loading (uv_dlopen /
    native addons) is a deferred, separate piece.
*/
#ifndef _AROS_LIBUV_DLFCN_H
#define _AROS_LIBUV_DLFCN_H

/* Declarations only (unused given RTLD_DEFAULT is undefined); no RTLD_* here. */
void *dlopen(const char *filename, int flag);
void *dlsym(void *handle, const char *symbol);
int   dlclose(void *handle);
char *dlerror(void);

#endif /* _AROS_LIBUV_DLFCN_H */
