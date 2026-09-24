/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8CompileScript() - Compile JavaScript code
*/

#include <proto/exec.h>
#include <aros/libcall.h>
#include <string.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_SCRIPT_COMPILATION
 * AROS_IMPL: Allocate script structure through exec.library
 * DESIGN: Separate compilation from execution for caching
 * MEMORY: Script structure and name allocated with AllocVec
 * THREAD_SAFETY: Must be called from isolate's thread
 * TODO: Add actual V8 script compilation when V8 is ported
 * REFERENCE: v8::Script::Compile() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH4(V8ScriptHandle, V8CompileScript,

/*  SYNOPSIS */
        AROS_LHA(V8IsolateHandle, isolate, A0),
        AROS_LHA(V8ContextHandle, context, A1),
        AROS_LHA(CONST_STRPTR, script, A2),
        AROS_LHA(CONST_STRPTR, scriptName, A3),

/*  LOCATION */
        struct V8Base *, V8Base, 12, V8)

/*  FUNCTION
        Compiles JavaScript code into a script that can be executed
        multiple times. This is more efficient than V8Eval() when the
        same code needs to be executed repeatedly.

    INPUTS
        isolate    - Handle to the isolate
        context    - Handle to the context for compilation
        script     - JavaScript code to compile
        scriptName - Name/identifier for the script (for error messages)

    RESULT
        Handle to the compiled script, or NULL on compilation error

    NOTES
        This is a stub implementation. When V8 is fully ported, this will:
        - Compile the script to bytecode
        - Cache the compiled code
        - Store script metadata
        
        The compiled script must be freed with V8FreeScript() when no
        longer needed.

    EXAMPLE
        V8ScriptHandle script = V8CompileScript(isolate, context, 
                                                "function add(a,b) { return a+b; }", 
                                                "add.js");
        if (script) {
            // Use script...
            V8FreeScript(script);
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8RunScript(), V8FreeScript(), V8Eval()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Isolate *v8isolate = (struct V8Isolate *)isolate;
    struct V8Context *v8context = (struct V8Context *)context;
    struct V8Script *v8script;
    
    if (!v8isolate || !v8context || !script)
    {
        return NULL;
    }
    
    /* Allocate internal script structure */
    v8script = V8_AllocScript(v8isolate, scriptName, V8F_SCRIPT_CACHED);
    if (!v8script)
    {
        return NULL;
    }
    
    /* TODO: When V8 is ported, compile script here
     * 
     * Example V8 script compilation (pseudo-code):
     * 
     * v8::Isolate* v8isolate = static_cast<v8::Isolate*>(v8isolate->vi_Isolate);
     * v8::HandleScope handle_scope(v8isolate);
     * v8::Context::Scope context_scope(v8context->vc_Context);
     * 
     * v8::ScriptOrigin origin(v8::String::NewFromUtf8(v8isolate, scriptName));
     * v8::Local<v8::String> source = v8::String::NewFromUtf8(v8isolate, script);
     * 
     * v8::TryCatch try_catch(v8isolate);
     * v8::MaybeLocal<v8::Script> compiled = v8::Script::Compile(context, source, &origin);
     * 
     * if (compiled.IsEmpty()) {
     *     V8_FreeScript(v8script);
     *     return NULL;
     * }
     * 
     * v8::Persistent<v8::Script>* persistent = 
     *     new v8::Persistent<v8::Script>(v8isolate, compiled.ToLocalChecked());
     * v8script->vs_Script = persistent;
     */
    
    /* Route through the bridge: with the real engine this stores the
     * script source (V8Bridge_RunScript later evaluates it through the
     * full Script::Compile+Run path).  Falls back to a dummy handle if
     * the bridge is unavailable. */
    v8script->vs_Script = V8Bridge_CompileScript(v8isolate->vi_Isolate,
                                                 v8context->vc_Context,
                                                 script, scriptName);
    if (!v8script->vs_Script)
        v8script->vs_Script = (APTR)v8script; /* dummy handle (stub mode) */

    /* Add to isolate's script list */
    ObtainSemaphore(&v8isolate->vi_Semaphore);
    AddTail((struct List *)&v8isolate->vi_Scripts, (struct Node *)v8script);
    ReleaseSemaphore(&v8isolate->vi_Semaphore);
    
    return (V8ScriptHandle)v8script;

    AROS_LIBFUNC_EXIT
}

/****************************************************************************/

/* Internal function to allocate script */
struct V8Script *V8_AllocScript(struct V8Isolate *isolate, CONST_STRPTR name, ULONG flags)
{
    struct V8Script *script;
    
    script = AllocVec(sizeof(struct V8Script), MEMF_PUBLIC | MEMF_CLEAR);
    if (!script)
    {
        return NULL;
    }
    
    /* Initialize script structure */
    script->vs_Isolate = isolate;
    script->vs_Flags = flags;
    
    /* Copy script name if provided */
    if (name)
    {
        ULONG len = strlen(name) + 1;
        script->vs_Name = AllocVec(len, MEMF_PUBLIC);
        if (script->vs_Name)
        {
            strcpy(script->vs_Name, name);
        }
    }
    
    return script;
}
