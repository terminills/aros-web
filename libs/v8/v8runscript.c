/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8RunScript() - Run a compiled script
*/

#include <proto/exec.h>
#include <aros/libcall.h>
#include <string.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_SCRIPT_EXECUTION
 * AROS_IMPL: Execute cached script multiple times
 * DESIGN: Run previously compiled script in context
 * MEMORY: Result string copied to caller's buffer
 * THREAD_SAFETY: Must be called from isolate's thread
 * TODO: Add actual V8 script execution when V8 is ported
 * REFERENCE: v8::Script::Run() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH5(LONG, V8RunScript,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),
        AROS_LHA(V8ScriptHandle, script, A2),
        AROS_LHA(STRPTR, result, A3),
        AROS_LHA(ULONG, resultSize, D0),

/*  LOCATION */
        struct V8Base *, V8Base, 13, V8)

/*  FUNCTION
        Executes a previously compiled script in the given context and
        returns the result as a string.

    INPUTS
        isolate    - Handle to the isolate
        context    - Handle to the context in which to execute
        script     - Handle to the compiled script
        result     - Buffer to receive result string (may be NULL)
        resultSize - Size of result buffer

    RESULT
        V8_SUCCESS on success, or an error code:
        - V8_ERROR_INVALID if any handle is NULL
        - V8_ERROR_RUNTIME if script throws an exception
        - V8_ERROR_NOMEM if result buffer is too small

    NOTES
        This is a stub implementation. When V8 is fully ported, this will:
        - Execute the compiled script
        - Convert result to string
        - Handle exceptions
        
        If result is NULL, the script is executed but the result is discarded.
        
        The same script can be executed multiple times, potentially with
        different global state.

    EXAMPLE
        V8ScriptHandle script = V8CompileScript(isolate, context, "2 + 2", "calc.js");
        if (script) {
            char result[256];
            LONG rc = V8RunScript(isolate, context, script, result, sizeof(result));
            if (rc == V8_SUCCESS) {
                printf("Result: %s\\n", result);
            }
            V8FreeScript(script);
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8CompileScript(), V8FreeScript(), V8Eval()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    struct V8Script *v8script = (struct V8Script *)script;
    
    if (!v8isolate || !v8context || !v8script)
    {
        return V8_ERROR_INVALID;
    }
    
    /* TODO: When V8 is ported, execute script here
     * 
     * Example V8 script execution (pseudo-code):
     * 
     * v8::Isolate* v8isolate = static_cast<v8::Isolate*>(v8isolate->vi_Isolate);
     * v8::HandleScope handle_scope(v8isolate);
     * v8::Local<v8::Context> context = v8context->vc_Context->Get(v8isolate);
     * v8::Context::Scope context_scope(context);
     * 
     * v8::Persistent<v8::Script>* persistent = 
     *     static_cast<v8::Persistent<v8::Script>*>(v8script->vs_Script);
     * v8::Local<v8::Script> local_script = persistent->Get(v8isolate);
     * 
     * v8::TryCatch try_catch(v8isolate);
     * v8::MaybeLocal<v8::Value> result_value = local_script->Run(context);
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
    
    /* Route through the bridge: real-engine RunScript evaluates the
     * stored source through the full Script::Compile+Run path and
     * captures the result value. */
    if (v8script->vs_Script && v8script->vs_Script != (APTR)v8script)
    {
        LONG bridge_rc = V8Bridge_RunScript(v8isolate->vi_Isolate,
                                            v8context->vc_Context,
                                            v8script->vs_Script,
                                            result, resultSize);
        return bridge_rc;
    }

    /* Stub mode (dummy handle): keep the historic placeholder */
    if (result && resultSize > 0)
    {
        strncpy(result, "V8 not yet ported", resultSize - 1);
        result[resultSize - 1] = '\0';
    }

    return V8_SUCCESS;

    AROS_LIBFUNC_EXIT
}
