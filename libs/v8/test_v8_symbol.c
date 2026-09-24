/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Focused exact-build V8 engine symbol bridge probe
*/

#include <exec/libraries.h>
#include <proto/debug.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/v8.h>

struct Library *V8Base = NULL;

typedef CONST_STRPTR (*V8GetVersionFn)(void);
typedef APTR (*V8GetCurrentPlatformFn)(void);

int main(void)
{
    static const char currentPlatformSymbol[] =
        "_ZN2v88internal2V818GetCurrentPlatformEv";
    static const char getVersionSymbol[] =
        "_ZN2v82V810GetVersionEv";
    static const char absentSymbol[] =
        "_ZN2v88internal2V8_AROSDeliberatelyAbsentEv";
    V8GetVersionFn getVersion;
    V8GetCurrentPlatformFn getCurrentPlatform;
    APTR platform;
    APTR versionAddress;
    APTR resolved;
    APTR absent;
    int result = RETURN_FAIL;

    V8Base = OpenLibrary("v8.library", 0);
    if (V8Base == NULL)
    {
        Printf("V8-SYMBOL: open failed\n");
        return RETURN_FAIL;
    }

    platform = V8Initialize();
    if (platform == NULL)
    {
        Printf("V8-SYMBOL: initialization failed\n");
        goto out;
    }

    versionAddress = V8FindEngineSymbol(getVersionSymbol);
    resolved = V8FindEngineSymbol(currentPlatformSymbol);
    absent = V8FindEngineSymbol(absentSymbol);
    KPrintF("[V8-SYMBOL-TEST] version=%p platform=%p absent=%p\n",
        versionAddress, resolved, absent);

    if (versionAddress == NULL || resolved == NULL || absent != NULL)
    {
        KPrintF("[V8-SYMBOL-TEST] lookup contract failed\n");
        goto cleanup;
    }

    getVersion = (V8GetVersionFn)versionAddress;
    KPrintF("[V8-SYMBOL-TEST] calling public GetVersion\n");
    KPrintF("[V8-SYMBOL-TEST] GetVersion returned %s\n", getVersion());

    getCurrentPlatform = (V8GetCurrentPlatformFn)resolved;
    KPrintF("[V8-SYMBOL-TEST] calling internal GetCurrentPlatform\n");
    platform = getCurrentPlatform();
    KPrintF("[V8-SYMBOL-TEST] GetCurrentPlatform returned %p\n", platform);
    if (platform == NULL)
    {
        KPrintF("[V8-SYMBOL-TEST] resolved call returned NULL\n");
        goto cleanup;
    }

    KPrintF("[V8-SYMBOL-TEST] cross-library calls passed\n");
    result = RETURN_OK;

cleanup:
    KPrintF("[V8-SYMBOL-TEST] cleanup begin\n");
    V8Cleanup();
    KPrintF("[V8-SYMBOL-TEST] cleanup end\n");
out:
    KPrintF("[V8-SYMBOL-TEST] explicit close begin base=%p\n", V8Base);
    CloseLibrary(V8Base);
    KPrintF("[V8-SYMBOL-TEST] explicit close end\n");
    KPrintF("[V8-SYMBOL-TEST] returning rc=%ld\n", (LONG)result);
    return result;
}
