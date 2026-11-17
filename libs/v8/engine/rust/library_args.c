/* A shared engine has no process command line. Embedders pass their settings
 * through the library API; do not expose one application's argv globally. */
int aros_argc = 0;
char **aros_argv = 0;
