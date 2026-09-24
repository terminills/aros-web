# Serving the VS Code web workbench to AROS

`serve-bundled-workbench.py <path-to-vscode-web>` serves the **bundled**
Code-OSS web build on port 8085 with `index.html` beside it.

Why not `@vscode/test-web`: its sources mode hands the browser roughly two
thousand separate AMD modules. On AROS that never finished inside a
fifteen-minute run - the document's `load end` never fired, and the window
stayed black with no error to explain it. The bundled build is one 31 MB
`workbench.web.main.js` and renders.

`index.html` is test-web's own generated page with three corrections, each of
which was a silent failure before it was made:

1. paths and port point at this server, not test-web's;
2. `out/nls.messages.js` is loaded - the bundled build keeps its strings in
   `globalThis._VSCODE_NLS_MESSAGES` and without it every lookup throws
   `!!! NLS MISSING !!!`;
3. `out/vs/workbench/workbench.web.main.js` is loaded BEFORE the entry module.
   A bundled build defines every module inside that file; without it the
   loader fetches `vs/base/...` one file at a time and 404s, reporting only
   "Loading vs/base/browser/browser failed".

Run it against the guest, from an AROS tree with cef/cef-blink/v8 built:

    AROS_HOSTED_ROOT=<tree>/bin/linux-x86_64/AROS AROS_NETWORK=tap \
    AROS_MEMORY_MB=12288 AROS_LAUNCH_VIA=shell \
    AROS_SHELL_CMD='SYS:workbench/libs/cef/test_cef_browser "SYS:Developer/CEF/vscode.ppm" "http://10.203.0.1:8085/" 4500' \
    AROS_RUN_SECONDS=700 tests/aros-hosted-run.sh <chrome> "http://10.203.0.1:8085/" out/<run>

The third argument to the probe is the settle window in pump ticks: it keeps
the browser alive that long after the first frame and snapshots the last one.
The workbench needs roughly 4500.

This is VS Code **web**. Desktop Electron loads the workbench from disk over
its own protocol handler and needs no server at all.
