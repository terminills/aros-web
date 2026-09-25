/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8OwnerRangeAdd/Remove - a block from a registered range is freed
          by its owner, not by v8.library's own allocator.

    The test calls v8.library's OWN operator delete (found with
    V8FindEngineSymbol) on blocks carved out of a private arena, the way V8
    destroys an object an embedder allocated. The arena's free function counts
    what it is given; nothing in the arena is ever passed to stdc's free, so a
    routing failure shows up as a wrong count (or, if it slipped through to
    TLSF, as the free-list trap this exists to prevent).

    Prints one "OWNER: PASS|FAIL <what>" line per check and "OWNER: done".
*/

#include <proto/exec.h>
#include <proto/v8.h>

/* Opened here rather than linked: uselibs=v8 would pull in the whole engine
   archive, not just the library calls. */
struct Library *V8Base;
#include <stdio.h>
#include <string.h>

typedef void (*DeleteFn)(void *);
typedef void *(*NewFn)(unsigned long);

static unsigned char arena[64 * 1024];
static int arena_frees;
static void *arena_last;

static void ArenaFree(void *p)
{
    arena_frees++;
    arena_last = p;
}

static int failures;

static void check(int ok, const char *what)
{
    printf("OWNER: %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

int main(void)
{
    V8Base = OpenLibrary((CONST_STRPTR)"v8.library", 1);
    if (V8Base == NULL || V8Base->lib_Version < 1
        || (V8Base->lib_Version == 1 && V8Base->lib_Revision < 1))
    {
        printf("OWNER: FAIL v8.library 1.1 not available\n");
        if (V8Base)
            CloseLibrary(V8Base);
        return 20;
    }

    DeleteFn v8_delete = (DeleteFn)V8FindEngineSymbol((CONST_STRPTR)"_ZdlPv");
    NewFn    v8_new    = (NewFn)V8FindEngineSymbol((CONST_STRPTR)"_Znwm");

    check(v8_delete != NULL && v8_new != NULL, "engine operator new/delete found");
    if (v8_delete == NULL || v8_new == NULL)
    {
        CloseLibrary(V8Base);
        return 20;
    }

    check(V8OwnerRangeAdd(arena, sizeof(arena), (APTR)ArenaFree), "arena range registered");
    check(!V8OwnerRangeAdd(arena + 100, 16, (APTR)ArenaFree), "overlapping range refused");
    check(!V8OwnerRangeAdd(NULL, 16, (APTR)ArenaFree), "NULL base refused");

    /* A block inside the arena, destroyed by V8: must reach ArenaFree. */
    v8_delete(arena + 4096);
    check(arena_frees == 1 && arena_last == arena + 4096, "in-range delete went to the owner");

    /* The first and last byte of the range belong to it; one past does not. */
    v8_delete(arena);
    v8_delete(arena + sizeof(arena) - 1);
    check(arena_frees == 3, "range bounds inclusive of first and last byte");

    /* A block V8 allocated itself must still go to V8's allocator. */
    {
        void *own = v8_new(64);
        int before = arena_frees;
        memset(own, 0xa5, 64);
        v8_delete(own);
        check(own != NULL && arena_frees == before, "v8-allocated block freed by v8, not the owner");
    }

    check(V8OwnerRangeRemove(arena), "arena range removed");
    check(!V8OwnerRangeRemove(arena), "second remove reports nothing to remove");

    /* After removal the arena must no longer be claimed; do not actually free
       an arena pointer here (it would go to stdc), just re-register to prove
       the slot was released. */
    check(V8OwnerRangeAdd(arena, sizeof(arena), (APTR)ArenaFree), "slot reusable after remove");
    V8OwnerRangeRemove(arena);

    printf("OWNER: done failures=%d\n", failures);
    CloseLibrary(V8Base);
    return failures ? 10 : 0;
}
