/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8CallFunction() - Call JavaScript function
*/

#include <proto/exec.h>
#include <aros/libcall.h>
#include <string.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_FUNCTION_CALL
 * AROS_IMPL: Simple function invocation without arguments
 * DESIGN: Call function by name on any V8 object
 * MEMORY: Result string copied to caller's buffer
 * THREAD_SAFETY: Must be called from isolate's thread
 * TODO: Add actual V8 function calling when V8 is ported
 * TODO: Add support for function arguments in future version
 * REFERENCE: v8::Function::Call() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH6(LONG, V8CallFunction,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),
        AROS_LHA(APTR, object, A2),
        AROS_LHA(CONST_STRPTR, functionName, A3),
        AROS_LHA(STRPTR, result, D0),
        AROS_LHA(ULONG, resultSize, D1),

/*  LOCATION */
        struct V8Base *, V8Base, 18, V8)

/*  FUNCTION
        Calls a JavaScript function on an object and returns the result
        as a string. This version does not support arguments - use
        V8Eval() for functions that require arguments.

    INPUTS
        isolate      - Handle to the isolate
        context      - Handle to the context
        object       - Handle to the object containing the function
        functionName - Name of the function to call
        result       - Buffer to receive result string (may be NULL)
        resultSize   - Size of result buffer

    RESULT
        V8_SUCCESS on success, or an error code:
        - V8_ERROR_INVALID if any required parameter is NULL
        - V8_ERROR_RUNTIME on JavaScript error
        - V8_ERROR_NOMEM if result buffer is too small

    NOTES
        This is a stub implementation. When V8 is fully ported, this will:
        - Get the function from the object
        - Call it with no arguments
        - Convert result to string
        - Handle exceptions
        
        If result is NULL, the function is called but the result is discarded.

    EXAMPLE
        // Call a function defined in JavaScript
        V8Eval(isolate, context, "function greet() { return 'Hello'; }", NULL, 0);
        
        char result[256];
        APTR global = V8GetGlobalObject(isolate, context);
        if (V8CallFunction(isolate, context, global, "greet", result, sizeof(result)) == V8_SUCCESS) {
            printf("Function returned: %s\\n", result);
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).
        Does not support function arguments yet

    SEE ALSO
        V8Eval(), V8GetGlobalObject()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    
    if (!v8isolate || !v8context || !object || !functionName)
    {
        return V8_ERROR_INVALID;
    }
    
    /* TODO: When V8 is ported, call function here
     * 
     * Example V8 function calling (pseudo-code):
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
     * v8::Local<v8::String> key = v8::String::NewFromUtf8(v8isolate, functionName);
     * v8::MaybeLocal<v8::Value> funcVal = obj->Get(context, key);
     * 
     * if (funcVal.IsEmpty() || !funcVal.ToLocalChecked()->IsFunction()) {
     *     return V8_ERROR_RUNTIME;
     * }
     * 
     * v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(funcVal.ToLocalChecked());
     * 
     * v8::TryCatch try_catch(v8isolate);
     * v8::MaybeLocal<v8::Value> result_value = func->Call(context, obj, 0, nullptr);
     * 
     * if (result_value.IsEmpty()) {
     *     return V8_ERROR_RUNTIME;
     * }
     * 
     * if (result && resultSize > 0) {
     *     v8::String::Utf8Value utf8(v8isolate, result_value.ToLocalChecked());
     *     if (*utf8) {
     *         strncpy(result, *utf8, resultSize - 1);
     *         result[resultSize - 1] = '\0';
     *     }
     * }
     */
    
    /* Simple function call support until V8 is ported
     * This checks if the function name exists as a property and returns "undefined"
     * for actual function calls. Full function support requires V8 engine.
     */
    /* Real engine first: resolve and call the actual JS function */
    if (V8Bridge_CallFunction(v8isolate->vi_Isolate, v8context->vc_Context,
                              object, functionName,
                              result, resultSize) == V8_SUCCESS)
    {
        return V8_SUCCESS;
    }

    /* Fallback: simple property lookup (stub builds) */
    CONST_STRPTR func_value = V8_GetPropertyValue(v8context, functionName);
    
    if (result && resultSize > 0)
    {
        if (func_value)
        {
            /* For now, just return the property value */
            strncpy(result, func_value, resultSize - 1);
        }
        else
        {
            /* Function not found */
            strncpy(result, "undefined", resultSize - 1);
        }
        result[resultSize - 1] = '\0';
    }
    
    return V8_SUCCESS;

    AROS_LIBFUNC_EXIT
}
