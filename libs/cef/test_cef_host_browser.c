/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Independent native application using the resident host. This diagnostic
 * reuses the browser probe's presentation/input handlers. CEF owns the loop;
 * all browser and Intuition work below runs on its captured UI runner.
 */
#include <stdio.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include "include/capi/cef_base_capi.h"
static struct SignalSemaphore reference_lock;
static struct { cef_base_ref_counted_t *base; LONG count; } references[8];
static unsigned reference_count;
static int reference_error;
static void host_register_reference(cef_base_ref_counted_t *base)
{
    ObtainSemaphore(&reference_lock);
    if (reference_count < 8) {
        references[reference_count].base = base;
        references[reference_count++].count = 1;
    } else reference_error = 1;
    ReleaseSemaphore(&reference_lock);
}
static LONG host_reference(cef_base_ref_counted_t *base, LONG delta)
{
    unsigned i; LONG count = -1;
    ObtainSemaphore(&reference_lock);
    for (i = 0; i < reference_count; ++i) if (references[i].base == base) {
        count = (references[i].count += delta);
        if (count < 0) reference_error = 1;
        break;
    }
    if (count < 0) reference_error = 1;
    ReleaseSemaphore(&reference_lock);
    return count;
}
#define CEF_BROWSER_HOST_MODE 1
#include "test_cef_browser.c"
#include <libraries/cef_host.h>
#include "include/capi/cef_task_capi.h"
#include "include/capi/cef_request_context_capi.h"

static cef_task_runner_t *runner;
static cef_request_context_t *context;
static cef_task_t tick_task;
static struct Task *application_task;
static BYTE done_signal = -1;
static ULONG hold_ticks = 1500;
static int view_started, view_done, view_result = RETURN_FAIL;
static const char *application_name;
static char window_title[96];

