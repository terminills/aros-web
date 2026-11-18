# Native resident CEF host

CEFHost owns initialization, the native AROS CEF message loop, and eventual
shutdown. Applications open sessions through cef.library version 3. Closing a
session leaves the engine running, including when there are no clients.

Build against the matching Chromium/CEF 156 headers and generated headers:
`workbench-tools-cefhost` (host) and `workbench-tools-cefhost-probe` (diagnostic).
The host requires the matching cef.library, cef-blink.library and v8.library,
plus resources under Developer:CEF and the normal writable PROFILE: assign.
The diagnostic boot fixture creates PROFILE: in RAM; production should retain the
user's configured profile location. The facade's version-2 vectors are unchanged.

Run with a 1 MiB stack **in the process executing the host**. For background
launch, use `Run Execute S:CEFHost-Run`, with this script:

```
Stack 1048576
SYS:System/CEFHost ENVARC:CEF/Profiles
```

The optional second argument `--allow-stop` enables diagnostic shutdown when no
sessions remain. Normal clients cannot stop a normally launched host. CEF cannot
be initialized again after shutdown in the current resident-engine implementation;
leave the production host running until OS shutdown.

## Application contract

Create a reply port and a zeroed `CEFHostRequest`, set its exact message length,
reply port, and protocol version, then call `CEFHostPostRequest`. A successful
post transfers temporary message ownership to the host: wait for ReplyMsg before
reading, reusing or freeing it. An unsuccessful post does not transfer ownership.
Never send directly to the discovery port. The resident transport serializes
posting and withdrawal with an Exec semaphore; dispatch uses CEF tasks and the
existing native AROS event loop, without a polling timer or POSIX transport.

OPEN requires an application name. Empty profile creates an independent
in-memory request context. An explicit profile uses root/application/profile;
the same explicit application/profile intentionally shares storage. Names allow
ASCII letters, digits, underscore and hyphen. The configured root currently
accepts ASCII native DOS paths.

The returned context and UI task runner are borrowed until CLOSE. Capture/use
the returned runner to post work from an independent application task: the
general CEFPostTask lookup follows the engine owner and its worker descendants,
so it cannot select an engine on behalf of an unrelated application. Follow CEF's reference-transfer
contract when passing it to another CEF API. CEF callbacks run on their specified
engine threads; application code must marshal UI work and honor CEF threading
rules. Before CLOSE, close all browsers, drain callbacks, and release all
application-held references. Only the task that opened a session can close it.
The host does not yet track or automatically close a client's browser objects.

This is shared-address-space application coordination, not a security boundary.
Protocol 1 does not recover from abrupt client or host task death. Clients must
keep code, callback objects, request messages and reply ports alive until all
outstanding work returns. The diagnostic probe checks ordinary session lifetime;
it does not qualify arbitrary application unloading or browser callback lifetime.
