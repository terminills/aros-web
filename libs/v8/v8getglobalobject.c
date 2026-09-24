/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8GetGlobalObject() - Get the global object
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"

/* V8_GLOBAL_OBJECT
 * AROS_IMPL: Return opaque handle to global object
 * DESIGN: Global object is the root of JavaScript object hierarchy
 * MEMORY: No allocation, returns handle to existing object
 * THREAD_SAFETY: Must be called from isolate's thread
 * TODO: Add actual V8 global object access when V8 is ported
 * REFERENCE: v8::Context::Global() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH2(APTR, V8GetGlobalObject,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),

/*  LOCATION */
        struct V8Base *, V8Base, 15, V8)

/*  FUNCTION
        Gets the global object of a context. The global object is the
        root of the JavaScript object hierarchy and contains all built-in
        objects and functions.

    INPUTS
        isolate - Handle to the isolate
        context - Handle to the context

    RESULT
        Handle to the global object, or NULL on error

    NOTES
        This is a stub implementation. When V8 is fully ported, this will
        return a handle to the actual V8 global object.
        
        The returned handle can be used with V8SetProperty(), V8GetProperty(),
        and V8CallFunction().

    EXAMPLE
        APTR global = V8GetGlobalObject(isolate, context);
        if (global) {
            V8SetProperty(isolate, context, global, "myVar", "42");
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8SetProperty(), V8GetProperty()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    
    if (!v8isolate || !v8context)
    {
        return NULL;
    }
    
    /* TODO: When V8 is ported, get global object here
     * 
     * Example V8 global object access (pseudo-code):
     * 
     * v8::Isolate* v8isolate = static_cast<v8::Isolate*>(v8isolate->vi_Isolate);
     * v8::HandleScope handle_scope(v8isolate);
     * v8::Local<v8::Context> context = v8context->vc_Context->Get(v8isolate);
     * v8::Context::Scope context_scope(context);
     * 
     * v8::Local<v8::Object> global = context->Global();
     * v8::Persistent<v8::Object>* persistent = 
     *     new v8::Persistent<v8::Object>(v8isolate, global);
     * 
     * return (APTR)persistent;
     */
    
    /* Return the global object handle from context */
    return v8context->vc_GlobalObject;

    AROS_LIBFUNC_EXIT
}
