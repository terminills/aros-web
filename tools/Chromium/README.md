# Native Chromium launcher

Build `workbench-tools-chromium-client` with the matching Chromium/CEF output.
The executable opens a session in the resident CEFHost and runs until all of
its Chrome tabs/windows close. It uses the persistent `Chromium/default`
application/profile identity unless a different profile is supplied:

```
SYS:System/Chromium https://example.com/ personal
```

The release layout installs the executable and CEFHost helpers in `SYS:System`,
`CEFHost-Run` in `S:`, and `StartChromium` as
`SYS:Applications/Internet/Chromium/Chromium`. That last file is a DOS script;
its native PROJECT icon uses `C:IconX`. The containing drawer also needs its
own DRAWER icon. The native executable has a TOOL icon when exposed directly.
Both the script and icon specify a 1 MiB stack.

Generate the three native icon files using the matching AROS build's host
converter:

```
python3 build-icons.py --ilbmtoicon /path/to/aros-build/bin/linux-x86_64/tools/ilbmtoicon --output /path/to/package-icons
```

Use `Start Chromium.info` beside the installed launcher script (renamed to
`Chromium.info`), `ChromiumDrawer.info` beside its containing drawer (also
renamed to `Chromium.info`), and the generated TOOL `Chromium.info` beside
the executable. The drawer opens tall enough to display the icon and label.

The script starts the shared host if its ready port is absent, waits for
readiness, then opens Chromium. The host keeps running when the last window
closes so another application can reuse the same engine. Host registration
arbitrates concurrent startup attempts; only the registered owner initializes
CEF. The regular startup script does not enable diagnostic host shutdown.

The host requires matching `cef.library`, `cef-blink.library`, `v8.library`,
CEF resources under `Developer:CEF`, and writable persistent profiles under
`ENVARC:CEF/Profiles`. Browser profiles provide storage separation in AROS's
shared address space; they are not an OS security sandbox.

The timed diagnostic clients still use the same callback/reference lifecycle
implementation. This launcher removes their automatic close timers; closing
a window before navigation finishes is an ordinary close, not a test failure.
Native icon launch and interactive browser qualification are required before
including a build in a release candidate.
