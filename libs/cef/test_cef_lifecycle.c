/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Focused cef.library process and initialization lifecycle probe
*/

#include <string.h>

#include <exec/libraries.h>
#include <proto/cef.h>
#include <proto/debug.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "include/capi/cef_app_capi.h"

struct Library *CEFBase = NULL;

static cef_char_t resources_path[] = u"Developer:Chromium";
static cef_char_t locales_path[] = u"Developer:Chromium/locales";

static void set_static_cef_string(cef_string_t *target, cef_char_t *value,
    size_t length)
{
    target->str = value;
    target->length = length;
    target->dtor = NULL;
}

int main(int argc, char **argv)
{
    cef_main_args_t main_args;
    cef_settings_t settings;
    LONG execute_result;
    LONG initialize_result;
    int result = RETURN_FAIL;

    KPrintF("[CEF-LIFECYCLE] open begin\n");
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (CEFBase == NULL)
    {
        KPrintF("[CEF-LIFECYCLE] open failed\n");
        return RETURN_FAIL;
    }

    main_args.argc = argc;
    main_args.argv = argv;
    memset(&settings, 0, sizeof(settings));
    settings.size = sizeof(settings);
    settings.no_sandbox = 1;
    set_static_cef_string(&settings.resources_dir_path, resources_path,
        (sizeof(resources_path) / sizeof(resources_path[0])) - 1);
    set_static_cef_string(&settings.locales_dir_path, locales_path,
        (sizeof(locales_path) / sizeof(locales_path[0])) - 1);

    KPrintF("[CEF-LIFECYCLE] execute begin argc=%ld\n", (LONG)argc);
    execute_result = CEFExecuteProcess(&main_args, NULL, NULL);
    KPrintF("[CEF-LIFECYCLE] execute end rc=%ld\n", execute_result);
    if (execute_result >= 0)
    {
        result = (int)execute_result;
        goto close_library;
    }

    KPrintF("[CEF-LIFECYCLE] initialize begin settings=%lu\n",
        (ULONG)sizeof(settings));
    KPrintF("[CEF-LIFECYCLE] resources=Developer:Chromium "
        "locales=Developer:Chromium/locales\n");
    initialize_result = CEFInitialize(&main_args, &settings, NULL, NULL);
    KPrintF("[CEF-LIFECYCLE] initialize end rc=%ld\n", initialize_result);
    if (initialize_result == 0)
        goto close_library;

    KPrintF("[CEF-LIFECYCLE] message-loop tick begin\n");
    CEFDoMessageLoopWork();
    KPrintF("[CEF-LIFECYCLE] message-loop tick end\n");

    KPrintF("[CEF-LIFECYCLE] shutdown begin\n");
    CEFShutdown();
    KPrintF("[CEF-LIFECYCLE] shutdown end\n");
    result = RETURN_OK;

close_library:
    KPrintF("[CEF-LIFECYCLE] close begin\n");
    CloseLibrary(CEFBase);
    CEFBase = NULL;
    KPrintF("[CEF-LIFECYCLE] close end rc=%ld\n", (LONG)result);
    return result;
}
