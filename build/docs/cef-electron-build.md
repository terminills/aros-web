# Building CEF for the AROS Electron stack

`cef-blink.library` in the ADT tree links `obj/cef/libcef.a` out of a Chromium
output directory. That output directory's `args.gn` is not tracked anywhere, so
this file records the configuration and the traps, because getting any of them
wrong fails in ways that do not look like configuration problems.

The consumer is `workbench/libs/cef-blink/mmakefile.src` in `aros-adt-src`,
which reads `CEF_OUTPUT` (default `out/aros-cef-bootstrap-adt`) and links
`$(CEF_OUTPUT)/obj/cef/libcef.a` whole-archive.

## The output directories

```sh
# parent: everything except sysroot comes from here
cat > out/aros-blink-platform-adt/args.gn <<'EOF'
# identical to out/aros-blink-platform except aros_sysroot
target_os = "aros"
target_cpu = "x64"
aros_toolchain_root = "/path/to/aros-crosstools"
aros_sysroot = "/path/to/aros-adt-hosted-clean/bin/linux-x86_64/AROS/Developer"
# ... remaining args copied verbatim from out/aros-blink-platform/args.gn
EOF

cat > out/aros-cef-bootstrap-adt/args.gn <<'EOF'
import("//out/aros-blink-platform-adt/args.gn")

aros_blink_platform_library_only = false
aros_blink_core_library_only = false
aros_blink_platform_use_library = false
aros_blink_modules_library_only = false
aros_cef_library_only = true
clang_use_chrome_plugins = false
enable_basic_printing = true
enable_cef = true
enable_pdf = true
enable_printing = true
enable_print_preview = true
enable_widevine = true
optimize_webui = true
dcheck_always_on = false
EOF

./buildtools/linux64/gn gen out/aros-cef-bootstrap-adt
```

## Trap 1: the sysroot must be the ADT tree, the toolchain must not change

`out/aros-cef-bootstrap` targets an earlier, non-ADT
sysroot. Current Chromium source cannot build against it at
all: `base/native_library_aros.cc` includes `linuxso.h`, which exists only in
the ADT sysroot, so the build stops with a missing header.

So `aros_sysroot` must move. **`aros_toolchain_root` must not.** The trees do
not agree on toolchain:

| tree | crosstools |
| --- | --- |
| `aros-adt-hosted-clean` | `aros-crosstools` |
| `aros-adt-pc-x86_64-clean` | `aros-adt-crosstools` |

CEF must be built with the same toolchain as the tree that links it, or the
module links with undefined symbols spread across libstdc++ and libgcc. Read
the target tree's `config.status` rather than assuming the `adt` name implies
the `adt` toolchain.

## Trap 2: libcef's closure does not generate the printing mojom headers

A from-scratch build fails compiling
`chrome/browser/devtools/protocol/page_handler.cc`:

```
fatal error: chrome/services/printing/public/mojom/print_backend_service.mojom.h: No such file or directory
```

Generating that one header is not enough — it includes
`printing/backend/mojom/print_backend.mojom.h`, which is also missing. Build
the groups first, then the archive:

```sh
/usr/bin/ninja -C out/aros-cef-bootstrap-adt -j32 \
    obj/printing/backend/mojom/mojom.stamp \
    obj/printing/mojom/mojom.stamp \
    obj/chrome/services/printing/public/mojom/mojom.stamp
/usr/bin/ninja -C out/aros-cef-bootstrap-adt -j32 obj/cef/libcef.a
```

The older `out/aros-cef-bootstrap` never hit this because it had those headers
from accumulated build history, which masked the missing dependency edge. The
edge itself is the real defect and should be fixed in `BUILD.gn`, together
with wiring Chromium's print system to `printer.device`.

## Trap 3: dcheck_always_on

`dcheck_always_on = false` is **required for a runnable CEF**, not a
preference. With DCHECKs on, the browser aborts once the message loop runs:

