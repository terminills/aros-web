#ifndef LIBRARIES_CEF_BLINK_H
#define LIBRARIES_CEF_BLINK_H

/*
 * Private exact-build adapter between cef.library and Chromium's Blink
 * implementation.  Application code should open cef.library, not this one.
 */

#include <exec/types.h>

#define CEFBLINKNAME "cef-blink.library"
#define CEFBLINKVERSION 1

/*
 * CEFBlinkInitialize() result when the engine is already running in this
 * boot: CEF was not initialised again and the caller is not its UI thread.
 */
#define CEFBLINK_ENGINE_RUNNING 2

#endif /* LIBRARIES_CEF_BLINK_H */
