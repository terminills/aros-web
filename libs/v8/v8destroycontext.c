/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8DestroyContext() - Destroy a V8 context
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_CONTEXT_DESTRUCTION
 * AROS_IMPL: Free context structure through exec.library
 * DESIGN: Clean up context and remove from isolate
 * MEMORY: Free all allocations with FreeVec
 * THREAD_SAFETY: Caller must ensure no code is running in the context
 * TODO: Destroy actual V8 context when V8 is ported
 * REFERENCE: v8::Persistent::Reset() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH2(void, V8DestroyContext,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),

/*  LOCATION */
        struct V8Base *, V8Base, 10, V8)

/*  FUNCTION
        Destroys a V8 context and frees all associated resources.

    INPUTS
        isolate - Handle to the isolate containing the context
        context - Handle to the context to destroy

    RESULT
        None

    NOTES
        This is a stub implementation. When V8 is fully ported, this will
        properly dispose of the V8 context.
        
        After this call, the context handle is no longer valid.

    EXAMPLE

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8CreateContext()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    
    if (!v8isolate || !v8context)
    {
        return;
    }
    
    /* Remove from isolate's context list */
    ObtainSemaphore(&v8isolate->vi_Semaphore);
    Remove((struct Node *)v8context);
    ReleaseSemaphore(&v8isolate->vi_Semaphore);
    
    /* Free the context */
    V8_FreeContext(v8context);

    AROS_LIBFUNC_EXIT
}

/****************************************************************************/

/* Internal function to free context */
void V8_FreeContext(struct V8Context *context)
{
    if (!context)
    {
        return;
    }
    
    /* Dispose of actual V8 context through C++ bridge */
    if (context->vc_Context)
    {
        V8Bridge_DestroyContext(context->vc_Context);
    }
    
    /* Free all properties */
    V8_FreeProperties(context);
    
    /* Free the context structure */
    FreeVec(context);
}
