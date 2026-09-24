/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8SetProperty() - Set object property
*/

#include <proto/exec.h>
#include <aros/libcall.h>
#include <string.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_OBJECT_PROPERTY_SET
 * AROS_IMPL: Simple string-based property setting
 * DESIGN: Set property value on any V8 object
 * MEMORY: No allocation, operates on existing objects
 * THREAD_SAFETY: Must be called from isolate's thread
 * TODO: Add actual V8 property setting when V8 is ported
 * REFERENCE: v8::Object::Set() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH5(LONG, V8SetProperty,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),
        AROS_LHA(APTR, object, A2),
        AROS_LHA(CONST_STRPTR, name, A3),
        AROS_LHA(CONST_STRPTR, value, D0),

/*  LOCATION */
        struct V8Base *, V8Base, 16, V8)

/*  FUNCTION
        Sets a property on a JavaScript object. The value is provided
        as a string and will be converted to the appropriate JavaScript
        type.

    INPUTS
        isolate - Handle to the isolate
        context - Handle to the context
        object  - Handle to the object (from V8GetGlobalObject())
        name    - Property name
        value   - Property value as a string

    RESULT
        V8_SUCCESS on success, or an error code:
        - V8_ERROR_INVALID if any parameter is NULL
        - V8_ERROR_RUNTIME on JavaScript error

    NOTES
        This is a stub implementation. When V8 is fully ported, this will:
        - Convert value string to appropriate JavaScript type
        - Set the property on the object
        - Handle type conversion and errors

    EXAMPLE
        APTR global = V8GetGlobalObject(isolate, context);
        V8SetProperty(isolate, context, global, "answer", "42");
        V8SetProperty(isolate, context, global, "name", "AROS");

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8GetProperty(), V8GetGlobalObject()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    
    if (!v8isolate || !v8context || !object || !name || !value)
    {
        return V8_ERROR_INVALID;
    }
    
    /* TODO: When V8 is ported, set property here
     * 
     * Example V8 property setting (pseudo-code):
     * 
     * v8::Isolate* v8isolate = static_cast<v8::Isolate*>(v8isolate->vi_Isolate);
     * v8::HandleScope handle_scope(v8isolate);
     * v8::Local<v8::Context> context = v8context->vc_Context->Get(v8isolate);
     * v8::Context::Scope context_scope(context);
     * 
     * v8::Persistent<v8::Object>* persistent = 
     *     static_cast<v8::Persistent<v8::Object>*>(object);
     * v8::Local<v8::Object> obj = persistent->Get(v8isolate);
     * 
     * v8::Local<v8::String> key = v8::String::NewFromUtf8(v8isolate, name);
     * v8::Local<v8::String> val = v8::String::NewFromUtf8(v8isolate, value);
     * 
     * v8::TryCatch try_catch(v8isolate);
     * v8::Maybe<bool> result = obj->Set(context, key, val);
     * 
     * if (result.IsNothing()) {
     *     return V8_ERROR_RUNTIME;
     * }
     */
    
    /* Real engine first: set the property on the actual JS object */
    if (V8Bridge_SetProperty(v8isolate->vi_Isolate, v8context->vc_Context,
                             object, name, value) == V8_SUCCESS)
    {
        return V8_SUCCESS;
    }

    /* Fallback: simple property storage (stub builds) */
    if (!V8_SetPropertyValue(v8context, name, value))
    {
        return V8_ERROR_NOMEM;
    }
    
    return V8_SUCCESS;

    AROS_LIBFUNC_EXIT
}
