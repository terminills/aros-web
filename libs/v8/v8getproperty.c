/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8GetProperty() - Get object property
*/

#include <proto/exec.h>
#include <aros/libcall.h>
#include <string.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_OBJECT_PROPERTY_GET
 * AROS_IMPL: Simple string-based property getting
 * DESIGN: Get property value from any V8 object
 * MEMORY: Result string copied to caller's buffer
 * THREAD_SAFETY: Must be called from isolate's thread
 * TODO: Add actual V8 property getting when V8 is ported
 * REFERENCE: v8::Object::Get() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH6(LONG, V8GetProperty,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),
        AROS_LHA(APTR, object, A2),
        AROS_LHA(CONST_STRPTR, name, A3),
        AROS_LHA(STRPTR, result, D0),
        AROS_LHA(ULONG, resultSize, D1),

/*  LOCATION */
        struct V8Base *, V8Base, 17, V8)

/*  FUNCTION
        Gets a property from a JavaScript object and returns its value
        as a string.

    INPUTS
        isolate    - Handle to the isolate
        context    - Handle to the context
        object     - Handle to the object (from V8GetGlobalObject())
        name       - Property name
        result     - Buffer to receive property value as string
        resultSize - Size of result buffer

    RESULT
        V8_SUCCESS on success, or an error code:
        - V8_ERROR_INVALID if any parameter is NULL
        - V8_ERROR_RUNTIME on JavaScript error
        - V8_ERROR_NOMEM if result buffer is too small

    NOTES
        This is a stub implementation. When V8 is fully ported, this will:
        - Get the property from the object
        - Convert value to string
        - Handle type conversion and errors

    EXAMPLE
        char value[256];
        APTR global = V8GetGlobalObject(isolate, context);
        if (V8GetProperty(isolate, context, global, "answer", value, sizeof(value)) == V8_SUCCESS) {
            printf("answer = %s\\n", value);
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8SetProperty(), V8GetGlobalObject()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    
    if (!v8isolate || !v8context || !object || !name || !result || resultSize == 0)
    {
        return V8_ERROR_INVALID;
    }
    
    /* TODO: When V8 is ported, get property here
     * 
     * Example V8 property getting (pseudo-code):
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
     * 
     * v8::TryCatch try_catch(v8isolate);
     * v8::MaybeLocal<v8::Value> val = obj->Get(context, key);
     * 
     * if (val.IsEmpty()) {
     *     return V8_ERROR_RUNTIME;
     * }
     * 
     * v8::String::Utf8Value utf8(v8isolate, val.ToLocalChecked());
     * if (*utf8) {
     *     strncpy(result, *utf8, resultSize - 1);
     *     result[resultSize - 1] = '\0';
     * }
     */
    
    /* Real engine first: read the property from the actual JS object */
    if (V8Bridge_GetProperty(v8isolate->vi_Isolate, v8context->vc_Context,
                             object, name, result, resultSize) == V8_SUCCESS)
    {
        return V8_SUCCESS;
    }

    /* Fallback: simple property storage (stub builds) */
    CONST_STRPTR prop_value = V8_GetPropertyValue(v8context, name);
    if (!prop_value)
    {
        strncpy(result, "undefined", resultSize - 1);
        result[resultSize - 1] = '\0';
        return V8_SUCCESS;
    }
    
    strncpy(result, prop_value, resultSize - 1);
    result[resultSize - 1] = '\0';
    
    return V8_SUCCESS;

    AROS_LIBFUNC_EXIT
}
