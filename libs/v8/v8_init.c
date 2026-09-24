/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8 JavaScript Engine Library - Initialization and Cleanup
*/

#include <aros/symbolsets.h>
#include <proto/exec.h>

#include LC_LIBDEFS_FILE
#include "v8_intern.h"

/* V8_INITIALIZATION
 * AROS_IMPL: Uses ADD2INITLIB/ADD2EXPUNGELIB symbolsets
 * DESIGN: Initialize V8 platform on library open, cleanup on library close
 * MEMORY: All allocations through exec.library AllocVec/FreeVec
 * THREAD_SAFETY: Initialization is single-threaded by library loader
 * TODO: Integrate actual V8 initialization when V8 is ported
 */

/****************************************************************************/

/* From glibc-stubs-aros.c — timer init/cleanup for per-task-lib bypass */
extern void v8_init_timer(void);
extern void v8_cleanup_timer(void);

static int V8_InitLib(LIBBASETYPEPTR V8Base)
{
    /* Initialize the library base semaphore */
    InitSemaphore(&V8Base->v8_Semaphore);
    
    /* Initialize isolate list */
    NEWLIST(&V8Base->v8_Isolates);
    
    /* Mark V8 as not yet initialized */
    V8Base->v8_Initialized = FALSE;
    V8Base->v8_Platform = NULL;
    V8Base->v8_ArrayBuffer = NULL;

    /* Open timer.device once so worker threads can call clock_gettime
     * without their own PosixCBase (per-task-lib bypass) */
    v8_init_timer();
    
    return TRUE;
}

/****************************************************************************/

static int V8_ExpungeLib(LIBBASETYPEPTR V8Base)
{
    /* Shutdown V8 platform if it was initialized */
    if (V8Base->v8_Initialized)
    {
        V8_ShutdownPlatform(V8Base);
    }
    
    /* Close global timer.device */
    v8_cleanup_timer();

    /* Clean up any remaining isolates (should not happen in normal operation) */
    struct V8Isolate *isolate, *nextIsolate;
    
    ObtainSemaphore(&V8Base->v8_Semaphore);
    
    ForeachNodeSafe(&V8Base->v8_Isolates, isolate, nextIsolate)
    {
        V8_FreeIsolate(V8Base, isolate);
    }
    
    ReleaseSemaphore(&V8Base->v8_Semaphore);
    
    return TRUE;
}

/****************************************************************************/

ADD2INITLIB(V8_InitLib, 0)
ADD2EXPUNGELIB(V8_ExpungeLib, 0)
