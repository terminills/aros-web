/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Exact-build CEF/Blink implementation bridge.
 *
 * All Chromium C++ implementation and data remain in this module.  The public
 * cef.library calls only these typed C ABI vectors.
 */

#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <exec/libraries.h>
#include <libraries/cef-blink.h>
#include <proto/exec.h>

#include <stdlib.h>

#include "cef-blink_libdefs.h"

extern int cef_engine_api_version(void) __asm__("cef_api_version");
extern int cef_engine_version_info(int entry) __asm__("cef_version_info");
extern const char *cef_engine_api_hash(int version, int entry) __asm__("cef_api_hash");
extern int cef_engine_execute_process(const void *args, void *application,
    void *sandbox_info) __asm__("cef_execute_process");
extern int cef_engine_initialize(const void *args, const void *settings,
    void *application, void *sandbox_info) __asm__("cef_initialize");
extern void cef_engine_shutdown(void) __asm__("cef_shutdown");
extern void cef_engine_do_message_loop_work(void)
    __asm__("cef_do_message_loop_work");
extern void cef_engine_run_message_loop(void) __asm__("cef_run_message_loop");
extern void cef_engine_quit_message_loop(void) __asm__("cef_quit_message_loop");
extern int cef_engine_create_browser(const void *window_info, void *client,
    const void *url, const void *settings, void *extra_info,
    void *request_context) __asm__("cef_browser_host_create_browser");
extern void *cef_engine_create_request_context(const void *settings, void *handler)
    __asm__("cef_request_context_create_context");
extern int cef_engine_post_task(int thread_id, void *task)
    __asm__("cef_post_task");
extern int cef_engine_post_delayed_task(int thread_id, void *task,
    long long delay_ms) __asm__("cef_post_delayed_task");
extern int cef_engine_register_extension(const void *name,
    const void *javascript_code, void *handler)
    __asm__("cef_register_extension");
extern int cef_engine_register_scheme_handler_factory(const void *scheme_name,
    const void *domain_name, void *factory)
    __asm__("cef_register_scheme_handler_factory");
extern void cef_engine_string_userfree_utf16_free(void *str)
    __asm__("cef_string_userfree_utf16_free");
extern void *cef_engine_v8_platform(void)
    __asm__("_ZN3gin10V8Platform3GetEv");
extern int cef_engine_v8_worker_threads(void *platform)
    __asm__("_ZN3gin10V8Platform21NumberOfWorkerThreadsEv");
extern int *__stdc_geterrnoptr(void);

int *__errno_location(void)
{
    return __stdc_geterrnoptr();
}

/* Call under cb_EngineLock before entering the generated C API. Clients
 * that skip an identity query must still use the same ABI as other apps. */
static int cef_engine_select_api(void)
{
    return cef_engine_api_hash(CEFBLINK_API_VERSION, 0) != NULL &&
        cef_engine_api_version() == CEFBLINK_API_VERSION;
}

static int CEFBlink_InitLib(LIBBASETYPEPTR CEFBlinkBase)
{
    InitSemaphore(&CEFBlinkBase->cb_EngineLock);
    CEFBlinkBase->cb_EngineState = CEFBLINK_ENGINE_UNINITIALIZED;
    CEFBlinkBase->cb_EngineWasShutDown = FALSE;
    CEFBlinkBase->cb_EngineOwner = NULL;
    return TRUE;
}

ADD2INITLIB(CEFBlink_InitLib, 0)

