/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    dosbash -- the AmigaDOS shell in the place of bash for programs that
    only know how to run commands through `bash -c` (Claude Code).

    Claude Code wraps every command it runs as

        bash -c [-l] "source <snapshot> 2>/dev/null || true && ... &&
                      eval '<command>' [< /dev/null] &&
                      pwd -P >| <file>"

    and reads the directory the command left behind from <file>. dosbash
    takes the command out of that wrapper, runs it in an AmigaDOS shell in
    the current directory - so CD, Assign, Execute and redirection behave
    as at a Shell prompt - and reports the shell's final directory and
    return code back the same way. A `-c` string that is not such a wrapper
    is run as AmigaDOS as it stands.

    The name has to contain "bash": that is how Claude Code accepts a shell.
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static const char version[] __attribute__((used)) =
    "$VER: dosbash 1.0 (13.10.2026)";

/* A command line after POSIX word parsing, plus what the wrapper asked
   for around it. */
struct wrapper
{
    char *command;      /* the eval payload, NULL if not a wrapper */
    char *pwdfile;      /* where `pwd -P >|` writes, NULL if absent */
    int nullinput;      /* `< /dev/null` after the payload */
};

/* Parses one shell word at *pp (quotes, backslashes, concatenation) into a
   new string and advances past it. */
static char *parse_word(const char **pp)
{
    const char *p = *pp;
    size_t cap = strlen(p) + 1, n = 0;
    char *out = malloc(cap);

    if (!out)
        return NULL;
    while (*p == ' ' || *p == '\t')
        p++;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n')
    {
        if (*p == '\'')
        {
            p++;
            while (*p && *p != '\'')
                out[n++] = *p++;
            if (*p)
                p++;
        }
        else if (*p == '"')
        {
            p++;
            while (*p && *p != '"')
            {
                if (*p == '\\' && p[1] && strchr("\"\\$`\n", p[1]))
                    p++;
                out[n++] = *p++;
            }
            if (*p)
                p++;
        }
        else if (*p == '\\' && p[1])
        {
            out[n++] = p[1];
            p += 2;
        }
        else
            out[n++] = *p++;
    }
    out[n] = '\0';
    *pp = p;
    return out;
}

/* Splits the -c string on top-level "&&" and picks out the eval payload and
   the pwd file. */
static void parse_wrapper(const char *script, struct wrapper *w)
{
    const char *p = script;

    memset(w, 0, sizeof(*w));
    while (*p)
    {
        const char *start, *end;
        char quote = 0;

        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        start = p;
        while (*p)
        {
            if (quote)
            {
                if (*p == quote)
                    quote = 0;
                else if (quote == '"' && *p == '\\' && p[1])
                    p++;
            }
            else if (*p == '\'' || *p == '"')
                quote = *p;
            else if (*p == '\\' && p[1])
                p++;
            else if (p[0] == '&' && p[1] == '&')
                break;
            p++;
        }
        end = p;
        if (*p)
            p += 2;

        if (end - start > 5 && !strncmp(start, "eval ", 5))
        {
            const char *q = start + 5;

            w->command = parse_word(&q);
            while (*q == ' ')
                q++;
            if (!strncmp(q, "< /dev/null", 11))
                w->nullinput = 1;
        }
        else if (end - start > 10 && !strncmp(start, "pwd -P >| ", 10))
        {
            const char *q = start + 10;

            w->pwdfile = parse_word(&q);
        }
    }
}

/* Reads a small file written by the script, trimmed; NULL if absent. */
static char *read_small(const char *name)
{
    BPTR fh = Open((STRPTR) name, MODE_OLDFILE);
    char *buf;
    LONG len;

    if (!fh)
        return NULL;
    buf = malloc(1024);
    if (!buf)
    {
        Close(fh);
        return NULL;
    }
    len = Read(fh, buf, 1023);
    Close(fh);
    if (len < 0)
        len = 0;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
        buf[len - 1] == ' '))
        len--;
    buf[len] = '\0';
    return buf;
}

static int run(const char *command, const char *pwdfile, int nullinput)
{
    char script[64], rcfile[64], dirfile[64], line[160];
    struct Process *me = (struct Process *) FindTask(NULL);
    ULONG id = (ULONG) (IPTR) me ^ (ULONG) time(NULL);
    BPTR fh, input = BNULL, errout;
    LONG result;
    int rc = 0;
    char *text;

    snprintf(script, sizeof(script), "T:dosbash-%08lx.script", (unsigned long) id);
    snprintf(rcfile, sizeof(rcfile), "T:dosbash-%08lx.rc", (unsigned long) id);
    snprintf(dirfile, sizeof(dirfile), "T:dosbash-%08lx.dir", (unsigned long) id);

    fh = Open((STRPTR) script, MODE_NEWFILE);
    if (!fh)
    {
        fprintf(stderr, "dosbash: cannot write %s\n", script);
        return 126;
    }
    /* Run every line, as bash does, and report the last return code. */
    FPuts(fh, (STRPTR) "FailAt 2147483647\n");
    FPuts(fh, (STRPTR) command);
    FPuts(fh, (STRPTR) "\n");
    snprintf(line, sizeof(line), "Echo \"$RC\" >\"%s\"\nCD >\"%s\"\n",
        rcfile, dirfile);
    FPuts(fh, (STRPTR) line);
    Close(fh);

    if (nullinput)
        input = Open((STRPTR) "NIL:", MODE_OLDFILE);
    errout = me->pr_CES ? me->pr_CES : Output();

    snprintf(line, sizeof(line), "Execute \"%s\"", script);
    result = SystemTags((STRPTR) line,
        SYS_Input, input ? input : Input(),
        SYS_Output, Output(),
#ifdef SYS_Error
        SYS_Error, errout,
#endif
        SYS_UserShell, TRUE,
        NP_StackSize, 262144,
        TAG_DONE);
    if (input)
        Close(input);

    text = read_small(rcfile);
    if (text)
    {
        rc = atoi(text);
        free(text);
    }
    else if (result != 0)
        rc = (result < 0) ? 127 : (int) result;

    if (pwdfile)
    {
        text = read_small(dirfile);
        if (text)
        {
            int fd = open(pwdfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (fd >= 0)
            {
                write(fd, text, strlen(text));
                close(fd);
            }
            free(text);
        }
    }

    DeleteFile((STRPTR) script);
    DeleteFile((STRPTR) rcfile);
    DeleteFile((STRPTR) dirfile);
    return rc;
}

int main(int argc, char **argv)
{
    const char *script = NULL;
    struct wrapper w;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--version"))
        {
            printf("dosbash 1.0 (AmigaDOS shell)\n");
            return 0;
        }
        /* bash takes options in any order before the -c string
           (Claude Code passes `-c -l <script>`) */
        if (argv[i][0] == '-' && argv[i][1] && !script)
            continue;
        if (!script)
            script = argv[i];
    }
    if (!script)
    {
        fprintf(stderr, "usage: dosbash -c <command>\n");
        return 2;
    }

    parse_wrapper(script, &w);
    if (w.command)
        return run(w.command, w.pwdfile, w.nullinput);
    return run(script, NULL, 0);
}
