/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * One native owner task for the resident engine. Clients own sessions, not
 * initialization or shutdown. Scheduling uses CEF tasks and its native AROS message loop.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <libraries/cef_client.h>
#include <libraries/cef_host.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/debug.h>
#include <proto/v8.h>
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_task_capi.h"
#include "include/capi/cef_request_context_capi.h"

struct Library *CEFBase;
static struct Task *owner;
static cef_task_t dispatch_task;
static cef_task_runner_t *ui_runner;
static int allow_stop;
static cef_char_t root[1024], resources[] = u"Developer:CEF", locales[] = u"Developer:CEF/locales";
static char root_ascii[768];
static ULONG session_count;
static IPTR next_session = 1;
struct Session {
    struct Session *next;
    struct Task *client;
    IPTR id;
    cef_request_context_t *context;
};
static struct Session *sessions;

static void CEF_CALLBACK add_ref(cef_base_ref_counted_t *p) { (void)p; }
static int CEF_CALLBACK release(cef_base_ref_counted_t *p) { (void)p; return 0; }
static int CEF_CALLBACK has_ref(cef_base_ref_counted_t *p) { (void)p; return 1; }
static void base_init(cef_base_ref_counted_t *b, size_t n)
{
    b->size = n; b->add_ref = add_ref; b->release = release;
    b->has_one_ref = has_ref; b->has_at_least_one_ref = has_ref;
}
static void text(cef_string_t *s, cef_char_t *v, size_t n)
{ s->str = v; s->length = n; s->dtor = NULL; }
static int to_utf16(cef_char_t *out, size_t capacity, const char *in)
{
    size_t n = strlen(in), i;
    if (n >= capacity) return 0;
    for (i = 0; i < n; ++i) {
        if ((unsigned char)in[i] > 127) return 0;
        out[i] = (unsigned char)in[i];
    }
    out[n] = 0; return 1;
}
static int valid_name(const char *s, int allow_empty)
{
    unsigned i;
    for (i = 0; i < CEF_HOST_NAME_MAX; ++i) {
        char c = s[i];
        if (!c) return i != 0 || allow_empty;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 0;
}
static void drop_context(cef_request_context_t *c)
{ if (c) c->base.base.release(&c->base.base); }
static int handle(struct CEFHostRequest *r, int allow_stop)
{
    struct Session *s, **link;
    int stop = 0;
    r->result = CEF_HOST_INVALID;
    r->context = NULL;
    r->engine_owner = owner;
    r->ui_runner = NULL;
    if (r->message.mn_Length != sizeof(*r) || r->protocol != CEF_HOST_PROTOCOL)
        goto reply;
    switch (r->command) {
    case CEF_HOST_OPEN: {
        cef_request_context_settings_t settings = {0};
        cef_char_t cache[1024];
        char cache_ascii[1024];
        if (!valid_name(r->application, 0) || !valid_name(r->profile, 1)) break;
        s = AllocMem(sizeof(*s), MEMF_PUBLIC | MEMF_CLEAR);
        if (!s) { r->result = CEF_HOST_NOMEM; break; }
        settings.size = sizeof(settings);
        if (r->profile[0]) {
            int length = snprintf(cache_ascii, sizeof(cache_ascii), "%s/%s/%s",
                root_ascii, r->application, r->profile);
            if (length < 0 || length >= sizeof(cache_ascii) ||
                !to_utf16(cache, sizeof(cache) / sizeof(*cache), cache_ascii)) {
                FreeMem(s, sizeof(*s)); break;
            }
            text(&settings.cache_path, cache, length);
            settings.persist_session_cookies = 1;
        }
        s->context = CEFCreateRequestContext(&settings, NULL);
        if (!s->context) { FreeMem(s, sizeof(*s)); r->result = CEF_HOST_UNAVAILABLE; break; }
        s->id = next_session++;
        if (!next_session) next_session = 1;
        s->client = r->sender;
        s->next = sessions; sessions = s; ++session_count;
        r->session = s->id; r->context = s->context; r->ui_runner = ui_runner;
        r->result = CEF_HOST_OK;
        KPrintF("[CEF-HOST] open session=%lu active=%lu\n", (ULONG)s->id, session_count);
        break;
    }
    case CEF_HOST_CLOSE:
        for (link = &sessions; (s = *link) != NULL; link = &s->next) {
            if (s->id != r->session) continue;
            if (s->client != r->sender) { r->result = CEF_HOST_DENIED; break; }
            *link = s->next; --session_count;
            drop_context(s->context); FreeMem(s, sizeof(*s));
            r->result = CEF_HOST_OK;
            KPrintF("[CEF-HOST] close session=%lu active=%lu\n", (ULONG)r->session, session_count);
            break;
        }
        break;
    case CEF_HOST_STATS: r->result = CEF_HOST_OK; break;
    case CEF_HOST_STOP:
        r->result = !allow_stop ? CEF_HOST_DENIED : (session_count ? CEF_HOST_BUSY : CEF_HOST_OK);
        stop = r->result == CEF_HOST_OK;
        break;
    }
reply:
    r->active_sessions = session_count;
    ReplyMsg(&r->message);
    return stop;
}

static void CEF_CALLBACK dispatch(cef_task_t *self)
{
    struct CEFHostRequest *r;
    (void)self;
    while ((r = CEFHostNextRequest()) != NULL) {
        if (handle(r, allow_stop)) { CEFQuitMessageLoop(); return; }
    }
}
static LONG wake_owner(void)
{
    return ui_runner->post_task(ui_runner, &dispatch_task);
}

int main(int argc, char **argv)
{
    struct MsgPort *port = NULL;
    struct Message *msg;
    int registered = 0, published = 0, initialized = 0, result = RETURN_FAIL;
    APTR alloc = NULL, free_fn = NULL;
    char *args[] = {argv[0], "--single-process", "--disable-gpu", "--disable-gpu-compositing", NULL};
    cef_main_args_t main_args = {4, args};
    cef_settings_t settings = {0};
    const char *profile_root = argc > 1 ? argv[1] : "ENVARC:CEF/Profiles";
    allow_stop = argc > 2 && !strcmp(argv[2], "--allow-stop");
    if (strlen(profile_root) >= sizeof(root_ascii) || !strchr(profile_root, ':') ||
        !to_utf16(root, sizeof(root) / sizeof(*root), profile_root)) return RETURN_ERROR;
    strcpy(root_ascii, profile_root);
    owner = FindTask(NULL);
    port = CreateMsgPort();
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    KPrintF("[CEF-HOST] startup port=%p facade=%p owner=%p\n", port, CEFBase, owner);
    if (!port || !CEFBase || CEFHostRegisterPort(port, NULL) != CEF_HOST_OK) goto done;
    registered = 1;
    CEFGetAllocator(&alloc, &free_fn);
    if (!alloc || !free_fn || !V8AllocatorInstall(alloc, free_fn)) goto done;
    base_init(&dispatch_task.base, sizeof(dispatch_task));
    dispatch_task.execute = dispatch;
    settings.size = sizeof(settings); settings.no_sandbox = 1;
    settings.windowless_rendering_enabled = 1;
    text(&settings.resources_dir_path, resources, sizeof(resources) / sizeof(*resources) - 1);
    text(&settings.locales_dir_path, locales, sizeof(locales) / sizeof(*locales) - 1);
    text(&settings.root_cache_path, root, strlen(root_ascii));
    result = CEFExecuteProcess(&main_args, NULL, NULL);
    KPrintF("[CEF-HOST] execute=%ld\n", (LONG)result);
    if (result >= 0) { result = RETURN_FAIL; goto done; }
    result = CEFInitialize(&main_args, &settings, NULL, NULL);
    KPrintF("[CEF-HOST] initialize=%ld\n", (LONG)result);
    if (result != 1) { result = RETURN_FAIL; goto done; }
    initialized = 1;
    result = RETURN_FAIL;
    ui_runner = CEFGetTaskRunner(TID_UI);
    if (!ui_runner) goto done;
    if (CEFHostRegisterPort(port, wake_owner) != CEF_HOST_OK) goto done;
    port->mp_Node.ln_Name = CEF_HOST_PORT;
    AddPort(port); published = 1;
    KPrintF("[CEF-HOST] READY owner=%p native-message-loop\n", owner);
    Printf("CEF shared engine ready\n");
    CEFRunMessageLoop();
    result = RETURN_OK;
done:
    KPrintF("[CEF-HOST] exit initialized=%ld result=%ld\n", (LONG)initialized, (LONG)result);
    if (registered) CEFHostUnregisterPort(port);
    if (published) RemPort(port);
    if (port) while ((msg = GetMsg(port))) {
        struct CEFHostRequest *r = (struct CEFHostRequest *)msg;
        r->result = CEF_HOST_UNAVAILABLE; r->context = NULL; r->ui_runner = NULL; ReplyMsg(msg);
    }
    while (sessions) {
        struct Session *s = sessions; sessions = s->next;
        drop_context(s->context); FreeMem(s, sizeof(*s));
    }
    if (ui_runner) { ui_runner->base.release(&ui_runner->base); ui_runner = NULL; }
    if (initialized) { CEFShutdown(); KPrintF("[CEF-HOST] shutdown returned\n"); }
    if (CEFBase) CloseLibrary(CEFBase);
    if (port) DeleteMsgPort(port);
    return result;
}
