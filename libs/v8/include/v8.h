/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8 JavaScript Engine Library - Public API
*/

#ifndef LIBRARIES_V8_H
#define LIBRARIES_V8_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef EXEC_LIBRARIES_H
#include <exec/libraries.h>
#endif

/****************************************************************************/

/* V8 Handle Types - Opaque pointers to internal V8 structures */
typedef APTR V8IsolateHandle;  /* Handle to V8 Isolate (execution context) */
typedef APTR V8ContextHandle;  /* Handle to V8 Context (JavaScript global environment) */
typedef APTR V8ScriptHandle;   /* Handle to compiled script */
typedef APTR V8ObjectHandle;   /* Handle to V8 object */

/****************************************************************************/

/* V8 Error Codes */
#define V8_SUCCESS           0
#define V8_ERROR_GENERAL    -1
#define V8_ERROR_SYNTAX     -2
#define V8_ERROR_COMPILE    -3
#define V8_ERROR_RUNTIME    -4
#define V8_ERROR_NOMEM      -5
#define V8_ERROR_INVALID    -6
#define V8_ERROR_EXCEPTION  -7

/****************************************************************************/

/* V8 Isolate Creation Flags */
#define V8F_ISOLATE_DEFAULT         0x00000000
#define V8F_ISOLATE_ENABLE_JIT      0x00000001  /* Enable JIT compilation */
#define V8F_ISOLATE_SNAPSHOT        0x00000002  /* Use startup snapshot */
#define V8F_ISOLATE_EXPOSE_GC       0x00000004  /* Expose GC to JavaScript */
#define V8F_ISOLATE_STRICT_MODE     0x00000008  /* Enable strict mode by default */

/****************************************************************************/

/* V8 Script Compilation Flags */
#define V8F_SCRIPT_CACHED           0x00000001  /* Cache compiled script */
#define V8F_SCRIPT_MODULE           0x00000002  /* Compile as ES6 module */
#define V8F_SCRIPT_STRICT           0x00000004  /* Force strict mode */

/****************************************************************************/

/*
 * V8FindEngineSymbol() is an experimental convergence bridge for consumers
 * built from the exact Chromium/V8 manifest packaged by this library. It
 * resolves a mangled engine text symbol retained in the loaded v8.library
 * module. It is not a source- or ABI-stable replacement for the public C API;
 * callers must reject NULL and must match the library's engine build exactly.
 */

/****************************************************************************/

/* V8_LIBRARY_INTERFACE
 * AROS_IMPL: Follows standard AROS library conventions, compatible with exec.library
 * REFERENCE: https://v8.dev/docs/embed - V8 embedder's guide
 * DESIGN: Single isolate per library user, multiple contexts supported
 * MEMORY: Uses exec.library AllocVec/FreeVec for memory management
 * THREAD_SAFETY: Each isolate is single-threaded, multiple isolates can run concurrently
 */

/****************************************************************************/

#endif /* LIBRARIES_V8_H */
