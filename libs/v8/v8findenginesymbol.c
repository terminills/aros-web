/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Exact-build V8 engine symbol lookup
*/

#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <libraries/debug.h>
#include <proto/debug.h>
#include <proto/exec.h>
#include <proto/v8.h>

#include <stdlib.h>
#include <string.h>

#include "v8_intern.h"

struct V8SymbolEntry
{
    CONST_STRPTR name;
    APTR address;
};

struct V8SymbolIndex
{
    struct V8SymbolEntry *entries;
    ULONG count;
    ULONG capacity;
    ULONG visited;
    CONST_STRPTR lastModuleName;
    BOOL lastModuleIsV8;
    BOOL failed;
};

struct V8SymbolSearch
{
    CONST_STRPTR name;
    APTR address;
    ULONG visited;
};

static BOOL V8_IsLibraryModule(CONST_STRPTR moduleName)
{
    static const char suffix[] = "v8.library";
    CONST_STRPTR suffixStart;
    size_t moduleLength;
    size_t suffixLength = sizeof(suffix) - 1;

    if (moduleName == NULL)
        return FALSE;

    moduleLength = strlen(moduleName);
    if (moduleLength < suffixLength)
        return FALSE;

    suffixStart = moduleName + moduleLength - suffixLength;
    if (strcmp(suffixStart, suffix) != 0)
        return FALSE;

    /*
     * Match the actual v8.library path component, not consumers such as
     * node-v8.library.  Debug module names may be bare names or use an AROS
     * volume/path separator.
     */
    return suffixStart == moduleName ||
        suffixStart[-1] == ':' ||
        suffixStart[-1] == '/' ||
        suffixStart[-1] == '\\';
}

static struct V8SymbolIndex v8SymbolIndex;
static volatile LONG v8SymbolIndexState;

AROS_UFH3(static void, V8_IndexSymbolHook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(struct SymbolInfo *, symbol, A1))
{
    AROS_USERFUNC_INIT

    struct V8SymbolIndex *index = (struct V8SymbolIndex *)hook->h_Data;

    (void)object;

    index->visited++;
    if (index->failed || symbol == NULL || symbol->si_SymbolName == NULL)
        return;

    if (symbol->si_ModuleName != index->lastModuleName)
    {
        index->lastModuleName = symbol->si_ModuleName;
        index->lastModuleIsV8 = V8_IsLibraryModule(symbol->si_ModuleName);
    }

    if (!index->lastModuleIsV8)
        return;

    if (index->count == index->capacity)
    {
        ULONG capacity = index->capacity ? index->capacity * 2 : 4096;
        struct V8SymbolEntry *entries =
            realloc(index->entries, capacity * sizeof(*entries));

        if (entries == NULL)
        {
            index->failed = TRUE;
            return;
        }

        index->entries = entries;
        index->capacity = capacity;
    }

    index->entries[index->count].name = symbol->si_SymbolName;
    index->entries[index->count].address = symbol->si_SymbolStart;
    index->count++;

    AROS_USERFUNC_EXIT
}

static int V8_CompareSymbolEntries(const void *left, const void *right)
{
    const struct V8SymbolEntry *leftEntry =
        (const struct V8SymbolEntry *)left;
    const struct V8SymbolEntry *rightEntry =
        (const struct V8SymbolEntry *)right;

    return strcmp(leftEntry->name, rightEntry->name);
}

static BOOL V8_BuildSymbolIndex(void)
{
    struct Hook hook = {0};

    memset(&v8SymbolIndex, 0, sizeof(v8SymbolIndex));
    hook.h_Entry = (HOOKFUNC)V8_IndexSymbolHook;
    hook.h_Data = &v8SymbolIndex;

    KPrintF("[V8-SYMBOL] index build begin DebugBase=%p\n", DebugBase);
    EnumerateSymbolsA(&hook, NULL);

    if (!v8SymbolIndex.failed && v8SymbolIndex.count != 0)
    {
        qsort(v8SymbolIndex.entries, v8SymbolIndex.count,
            sizeof(v8SymbolIndex.entries[0]), V8_CompareSymbolEntries);
    }

    KPrintF("[V8-SYMBOL] index build end visited=%lu indexed=%lu failed=%ld\n",
        v8SymbolIndex.visited, v8SymbolIndex.count,
        (LONG)v8SymbolIndex.failed);

    return !v8SymbolIndex.failed && v8SymbolIndex.count != 0;
}

