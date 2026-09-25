/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8OwnerRangeAdd() - hand pointers in an address range back to the
          heap that owns them.
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_allocator.h"

/*****************************************************************************

    NAME */
        AROS_LH3(BOOL, V8OwnerRangeAdd,

/*  SYNOPSIS */
        AROS_LHA(APTR, base, A0),
        AROS_LHA(IPTR, size, D0),
        AROS_LHA(APTR, freeFn, A1),

/*  LOCATION */
        struct V8Base *, V8Base, 22, V8)

/*  FUNCTION
        Declare that the memory in [base, base + size) belongs to a heap whose
        free function is freeFn. From then on, when this library's operator
        delete is given a pointer inside that range - an object the embedder
        allocated and handed to V8 to destroy - it calls freeFn instead of
        freeing the block to its own allocator.

        AROS has one address space, so this works whichever task or process
        releases the pointer: ownership is decided by the address, not by the
        caller.

    INPUTS
        base   - start of the range
        size   - length of the range in bytes, not 0
        freeFn - void (*)(void *) that frees a block of that heap

    RESULT
        TRUE if registered. FALSE if the arguments are invalid, the range
        overlaps one already registered, or the table is full.

    NOTES
        A browser registers each PartitionAlloc pool once the pools are
        reserved, and removes them with V8OwnerRangeRemove() before it exits.

    SEE ALSO
        V8OwnerRangeRemove(), libs/v8/v8_allocator.cpp

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)V8Base;

    return V8OwnerRangeAddImpl(base, size, (V8FreeFunc)freeFn);

    AROS_LIBFUNC_EXIT
}
