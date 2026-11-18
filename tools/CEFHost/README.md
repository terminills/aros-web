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

## Experimental native browser clients

`workbench-tools-cefsubprocess` builds a small helper that opens the resident
libraries and calls CEFExecuteProcess with the real child arguments. It does not
initialize or shut down the host. Native child launch is an explicit diagnostic
opt-in, using the existing AROS CreateNewProc and MessagePort transport:

```
Stack 1048576
SYS:System/CEFHost T:CEFHostProfiles --allow-stop --native-children
```

Place CEFSubprocess beside CEFHost. The host resolves that directory to a native
DOS path and enables the port's `--aros-multi-process` mode. The default host
mode remains unchanged. Host/child diagnostics report allocator function addresses
and native AvailMem samples; these measure the whole guest, not exclusive engine
memory.

`workbench-libs-cef-test-host-browser` builds the independent application probe:
`test_cef_host_browser A http://server/test.html 1500`. The last argument is the
number of 20 ms ticks to hold after first paint. It posts presentation work to the
host UI runner, counts callback references, closes the browser, and uses a host
request as a final UI-queue barrier before unloading application code. A failed
callback barrier retains the task rather than unloading referenced code. An
early close request is preserved until asynchronous browser creation completes.

Both x86-64 and AArch64 clients/helpers cross-build. Native x86-64 run 90 launches
GPU, utility and renderer helpers, and host/child allocator addresses match.
Actual two-browser storage isolation and callback-drain acceptance have **not**
passed: run 90 stops at a shared startup-tracing singleton in the engine. The
Chromium development patch branch carries the ongoing process-state work.
Do not treat this diagnostic client as a qualified production embedding API.