AROS_LH1(LONG, CEFBlinkVersionInfo,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 5, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_version_info((int)entry);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(CONST_STRPTR, CEFBlinkLegacyApiHash,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 6, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    /* The pre-versioned CEF 124 ABI is not compatible with this engine. */
    (void)entry;
    return NULL;
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFBlinkExecuteProcess,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, application, A1),
    AROS_LHA(APTR, sandbox_info, A2),
    LIBBASETYPEPTR, LIBBASE, 7, CEFBlink)
{
    AROS_LIBFUNC_INIT
    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    int ready = cef_engine_select_api();
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    if (!ready)
        return 1; /* Child-process failure; do not enter an incompatible ABI. */
    return (LONG)cef_engine_execute_process(args, application, sandbox_info);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, CEFBlinkInitialize,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, settings, A1),
    AROS_LHA(APTR, application, A2),
    AROS_LHA(APTR, sandbox_info, A3),
    LIBBASETYPEPTR, LIBBASE, 8, CEFBlink)
{
    LONG rc;

    AROS_LIBFUNC_INIT

    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    if (!cef_engine_select_api())
    {
        ReleaseSemaphore(&LIBBASE->cb_EngineLock);
        return 0;
    }
    if (LIBBASE->cb_EngineState == CEFBLINK_STATE_RUNNING)
    {
        ReleaseSemaphore(&LIBBASE->cb_EngineLock);
        return CEFBLINK_ENGINE_RUNNING;
    }
    if (LIBBASE->cb_EngineWasShutDown ||
        LIBBASE->cb_EngineState != CEFBLINK_ENGINE_UNINITIALIZED)
    {
        ReleaseSemaphore(&LIBBASE->cb_EngineLock);
        return 0;
    }
    LIBBASE->cb_EngineState = CEFBLINK_ENGINE_INITIALIZING;
    LIBBASE->cb_EngineOwner = FindTask(NULL);
    /* CEF initialization can call back into clients or wait on worker tasks.
     * Publish the transition, then release the lock before entering CEF. */
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    rc = (LONG)cef_engine_initialize(args, settings, application,
        sandbox_info);
    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    if (rc != 0)
        LIBBASE->cb_EngineState = CEFBLINK_STATE_RUNNING;
    else
    {
        LIBBASE->cb_EngineState = CEFBLINK_ENGINE_UNINITIALIZED;
        LIBBASE->cb_EngineOwner = NULL;
    }
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFBlinkShutdown,
    LIBBASETYPEPTR, LIBBASE, 9, CEFBlink)
{
    AROS_LIBFUNC_INIT

    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    if (LIBBASE->cb_EngineState != CEFBLINK_STATE_RUNNING ||
        LIBBASE->cb_EngineOwner != FindTask(NULL))
    {
        ReleaseSemaphore(&LIBBASE->cb_EngineLock);
        return;
    }
    LIBBASE->cb_EngineState = CEFBLINK_ENGINE_SHUTTING_DOWN;
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    cef_engine_shutdown();
    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    LIBBASE->cb_EngineWasShutDown = TRUE;
    LIBBASE->cb_EngineOwner = NULL;
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFBlinkDoMessageLoopWork,
    LIBBASETYPEPTR, LIBBASE, 10, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    cef_engine_do_message_loop_work();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFBlinkRunMessageLoop,
    LIBBASETYPEPTR, LIBBASE, 11, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    cef_engine_run_message_loop();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFBlinkQuitMessageLoop,
    LIBBASETYPEPTR, LIBBASE, 12, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    cef_engine_quit_message_loop();
    AROS_LIBFUNC_EXIT
}

AROS_LH6(LONG, CEFBlinkCreateBrowser,
    AROS_LHA(APTR, window_info, A0),
    AROS_LHA(APTR, client, A1),
    AROS_LHA(APTR, url, A2),
    AROS_LHA(APTR, settings, A3),
    AROS_LHA(APTR, extra_info, D0),
    AROS_LHA(APTR, request_context, D1),
    LIBBASETYPEPTR, LIBBASE, 13, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_create_browser(window_info, client, url, settings,
        extra_info, request_context);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, CEFBlinkPostTask,
    AROS_LHA(LONG, thread_id, D0),
    AROS_LHA(APTR, task, A0),
    LIBBASETYPEPTR, LIBBASE, 14, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_post_task((int)thread_id, task);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFBlinkPostDelayedTask,
    AROS_LHA(LONG, thread_id, D0),
    AROS_LHA(APTR, task, A0),
    AROS_LHA(LONG, delay_ms, D1),
    LIBBASETYPEPTR, LIBBASE, 15, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_post_delayed_task((int)thread_id, task,
        (long long)delay_ms);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFBlinkRegisterV8Extension,
    AROS_LHA(APTR, name, A0),
    AROS_LHA(APTR, javascript_code, A1),
    AROS_LHA(APTR, handler, A2),
    LIBBASETYPEPTR, LIBBASE, 16, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_register_extension(name, javascript_code,
        handler);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, CEFBlinkDefaultV8WorkerThreadCount,
    LIBBASETYPEPTR, LIBBASE, 17, CEFBlink)
{
    void *platform;

    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    platform = cef_engine_v8_platform();
    if (platform == NULL)
        return 0;
    return (LONG)cef_engine_v8_worker_threads(platform);
    AROS_LIBFUNC_EXIT
}

