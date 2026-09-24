/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Focused cef.library identity and lifecycle probe
*/

#include <exec/libraries.h>
#include <proto/cef.h>
#include <proto/debug.h>
#include <proto/dos.h>
#include <proto/exec.h>

struct Library *CEFBase = NULL;

int main(void)
{
    const LONG cef_major_expected = 124;
    const LONG cef_commit_expected = 2933;
    const LONG chrome_major_expected = 124;
    const LONG chrome_build_expected = 6367;
    LONG cef_major;
    LONG cef_commit;
    LONG chrome_major;
    LONG chrome_build;
    CONST_STRPTR platform_hash;
    CONST_STRPTR universal_hash;
    CONST_STRPTR commit_hash;
    int result = RETURN_FAIL;

    KPrintF("[CEF-VERSION] open begin\n");
    CEFBase = OpenLibrary(CEFNAME, CEFVERSION);
    if (CEFBase == NULL)
    {
        KPrintF("[CEF-VERSION] open failed\n");
        return RETURN_FAIL;
    }

    KPrintF("[CEF-VERSION] open passed base=%p\n", CEFBase);
    cef_major = cef_version_info(0);
    cef_commit = cef_version_info(3);
    chrome_major = cef_version_info(4);
    chrome_build = cef_version_info(6);
    platform_hash = cef_api_hash(0);
    universal_hash = cef_api_hash(1);
    commit_hash = cef_api_hash(2);

    KPrintF("[CEF-VERSION] cef=%ld commit=%ld chrome=%ld.%ld\n",
        cef_major, cef_commit, chrome_major, chrome_build);
    KPrintF("[CEF-VERSION] platform=%s universal=%s commit=%s\n",
        platform_hash, universal_hash, commit_hash);

    if (cef_major == cef_major_expected &&
        cef_commit == cef_commit_expected &&
        chrome_major == chrome_major_expected &&
        chrome_build == chrome_build_expected &&
        platform_hash != NULL &&
        universal_hash != NULL &&
        commit_hash != NULL)
    {
        KPrintF("[CEF-VERSION] PASS\n");
        result = RETURN_OK;
    }
    else
    {
        KPrintF("[CEF-VERSION] FAIL\n");
    }

    KPrintF("[CEF-VERSION] close begin\n");
    CloseLibrary(CEFBase);
    CEFBase = NULL;
    KPrintF("[CEF-VERSION] close end rc=%ld\n", (LONG)result);
    return result;
}
