/*
 * UVHomeTest -- prove uv1.library provides an AROS home without HOME/passwd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

int main(void)
{
    char home[64];
    size_t size = sizeof(home);
    int result;

    unsetenv("HOME");
    result = uv_os_homedir(home, &size);
    if (result != 0)
    {
        printf("UV_HOME_FAIL result=%d (%s)\n", result, uv_strerror(result));
        return 1;
    }

    if (strcmp(home, "SYS:") != 0 || size != 4)
    {
        printf("UV_HOME_FAIL home='%s' size=%lu\n",
               home, (unsigned long)size);
        return 1;
    }

    printf("UV_HOME_PASS home='%s' size=%lu\n",
           home, (unsigned long)size);
    return 0;
}
