/*
    UVNetTest -- drives uv.library's internal networking self-test.

    The real work runs inside uv.library (uv_aros_net_selftest): a TCP loopback
    pair watched with uv_poll over the WaitSelect backend, all under the
    library's own bsdsocket SocketBase. This program just invokes it and reports
    PASS/FAIL. Needs AROSTCP (bsdsocket.library) up with a loopback interface.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>

#include <stdio.h>

/* Exported LVO (uv1.conf). */
extern int uv_aros_net_selftest(void);

#define TLOG(...) do { printf("[UVNet] " __VA_ARGS__); printf("\n"); \
                       bug("[UVNet] " __VA_ARGS__); bug("\n"); } while (0)

int main(void)
{
    int rc;

    TLOG("invoking uv_aros_net_selftest (uv_poll over bsdsocket loopback)...");
    rc = uv_aros_net_selftest();

    if (rc == 0) {
        TLOG("PASS: libuv networking readiness path verified end-to-end");
        return 0;
    }
    TLOG("FAIL: uv_aros_net_selftest rc=%d (see [UVNet] serial lines above)", rc);
    return 20;
}
