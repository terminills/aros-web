/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8AllocatorInstall() - library entry point for the allocator seam.
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_allocator.h"

/*****************************************************************************

    NAME */
        AROS_LH2(BOOL, V8AllocatorInstall,

/*  SYNOPSIS */
        AROS_LHA(APTR, allocFn, A0),
        AROS_LHA(APTR, freeFn, A1),

/*  LOCATION */
        struct V8Base *, V8Base, 20, V8)

/*  FUNCTION
        Route this library's operator new/delete through the caller's
        allocator, so a process that also loads a Chromium component with its
        own heap (cef-blink.library, which uses PartitionAlloc) has ONE heap
        rather than two.

        Without this, an object allocated by Blink and destroyed by V8 is freed
        to the wrong allocator and corrupts stdc.library's TLSF free list. That
        is the known failure on script-heavy pages.

    INPUTS
        allocFn - void *(*)(size_t), must not be NULL
        freeFn  - void (*)(void *), must not be NULL

    RESULT
        TRUE if installed. FALSE if it is TOO LATE - this library has already
        served an allocation from the default heap, and honouring the request
        would free those objects to a different allocator.

        A FALSE return is fatal to correctness and must not be ignored: it
        means the caller installed after something already allocated. Install
        before anything else touches V8, remembering that node uses V8 and, in
        Electron, runs before CEF is initialised.

    NOTES
        Calling with the pair already installed is a no-op returning TRUE.
        Installing a DIFFERENT pair afterwards is refused.

        Without any call the library uses malloc/free, which is the behaviour
        it had before this entry point existed.

    SEE ALSO
        workbench/libs/v8/v8_allocator.cpp for the reasoning and the ordering
        constraint.

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)V8Base;

    return V8AllocatorInstallImpl((V8AllocFunc)allocFn, (V8FreeFunc)freeFn);

    AROS_LIBFUNC_EXIT
}
