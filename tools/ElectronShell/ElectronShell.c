/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

/*
 * Visible AROS Electron bring-up shell.
 *
 * Keep this target on the same audited CEF/Node lifecycle implementation as
 * ElectronContextTest.  The compile-time mode changes only presentation and
 * leaves the default off-screen regression semantics intact.
 */

#define ELECTRON_SHELL_WINDOWED 1
#include "../../libs/electron/test_electron_context.c"
