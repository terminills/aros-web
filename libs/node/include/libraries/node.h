#ifndef LIBRARIES_NODE_H
#define LIBRARIES_NODE_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run the Node command-line entry point through node.library.
 *
 * Node owns process-global V8 and libuv state, so an embedding process should
 * call this at most once.  Electron will use the lower-level embedding seam
 * added after this packaging boundary is runtime-proven.
 */
LONG NodeStart(LONG argc, STRPTR *argv);

/* The exact Node release engulfed by this node.library build. */
CONST_STRPTR NodeVersion(void);

/*
 * Lower-level embedding seam used by electron.library.
 *
 * NodePrepareEmbedder() initializes Node process state without initializing or
 * owning V8.  NodeAttachCurrentV8Context() must be called while the embedder's
 * V8 isolate and context are current.  Passing NULL creates the Environment
 * without loading Node's execution bootstrap; this supports embedders that
 * need to finish context integration first.  Pump and detach stay explicit so
 * CEF can interleave its message loop and release Node before destroying the
 * renderer context.
 */
LONG NodePrepareEmbedder(void);
LONG NodeAttachCurrentV8Context(CONST_STRPTR script);
LONG NodePumpEmbedder(void);
void NodeDetachEmbedder(void);
LONG NodeAttachIsolatedV8Context(CONST_STRPTR script);

typedef CONST_STRPTR (*NodeEmbedderLinkedBindingInvoke)(
    CONST_STRPTR method, CONST_STRPTR payload, APTR private_data);

/*
 * Register a linked binding for the next embedded Environment.  JavaScript
 * receives an object with invoke(method, payload); Node performs all V8 ABI
 * work and calls the owner's stable string-message callback.
 */
LONG NodeRegisterEmbedderLinkedBinding(CONST_STRPTR name,
    APTR invoke, APTR private_data);
LONG NodeAttachOwnedV8Context(CONST_STRPTR script);

/*
 * Configure the worker count that Node's owned platform reports to the shared
 * V8 engine. This must be called before NodeAttachOwnedV8Context(). Embedders
 * sharing one resident engine must report the same count because V8 caches it
 * in process-global GC paths.
 */
LONG NodeConfigureOwnedV8WorkerThreads(LONG worker_threads);

#ifdef __cplusplus
}
#endif

#endif /* LIBRARIES_NODE_H */
