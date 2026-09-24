/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8CreateContext() - Create a new V8 context
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_CONTEXT_CREATION
 * AROS_IMPL: Allocate context structure through exec.library
 * DESIGN: Context provides isolated JavaScript execution environment
 * MEMORY: Context structure allocated with AllocVec
 * THREAD_SAFETY: Context bound to isolate's thread
 * TODO: Create actual V8 context when V8 is ported
 * REFERENCE: v8::Context::New() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH1(V8ContextHandle, V8CreateContext,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),

/*  LOCATION */
        struct V8Base *, V8Base, 9, V8)

/*  FUNCTION
        Creates a new V8 context within an isolate. A context provides
        a complete JavaScript execution environment with its own global
        object and built-in objects.

    INPUTS
        isolate - Handle to the isolate in which to create the context

    RESULT
        Handle to the new context, or NULL on failure

    NOTES
        This is a stub implementation. When V8 is fully ported, this will
        create an actual V8 context with proper global object initialization.
        
        Each context should be destroyed with V8DestroyContext() when no
        longer needed.

    EXAMPLE

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8DestroyContext(), V8CreateIsolate()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *context;
    
    if (!v8isolate)
    {
        return NULL;
    }
    
    /* Allocate internal context structure */
    context = V8_AllocContext(v8isolate);
    if (!context)
    {
        return NULL;
    }
    
    /* Add to isolate's context list */
    ObtainSemaphore(&v8isolate->vi_Semaphore);
    AddTail((struct List *)&v8isolate->vi_Contexts, (struct Node *)context);
    ReleaseSemaphore(&v8isolate->vi_Semaphore);
    
    return (V8ContextHandle)context;

    AROS_LIBFUNC_EXIT
}

/****************************************************************************/

/* Internal function to allocate context */
struct V8Context *V8_AllocContext(struct V8Isolate *isolate)
{
    struct V8Context *context;
    
    context = AllocVec(sizeof(struct V8Context), MEMF_PUBLIC | MEMF_CLEAR);
    if (!context)
    {
        return NULL;
    }
    
    /* Initialize context structure */
    context->vc_Isolate = isolate;
    
    /* Initialize property list for simple property storage */
    NEWLIST(&context->vc_Properties);
    
    /* Create actual V8 context through C++ bridge */
    APTR v8context = V8Bridge_CreateContext(isolate->vi_Isolate);
    if (!v8context)
    {
        FreeVec(context);
        return NULL;
    }
    
    context->vc_Context = v8context;
    context->vc_GlobalObject = V8Bridge_GetGlobalObject(isolate->vi_Isolate, v8context);
    
    return context;
}
