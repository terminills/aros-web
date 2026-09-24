/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8FreeScript() - Free a compiled script
*/

#include <proto/exec.h>
#include <aros/libcall.h>

#include "v8_intern.h"
#include "v8_bridge.h"

/* V8_SCRIPT_CLEANUP
 * AROS_IMPL: Free script structure through exec.library
 * DESIGN: Clean up script and remove from isolate
 * MEMORY: Free all allocations with FreeVec
 * THREAD_SAFETY: Caller must ensure script is not in use
 * TODO: Dispose of actual V8 script when V8 is ported
 * REFERENCE: v8::Persistent::Reset() in V8 embedder's guide
 */

/*****************************************************************************

    NAME */
#include <proto/v8.h>

        AROS_LH1(void, V8FreeScript,

/*  SYNOPSIS */
        AROS_LHA(V8ScriptHandle, script, A0),

/*  LOCATION */
        struct V8Base *, V8Base, 14, V8)

/*  FUNCTION
        Frees a compiled script and all associated resources.

    INPUTS
        script - Handle to the script to free

    RESULT
        None

    NOTES
        This is a stub implementation. When V8 is fully ported, this will
        properly dispose of the V8 compiled script.
        
        After this call, the script handle is no longer valid.

    EXAMPLE
        V8ScriptHandle script = V8CompileScript(isolate, context, "1+1", "test.js");
        if (script) {
            V8RunScript(isolate, context, script, NULL, 0);
            V8FreeScript(script);
        }

    BUGS
        None known — routed to the real V8 engine via V8Bridge (stub fallback retained for engine-less builds).

    SEE ALSO
        V8CompileScript()

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    struct V8Script *v8script = (struct V8Script *)script;
    
    if (!v8script)
    {
        return;
    }
    
    /* Remove from isolate's script list */
    if (v8script->vs_Isolate)
    {
        ObtainSemaphore(&v8script->vs_Isolate->vi_Semaphore);
        Remove((struct Node *)v8script);
        ReleaseSemaphore(&v8script->vs_Isolate->vi_Semaphore);
    }
    
    /* Free the script */
    V8_FreeScript(v8script);

    AROS_LIBFUNC_EXIT
}

/****************************************************************************/

/* Internal function to free script */
void V8_FreeScript(struct V8Script *script)
{
    if (!script)
    {
        return;
    }
    
    /* TODO: When V8 is ported, dispose of actual V8 script here
     * 
     * Example V8 script disposal (pseudo-code):
     * 
     * v8::Persistent<v8::Script>* persistent = 
     *     static_cast<v8::Persistent<v8::Script>*>(script->vs_Script);
     * persistent->Reset();
     * delete persistent;
     */

    /* Free the bridge-side script storage (real engine: stored source).
     * A dummy handle (== the struct itself, stub mode) is not freed. */
    if (script->vs_Script && script->vs_Script != (APTR)script)
    {
        V8Bridge_FreeScript(script->vs_Script);
    }

    /* Free script name */
    if (script->vs_Name)
    {
        FreeVec(script->vs_Name);
    }
    
    /* Free the script structure */
    FreeVec(script);
}