static BOOL V8_EnsureSymbolIndex(void)
{
    for (;;)
    {
        LONG state =
            __atomic_load_n(&v8SymbolIndexState, __ATOMIC_ACQUIRE);

        if (state == 2)
            return TRUE;
        if (state == 3)
            return FALSE;

        if (state == 0)
        {
            LONG expected = 0;

            if (__atomic_compare_exchange_n(&v8SymbolIndexState, &expected, 1,
                    FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            {
                BOOL built = V8_BuildSymbolIndex();

                __atomic_store_n(&v8SymbolIndexState, built ? 2 : 3,
                    __ATOMIC_RELEASE);
                return built;
            }
        }

        Reschedule();
    }
}

static APTR V8_FindIndexedSymbol(CONST_STRPTR symbolName)
{
    ULONG low = 0;
    ULONG high = v8SymbolIndex.count;

    while (low < high)
    {
        ULONG middle = low + (high - low) / 2;
        int comparison =
            strcmp(symbolName, v8SymbolIndex.entries[middle].name);

        if (comparison < 0)
            high = middle;
        else if (comparison > 0)
            low = middle + 1;
        else
            return v8SymbolIndex.entries[middle].address;
    }

    return NULL;
}

AROS_UFH3(static void, V8_FindSymbolHook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(struct SymbolInfo *, symbol, A1))
{
    AROS_USERFUNC_INIT

    struct V8SymbolSearch *search = (struct V8SymbolSearch *)hook->h_Data;

    (void)object;

    search->visited++;
    if (search->address == NULL &&
        symbol != NULL &&
        symbol->si_SymbolName != NULL &&
        V8_IsLibraryModule(symbol->si_ModuleName) &&
        strcmp(symbol->si_SymbolName, search->name) == 0)
    {
        search->address = symbol->si_SymbolStart;
    }

    AROS_USERFUNC_EXIT
}

/*****************************************************************************

    NAME */
        AROS_LH1(APTR, V8FindEngineSymbol,

/*  SYNOPSIS */
        AROS_LHA(CONST_STRPTR, symbolName, A0),

/*  LOCATION */
        struct V8Base *, V8Base, 19, V8)

/*  FUNCTION
        Resolves one exact-build V8 symbol from the loaded v8.library.

    INPUTS
        symbolName - Mangled ELF symbol name from the matching Chromium V8
                     build.

    RESULT
        Function address, or NULL when the symbol is absent.

    NOTES
        This exact-build bridge depends on debug.library's retained module
        symbols. The first lookup builds a private sorted index; later lookups
        use a binary search without re-enumerating every loaded module.

    BUGS
        The caller and library must use the exact same V8 build and C++ ABI.

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    APTR address;

    (void)V8Base;

    if (symbolName == NULL || symbolName[0] == '\0')
        return NULL;

    if (V8_EnsureSymbolIndex())
    {
        address = V8_FindIndexedSymbol(symbolName);
        if (address == NULL)
            KPrintF("[V8-SYMBOL] indexed lookup miss name=%s\n", symbolName);
        return address;
    }
    else
    {
        struct Hook hook = {0};
        struct V8SymbolSearch search = {symbolName, NULL, 0};

        hook.h_Entry = (HOOKFUNC)V8_FindSymbolHook;
        hook.h_Data = &search;
        EnumerateSymbolsA(&hook, NULL);
        KPrintF("[V8-SYMBOL] fallback lookup name=%s visited=%lu result=%p\n",
            symbolName, search.visited, search.address);
        return search.address;
    }

    AROS_LIBFUNC_EXIT
}