static int handlers_released(int include_task)
{
    unsigned i; int ready = 1;
    ObtainSemaphore(&reference_lock);
    for (i = 0; i < reference_count; ++i) {
        if (!include_task && references[i].base == &tick_task.base) continue;
        if (references[i].count != 1) ready = 0;
    }
    if (reference_error) ready = 0;
    ReleaseSemaphore(&reference_lock);
    return ready;
}
static int schedule_tick(void)
{
    tick_task.base.add_ref(&tick_task.base); /* C API transfers this reference. */
    return runner->post_delayed_task(runner, &tick_task, 20);
}
static void finish_view(void)
{
    if (browser_window) { CloseWindow(browser_window); browser_window = NULL; }
    if (last_frame) { FreeVec(last_frame); last_frame = NULL; }
    view_result = first_paint && !reference_error ? RETURN_OK : RETURN_FAIL;
    view_done = 1;
    Signal(application_task, 1UL << done_signal);
    /* The application sends a host request as a UI-queue barrier before it
     * unloads. This callback and its CEF wrapper must return first. */
}
static int start_view(void)
{
    cef_window_info_t info = {0};
    cef_browser_settings_t settings = {0};
    cef_string_t url = {0};
    snprintf(window_title, sizeof(window_title), "CEF shared engine - %s", application_name);
    browser_window = OpenWindowTags(NULL,
        WA_Title, (IPTR)window_title, WA_Left, 20 + (application_name[0] == 'B' ? 160 : 0),
        WA_Top, 45 + (application_name[0] == 'B' ? 90 : 0),
        WA_InnerWidth, 580, WA_InnerHeight, 340, WA_AutoAdjust, TRUE,
        WA_GimmeZeroZero, TRUE, WA_Activate, TRUE, WA_ReportMouse, TRUE,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW |
            IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
        TAG_DONE);
    if (!browser_window) return 0;
    initialize_handlers();
    info.size = sizeof(info); info.windowless_rendering_enabled = 1;
    info.bounds.width = browser_window->GZZWidth; info.bounds.height = browser_window->GZZHeight;
    settings.size = sizeof(settings);
    set_static_cef_string(&url, url_override, url_override_len);
    client.base.add_ref(&client.base);
    context->base.base.add_ref(&context->base.base);
    return CEFCreateBrowser(&info, &client, &url, &settings, NULL, context);
}
static void CEF_CALLBACK tick(cef_task_t *self)
{
    (void)self;
    if (!view_started) {
        view_started = 1;
        if (!start_view()) {
            browser_closed = 1;
            KPrintF("[CEF-HOST-VIEW] %s create failed\n", application_name);
        }
    }
    ++pump_tick;
    if (browser_window) handle_window_messages();
    if (first_paint && pump_tick >= first_paint_tick + hold_ticks)
        request_browser_close();
    if (!first_paint && pump_tick > CREATE_TIMEOUT_TICKS)
        request_browser_close();
    if (browser_closed && handlers_released(0)) { finish_view(); return; }
    if (!schedule_tick()) {
        /* Keep the application loaded if callbacks still exist. A failed post
         * is diagnostic failure, never permission to unload referenced code. */
        KPrintF("[CEF-HOST-VIEW] %s cannot schedule; retaining callback code\n", application_name);
    }
}
static LONG host_request(struct CEFHostRequest *request, ULONG command)
{
    LONG result;
    request->command = command;
    result = CEFHostPostRequest(request);
    if (result != CEF_HOST_OK) return result;
    WaitPort(request->message.mn_ReplyPort);
    if (GetMsg(request->message.mn_ReplyPort) != &request->message) return CEF_HOST_INVALID;
    return request->result;
}
int main(int argc, char **argv)
{
    struct CEFHostRequest request = {0};
    struct MsgPort *reply = NULL;
    IPTR session = 0;
    unsigned i;
    int result = RETURN_FAIL;
    if (argc < 3 || strlen(argv[1]) >= CEF_HOST_NAME_MAX || !set_url_override(argv[2])) return RETURN_ERROR;
    application_name = argv[1];
    if (argc > 3) hold_ticks = strtoul(argv[3], NULL, 10);
    settle_ticks = hold_ticks ? hold_ticks : 1;
    InitSemaphore(&reference_lock);
    application_task = FindTask(NULL);
    done_signal = AllocSignal(-1);
    reply = CreateMsgPort();
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (done_signal < 0 || !reply || !CEFBase) goto done;
    request.message.mn_Length = sizeof(request); request.message.mn_ReplyPort = reply;
    request.protocol = CEF_HOST_PROTOCOL;
    strcpy(request.application, application_name);
    if (host_request(&request, CEF_HOST_OPEN) != CEF_HOST_OK) goto done;
    session = request.session; context = request.context; runner = request.ui_runner;
    KPrintF("[CEF-HOST-VIEW] %s connected client=%p owner=%p context=%p\n", application_name, application_task, request.engine_owner, context);
    report_guest_memory("host-client-connected");
    initialize_base(&tick_task.base, sizeof(tick_task)); tick_task.execute = tick;
    if (!schedule_tick()) goto close_session;
    Wait(1UL << done_signal);
    /* Same UI runner dispatches this acknowledgement after the final tick
     * returned and released its posted reference. */
    if (host_request(&request, CEF_HOST_STATS) != CEF_HOST_OK || !view_done || !handlers_released(1)) {
        KPrintF("[CEF-HOST-VIEW] %s callback barrier failed; retaining code\n", application_name);
        for (;;) Wait(1UL << done_signal);
    }
    result = view_result;
    for (i = 0; i < reference_count; ++i) references[i].base->release(references[i].base);
close_session:
    request.session = session;
    if (host_request(&request, CEF_HOST_CLOSE) != CEF_HOST_OK) result = RETURN_FAIL;
    KPrintF("[CEF-HOST-VIEW] %s %s callbacks-drained active=%lu owner=%p\n", application_name,
        result == RETURN_OK ? "PASS" : "FAIL", request.active_sessions, request.engine_owner);
    report_guest_memory("host-client-disconnected");
done:
    if (CEFBase) CloseLibrary(CEFBase);
    if (reply) DeleteMsgPort(reply);
    if (done_signal >= 0) FreeSignal(done_signal);
    return result;
}
