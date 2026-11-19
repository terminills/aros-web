/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Small native child executable. Engine code remains in resident libraries. */
#include <string.h>
#include <exec/memory.h>
#include <exec/tasks.h>
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
    const char *subtype = "none";
    LONG result = RETURN_FAIL;
    int i;
    for (i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--type=", 7)) type = argv[i] + 7;
    for (i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--utility-sub-type=", 19)) subtype = argv[i] + 19;
    if (!type || !*type) return RETURN_ERROR;
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (!CEFBase) return RETURN_FAIL;
    CEFGetAllocator(&alloc, &free_fn);
    if (alloc && free_fn && V8AllocatorInstall(alloc, free_fn)) {
        /* Short ID-keyed records supplement verbose lines that can interleave
         * with another native task's serial output. IDs survive Task reuse. */
        KPrintF("[CEF-CHILD-EVENT] E %lu %s\n",
            (ULONG)GetETaskID(FindTask(NULL)), type);
        KPrintF("[CEF-CHILD] enter type=%s task=%p facade=%p allocator=%p free=%p\n", type, FindTask(NULL), CEFBase, alloc, free_fn);
        KPrintF("[CEF-CHILD-DETAIL] type=%s subtype=%s task=%p id=%lu\n",
            type, subtype, FindTask(NULL), (ULONG)GetETaskID(FindTask(NULL)));
        KPrintF("[CEF-CHILD-MEM] enter type=%s total=%p free=%p\n", type,
            (APTR)AvailMem(MEMF_ANY | MEMF_TOTAL), (APTR)AvailMem(MEMF_ANY));
        result = CEFExecuteProcess(&args, NULL, NULL);
        KPrintF("[CEF-CHILD-EVENT] X %lu %s %ld\n",
            (ULONG)GetETaskID(FindTask(NULL)), type, result);
        KPrintF("[CEF-CHILD-MEM] exit type=%s total=%p free=%p\n", type,
            (APTR)AvailMem(MEMF_ANY | MEMF_TOTAL), (APTR)AvailMem(MEMF_ANY));
        KPrintF("[CEF-CHILD] exit type=%s result=%ld\n", type, result);
    }
    CloseLibrary(CEFBase);
    return result < 0 ? RETURN_FAIL : result;
}
