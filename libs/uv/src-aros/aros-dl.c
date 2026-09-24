/*
    uv_dlopen family for the AROS libuv port.

    libuv's public dynamic-loading API (uv_dlopen/uv_dlsym/uv_dlclose/
    uv_dlerror) normally wraps POSIX dlfcn (src/unix/dl.c). AROS has no native
    ELF/.so dynamic loader for the kind of relocatable objects Node's native
    addons are (see aros-elf-loader-got-wrong-layer): InternalLoadSeg is a
    static link finaliser, not a runtime linker. So rather than leave these
    entry points undefined -- which breaks Node's link and gives it no defined
    behaviour -- this provides the family as a STABLE, well-defined failure:
    uv_dlopen/uv_dlsym always fail with a clear message, uv_dlclose is a no-op,
    uv_dlerror returns the message. Node's addon loader then sees a clean
    "native dynamic loading unsupported" capability instead of missing symbols.

    src/unix/dl.c stays excluded from the build (it needs dlopen); these four
    are the AROS replacements and are exported as uv1.library LVOs.

    If AROS ever grows a real addon loader, replace the bodies here.
*/
#include "uv.h"
#include "internal.h"

static const char AROS_DL_UNSUPPORTED[] =
    "native dynamic loading (uv_dlopen) is not supported on AROS";

int uv_dlopen(const char* filename, uv_lib_t* lib) {
  (void)filename;
  if (lib == NULL)
    return -1;
  lib->handle = NULL;
  lib->errmsg = (char*) AROS_DL_UNSUPPORTED;
  return -1;
}

void uv_dlclose(uv_lib_t* lib) {
  if (lib == NULL)
    return;
  lib->handle = NULL;
  lib->errmsg = NULL;
}

int uv_dlsym(uv_lib_t* lib, const char* name, void** ptr) {
  (void)name;
  if (ptr != NULL)
    *ptr = NULL;
  if (lib != NULL)
    lib->errmsg = (char*) AROS_DL_UNSUPPORTED;
  return -1;
}

const char* uv_dlerror(const uv_lib_t* lib) {
  return (lib != NULL && lib->errmsg != NULL) ? lib->errmsg : "no error";
}
