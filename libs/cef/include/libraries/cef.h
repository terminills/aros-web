#ifndef LIBRARIES_CEF_H
#define LIBRARIES_CEF_H

/*
 * Public identity for cef.library.
 *
 * The ABI exposes CEF's identity queries plus its process and message-loop
 * lifecycle. Pointer arguments intentionally remain opaque here so the
 * resident library does not duplicate CEF's generated public structures.
 * Clients that use lifecycle or browser vectors compile against the matching
 * generated CEF C API headers.
 */

#include <exec/types.h>

#define CEFNAME "cef.library"
#define CEFVERSION 3

/* Shared by every client of this exact-build engine. */
#define CEF_AROS_API_VERSION 15600

/* CEFInitialize() result: the resident engine is already running. */
#define CEF_INITIALIZE_ENGINE_RUNNING 2

/* CEFCreateRequestContext takes cef_request_context_settings_t and an optional
 * cef_request_context_handler_t from the pinned CEF headers. Use a distinct
 * persistent cache_path per app/profile (beneath the engine root_cache_path),
 * or an empty cache_path for an in-memory context. Pass the resulting context
 * to CEFCreateBrowser and release its CEF reference when no longer needed.
 * Context creation is available only while the shared engine is running. */

/* CEFGetTaskRunner returns one owned cef_task_runner_t reference. Capture it
 * on an engine-owned task and release it before engine shutdown. Its post_task
 * method can dispatch from independent native application tasks. */

#endif /* LIBRARIES_CEF_H */
