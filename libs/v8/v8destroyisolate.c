/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8DestroyIsolate() - Destroy a V8 isolate
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_ISOLATE_DESTRUCTION
 * AROS_IMPL: Free isolate structure through exec.library
 * DESIGN: Clean up all contexts and scripts before destroying isolate
 * MEMORY: Free all allocations with FreeVec
 * THREAD_SAFETY: Caller must ensure no other threads are using the isolate
 * TODO: Destroy actual V8 isolate when V8 is ported
 * REFERENCE: v8::Isolate::Dispose() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH1(void, V8DestroyIsolate,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),

/*  LOCATION */
        struct V8Base *, V8Base, 8, V8)

/*  FUNCTION
        Destroys a V8 isolate and frees all associated resources. All
        contexts and scripts created in this isolate are also destroyed.

    INPUTS
        isolate - Handle to the isolate to destroy

    RESULT
        None

    NOTES
        This is a stub implementation. When V8 is fully ported, this will
        properly dispose of the V8 isolate.
        
        After this call, the isolate handle is no longer valid.

    EXAMPLE

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8CreateIsolate()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    
    if (!v8isolate)
    {
        return;
    }
    
    /* Remove from library's isolate list */
    ObtainSemaphore(&V8Base->v8_Semaphore);
    Remove((struct Node *)v8isolate);
    ReleaseSemaphore(&V8Base->v8_Semaphore);
    
    /* Free the isolate */
    V8_FreeIsolate(V8Base, v8isolate);

    AROS_LIBFUNC_EXIT
}

/****************************************************************************/

/* Internal function to free isolate */
void V8_FreeIsolate(struct V8Base *V8Base, struct V8Isolate *isolate)
{
    struct V8Context *context, *nextContext;
    struct V8Script *script, *nextScript;
    
    if (!isolate)
    {
        return;
    }
    
    ObtainSemaphore(&isolate->vi_Semaphore);
    
    /* Free all contexts */
    ForeachNodeSafe(&isolate->vi_Contexts, context, nextContext)
    {
        V8_FreeContext(context);
    }
    
    /* Free all scripts */
    ForeachNodeSafe(&isolate->vi_Scripts, script, nextScript)
    {
        V8_FreeScript(script);
    }
    
    /* Dispose of actual V8 isolate through C++ bridge */
    if (isolate->vi_Isolate)
    {
        V8Bridge_DestroyIsolate(isolate->vi_Isolate);
    }
    
    ReleaseSemaphore(&isolate->vi_Semaphore);
    
    /* Free the isolate structure */
    FreeVec(isolate);
}
