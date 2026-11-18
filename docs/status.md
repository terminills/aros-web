# Source and validation status — November 18, 2026

## Chromium 156 branch

`chromium-156-aros-cef` targets Chromium **156.0.8078.96 / CEF 8078**.
Use `build/chromium.pin` and the companion repository's `upgrade/156/snapshot`
queue. The October 21 material below records the older 124 baseline.

Native x86-64 and AArch64 builds of `cef-blink.library`, the public CEF facade,
and shared `v8.library` have run under QEMU. Both guests render local HTML and
pass the retained JavaScript/WebAssembly worker fixture: worker A is asked to
terminate, then worker B executes its own JavaScript function and Wasm export.
Both recorded runs return from browser close and full CEF shutdown. This is a
bounded worker-lifetime test, not complete independent-application or profile
isolation qualification. ARM also has intermittent teardown stalls; their logs
are retained and investigation is deferred, not declared fixed.

The shared V8 code arena now honors its requested size. Chromium's native
2 MiB superpages remain enabled. The x86 CEF packager deduplicates section
names after normal SDK stripping, preserving function symbols, relocation
records and executable bytes. The ARM packager uses PIC branch veneers and a
compact native image to handle branches beyond AArch64's direct-call range.

Native Exec `AvailMem` measurements for a local first-paint fixture:

| Guest | Before these memory changes | Latest paired result |
| --- | ---: | ---: |
| x86-64 | 2053.69 MiB | 1538.59 MiB |
| AArch64 | 1397.98 MiB | 1188.91 MiB |

These are whole-guest used-memory readings, including desktop and other tasks,
not exclusive engine residency or host QEMU RSS. The runs use one active x86
CPU and two active ARM CPUs. Timing and allocation layout contribute to the
measured differences; no bare-metal performance claim follows from them.

Evidence and exact inputs are published in the companion repository:
[section-name packaging and memory](https://github.com/terminills/chromium-aros-patches/blob/chromium-156-aros-cef/upgrade/156/evidence/cef-section-name-compaction.md)
and [runtime evidence directory](https://github.com/terminills/chromium-aros-patches/tree/chromium-156-aros-cef/upgrade/156/evidence).
The relevant implementation is `aros-web` 97ecd87; companion evidence through
66e12c8 includes the ARM18 and x86-74 JS/Wasm runs.

A native x86-64 HTTP fixture also loads HTML, JSON with `fetch`, and a worker
script through AROSTCP/PCnet and QEMU user networking. The worker executes a
Wasm export and the page displays `AROS_HTTP_ENGINE_PASS`. The server records
HTTP 200 for all three resources. This is local HTTP transport validation;
a separate run77 also resolves and loads `https://example.com/` with HTTP 200,
without a certificate-bypass flag, then returns from full CEF shutdown. Its
screenshot shows missing non-Latin font glyphs. Arbitrary-site compatibility
and ARM networking remain separate checks. The retained network evidence is
[documented here](https://github.com/terminills/chromium-aros-patches/blob/chromium-156-aros-cef/upgrade/156/evidence/cef-network-validation.md).

The native x86 request-context probe also passes same-origin cookie separation
between two in-memory contexts and continued access to B after releasing A
references, followed by full CEF shutdown. It initializes the shared engine
once. This is not persistent-profile or independent-application qualification.

Full Chrome UI, general network browsing, hardware acceleration, Web Notification
API delivery, Widevine playback, CEF.class and Node/Electron integration with this
156 engine remain unqualified. Native notification-library tests and historical
124 media tests do not establish those 156 browser features. The current browser
fixture uses single-process software rendering; it does not prove production
sandboxing or independent application isolation.

## Historical October 21 baseline

### Published source snapshot

This refresh incorporates the committed AROS web stack through
`dd40e6dbac1c48c581ff27397ecbc80e3d861983`, plus the documentation and source-pin
updates in this publication. The package-layout script comes from build-tool
commit `aa9bddd` (full revision in `build/source-snapshot.json`). Local dirty
build ledgers and test harness edits were not imported.

The companion Chromium queue ends at
`c15a9e09b9` (documentation); executable capture code ends at
`ee28c359070cba8f04bca34cc0ae5836f2c9cf5b`. That baseline used Chromium **124.0.6367.0**.
The patch export records original source commits and expected Git tree IDs.
Uncommitted development snapshots are identified separately in its `wip/`
directory and are not automatically applied by its queue script.

### Native Chromium camera/microphone milestone

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

### Historical runtime architecture and remaining work

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

### Console NG publication correction

The initial October 21 web-stack refresh stopped at Console NG's October 16
cursor-save changes. The follow-up exports its committed development subtree
from `7df76f55d62bfa88ac3ae20638d07850d8735748`, incorporating the work of
330 commits beyond the shared `eb9aa26` baseline. Source-owner guidance calls
for a subtree snapshot and publication-clean documentation rather than merging
private development history. This includes terminal parsing/rendering,
Unicode tables, search/navigation, clipboard, input protocols and preferences.

The imported implementation and tests were compared exactly with that committed
source; public-facing Markdown comes from the matching Contrib delivery, with
build-path adjustments for this repository.
The development delivery separately recorded 177 passing host suites and a
26-stage native integration run; the publication clone also passed all 177 host regression scripts. See [Console NG](../devs/consoleng/README.md) for build targets,
companion core changes and feature limits. No demo binary or hardware was
changed by this source publication.
