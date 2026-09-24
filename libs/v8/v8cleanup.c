/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8Cleanup() - Cleanup the V8 engine
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_ENGINE_CLEANUP
 * AROS_IMPL: Thread-safe cleanup with semaphore protection
 * DESIGN: Should be called when application is done with V8
 * MEMORY: Platform cleanup through V8 bridge and AROS platform implementation
 * THREAD_SAFETY: Protected by library semaphore
 * INTEGRATION: Calls V8Bridge_ShutdownPlatform() to cleanup V8 AROS platform
 * REFERENCE: v8::V8::Dispose() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH0(void, V8Cleanup,

/*  LOCATION */
        struct V8Base *, V8Base, 6, V8)

/*  FUNCTION
        Cleans up the V8 JavaScript engine. Should be called when the
        application is done using V8. After this call, V8Initialize()
        must be called again before using any other V8 functions.

    INPUTS
        None

    RESULT
        None

    NOTES
        This cleans up the V8 JavaScript engine using the AROS platform
        implementation. The cleanup:
        - Shuts down V8 platform instance
        - Frees memory management resources
        - Cleans up threading resources
        - Disposes of isolate management

    EXAMPLE

    BUGS
        Full V8 JavaScript engine features pending complete V8 source integration

    SEE ALSO
        V8Initialize()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    ObtainSemaphore(&V8Base->v8_Semaphore);
    
    if (V8Base->v8_Initialized)
    {
        /* Shutdown V8 platform through C++ bridge */
        V8Bridge_ShutdownPlatform();
        
        /* Mark as cleaned up */
        V8Base->v8_Platform = NULL;
        V8Base->v8_ArrayBuffer = NULL;
        V8Base->v8_Initialized = FALSE;
    }
    
    ReleaseSemaphore(&V8Base->v8_Semaphore);

    AROS_LIBFUNC_EXIT
}
