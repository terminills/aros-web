/*
 * UVGuessHandleTest -- classify regular and interactive AROS descriptors
 * through uv1.library.
 */

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <uv.h>

int main(void)
{
    const char *path = "T:uv-guess-handle.tmp";
    uv_handle_type file_type;
    uv_handle_type stdout_type;
    int descriptor;

    descriptor = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (descriptor < 0)
    {
        printf("UV_GUESS_FAIL open\n");
        return 20;
    }

    file_type = uv_guess_handle(descriptor);
    stdout_type = uv_guess_handle(1);
    unlink(path);

    if (file_type != UV_FILE ||
        stdout_type != (isatty(1) ? UV_TTY : UV_FILE))
    {
        printf("UV_GUESS_FAIL file=%d stdout=%d\n",
               (int)file_type, (int)stdout_type);
        return 20;
    }

    printf("UV_GUESS_PASS file=%d stdout=%d\n",
           (int)file_type, (int)stdout_type);
    return 0;
}
