# Known site issues — Chromium on AROS

Sites that crash or misrender, kept as a list rather than chased one by one:
the AmiWest test ISO ships first, then these get worked through. Add a row the
moment a site misbehaves, with the crash signature if there is one — a
signature is what makes a report actionable months later.

The crash reporter writes to `ENVARC:CrashReports`, falling back to
`RAM:CrashReports` on read-only media (a Live CD). RAM: is volatile, so send
or copy a report before rebooting.

## Crashes

| Site | Task | Alert | Signature | Notes |
|---|---|---|---|---|
| `web.whatsapp.com` | NetworkService | `0x80000003` illegal address access | `net::WebSocketClientSocketHandleAdapter::~D0Ev` → `net::WebSocketBasicStream::~D1Ev` | Crash is in WebSocket **teardown**, not connect: the page loads and renders (QR "Scan to log in" screen is drawn) before the network service dies. WhatsApp Web keeps a long-lived WebSocket, so it is a good reproducer for the socket close path generally. Found 2026-09-09 on the Live CD build. |

## Memory / process model

| Where | Task | Alert | Signature | Notes |
|---|---|---|---|---|
| Any site, after the browser is already running | Chromium | `0x80000004` illegal instruction | `[FATAL:partition_address_space.cc(77)] Check failed: false.` → `partition_alloc::internal::logging::LogMessage::~LogMessage` | **PartitionAlloc could not reserve its pool.** Line 77 is the `PA_CHECK(false)` inside `HandlePoolAllocFailure()`; the `LogMessage` destructor in the backtrace is just how a PA FATAL exits (`IMMEDIATE_CRASH()`), *not* a logging or disk-write failure. Distinct from the known 8 GB startup case - here the browser was up and rendering before it died, so it is a **later** reservation, almost certainly a new renderer process. PA pools on AROS are physically backed because `KrnAllocPages` cannot do anonymous mappings, so every child process needs its own real contiguous block; enough tabs and one cannot be satisfied. Reported 2026-09-10 on aros.games / arosworld.org. |

## Network buffer exhaustion — Chromium starving the shared mbuf pool

| Where | Symptom | Notes |
|---|---|---|
| `fast.com`, heavy pages | Networking dies **system-wide**; `ping` in an unrelated shell reports `sendto: No buffer space available` (ENOBUFS) and `ret=-1` | Reproduced 2026-09-10. |

**The driver is exonerated.** Reproduced identically on **e1000** and on
**rtl8139** with everything else held constant - same ISO, same AROSTCP, same
QEMU, only the NIC model swapped. A bug present on two unrelated drivers is
not in either of them.

**The symptom is system-wide, but the cause is Chromium.** The process that
fails is `ping` - 64 bytes of ICMP, no relation to the browser - so the shared
mbuf pool is empty and *every* sender on the machine is refused. That locates
the *resource*, not the culprit: **Odyssey runs a speed test on the same stack
without killing networking**, so AROSTCP's pool is adequate for ordinary load
and what differs is how much of it Chromium takes.

ENOBUFS is specific - it is mbuf/cluster exhaustion, not descriptor exhaustion,
which would be EMFILE. The likely mechanism is concurrency rather than
throughput: Chromium opens many more simultaneous sockets than Odyssey (six or
more per host, plus prefetch and speculative connections), and every socket
reserves send and receive buffer space whether or not it carries data. A
hundred sockets at 64 KB each is megabytes of clusters gone before any payload
moves; a browser using a handful of connections never approaches it.

Measurable directly: compare `netstat` socket counts during fast.com under
Chromium and under Odyssey. If the difference is an order of magnitude, the
fix is capping Chromium's connection pool, not enlarging AROS's.

The pool did not recover on its own while Chromium sat idle on a failed page,
which points at buffers not being returned rather than the pool merely being
too small. The discriminator worth running next: close Chromium entirely and
see whether `ping` starts working again. If it does not, it is a leak; if it
does, it is a sizing/backpressure problem.

Instrument: `netstat -m` reports the mbuf statistics and denied requests
(fixed in `a54cf5d1` - before that it read a hardcoded 256 where the array is
`MTCOUNT`-sized). Pool sizing is `MB_RAM_DIVISOR` / `MB_MAXMEM_CEIL` in
`uipc_mbuf.c`, last touched in `0003d4b5`.

## Responsiveness

| Where | Symptom | Notes |
|---|---|---|
| `youtube.com` | **The entire AROS GUI freezes until the page finishes loading** - not just Chromium's window | Whole-desktop stall, so something is holding the thread that pumps Intuition rather than merely being slow. Suspects: `message_pump_aros` starving the GUI while the load runs, or synchronous work on the browser UI thread. Worth checking whether other heavy pages do the same or whether it is specific to YouTube's media path. Reported 2026-09-10. |

## Browser features

| Where | Task | Alert | Signature | Notes |
|---|---|---|---|---|
| `chrome://settings/downloads` (file picker) | CrBrowserMain | `0x80000003` illegal address access | `ui::SelectFileDialog::Sele...` | Anything that opens a file chooser - Downloads "Change", Save As, an `<input type=file>` on a page - takes the browser process down. AROS has no `SelectFileDialog` implementation, so this is a missing platform piece rather than a bug in a site. Found 2026-09-09. **Fixed 2026-09-10** in chromium-aros `b1d0a9ed0f`: `SelectFileDialogAros` backed by asl.library. Root cause was `shell_dialog_stub.cc` returning `nullptr` from `CreateSelectFileDialog()`, which callers then dereferenced. Compile-verified against the SDK; **not yet exercised on hardware**. |

## Platform gaps

| Where | Symptom | Notes |
|---|---|---|
| Live CD, any cache write | `mkdir failed ... errno=1214` repeatedly in the log | `DIR_CACHE` resolved to `<executable dir>/Cache`, i.e. the read-only boot medium, so the browser ran with no cache from CD. **Fixed 2026-09-10** in chromium-aros `2e370e1584`: probe by creating the directory, fall back to `RAM:Chromium/Cache`. Installed systems keep a persistent cache beside the executable. |
| Boot, roughly 1 in 3 | Black screen, hang during startup | Stalls in `KrnSpinLock`. Kernel/SMP lane, **not** a Chromium defect — needs an owner outside the browser work. Still open; restarting clears it. |

## Rendering

| Site | Symptom | Notes |
|---|---|---|
| `aros.sourceforge.io` | Text invisible, images and link underlines drawn | Fixed 2026-09-09. Was not site-specific: Skia had no usable fonts and drew everything with `SkTypeface_Empty`. Any page relying on *local* fonts was blank, while Google/YouTube/Netflix looked fine because they fetch their own webfonts. |

## How to capture a crash for this list

1. Note the URL that triggered it.
2. On the crash card choose **View** — the report opens with `Task`, `Alert`,
   registers, backtrace and the loaded origins (`loaded-origin-N` lines name
   the sites that were live in that process, which is often the real clue).
3. Copy the top two or three symbolized frames into the table above. Those,
   plus the alert code, are enough to find the defect later without the VM.