```
FATAL:thread_restrictions.cc(58) Check failed: !tls_blocking_disallowed.
Function marked as blocking was called from a scope that disallows blocking!
```

`out/aros-chrome-adt-clean-nodcheck` does the same thing for the standalone
browser. **The annotation violation this hides is real and undiagnosed** — the
flag makes it non-fatal, it does not fix it. Anyone re-running `gn gen` without
this arg gets the abort back and it will not look like a configuration
problem.

## Trap 4: ninja version, and thin archives

Only `/usr/bin/ninja` (1.13.x) may touch a Chromium output directory. Another
version destroys `.ninja_log` and forces a full rebuild — **including with
`-n`**, so a dry run is not safe.

`libcef.a` is a **thin** archive: its members are the live `.o` files on disk,
not copies. A partial rebuild therefore invalidates the *existing* artifact,
because the archive index no longer agrees with its own members:

```
x86_64-aros-ld: obj/cef/libcef.a: member obj/base/base/dispatcher.o in archive is not an object
```

Do not run a build in an existing output directory before reading its
`args.gn`. If a rebuild there fails partway, that archive is no longer
relinkable and has to be rebuilt, not repaired.

## Trap 5: one process, one heap — `use_partition_alloc_as_malloc = false`

Also **required**, and the failure is remote from the cause.

The allocator shim, and so PartitionAlloc-Everywhere, is linked only into
whichever library pulls in `//base` — here `cef-blink.library` alone.
`v8.library` and `node.library` get libstdc++'s stock `operator new`/`delete`,
which call `stdc.library`'s `malloc()`/`free()`. Leave PA-E on and the process
has **two heaps that do not know about each other**, so an object allocated in
one library and deleted in another is freed to the wrong allocator:

```
Utf8ExternalStreamingStream::~Utf8ExternalStreamingStream   (v8.library)
  operator delete[] -> operator delete -> free              (v8.library)
    Exec_119_FreePooled -> tlsf_freemem -> tlsf_freevec
      [Kernel:TLSF] free-list corruption at FREE outside TLSF area
```

Blink's background script streamer allocates the chunk vector and V8 destroys
it, so this needs a page carrying a script big enough to stream — `example.com`
passes HTTP *and* HTTPS without ever touching it. Expect it to look like heap
corruption, or, without gdb, like a page fault reading decommitted memory.

The standalone browser never hits this: `out/aros-chrome-adt-clean` sets
`v8_use_aros_library = false`, i.e. one link unit and one allocator. The library
build cannot copy that, because CEF and node have to share a single V8.

So any output directory that builds AROS **shared libraries** needs
`use_partition_alloc_as_malloc = false`, which routes the shim to
`shim/allocator_shim_default_dispatch_to_aros_libc.cc` and hands out
`stdc.library`'s heap — the only one every library in the process can reach.
PartitionAlloc is still built and still backs WTF, Oilpan and ArrayBuffers;
those never cross a library boundary. A single-binary target should keep PA-E
on.

## Linking into the ADT tree

`cef-blink.library` needs `linuxso` in `CEF_BLINK_USELIBS`
(`linuxso_open`/`linuxso_sym`/`linuxso_close`, the native Widevine CDM path);
without it exactly those three symbols are undefined and nothing else.

```sh
make workbench-libs-cef-blink -j32
```

Expect roughly 820 MB with `dcheck_always_on=false`, or 1.1 GB with DCHECKs on.

## Verifying

`SYS:workbench/libs/cef/test_cef_browser <snapshot.ppm> [url]` renders
windowless and writes a PPM. With no URL it uses a built-in `data:` document,
so it needs no networking. For a real page, bring AROSTCP up first with the
sequence in `tests/aros-hosted-run.sh` (`MakeDir ENV:/ENVARC:AROSTCP`,
`SetEnv SAVE AROSTCP/Config`, run `AROSTCP`, `WaitForPort AROSTCP`).

Prefer taking the verdict from outside the guest where possible — a host
`http.server` logging the request from the guest's address proves the fetch
without depending on reading a screenshot correctly.
