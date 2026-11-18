/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Bounded native request-context cookie isolation and reference-lifetime probe.
 * No network request or real credentials are used.
 */
#include <string.h>
#include <exec/libraries.h>
#include <libraries/cef_client.h>
#include <proto/v8.h>
#include <proto/debug.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_cookie_capi.h"
#include "include/capi/cef_request_context_capi.h"

struct Library *CEFBase;
struct SetResult { cef_set_cookie_callback_t api; int done, ok; };
struct VisitResult { cef_cookie_visitor_t api; int done, ok, seen; cef_char_t expected; };
static struct SetResult setters[2];
static struct VisitResult visitors[3];
static cef_char_t resources[] = u"Developer:CEF";
static cef_char_t locales[] = u"Developer:CEF/locales";
static cef_char_t root[] = u"T:CEFContextProbe";
static cef_char_t origin[] = u"https://context-isolation.invalid/";
static cef_char_t name[] = u"isolation_probe";
static cef_char_t path[] = u"/";
static cef_char_t values[2][2] = {u"A", u"B"};

/* Static objects outlive all callbacks and engine shutdown. Callbacks here run
 * on CEF's UI thread, the same task that pumps and inspects their results. */
static void CEF_CALLBACK add_ref(cef_base_ref_counted_t *p) { (void)p; }
static int CEF_CALLBACK release(cef_base_ref_counted_t *p) { (void)p; return 0; }
static int CEF_CALLBACK has_ref(cef_base_ref_counted_t *p) { (void)p; return 1; }
static void base_init(cef_base_ref_counted_t *p, size_t size)
{
    p->size = size; p->add_ref = add_ref; p->release = release;
    p->has_one_ref = has_ref; p->has_at_least_one_ref = has_ref;
}
static void str(cef_string_t *s, cef_char_t *v, size_t n)
{ s->str = v; s->length = n; s->dtor = NULL; }
#define STRING(s, v) str(&(s), (v), sizeof(v) / sizeof((v)[0]) - 1)
static int equal(const cef_string_t *s, cef_char_t *v, size_t n)
{ return s->str && s->length == n && memcmp(s->str, v, n * sizeof(*v)) == 0; }
static void CEF_CALLBACK set_done(cef_set_cookie_callback_t *self, int ok)
{ struct SetResult *r = (struct SetResult *)self; r->ok = ok; r->done = 1; }
static int CEF_CALLBACK visit(cef_cookie_visitor_t *self,
    const cef_cookie_t *cookie, int count, int total, int *delete_cookie)
{
    struct VisitResult *r = (struct VisitResult *)self;
    *delete_cookie = 0;
    ++r->seen;
    if (total != 1 || count != 0 ||
        !equal(&cookie->name, name, sizeof(name) / sizeof(*name) - 1) ||
        cookie->value.length != 1 || !cookie->value.str ||
        cookie->value.str[0] != r->expected)
        r->ok = 0;
    if (count + 1 == total) r->done = 1;
    return 1;
}
static int await_result(int *done)
{
    ULONG tick;
    for (tick = 0; tick < 1500 && !*done; ++tick)
    { CEFDoMessageLoopWork(); Delay(1); }
    return *done;
}
static int check_cookie(cef_cookie_manager_t *manager, int slot, cef_char_t expected)
{
    struct VisitResult *r = &visitors[slot];
    base_init(&r->api.base, sizeof(r->api));
    r->api.visit = visit; r->expected = expected; r->ok = 1;
    return manager->visit_all_cookies(manager, &r->api) &&
        await_result(&r->done) && r->seen == 1 && r->ok;
}
#define DROP(p) do { if (p) { cef_base_ref_counted_t *b = (cef_base_ref_counted_t *)(p); b->release(b); (p) = NULL; } } while (0)

int main(int argc, char **argv)
{
    char *args[] = {argv[0], "--single-process", "--disable-gpu", "--disable-gpu-compositing", NULL};
    cef_main_args_t main_args = {4, args};
    cef_settings_t settings = {0};
    cef_request_context_settings_t context_settings = {0};
    cef_request_context_t *contexts[2] = {NULL, NULL};
    cef_cookie_manager_t *managers[2] = {NULL, NULL};
    cef_string_t url = {0};
    APTR allocate = NULL, deallocate = NULL;
    int initialized = 0, ok = 0, i, same, sharing;
    (void)argc;
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (!CEFBase) return RETURN_FAIL;
    CEFGetAllocator(&allocate, &deallocate);
    if (!allocate || !deallocate || !V8AllocatorInstall(allocate, deallocate)) goto done;
    settings.size = sizeof(settings); settings.no_sandbox = 1;
    STRING(settings.resources_dir_path, resources);
    STRING(settings.locales_dir_path, locales);
    STRING(settings.root_cache_path, root);
    if (CEFExecuteProcess(&main_args, NULL, NULL) >= 0) goto done;
    if (!CEFInitialize(&main_args, &settings, NULL, NULL)) goto done;
    initialized = 1;
    context_settings.size = sizeof(context_settings);
    STRING(url, origin);
    for (i = 0; i < 2; ++i)
    {
        cef_cookie_t cookie = {0};
        contexts[i] = CEFCreateRequestContext(&context_settings, NULL);
        if (!contexts[i]) goto done;
        managers[i] = contexts[i]->get_cookie_manager(contexts[i], NULL);
        if (!managers[i]) goto done;
        cookie.size = sizeof(cookie); cookie.secure = 1;
        STRING(cookie.name, name); STRING(cookie.path, path);
        str(&cookie.value, values[i], 1);
        base_init(&setters[i].api.base, sizeof(setters[i].api));
        setters[i].api.on_complete = set_done;
        if (!managers[i]->set_cookie(managers[i], &url, &cookie, &setters[i].api) ||
            !await_result(&setters[i].done) || !setters[i].ok) goto done;
        KPrintF("[CEF-CONTEXTS] set context=%ld passed\n", (LONG)i);
    }
    /* CEF consumes one wrapper reference for refptr_same arguments, unlike
     * the borrowed self argument. Preserve our ownership across both calls. */
    contexts[1]->base.base.add_ref(&contexts[1]->base.base);
    same = contexts[0]->is_same(contexts[0], contexts[1]);
    contexts[1]->base.base.add_ref(&contexts[1]->base.base);
    sharing = contexts[0]->is_sharing_with(contexts[0], contexts[1]);
    if (same || sharing ||
        !check_cookie(managers[0], 0, 'A') || !check_cookie(managers[1], 1, 'B'))
        goto done;
    KPrintF("[CEF-CONTEXTS] same-origin cookies isolated\n");
    DROP(managers[0]); DROP(contexts[0]);
    for (i = 0; i < 30; ++i) { CEFDoMessageLoopWork(); Delay(1); }
    if (!check_cookie(managers[1], 2, 'B')) goto done;
    KPrintF("[CEF-CONTEXTS] B survives release of A references\n");
    ok = 1;
done:
    for (i = 0; i < 2; ++i) { DROP(managers[i]); DROP(contexts[i]); }
    if (initialized)
    {
        for (i = 0; i < 30; ++i) { CEFDoMessageLoopWork(); Delay(1); }
        CEFShutdown();
        KPrintF("[CEF-CONTEXTS] shutdown returned\n");
    }
    CloseLibrary(CEFBase);
    KPrintF("[CEF-CONTEXTS] %s\n", ok ? "PASS" : "FAIL");
    Printf("CEF request-context cookies: %s\n", ok ? "PASS - isolated, B survives A release" : "FAIL");
    return ok ? RETURN_OK : RETURN_FAIL;
}
