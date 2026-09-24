/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Exact-build CEF/Blink implementation bridge.
 *
 * All Chromium C++ implementation and data remain in this module.  The public
 * cef.library calls only these typed C ABI vectors.
 */

#include <aros/libcall.h>
#include <exec/libraries.h>

#include <stdlib.h>

#include "cef-blink_libdefs.h"

extern int cef_engine_version_info(int entry) __asm__("cef_version_info");
extern const char *cef_engine_api_hash(int entry) __asm__("cef_api_hash");
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

AROS_LH1(LONG, CEFBlinkVersionInfo,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 5, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_version_info((int)entry);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(CONST_STRPTR, CEFBlinkApiHash,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 6, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (CONST_STRPTR)cef_engine_api_hash((int)entry);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFBlinkExecuteProcess,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, application, A1),
    AROS_LHA(APTR, sandbox_info, A2),
    LIBBASETYPEPTR, LIBBASE, 7, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
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
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return (LONG)cef_engine_initialize(args, settings, application,
        sandbox_info);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFBlinkShutdown,
    LIBBASETYPEPTR, LIBBASE, 9, CEFBlink)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    cef_engine_shutdown();
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
