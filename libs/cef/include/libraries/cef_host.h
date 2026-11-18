#ifndef LIBRARIES_CEF_HOST_H
#define LIBRARIES_CEF_HOST_H
/* Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Native shared-address-space protocol. This is not a security boundary.
 * Send through CEFHostPostRequest, never FindPort/PutMsg: the resident facade
 * serializes posting against port withdrawal on all CPUs. Keep the message,
 * reply port and client code alive until ReplyMsg is received. No cancellation
 * or abrupt client-task death recovery is provided by protocol version 1.
 */
#include <exec/ports.h>
#include <exec/tasks.h>
#define CEF_HOST_PROTOCOL 1
#define CEF_HOST_PORT "AROS.CEF.Host.v1"
#define CEF_HOST_NAME_MAX 48
#define CEF_HOST_OPEN 1
#define CEF_HOST_CLOSE 2
#define CEF_HOST_STATS 3
#define CEF_HOST_STOP 4 /* Accepted only by a host launched with --allow-stop. */
#define CEF_HOST_OK 0
#define CEF_HOST_UNAVAILABLE -1
#define CEF_HOST_INVALID -2
#define CEF_HOST_BUSY -3
#define CEF_HOST_DENIED -4
#define CEF_HOST_NOMEM -5
struct CEFHostRequest {
    struct Message message;
    ULONG protocol;
    ULONG command;
    LONG result;
    struct Task *sender; /* Filled by resident CEFHostPostRequest. */
    IPTR session;
    char application[CEF_HOST_NAME_MAX];
    char profile[CEF_HOST_NAME_MAX]; /* Empty means independent in-memory. */
    APTR context; /* Borrowed, valid until CLOSE. AddRef for CEF refptr arguments. */
    APTR ui_runner; /* Borrowed CEF runner; marshal engine UI work with post_task. */
    struct Task *engine_owner;
    ULONG active_sessions;
};
/* Close browsers, drain callbacks, and release client-held CEF references before
 * CLOSE. Ordinary disconnect never shuts down the host or resident engine.
 * Profile directories are namespaced by application/profile under the host's
 * configured root. Same explicit app/profile intentionally shares storage.
 */
#endif
