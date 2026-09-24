/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8Eval() - Evaluate JavaScript code
*/

#include <proto/exec.h>
#include <aros/libcall.h>
#include <string.h>

#include "v8_bridge.h"
#include "v8_intern.h"

/* V8_SCRIPT_EVALUATION
 * AROS_IMPL: Uses simple evaluator first (returns actual values), V8 bridge as fallback
 * DESIGN: Compile and execute JavaScript in one call
 * MEMORY: Result string copied to caller's buffer
 * THREAD_SAFETY: Must be called from isolate's thread
 * INTEGRATION: Calls V8_EvaluateSimpleExpression() first, V8Bridge_ExecuteScript() as fallback
 * REFERENCE: v8::Script::Compile() and Run() in V8 embedder's guide
 * FIX: Changed to use simple evaluator first since V8 bridge cannot capture result values
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH5(LONG, V8Eval,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),
        AROS_LHA(CONST_STRPTR, script, A2),
        AROS_LHA(STRPTR, result, A3),
        AROS_LHA(ULONG, resultSize, D0),

/*  LOCATION */
        struct V8Base *, V8Base, 11, V8)

/*  FUNCTION
        Evaluates JavaScript code in the given context and returns the
        result as a string.

    INPUTS
        isolate    - Handle to the isolate
        context    - Handle to the context in which to evaluate
        script     - JavaScript code to evaluate
        result     - Buffer to receive result string (may be NULL)
        resultSize - Size of result buffer

    RESULT
        V8_SUCCESS on success, or an error code:
        - V8_ERROR_INVALID if isolate or context is NULL
        - V8_ERROR_COMPILE if script fails to compile
        - V8_ERROR_RUNTIME if script throws an exception
        - V8_ERROR_NOMEM if result buffer is too small

    NOTES
        This is a stub implementation. When V8 is fully ported, this will:
        - Compile the script
        - Execute it in the context
        - Convert result to string
        - Handle exceptions
        
        If result is NULL, the script is executed but the result is discarded.

    EXAMPLE
        char result[256];
        LONG rc = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
        if (rc == V8_SUCCESS) {
            printf("Result: %s\\n", result);
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8CompileScript(), V8RunScript()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    
    if (!v8isolate || !v8context || !script)
    {
        return V8_ERROR_INVALID;
    }
    
    /* TODO: When V8 is ported, evaluate script here
     * 
     * Example V8 script evaluation (pseudo-code):
     * 
     * v8::Isolate* v8isolate = static_cast<v8::Isolate*>(v8isolate->vi_Isolate);
     * v8::HandleScope handle_scope(v8isolate);
     * v8::Context::Scope context_scope(context);
     * 
     * v8::TryCatch try_catch(v8isolate);
     * v8::Local<v8::String> source = v8::String::NewFromUtf8(v8isolate, script);
     * v8::Local<v8::Script> compiled = v8::Script::Compile(context, source);
     * 
     * if (compiled.IsEmpty()) {
     *     return V8_ERROR_COMPILE;
     * }
     * 
     * v8::Local<v8::Value> result_value = compiled->Run(context);
     * if (result_value.IsEmpty()) {
     *     return V8_ERROR_RUNTIME;
     * }
     * 
     * if (result && resultSize > 0) {
     *     v8::String::Utf8Value utf8(v8isolate, result_value);
     *     if (*utf8) {
     *         strncpy(result, *utf8, resultSize - 1);
     *         result[resultSize - 1] = '\0';
     *     }
     * }
     */
    
    /* Real engine FIRST: V8Bridge_ExecuteScript captures actual return
     * values (Script::Compile + Run + Utf8Value) since the real-engine
     * EvaluateScript landed.  The simple expression parser remains only
     * as a fallback for engine-less (stub) builds — with it first, the
     * historic "2+2=4" result never actually touched V8. */
    LONG bridge_rc = V8Bridge_ExecuteScript(v8isolate->vi_Isolate, v8context->vc_Context,
                                            script, NULL, result, resultSize);
    if (bridge_rc == V8_SUCCESS || bridge_rc == V8_ERROR_RUNTIME)
    {
        return bridge_rc;
    }

    /* Engine unavailable: fall back to the simple expression evaluator */
    return V8_EvaluateSimpleExpression(script, v8context, result, resultSize);

    AROS_LIBFUNC_EXIT
}
