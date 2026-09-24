#include <libraries/electron.h>
#include <proto/electron.h>

#include <stdio.h>

int main(void)
{
    CONST_STRPTR adapter = ElectronVersion();
    CONST_STRPTR node = ElectronNodeVersion();
    ULONG capabilities = ElectronCapabilities();
    LONG cef_major = ElectronCEFVersionInfo(0);

    printf("ELECTRON_LIBRARY adapter=%s node=%s cef=%ld caps=0x%08lx\n",
        adapter != NULL ? (const char *)adapter : "(null)",
        node != NULL ? (const char *)node : "(null)",
        (long)cef_major,
        (unsigned long)capabilities);

    if (adapter == NULL || node == NULL || cef_major <= 0)
        return 20;
    if ((capabilities & (ELECTRON_CAP_CEF | ELECTRON_CAP_NODE)) !=
        (ELECTRON_CAP_CEF | ELECTRON_CAP_NODE))
        return 20;

    puts("ELECTRON_LIBRARY_TOPOLOGY_PASS");
    return 0;
}
