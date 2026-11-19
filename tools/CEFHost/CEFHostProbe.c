/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Independent native client: never initializes or shuts down CEF directly. */
#include <string.h>
#include <stdlib.h>
#include <libraries/cef_host.h>
#include <proto/cef.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/debug.h>
struct Library *CEFBase;
static LONG request(struct CEFHostRequest *r, ULONG command)
{
    LONG status;
    r->command = command;
    status = CEFHostPostRequest(r);
    if (status != CEF_HOST_OK) return status;
    WaitPort(r->message.mn_ReplyPort);
    if (GetMsg(r->message.mn_ReplyPort) != &r->message) return CEF_HOST_INVALID;
    return r->result;
}
int main(int argc, char **argv)
{
    struct CEFHostRequest r = {0};
    struct MsgPort *port = CreateMsgPort();
    struct Task *owner;
    IPTR session;
    LONG result = CEF_HOST_UNAVAILABLE;
    int ok = 0;
    KPrintF("[CEF-HOST-PROBE] opening facade task=%p\n", FindTask(NULL));
    CEFBase = OpenLibrary("cef.library", 3);
    KPrintF("[CEF-HOST-PROBE] facade=%p ioerr=%ld\n", CEFBase, CEFBase ? 0L : IoErr());
    if (!port || !CEFBase || argc < 2) goto done;
    r.message.mn_Length = sizeof(r); r.message.mn_ReplyPort = port;
    r.protocol = CEF_HOST_PROTOCOL;
    if (!strcmp(argv[1], "wait")) {
        unsigned attempt;
        for (attempt = 0; attempt < 600; ++attempt) {
            result = request(&r, CEF_HOST_STATS);
            if (result == CEF_HOST_OK) { ok = 1; break; }
            if (result != CEF_HOST_UNAVAILABLE) break;
            Delay(50);
        }
        KPrintF("[CEF-HOST-PROBE] readiness %s owner=%p\n", ok ? "PASS" : "FAIL", r.engine_owner);
        goto done;
    }
    if (!strcmp(argv[1], "stats") || !strcmp(argv[1], "stop")) {
        result = request(&r, !strcmp(argv[1], "stats") ? CEF_HOST_STATS : CEF_HOST_STOP);
        KPrintF("[CEF-HOST-PROBE] %s result=%ld active=%lu owner=%p\n", argv[1], result, r.active_sessions, r.engine_owner);
        Printf("CEF host %s: result=%ld active=%lu owner=%p\n", argv[1], result, r.active_sessions, r.engine_owner);
        ok = result == CEF_HOST_OK;
        goto done;
    }
    if (strlen(argv[1]) >= sizeof(r.application)) goto done;
    strcpy(r.application, argv[1]);
    r.protocol = CEF_HOST_PROTOCOL + 1;
    if (request(&r, CEF_HOST_STATS) != CEF_HOST_INVALID) goto done;
    r.protocol = CEF_HOST_PROTOCOL;
    /* Invalid names must fail without creating a session. */
    strcpy(r.profile, "../escape");
    if (request(&r, CEF_HOST_OPEN) != CEF_HOST_INVALID) goto done;
    r.profile[0] = 0;
    result = request(&r, CEF_HOST_OPEN);
    if (result != CEF_HOST_OK || !r.context || !r.ui_runner || !r.session || r.engine_owner == FindTask(NULL)) goto done;
    owner = r.engine_owner; session = r.session;
    KPrintF("[CEF-HOST-PROBE] %s OPEN session=%lu context=%p owner=%p active=%lu client=%p\n", argv[1], (ULONG)session, r.context, owner, r.active_sessions, FindTask(NULL));
    if (request(&r, CEF_HOST_STOP) != CEF_HOST_BUSY) goto close;
    if (argc > 3) {
        r.session = strtoul(argv[3], NULL, 10);
        result = request(&r, CEF_HOST_CLOSE);
        r.session = session;
        if (result != CEF_HOST_DENIED) goto close;
        KPrintF("[CEF-HOST-PROBE] foreign session close denied\n");
    }
    if (argc > 2) Delay(strtoul(argv[2], NULL, 10));
    result = request(&r, CEF_HOST_STATS);
    if (result != CEF_HOST_OK || r.engine_owner != owner || !r.active_sessions) goto close;
    ok = 1;
close:
    r.session = session;
    result = request(&r, CEF_HOST_CLOSE);
    if (result != CEF_HOST_OK) ok = 0;
    if (request(&r, CEF_HOST_CLOSE) != CEF_HOST_INVALID) ok = 0;
    Printf("CEF client %s: %s; active=%lu owner=%p\n", argv[1], ok ? "PASS" : "FAIL", r.active_sessions, r.engine_owner);
    KPrintF("[CEF-HOST-PROBE] %s %s active=%lu owner=%p\n", argv[1], ok ? "PASS" : "FAIL", r.active_sessions, r.engine_owner);
done:
    if (CEFBase) CloseLibrary(CEFBase);
    if (port) DeleteMsgPort(port);
    return ok ? RETURN_OK : RETURN_FAIL;
}
