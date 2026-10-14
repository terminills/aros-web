/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8OwnerCheckAdd() - hand pointers a predicate claims back to the
          heap that owns them.
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_allocator.h"

/*****************************************************************************

    NAME */
        AROS_LH2(BOOL, V8OwnerCheckAdd,

/*  SYNOPSIS */
        AROS_LHA(APTR, ownsFn, A0),
        AROS_LHA(APTR, freeFn, A1),

/*  LOCATION */
        struct V8Base *, V8Base, 24, V8)

/*  FUNCTION
        Declare that every pointer for which ownsFn returns TRUE belongs to a
        heap whose free function is freeFn. From then on, when this library's
        operator delete is given such a pointer - an object the embedder
        allocated and handed to V8 to destroy - it calls freeFn instead of
        freeing the block to its own allocator.

        For heaps that are not a few contiguous ranges: PartitionAlloc reserving
        super pages one at a time answers with its own bitmap.

    INPUTS
        ownsFn - BOOL (*)(void *) that tells whether a pointer is the heap's;
                 called from any task, so it must not block
        freeFn - void (*)(void *) that frees a block of that heap

    RESULT
        TRUE if registered. FALSE if an argument is NULL, ownsFn is already
        registered, or the table is full.

    NOTES
        Registered ranges are consulted first, then the predicates.

    SEE ALSO
        V8OwnerCheckRemove(), V8OwnerRangeAdd(), libs/v8/v8_allocator.cpp

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)V8Base;

    return V8OwnerCheckAddImpl((V8OwnsFunc)ownsFn, (V8FreeFunc)freeFn);

    AROS_LIBFUNC_EXIT
}
