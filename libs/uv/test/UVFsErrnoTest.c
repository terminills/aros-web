/*
 * UVFsErrnoTest -- preserve POSIX filesystem errors across uv1.library.
 *
 * AROS libraries have independent relative bases.  A PosixC call made by
 * uv1.library can therefore fail without updating uv1's errno slot.  Missing
 * paths must still reach Node as UV_ENOENT instead of the generic UV_EIO.
 */

#include <aros/debug.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <uv.h>

#define TLOG(...) do { printf("[UVFsErrno] " __VA_ARGS__); printf("\n"); \
                       bug("[UVFsErrno] " __VA_ARGS__); bug("\n"); } while (0)

static const char *const missing_path = "RAM:uv-fs-errno-missing";
static const char *const created_path = "RAM:uv-fs-errno-created";

static int expect_result(const char *operation, int actual, int expected)
{
    if (actual == expected)
    {
        TLOG("PASS: %s result=%d", operation, actual);
        return 0;
    }

    TLOG("FAIL: %s result=%d expected=%d", operation, actual, expected);
    return 1;
}

int main(void)
{
    uv_fs_t request;
    int failed = 0;
    int fd;

    uv_fs_unlink(NULL, &request, created_path, NULL);
    uv_fs_req_cleanup(&request);

    failed += expect_result(
        "access missing",
        uv_fs_access(NULL, &request, missing_path, F_OK, NULL),
        UV_ENOENT);
    uv_fs_req_cleanup(&request);

    failed += expect_result(
        "open missing",
        uv_fs_open(NULL, &request, missing_path, O_RDONLY, 0, NULL),
        UV_ENOENT);
    uv_fs_req_cleanup(&request);

    fd = uv_fs_open(
        NULL,
        &request,
        created_path,
        O_CREAT | O_EXCL | O_WRONLY,
        0600,
        NULL);
    failed += expect_result("create file", fd < 0 ? fd : 0, 0);
    uv_fs_req_cleanup(&request);

    if (fd >= 0)
    {
        failed += expect_result(
            "close file",
            uv_fs_close(NULL, &request, fd, NULL),
            0);
        uv_fs_req_cleanup(&request);

        failed += expect_result(
            "access existing",
            uv_fs_access(NULL, &request, created_path, F_OK, NULL),
            0);
        uv_fs_req_cleanup(&request);

        failed += expect_result(
            "unlink file",
            uv_fs_unlink(NULL, &request, created_path, NULL),
            0);
        uv_fs_req_cleanup(&request);
    }

    if (failed == 0)
    {
        TLOG("PASS: uv1 preserved filesystem errors and successful operations");
        return 0;
    }

    TLOG("FAIL: %d filesystem error checks failed", failed);
    return 20;
}
