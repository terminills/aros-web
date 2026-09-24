/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8Initialize() - Initialize the V8 engine
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_ENGINE_INITIALIZATION
 * AROS_IMPL: Thread-safe initialization with semaphore protection
 * DESIGN: Lazy initialization - V8 platform created on first call
 * MEMORY: Platform created through V8 bridge and AROS platform implementation
 * THREAD_SAFETY: Protected by library semaphore
 * INTEGRATION: Calls V8Bridge_InitializePlatform() to initialize V8 AROS platform
 * REFERENCE: v8::V8::InitializePlatform() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH0(APTR, V8Initialize,

/*  LOCATION */
        struct V8Base *, V8Base, 5, V8)

/*  FUNCTION
        Initializes the V8 JavaScript engine. This must be called before
        using any other V8 functions. The function is idempotent - multiple
        calls are safe and will return the same result.

    INPUTS
        None

    RESULT
        Platform handle on success, NULL on failure

    NOTES
        This initializes the V8 JavaScript engine using the AROS platform
        implementation from external/electron/v8. The implementation:
        - Creates V8 platform instance with AROS integration
        - Initializes memory management using AROS memory pools
        - Sets up threading using AROS task system
        - Configures isolate management

    EXAMPLE

    BUGS
        Full V8 JavaScript engine features pending complete V8 source integration

    SEE ALSO
        V8Cleanup()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    APTR platform = NULL;
    
    ObtainSemaphore(&V8Base->v8_Semaphore);
    
    if (!V8Base->v8_Initialized)
    {
        /* Initialize V8 platform through C++ bridge */
        BOOL success = V8Bridge_InitializePlatform();
        
        if (success)
        {
            /* Mark as initialized and store platform handle */
            V8Base->v8_Platform = (APTR)0x1; /* Non-NULL to indicate initialized */
            V8Base->v8_Initialized = TRUE;
            platform = V8Base->v8_Platform;
        }
        else
        {
            /* Initialization failed */
            platform = NULL;
        }
    }
    else
    {
        platform = V8Base->v8_Platform;
    }
    
    ReleaseSemaphore(&V8Base->v8_Semaphore);
    
    return platform;

    AROS_LIBFUNC_EXIT
}
