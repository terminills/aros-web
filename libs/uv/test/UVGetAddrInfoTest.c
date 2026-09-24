/*
 * UVGetAddrInfoTest -- asynchronous DNS through AROSTCP and uv1.library.
 */

#include <stdio.h>

#include <uv.h>

static int callback_seen;
static int callback_status;

static void resolved(uv_getaddrinfo_t *request,
                     int status,
                     struct addrinfo *addresses)
{
    callback_seen = 1;
    callback_status = status;
    if (addresses != NULL)
        uv_freeaddrinfo(addresses);
    (void)request;
}

int main(void)
{
    uv_getaddrinfo_t request;
    uv_loop_t *loop = uv_default_loop();
    int result;

    result = uv_getaddrinfo(loop,
                            &request,
                            resolved,
                            "registry.npmjs.org",
                            "80",
                            NULL);
    if (result != 0)
    {
        printf("UV_DNS_FAIL submit=%d\n", result);
        return 20;
    }

    uv_run(loop, UV_RUN_DEFAULT);
    result = uv_loop_close(loop);
    if (!callback_seen || callback_status != 0 || result != 0)
    {
        printf("UV_DNS_FAIL callback=%d status=%d close=%d\n",
               callback_seen, callback_status, result);
        return 20;
    }

    printf("UV_DNS_PASS status=%d\n", callback_status);
    return 0;
}
