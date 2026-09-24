/*
 * UVHostnameTest -- uv1 hostname lookup without a PosixC wrapper dependency.
 */

#include <aros/debug.h>
#include <stdio.h>
#include <string.h>

#include <uv.h>

int main(void)
{
    char hostname[64];
    size_t size = sizeof(hostname);
    int result = uv_os_gethostname(hostname, &size);

    printf("[UVHostname] result=%d size=%lu value=%s\n",
           result, (unsigned long)size, result == 0 ? hostname : "");
    bug("[UVHostname] result=%d size=%lu value=%s\n",
        result, (unsigned long)size, result == 0 ? hostname : "");

    if (result == 0 && strcmp(hostname, "AROS") == 0 && size == 4)
    {
        printf("[UVHostname] PASS\n");
        return 0;
    }

    printf("[UVHostname] FAIL\n");
    return 20;
}
