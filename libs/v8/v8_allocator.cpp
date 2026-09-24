/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: v8.library's allocator seam - one heap per process, chosen once.

    WHY THIS FILE EXISTS

    On AROS each Chromium component that becomes a .library gets its own
    operator new/delete, and that is a correctness problem rather than a
    tidiness one. Today:

        v8.library        operator new -> libstdc++ -> stdc.library malloc
        node.library      operator new -> libstdc++ -> stdc.library malloc
        cef-blink.library operator new -> PartitionAlloc  (PA-E is on there)

    Two heaps in one process that know nothing about each other. Anything
    allocated on one side and destroyed on the other is freed to the wrong
    allocator. That is the proven root cause of the CEF crash on script-heavy
    pages: Blink's background script streamer allocates a chunk vector in
    cef-blink.library and hands it to V8 to destroy, so the free lands in
    stdc's TLSF with a PartitionAlloc pointer and corrupts the free list
    ("FREE outside TLSF area").

    THE FIX THAT WAS TRIED FIRST, AND WHY THIS IS BETTER

    The first attempt turned PA-E off in the CEF build so everything used
    stdc's malloc. It removed the corruption and replaced it with something
    worse: stdc's malloc needs a library base, and on a Chromium-created
    thread that base is NULL, so every page faulted early
    (CR2 = offsetof(StdCIntBase, mempool)). A late crash on some pages became
    an early crash on all of them.

    PartitionAlloc needs no AROS library base, so the shared heap should be
    PartitionAlloc and the redirection should happen HERE - in the library
    that currently has no allocator of its own - rather than in the one that
    does.

    ORDERING IS THE WHOLE DESIGN

    Electron's lifecycle runs node FIRST and only then initialises CEF
    (see workbench/libs/electron/test_electron_lifecycle.c). Node uses V8. So
    switching allocator when CEF arrives would leave every V8 object created
    during node's phase allocated by stdc and freed by PartitionAlloc - the
    same bug, inverted, and harder to see.

    Therefore the allocator is chosen ONCE, before anything allocates, and is
    then immutable:

      * The default is malloc/free, i.e. exactly today's behaviour. A process
        that only loads node.library and v8.library is bit-for-bit unchanged.
      * An embedder that will also load cef-blink.library (Electron) installs
        PartitionAlloc BEFORE calling anything else.
      * Once a single allocation has been served, the choice latches. A late
        install is REFUSED and reported rather than honoured, because honouring
        it would corrupt the heap exactly like the bug this file exists to fix.

    Refusing loudly is the point: a diagnostic beats a heap corruption that
    surfaces minutes later inside an unrelated free.
*/

#include <proto/exec.h>

#include <stdlib.h>
#include <new>

#include "v8_allocator.h"

namespace
{

/* Default: the C library this module already used before this file existed. */
void *DefaultAlloc(size_t size)
{
    return malloc(size);
}

void DefaultFree(void *ptr)
{
    free(ptr);
}

V8AllocFunc  g_alloc = DefaultAlloc;
V8FreeFunc   g_free  = DefaultFree;

/* Set on the first allocation served. After this the vtable is frozen: see
   the ordering note at the top of the file. */
volatile BOOL g_allocator_used = FALSE;
BOOL          g_allocator_installed = FALSE;

inline void *V8HeapAllocate(size_t size)
{
    /* operator new(0) must return a distinct non-null pointer. */
    if (size == 0)
        size = 1;

    g_allocator_used = TRUE;

    void *p = g_alloc(size);
    /* V8 is built without exceptions on this target, so a throwing operator
       new cannot actually throw here; returning NULL lets the caller's own
       OOM handling run instead of unwinding through a nothrow boundary. */
    return p;
}

inline void V8HeapRelease(void *ptr)
{
    if (ptr == nullptr)
        return;

    /* Deliberately NOT gated on g_allocator_used: a free can only reach here
       after an allocation, and setting the flag on the free path would hide an
       ordering mistake rather than expose it. */
    g_free(ptr);
}

} /* namespace */

/*****************************************************************************

    Install the process-wide allocator for this library.

    Returns TRUE if installed, FALSE if it is already too late - see the
    ordering note at the top of this file. A FALSE return must be treated as
    fatal by the embedder: continuing means two heaps.

*****************************************************************************/
extern "C" BOOL V8AllocatorInstallImpl(V8AllocFunc allocFn, V8FreeFunc freeFn)
{
    if (allocFn == nullptr || freeFn == nullptr)
        return FALSE;

    if (g_allocator_installed)
    {
        /* Idempotent only if it is the same pair; a DIFFERENT pair is the
           dangerous case and is refused. */
        return (g_alloc == allocFn && g_free == freeFn);
    }

    if (g_allocator_used)
    {
        /* Too late: memory has already been served from the default heap, and
           honouring this would free those objects to a different allocator.
           Reported by return value ONLY - deliberately no bug()/kprintf here.
           v8.library is a DISK library, and on a pc target kprintf needs
           _arosdebuglock, a symbol that lives in the kernel ROM, so any
           disk-loaded library calling it fails to LINK. Hosted builds hide
           that because their arossupport uses a different vkprintf.
           See aros-kprintf-unlinkable-in-disk-libraries. */
        return FALSE;
    }

    g_alloc = allocFn;
    g_free = freeFn;
    g_allocator_installed = TRUE;
    return TRUE;
}

extern "C" BOOL V8AllocatorIsInstalledImpl(void)
{
    return g_allocator_installed;
}

/*
 * These definitions win over libstdc++'s weak ones for everything linked into
 * this module, which is what redirects V8's own new/delete. All the standard
 * shapes are provided together: a partial set silently leaves some
 * allocations on libstdc++'s malloc path, which would reintroduce exactly the
 * split this file removes.
 */
void *operator new(size_t size)                            { return V8HeapAllocate(size); }
void *operator new[](size_t size)                          { return V8HeapAllocate(size); }
void *operator new(size_t size, const std::nothrow_t &) noexcept   { return V8HeapAllocate(size); }
void *operator new[](size_t size, const std::nothrow_t &) noexcept { return V8HeapAllocate(size); }

void operator delete(void *ptr) noexcept                   { V8HeapRelease(ptr); }
void operator delete[](void *ptr) noexcept                 { V8HeapRelease(ptr); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept   { V8HeapRelease(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { V8HeapRelease(ptr); }

/* Sized deletes (C++14). GCC emits these in preference to the unsized forms,
   so omitting them would leave most of V8's deallocations on libstdc++. */
void operator delete(void *ptr, size_t) noexcept           { V8HeapRelease(ptr); }
void operator delete[](void *ptr, size_t) noexcept         { V8HeapRelease(ptr); }
