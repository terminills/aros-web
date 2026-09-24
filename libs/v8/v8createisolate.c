/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8CreateIsolate() - Create a new V8 isolate
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_ISOLATE_CREATION
 * AROS_IMPL: Allocate isolate structure through exec.library and V8 platform
 * DESIGN: Each isolate is independent JavaScript VM instance
 * MEMORY: Isolate structure allocated with AllocVec, V8 isolate via bridge
 * THREAD_SAFETY: Each isolate is single-threaded
 * INTEGRATION: Calls V8Bridge_CreateIsolate() to create V8 isolate
 * REFERENCE: v8::Isolate::New() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH0(V8IsolateHandle, V8CreateIsolate,

/*  LOCATION */
        struct V8Base *, V8Base, 7, V8)

/*  FUNCTION
        Creates a new V8 isolate. An isolate is an isolated instance of
        the V8 engine with its own heap and garbage collector. Multiple
        isolates can run concurrently but cannot share objects.

    INPUTS
        None

    RESULT
        Handle to the new isolate, or NULL on failure

    NOTES
        This creates a V8 isolate using the AROS platform implementation.
        Each isolate is an independent JavaScript VM instance with its own
        heap and garbage collector.
        
        Each isolate should be destroyed with V8DestroyIsolate() when no
        longer needed.

    EXAMPLE

    BUGS
        Full V8 JavaScript engine features pending complete V8 source integration

    SEE ALSO
        V8DestroyIsolate(), V8CreateContext()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *isolate;
    
    /* Ensure V8 is initialized */
    if (!V8Base->v8_Initialized)
    {
        V8Initialize();
    }
    
    /* Allocate internal isolate structure */
    isolate = V8_AllocIsolate(V8Base, V8F_ISOLATE_DEFAULT);
    if (!isolate)
    {
        return NULL;
    }
    
    /* Add to library's isolate list */
    ObtainSemaphore(&V8Base->v8_Semaphore);
    AddTail((struct List *)&V8Base->v8_Isolates, (struct Node *)isolate);
    ReleaseSemaphore(&V8Base->v8_Semaphore);
    
    return (V8IsolateHandle)isolate;

    AROS_LIBFUNC_EXIT
}

/****************************************************************************/

/* Internal function to allocate isolate */
struct V8Isolate *V8_AllocIsolate(struct V8Base *V8Base, ULONG flags)
{
    struct V8Isolate *isolate;
    
    isolate = AllocVec(sizeof(struct V8Isolate), MEMF_PUBLIC | MEMF_CLEAR);
    if (!isolate)
    {
        return NULL;
    }
    
    /* Initialize isolate structure */
    InitSemaphore(&isolate->vi_Semaphore);
    NEWLIST(&isolate->vi_Contexts);
    NEWLIST(&isolate->vi_Scripts);
    isolate->vi_Flags = flags;
    
    /* Create actual V8 isolate through C++ bridge */
    APTR v8isolate = V8Bridge_CreateIsolate();
    if (!v8isolate)
    {
        FreeVec(isolate);
        return NULL;
    }
    
    isolate->vi_Isolate = v8isolate;
    
    return isolate;
}
