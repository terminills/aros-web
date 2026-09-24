/*
 * UVPasswdTest -- prove an AROS usergroup/MuFS record cannot expose NULL
 * strings through libuv's owned uv_passwd_t contract.
 */

#include <stdio.h>

#include <uv.h>

int main(void)
{
    uv_passwd_t passwd;
    int result;

    result = uv_os_get_passwd(&passwd);
    if (result != 0)
    {
        printf("UV_PASSWD_FAIL result=%d (%s)\n",
               result, uv_strerror(result));
        return 1;
    }

    if (passwd.username == NULL ||
        passwd.homedir == NULL ||
        passwd.shell == NULL)
    {
        printf("UV_PASSWD_FAIL null-field username=%p homedir=%p shell=%p\n",
               (void *)passwd.username,
               (void *)passwd.homedir,
               (void *)passwd.shell);
        uv_os_free_passwd(&passwd);
        return 1;
    }

    printf("UV_PASSWD_PASS username='%s' homedir='%s' shell='%s' uid=%lu gid=%lu\n",
           passwd.username,
           passwd.homedir,
           passwd.shell,
           (unsigned long)passwd.uid,
           (unsigned long)passwd.gid);
    uv_os_free_passwd(&passwd);
    return 0;
}
