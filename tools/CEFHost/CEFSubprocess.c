/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Small native child executable. Engine code remains in resident libraries. */
#include <string.h>
#include <stdio.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <libraries/cef_client.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/debug.h>
#include <proto/v8.h>
#include "include/capi/cef_app_capi.h"
struct Library *CEFBase;

/* Optional validation evidence, independent of character-interleaved DEBUG:.
 * Each native child owns separate files. Disabled during ordinary use. */
static int record_child(const char *directory, ULONG id, const char *type,
    const char *event, LONG result)
{
    char path[1024], leaf[48], record[192];
    BPTR file;
    int length;
    LONG written;
    BOOL closed;
    if (!directory[0]) return 1;
    if (strlen(directory) >= sizeof(path)) return 0;
    strcpy(path, directory);
    snprintf(leaf, sizeof(leaf), "%lu.%s", (unsigned long)id, event);
    if (!AddPart(path, leaf, sizeof(path))) return 0;
    length = snprintf(record, sizeof(record),
        "CEFCHILD1\nid=%lu\ntype=%s\nevent=%s\nresult=%ld\n",
        (unsigned long)id, type, event, (long)result);
    if (length < 0 || (size_t)length >= sizeof(record)) return 0;
    file = Open(path, MODE_NEWFILE);
    if (!file) return 0;
    written = Write(file, record, length);
    closed = Close(file);
    return written == length && closed;
}

int main(int argc, char **argv)
{
    cef_main_args_t args = {argc, argv};
    APTR alloc = NULL, free_fn = NULL;
    const char *type = NULL;
    const char *subtype = "none";
    LONG result = RETURN_FAIL;
    char ledger[1024] = {0};
    ULONG child_id = (ULONG)GetETaskID(FindTask(NULL));
    LONG ledger_length;
    int i;
    for (i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--type=", 7)) type = argv[i] + 7;
    for (i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--utility-sub-type=", 19)) subtype = argv[i] + 19;
    if (!type || !*type) return RETURN_ERROR;
    /* Restrict the diagnostic record's type field, not Chromium arguments. */
    ledger_length = GetVar("CEF_CHILD_LEDGER", ledger, sizeof(ledger), GVF_GLOBAL_ONLY);
    if (ledger_length >= 0) {
        if (ledger_length >= (LONG)sizeof(ledger) - 1 || strlen(type) > 63)
            return RETURN_ERROR;
        for (const char *p = type; *p; ++p)
            if ((*p < 'a' || *p > 'z') && *p != '-') return RETURN_ERROR;
        if (!record_child(ledger, child_id, type, "enter", 0)) {
            KPrintF("[CEF-LEDGER] entry write failed id=%lu\n", child_id);
            return RETURN_FAIL;
        }
    } else ledger[0] = 0;
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (!CEFBase) goto finished;
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
finished:
    if (result < 0) result = RETURN_FAIL;
    if (!record_child(ledger, child_id, type, "exit", result)) {
        KPrintF("[CEF-LEDGER] exit write failed id=%lu\n", child_id);
        return RETURN_FAIL;
    }
    return result;
}
