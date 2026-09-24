/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Stable native AROS vectors forwarding into cef-blink.library.
 *
 * The adapter owns the exact-build CEF/Blink C++ closure.  Keeping this facade
 * purely in the C ABI prevents direct Blink data symbols from crossing a
 * resident-library boundary.
 */

#include <aros/libcall.h>
#include <exec/libraries.h>
#include <proto/cef-blink.h>

#include "cef_libdefs.h"

AROS_LH1(LONG, cef_version_info,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 5, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkVersionInfo(entry);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(CONST_STRPTR, cef_api_hash,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 6, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkApiHash(entry);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFExecuteProcess,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, application, A1),
    AROS_LHA(APTR, sandbox_info, A2),
    LIBBASETYPEPTR, LIBBASE, 7, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkExecuteProcess(args, application, sandbox_info);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, CEFInitialize,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, settings, A1),
    AROS_LHA(APTR, application, A2),
    AROS_LHA(APTR, sandbox_info, A3),
    LIBBASETYPEPTR, LIBBASE, 8, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkInitialize(args, settings, application, sandbox_info);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFShutdown,
    LIBBASETYPEPTR, LIBBASE, 9, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFBlinkShutdown();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFDoMessageLoopWork,
    LIBBASETYPEPTR, LIBBASE, 10, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFBlinkDoMessageLoopWork();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFRunMessageLoop,
    LIBBASETYPEPTR, LIBBASE, 11, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFBlinkRunMessageLoop();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CEFQuitMessageLoop,
    LIBBASETYPEPTR, LIBBASE, 12, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFBlinkQuitMessageLoop();
    AROS_LIBFUNC_EXIT
}

AROS_LH6(LONG, CEFCreateBrowser,
    AROS_LHA(APTR, window_info, A0),
    AROS_LHA(APTR, client, A1),
    AROS_LHA(APTR, url, A2),
    AROS_LHA(APTR, settings, A3),
    AROS_LHA(APTR, extra_info, D0),
    AROS_LHA(APTR, request_context, D1),
    LIBBASETYPEPTR, LIBBASE, 13, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkCreateBrowser(window_info, client, url, settings,
        extra_info, request_context);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, CEFPostTask,
    AROS_LHA(LONG, thread_id, D0),
    AROS_LHA(APTR, task, A0),
    LIBBASETYPEPTR, LIBBASE, 14, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkPostTask(thread_id, task);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFPostDelayedTask,
    AROS_LHA(LONG, thread_id, D0),
    AROS_LHA(APTR, task, A0),
    AROS_LHA(LONG, delay_ms, D1),
    LIBBASETYPEPTR, LIBBASE, 15, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkPostDelayedTask(thread_id, task, delay_ms);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFRegisterV8Extension,
    AROS_LHA(APTR, name, A0),
    AROS_LHA(APTR, javascript_code, A1),
    AROS_LHA(APTR, handler, A2),
    LIBBASETYPEPTR, LIBBASE, 16, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkRegisterV8Extension(name, javascript_code, handler);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, CEFDefaultV8WorkerThreadCount,
    LIBBASETYPEPTR, LIBBASE, 17, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkDefaultV8WorkerThreadCount();
    AROS_LIBFUNC_EXIT
}

/*
 * Passthrough for cef-blink's heap, so an embedder can give the whole process
 * one allocator.
 *
 * cef-blink.library is built with PartitionAlloc-as-malloc; v8.library and
 * node.library fall through libstdc++ to stdc.library's malloc. Two heaps in
 * one process means an object allocated by Blink and destroyed by V8 is freed
 * to the wrong allocator, which is the proven cause of the crash on
 * script-heavy pages.
 *
 * The embedder hands what this returns to V8AllocatorInstall() BEFORE anything
 * else runs - see workbench/libs/v8/v8_allocator.cpp for why the ordering is
 * not negotiable. Exposed here rather than making callers open
 * cef-blink.library directly, so the existing layering (electron -> cef ->
 * cef-blink) is preserved.
 */
AROS_LH2(void, CEFGetAllocator,
    AROS_LHA(APTR *, allocFn, A0),
    AROS_LHA(APTR *, freeFn, A1),
    LIBBASETYPEPTR, LIBBASE, 18, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFBlinkGetAllocator(allocFn, freeFn);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, CEFRegisterSchemeHandlerFactory,
    AROS_LHA(APTR, scheme_name, A0),
    AROS_LHA(APTR, domain_name, A1),
    AROS_LHA(APTR, factory, A2),
    LIBBASETYPEPTR, LIBBASE, 19, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFBlinkRegisterSchemeHandlerFactory(scheme_name, domain_name,
        factory);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, CEFStringUserfreeFree,
    AROS_LHA(APTR, str, A0),
    LIBBASETYPEPTR, LIBBASE, 20, CEF)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFBlinkStringUserfreeFree(str);
    AROS_LIBFUNC_EXIT
}
