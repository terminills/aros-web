/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8 Library C++ Bridge - Connects C API to Real V8 JavaScript Engine
    Updated: Phase 2.3 - Real V8 Integration
*/

/* V8_CPP_BRIDGE
 * AROS_IMPL: Provides extern "C" wrappers for V8 embedder API
 * DESIGN: Allows C code to call C++ V8 engine without exposing C++ to library
 * REFERENCE: engine/v8-embedder.h for V8 embedder API
 */

#include <exec/types.h>
#include <proto/exec.h>

// Undefine AROS macros that conflict with C++ method names in V8 platform headers.
// - Signal/Wait: Conflict with AROSSemaphore class methods in threading-aros.h
// - Allocate/CreatePool/DeletePool: Conflict with AROSMemoryManager methods in memory-aros.h
#undef Signal
#undef Wait
#undef Allocate
#undef CreatePool
#undef DeletePool

// Include V8 Embedder (Real V8 Integration)
#include "engine/v8-embedder.h"
// Keep isolate-aros.h for backward compatibility during transition
#include "engine/isolate-aros.h"

// Extern C wrappers for V8 platform functionality
extern "C" {

/****************************************************************************/
/* Platform Initialization                                                  */
/****************************************************************************/

/* Initialize V8 platform - called from v8initialize.c */
BOOL V8Bridge_InitializePlatform(void)
{
    try {
        // Initialize Real V8 Engine
        bool success = v8::aros::V8Embedder::Initialize();
        return success ? TRUE : FALSE;
    }
    catch (...) {
        return FALSE;
    }
}

/* Shutdown V8 platform - called from v8cleanup.c */
void V8Bridge_ShutdownPlatform(void)
{
    try {
        v8::aros::V8Embedder::Shutdown();
    }
    catch (...) {
        // Ignore exceptions during shutdown
    }
}

/****************************************************************************/
/* Isolate Management                                                       */
/****************************************************************************/

/* Create V8 isolate - called from v8createisolate.c */
APTR V8Bridge_CreateIsolate(void)
{
    try {
        void* isolate = v8::aros::V8Embedder::CreateIsolate();
        return (APTR)isolate;
    }
    catch (...) {
        return NULL;
    }
}

/* Destroy V8 isolate - called from v8destroyisolate.c */
void V8Bridge_DestroyIsolate(APTR isolate)
{
    try {
        if (isolate) {
            v8::aros::V8Embedder::DestroyIsolate(isolate);
        }
    }
    catch (...) {
        // Ignore exceptions during cleanup
    }
}

/****************************************************************************/
/* Context Management                                                       */
/****************************************************************************/

/* Create V8 context - called from v8createcontext.c */
APTR V8Bridge_CreateContext(APTR isolate)
{
    try {
        if (!isolate) return NULL;
        
        void* context = v8::aros::V8Embedder::CreateContext(isolate);
        return (APTR)context;
    }
    catch (...) {
        return NULL;
    }
}

/* Destroy V8 context - called from v8destroycontext.c */
void V8Bridge_DestroyContext(APTR context)
{
    try {
        if (context) {
            v8::aros::V8Embedder::DestroyContext(NULL, context);
        }
    }
    catch (...) {
        // Ignore exceptions during cleanup
    }
}

/****************************************************************************/
/* JavaScript Execution                                                     */
/****************************************************************************/

/* Execute JavaScript code - called from v8eval.c */
LONG V8Bridge_ExecuteScript(APTR isolate, APTR context, 
                           CONST_STRPTR script, CONST_STRPTR name,
                           STRPTR result, ULONG resultSize)
{
    try {
        if (!isolate || !context || !script) return -6; // V8_ERROR_INVALID
        
        // Execute script using real V8 engine
        int status = v8::aros::V8Embedder::EvaluateScript(
            isolate,
            context,
            script,
            name ? name : "<eval>",
            result,
            resultSize);
        
        if (status != 0) {
            const char* error = v8::aros::V8Embedder::GetLastError();
            if (result && resultSize > 0 && error) {
                // Error message already in result buffer from EvaluateScript
            }
            return -4; // V8_ERROR_RUNTIME
        }
        
        return 0; // V8_SUCCESS
    }
    catch (...) {
        if (result && resultSize > 0) {
            strncpy(result, "Exception during script execution", resultSize - 1);
            result[resultSize - 1] = '\0';
        }
        return -4; // V8_ERROR_RUNTIME
    }
}

/* Compile JavaScript script - called from v8compilescript.c */
APTR V8Bridge_CompileScript(APTR isolate, APTR context,
                           CONST_STRPTR source, CONST_STRPTR name)
{
    try {
        if (!isolate || !context || !source) return NULL;
        
        void* script = v8::aros::V8Embedder::CompileScript(
            isolate,
            context,
            source,
            name ? name : "<script>");
        
        return (APTR)script;
    }
    catch (...) {
        return NULL;
    }
}

/* Run compiled script - called from v8runscript.c */
LONG V8Bridge_RunScript(APTR isolate, APTR context, APTR script,
                       STRPTR result, ULONG resultSize)
{
    try {
        if (!isolate || !context || !script) return -6;
        
        int status = v8::aros::V8Embedder::RunScript(
            isolate,
            context,
            script,
            result,
            resultSize);
        
        return (status == 0) ? 0 : -4;
    }
    catch (...) {
        return -4; // V8_ERROR_RUNTIME
    }
}

/* Free compiled script - called from v8freescript.c */
void V8Bridge_FreeScript(APTR script)
{
    try {
        if (script) {
            v8::aros::V8Embedder::FreeScript(script);
        }
    }
    catch (...) {
        // Ignore exceptions during cleanup
    }
}

/****************************************************************************/
/* Property Management                                                      */
/****************************************************************************/

/* Set object property - called from v8setproperty.c */
LONG V8Bridge_SetProperty(APTR isolate, APTR context, APTR object,
                         CONST_STRPTR name, CONST_STRPTR value)
{
    try {
        if (!isolate || !context || !object || !name || !value) return -6;
        
        int status = v8::aros::V8Embedder::SetObjectProperty(
            isolate,
            context,
            object,
            name,
            value);
        
        return (status == 0) ? 0 : -4;
    }
    catch (...) {
        return -4; // V8_ERROR_RUNTIME
    }
}

/* Get object property - called from v8getproperty.c */
LONG V8Bridge_GetProperty(APTR isolate, APTR context, APTR object,
                         CONST_STRPTR name, STRPTR result, ULONG resultSize)
{
    try {
        if (!isolate || !context || !object || !name) return -6;
        
        int status = v8::aros::V8Embedder::GetObjectProperty(
            isolate,
            context,
            object,
            name,
            result,
            resultSize);
        
        return (status == 0) ? 0 : -4;
    }
    catch (...) {
        return -4; // V8_ERROR_RUNTIME
    }
}

/* Get global object - called from v8getglobalobject.c */
APTR V8Bridge_GetGlobalObject(APTR isolate, APTR context)
{
    try {
        if (!isolate || !context) return NULL;
        
        void* global = v8::aros::V8Embedder::GetGlobalObject(
            isolate,
            context);
        
        return (APTR)global;
    }
    catch (...) {
        return NULL;
    }
}

/* Call JavaScript function - called from v8callfunction.c */
LONG V8Bridge_CallFunction(APTR isolate, APTR context, APTR object,
                          CONST_STRPTR name, STRPTR result, ULONG resultSize)
{
    try {
        if (!isolate || !context || !name) return -6;
        
        int status = v8::aros::V8Embedder::CallFunction(
            isolate,
            context,
            object ? object : context,
            name,
            NULL,  // No arguments for now
            0,     // Argument count
            result,
            resultSize);
        
        return (status == 0) ? 0 : -4;
    }
    catch (...) {
        return -4; // V8_ERROR_RUNTIME
    }
}

} // extern "C"
