/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

/*
 * git-remote-https for AROS: git's HTTPS transport, on node's TLS.
 *
 * AROS has no TLS of its own - no AmiSSL, no OpenSSL, no curl - so git is built
 * without any HTTP(S) transport, while GitHub and every other forge serve only
 * https/ssh. node.library does have a working OpenSSL here, and git's http
 * support is simply an external helper it spawns, so the helper is a node
 * script and this is the launcher that hands git's argv to it.
 *
 * Same shape as npm_cli.c, which runs npm's bootstrap the same way.
 */

#include <proto/node.h>

#include <string.h>

#define GIT_REMOTE_HTTPS_SCRIPT "SYS:S/git_remote_https.js"
static char node_name[] = "C:Node";

int main(int argc, char **argv)
{
    STRPTR node_argv[argc + 2];
    int i;

    memset(node_argv, 0, sizeof(node_argv));

    node_argv[0] = node_name;
    node_argv[1] = GIT_REMOTE_HTTPS_SCRIPT;
    /* git calls us as `git-remote-https <remote> <url>`; both go through. */
    for (i = 1; i < argc; ++i)
        node_argv[i + 1] = argv[i];

    return (int)NodeStart((LONG)argc + 1, node_argv);
}
