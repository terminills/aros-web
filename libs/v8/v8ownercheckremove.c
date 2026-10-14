/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8OwnerCheckRemove() - retire an owner predicate.
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_allocator.h"

/*****************************************************************************

    NAME */
        AROS_LH1(BOOL, V8OwnerCheckRemove,

/*  SYNOPSIS */
        AROS_LHA(APTR, ownsFn, A0),

/*  LOCATION */
        struct V8Base *, V8Base, 25, V8)

/*  FUNCTION
        Retire the predicate registered with V8OwnerCheckAdd(). Call it before
        the heap behind it goes away, typically at process exit, once no
        pointer from that heap is still held by V8.

    INPUTS
        ownsFn - the predicate passed to V8OwnerCheckAdd()

    RESULT
        TRUE if the predicate was removed, FALSE otherwise.

    SEE ALSO
        V8OwnerCheckAdd(), libs/v8/v8_allocator.cpp

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)V8Base;

    return V8OwnerCheckRemoveImpl((V8OwnsFunc)ownsFn);

    AROS_LIBFUNC_EXIT
}
