/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * UvTtyProbe - drive uv1.library's tty backend the way node.library does:
 * resolve the non-exported uv_tty_init/uv_tty_set_mode through
 * uv_aros_find_internal_symbol() (node's lazy thunks do exactly this) and
 * run them on fd 0, printing every return code, so a node-level
 * "setRawMode EINVAL" can be split into uv1.library vs node.
 *
 *   UvTtyProbe
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/uv1.h>
#include <uv.h>

typedef int (*tty_init_fn)(uv_loop_t*, uv_tty_t*, uv_file, int);
typedef int (*tty_set_mode_fn)(uv_tty_t*, uv_tty_mode_t);
typedef int (*tty_get_winsize_fn)(uv_tty_t*, int*, int*);
typedef int (*tty_reset_mode_fn)(void);

int main(void)
{
    static uv_loop_t loop;
    static uv_tty_t tty;
    tty_init_fn tty_init;
    tty_set_mode_fn tty_set_mode;
    tty_get_winsize_fn tty_get_winsize;
    tty_reset_mode_fn tty_reset_mode;
    int rc, w = -1, h = -1;

    printf("UvTtyProbe: uv %s\n", uv_version_string());
    printf("UvTtyProbe: uv_guess_handle(0)=%d (UV_TTY=%d)\n",
           (int)uv_guess_handle(0), (int)UV_TTY);

    tty_init        = (tty_init_fn)uv_aros_find_internal_symbol("uv_tty_init");
    tty_set_mode    = (tty_set_mode_fn)uv_aros_find_internal_symbol("uv_tty_set_mode");
    tty_get_winsize = (tty_get_winsize_fn)uv_aros_find_internal_symbol("uv_tty_get_winsize");
    tty_reset_mode  = (tty_reset_mode_fn)uv_aros_find_internal_symbol("uv_tty_reset_mode");
    printf("UvTtyProbe: resolved init=%p set_mode=%p get_winsize=%p reset=%p\n",
           (void *)tty_init, (void *)tty_set_mode, (void *)tty_get_winsize,
           (void *)tty_reset_mode);
    if (!tty_init || !tty_set_mode)
        return 20;

    rc = uv_loop_init(&loop);
    printf("UvTtyProbe: uv_loop_init=%d\n", rc);

    errno = 0;
    rc = tty_init(&loop, &tty, 0, 1);
    printf("UvTtyProbe: uv_tty_init(fd 0)=%d (%s) errno=%d mode=%d\n",
           rc, rc ? uv_strerror(rc) : "ok", errno, (int)tty.mode);
    if (rc != 0)
        return 10;

    if (tty_get_winsize)
    {
        rc = tty_get_winsize(&tty, &w, &h);
        printf("UvTtyProbe: uv_tty_get_winsize=%d %dx%d\n", rc, w, h);
    }

    errno = 0;
    rc = tty_set_mode(&tty, UV_TTY_MODE_RAW_VT);
    printf("UvTtyProbe: uv_tty_set_mode(RAW_VT=%d)=%d (%s) errno=%d\n",
           (int)UV_TTY_MODE_RAW_VT, rc, rc ? uv_strerror(rc) : "ok", errno);

    errno = 0;
    rc = tty_set_mode(&tty, UV_TTY_MODE_RAW);
    printf("UvTtyProbe: uv_tty_set_mode(RAW)=%d (%s) errno=%d\n",
           rc, rc ? uv_strerror(rc) : "ok", errno);

    errno = 0;
    rc = tty_set_mode(&tty, UV_TTY_MODE_NORMAL);
    printf("UvTtyProbe: uv_tty_set_mode(NORMAL)=%d (%s) errno=%d\n",
           rc, rc ? uv_strerror(rc) : "ok", errno);

    if (tty_reset_mode)
        printf("UvTtyProbe: uv_tty_reset_mode=%d\n", tty_reset_mode());

    return 0;
}
