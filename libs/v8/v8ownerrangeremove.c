/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8OwnerRangeRemove() - retire an owned address range.
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_allocator.h"

/*****************************************************************************

    NAME */
        AROS_LH1(BOOL, V8OwnerRangeRemove,

/*  SYNOPSIS */
        AROS_LHA(APTR, base, A0),

/*  LOCATION */
        struct V8Base *, V8Base, 23, V8)

/*  FUNCTION
        Retire the range registered with V8OwnerRangeAdd() that starts at base.
        Call it before the heap behind the range goes away, typically at
        process exit, once no pointer from that heap is still held by V8.

    INPUTS
        base - the base passed to V8OwnerRangeAdd()

    RESULT
        TRUE if a range starting at base was removed, FALSE otherwise.

    SEE ALSO
        V8OwnerRangeAdd(), libs/v8/v8_allocator.cpp

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)V8Base;

    return V8OwnerRangeRemoveImpl(base);

    AROS_LIBFUNC_EXIT
}
