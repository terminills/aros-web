/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: V8AllocatorIsInstalled() - read back the allocator seam's state.
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_allocator.h"

/*****************************************************************************

    NAME */
        AROS_LH0(BOOL, V8AllocatorIsInstalled,

/*  SYNOPSIS */
        /* void */

/*  LOCATION */
        struct V8Base *, V8Base, 21, V8)

/*  FUNCTION
        Report whether a non-default allocator has been installed through
        V8AllocatorInstall().

    RESULT
        TRUE  - this library's operator new/delete route through an installed
                allocator, so the process shares one heap.
        FALSE - the default malloc/free is still in use.

    NOTES
        This exists because the libraries involved cannot log. v8.library and
        electron.library are DISK libraries, and on a pc target kprintf needs
        _arosdebuglock from the kernel ROM, so a disk-loaded library that calls
        it fails to link. Without a read-back, "the shared heap was installed"
        and "the install silently did nothing" look identical from outside -
        the Electron lifecycle passes either way. This turns that into
        something a test can assert.

    SEE ALSO
        V8AllocatorInstall(), workbench/libs/v8/v8_allocator.cpp

******************************************************************************/
{
    AROS_LIBFUNC_INIT

    (void)V8Base;

    return V8AllocatorIsInstalledImpl();

    AROS_LIBFUNC_EXIT
}
