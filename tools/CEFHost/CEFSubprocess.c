/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Small native child executable. Engine code remains in resident libraries. */
#include <string.h>
#include <exec/memory.h>
#include <libraries/cef_client.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/debug.h>
#include <proto/v8.h>
#include "include/capi/cef_app_capi.h"
struct Library *CEFBase;
int main(int argc, char **argv)
{
    cef_main_args_t args = {argc, argv};
    APTR alloc = NULL, free_fn = NULL;
    const char *type = NULL;
    LONG result = RETURN_FAIL;
    int i;
    for (i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--type=", 7)) type = argv[i] + 7;
    if (!type || !*type) return RETURN_ERROR;
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (!CEFBase) return RETURN_FAIL;
    CEFGetAllocator(&alloc, &free_fn);
    if (alloc && free_fn && V8AllocatorInstall(alloc, free_fn)) {
        KPrintF("[CEF-CHILD] enter type=%s task=%p facade=%p allocator=%p free=%p\n", type, FindTask(NULL), CEFBase, alloc, free_fn);
        KPrintF("[CEF-CHILD-MEM] enter type=%s total=%p free=%p\n", type,
            (APTR)AvailMem(MEMF_ANY | MEMF_TOTAL), (APTR)AvailMem(MEMF_ANY));
        result = CEFExecuteProcess(&args, NULL, NULL);
        KPrintF("[CEF-CHILD-MEM] exit type=%s total=%p free=%p\n", type,
            (APTR)AvailMem(MEMF_ANY | MEMF_TOTAL), (APTR)AvailMem(MEMF_ANY));
        KPrintF("[CEF-CHILD] exit type=%s result=%ld\n", type, result);
    }
    CloseLibrary(CEFBase);
    return result < 0 ? RETURN_FAIL : result;
}