/*
 * Expose THIS library's heap so the rest of the process can share it.
 *
 * cef-blink.library is built with use_partition_alloc_as_malloc=true, so its
 * malloc/free are PartitionAlloc's. v8.library and node.library have no
 * allocator of their own and fall through libstdc++ to stdc.library's malloc,
 * which gives the process two heaps that know nothing about each other. An
 * object allocated by Blink and destroyed by V8 is then freed to the wrong
 * allocator - the proven cause of the crash on script-heavy pages.
 *
 * The fix is for v8.library to route its operator new/delete through this
 * heap (see workbench/libs/v8/v8_allocator.cpp). PartitionAlloc is the right
 * side to standardise on because, unlike stdc's malloc, it needs no AROS
 * library base and therefore works on a Chromium-created thread.
 *
 * These two wrappers deliberately just call malloc/free. They are compiled
 * INTO cef-blink.library, so those names bind to whatever allocator this
 * library actually uses - today PartitionAlloc, and still correct if that ever
 * changes. The contract exported here is "the heap cef-blink uses", not
 * "PartitionAlloc", which is exactly the property the caller needs.
 */
static void *CEFBlinkHeapAlloc(size_t size)
{
    return malloc(size);
}

static void CEFBlinkHeapFree(void *ptr)
{
    free(ptr);
}

AROS_LH2(void, CEFBlinkGetAllocator,
    AROS_LHA(APTR *, allocFn, A0),
    AROS_LHA(APTR *, freeFn, A1),
    LIBBASETYPEPTR, LIBBASE, 18, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;

    if (allocFn != NULL)
        *allocFn = (APTR)CEFBlinkHeapAlloc;
    if (freeFn != NULL)
        *freeFn = (APTR)CEFBlinkHeapFree;

    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFBlinkRegisterSchemeHandlerFactory,
    AROS_LHA(APTR, scheme_name, A0),
    AROS_LHA(APTR, domain_name, A1),
    AROS_LHA(APTR, factory, A2),
    LIBBASETYPEPTR, LIBBASE, 19, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return cef_engine_register_scheme_handler_factory(scheme_name, domain_name,
        factory);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, CEFBlinkStringUserfreeFree,
    AROS_LHA(APTR, str, A0),
    LIBBASETYPEPTR, LIBBASE, 20, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    cef_engine_string_userfree_utf16_free(str);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(const char *, CEFBlinkApiHash,
    AROS_LHA(LONG, version, D0),
    AROS_LHA(LONG, entry, D1),
    LIBBASETYPEPTR, LIBBASE, 21, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    /* One resident engine uses one generated C API version for all apps. */
    if (version != CEFBLINK_API_VERSION)
        return NULL;
    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    const char *hash = cef_engine_api_hash((int)version, (int)entry);
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    return hash;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, CEFBlinkApiVersion,
    LIBBASETYPEPTR, LIBBASE, 22, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_api_version();
    AROS_LIBFUNC_EXIT
}

AROS_LH2(APTR, CEFBlinkCreateRequestContext,
    AROS_LHA(APTR, settings, A0),
    AROS_LHA(APTR, handler, A1),
    LIBBASETYPEPTR, LIBBASE, 23, CEFBlink)
{
    AROS_LIBFUNC_INIT
    ObtainSemaphore(&LIBBASE->cb_EngineLock);
    int ready = LIBBASE->cb_EngineState == CEFBLINK_STATE_RUNNING;
    ReleaseSemaphore(&LIBBASE->cb_EngineLock);
    if (!ready || settings == NULL)
        return NULL;
    return cef_engine_create_request_context(settings, handler);
    AROS_LIBFUNC_EXIT
}

/* Capture on an engine-owned task, then retain this CEF runner for posting
 * from independent native clients. No caller-parent lookup is needed to post. */
extern void *cef_engine_get_task_runner(int thread_id)
    __asm__("cef_task_runner_get_for_thread");
AROS_LH1(APTR, CEFBlinkGetTaskRunner,
    AROS_LHA(LONG, thread_id, D0),
    LIBBASETYPEPTR, LIBBASE, 24, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return cef_engine_get_task_runner((int)thread_id);
    AROS_LIBFUNC_EXIT
}
