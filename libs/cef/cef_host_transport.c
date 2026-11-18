/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <exec/semaphores.h>
#include <libraries/cef_host.h>
#include <proto/exec.h>
#include "cef_libdefs.h"

/* cef.library is resident/noexpunge. This lock and registry outlive any host
 * process; callers cannot race a removed/deleted port through FindPort. */
static struct SignalSemaphore host_lock;
static struct MsgPort *host_port;
static struct Task *host_owner;
static LONG (*host_wake)(void);
static BOOL work_pending;
static int CEF_InitHostTransport(LIBBASETYPEPTR base)
{ (void)base; InitSemaphore(&host_lock); return TRUE; }
ADD2INITLIB(CEF_InitHostTransport, 0)

AROS_LH2(LONG, CEFHostRegisterPort, AROS_LHA(APTR, port, A0),
    AROS_LHA(APTR, wake, A1),
    LIBBASETYPEPTR, LIBBASE, 24, CEF)
{
    AROS_LIBFUNC_INIT
    LONG result = CEF_HOST_BUSY;
    struct MsgPort *p = port;
    (void)LIBBASE;
    if (!p || p->mp_SigTask != FindTask(NULL)) return CEF_HOST_INVALID;
    ObtainSemaphore(&host_lock);
    if (!host_port || (host_port == p && host_owner == FindTask(NULL))) {
        host_port = p; host_owner = FindTask(NULL); host_wake = (LONG (*)(void))wake;
        result = CEF_HOST_OK;
    }
    ReleaseSemaphore(&host_lock);
    return result;
    AROS_LIBFUNC_EXIT
}
AROS_LH1(LONG, CEFHostUnregisterPort, AROS_LHA(APTR, port, A0),
    LIBBASETYPEPTR, LIBBASE, 25, CEF)
{
    AROS_LIBFUNC_INIT
    LONG result = CEF_HOST_DENIED;
    (void)LIBBASE;
    ObtainSemaphore(&host_lock);
    if (host_port == port && host_owner == FindTask(NULL)) {
        host_port = NULL; host_owner = NULL; host_wake = NULL; work_pending = FALSE; result = CEF_HOST_OK;
    }
    ReleaseSemaphore(&host_lock);
    return result;
    AROS_LIBFUNC_EXIT
}
AROS_LH1(LONG, CEFHostPostRequest, AROS_LHA(APTR, request, A0),
    LIBBASETYPEPTR, LIBBASE, 26, CEF)
{
    AROS_LIBFUNC_INIT
    struct CEFHostRequest *r = request;
    LONG result = CEF_HOST_UNAVAILABLE;
    (void)LIBBASE;
    if (!r || r->message.mn_Length != sizeof(*r) ||
        !r->message.mn_ReplyPort || r->protocol != CEF_HOST_PROTOCOL)
        return CEF_HOST_INVALID;
    ObtainSemaphore(&host_lock);
    if (host_port && host_wake && (work_pending || host_wake())) {
        /* The posted UI task takes this same lock before GetMsg, so it cannot
         * observe an empty queue before this PutMsg completes. */
        work_pending = TRUE;
        r->sender = FindTask(NULL);
        PutMsg(host_port, &r->message);
        result = CEF_HOST_OK;
    }
    ReleaseSemaphore(&host_lock);
    return result;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(APTR, CEFHostNextRequest, LIBBASETYPEPTR, LIBBASE, 27, CEF)
{
    AROS_LIBFUNC_INIT
    struct Message *message = NULL;
    (void)LIBBASE;
    ObtainSemaphore(&host_lock);
    if (host_port && host_owner == FindTask(NULL)) {
        message = GetMsg(host_port);
        if (!message) work_pending = FALSE;
    }
    ReleaseSemaphore(&host_lock);
    return message;
    AROS_LIBFUNC_EXIT
}
