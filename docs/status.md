# Source and validation status — October 21, 2026

## Published source snapshot

This refresh incorporates the committed AROS web stack through
`dd40e6dbac1c48c581ff27397ecbc80e3d861983`, plus the documentation and source-pin
updates in this publication. The package-layout script comes from build-tool
commit `aa9bddd` (full revision in `build/source-snapshot.json`). Local dirty
build ledgers and test harness edits were not imported.

The companion Chromium queue ends at
`c15a9e09b9` (documentation); executable capture code ends at
`ee28c359070cba8f04bca34cc0ae5836f2c9cf5b`. Chromium remains **124.0.6367.0**.
The patch export records original source commits and expected Git tree IDs.
Uncommitted development snapshots are identified separately in its `wip/`
directory and are not automatically applied by its queue script.

## Native Chromium camera/microphone milestone

The following results belong to a native x86-64 ADT AROS guest in QEMU with
physical USB passthrough of a UGREEN Camera 2K (`0c45:636f`). They are historical
validation of the exact artifact below, not a new test of the entire refreshed
web stack.

| Check | Result |
| --- | --- |
| Camera enumeration and real preview | Passed through VHI, 640×480 |
| USB microphone | Live 48 kHz stereo AHI capture with nonzero signal |
| Combined capture | Five sessions and four stop/reopen cycles passed |
| Camera absent | `NotFoundError` returned |
| Reattachment | Combined capture restored |
| Browser playback | Big Buck Bunny video and nonzero audio output passed on the same binary |
| Recorded serial traps | None observed in the validation run |
| AROS SourceForge page | Cloudflare verification prevented the page-render check; not a pass |
| Sustained preview | 175 displayed frames in 226 seconds, approximately 0.77 fps |
| Twitch, A/V synchronization, bare-metal capture | Not validated |

Initial short sessions often achieved 2–4 fps at a requested 5 fps; longer
preview degraded below 1 fps. Select the **UGREEN microphone** explicitly:
default AHI input selection may instead choose a virtual HDA device.

Validated Chromium binary SHA-256:

```text
1243020350c1e63bea0959e5809925682350e69cefad6e3e229fb812d4d02bfc
```

The tested OS camera changes were ADT `3653a3c8c6` together with `8d5c8b0449`,
on top of the earlier UVC/USB-microphone integration. The runtime requires
`uvc.class`, `uvc.library`, `usbaudio.class`, `jfif.library` and the matching
xHCI implementation. Those OS drivers are outside this repository. This
milestone does not establish that any later laptop release image contains the
same modules or behaves identically.

## Runtime architecture and remaining work

The sources implement shared V8 residency, per-task platform selection,
allocation-owner handling, Node integration and resident Electron engine
registration. They do not establish a fully unified Chromium/Node/Electron
engine with complete independent application isolation. The broader engine
consolidation follows AmiWest and the Chromium upgrade.

The tested native browser uses single-process mode, no sandbox and software
rendering. Those flags describe the current development configuration, not
production isolation. Upstream Chromium feature parity, newer-engine security
updates, GPU acceleration and arbitrary Electron application compatibility
must not be inferred from the camera test.

No full SDK/browser/CEF/Node/Electron rebuild was performed just to publish
these sources and READMEs. Patch replay, source provenance and documentation
checks are reported separately from the earlier native runtime evidence.
