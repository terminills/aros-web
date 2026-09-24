#include <proto/node.h>

#include <string.h>

#define NPM_BOOTSTRAP "SYS:S/npm_bootstrap.js"
static char node_name[] = "C:Node";

int main(int argc, char **argv)
{
    STRPTR node_argv[argc + 2];
    int i;

    memset(node_argv, 0, sizeof(node_argv));

    node_argv[0] = node_name;
    node_argv[1] = NPM_BOOTSTRAP;
    for (i = 1; i < argc; ++i)
        node_argv[i + 1] = argv[i];

    return (int)NodeStart((LONG)argc + 1, node_argv);
}
