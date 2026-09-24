/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    electron.library public interface.
*/

#ifndef LIBRARIES_ELECTRON_H
#define LIBRARIES_ELECTRON_H

#include <exec/types.h>

#define ELECTRONNAME "electron.library"
#define ELECTRONVERSION 1

/*
 * Capabilities describe which external native runtimes this adapter resolved.
 * They are intentionally independent: later embedders must not assume that a
 * browser engine also implies an available Node environment, or vice versa.
 */
#define ELECTRON_CAP_CEF  (1UL << 0)
#define ELECTRON_CAP_NODE (1UL << 1)
#define ELECTRON_CAP_MODULE_SHIM (1UL << 2)
#define ELECTRON_CAP_BROWSER_WINDOW_REQUESTS (1UL << 3)

/*
 * The first lifecycle ABI deliberately preserves each engine's native opaque
 * structures.  electron.library sequences and adapts policy; it does not
 * clone CEF or Node data structures into a competing ABI.
 *
 * ElectronRunNode currently uses Node's command-line lifecycle and therefore
 * remains a one-shot call.  The later simultaneous embedder will be appended
 * without changing these proven vectors.
 */
LONG ElectronAttachNodeIsolatedV8Context(CONST_STRPTR script);
LONG ElectronAttachNodeIsolatedV8ContextWithModule(CONST_STRPTR script);
LONG ElectronPollNativeRequest(STRPTR method, ULONG method_size,
    STRPTR payload, ULONG payload_size);
LONG ElectronAttachNodeOwnedV8ContextWithModule(CONST_STRPTR script);

/*
 * Renderer/protocol seam.  The embedder declares the custom schemes it
 * registered with CEF (so the bootstrap can warn about privileged schemes CEF
 * never heard of), registers the renderer extension from
 * on_web_kit_initialized, tracks the live browser from on_after_created /
 * on_before_close, and injects the preload runtime from on_context_created.
 */
LONG ElectronDeclareCustomScheme(CONST_STRPTR name, ULONG options);
LONG ElectronRegisterRendererExtension(void);
void ElectronSetActiveBrowser(APTR browser);
LONG ElectronInjectRendererContext(APTR browser, APTR frame, APTR context);
LONG ElectronPostWindowEvent(CONST_STRPTR name, CONST_STRPTR payload);

/*
 * Several apps in one process, several windows per app.  Each app runs in
 * its own Node runtime slot (ElectronCreateAppRuntime / SelectAppRuntime /
 * ReleaseAppRuntime); a window is addressed by (slot, BrowserWindow id),
 * since every app numbers its windows from 1.  The host polls requests with
 * the slot that made them, binds each CEF browser it creates to the window
 * that asked for it, and reports window-system events to that window.
 */
LONG ElectronCreateAppRuntime(void);
LONG ElectronSelectAppRuntime(LONG slot);
LONG ElectronReleaseAppRuntime(LONG slot);
LONG ElectronPollAppRequest(STRPTR method, ULONG method_size,
    STRPTR payload, ULONG payload_size, LONG *slot);
LONG ElectronBindWindowBrowser(LONG slot, ULONG id, APTR browser);
LONG ElectronPostAppWindowEvent(LONG slot, ULONG id, CONST_STRPTR name,
    CONST_STRPTR payload);

#endif /* LIBRARIES_ELECTRON_H */
