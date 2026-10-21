# AROS web stack

Native ADT AROS integration for **V8, Node.js, libuv, CEF/Blink and Electron-style
applications**, plus the build tooling and launchers that connect them to the
AROS desktop.

This repository contains the AROS libraries and adapters. The Chromium engine
port lives in [chromium-aros-patches](https://github.com/terminills/chromium-aros-patches)
as patch queues for Chromium **124.0.6367.0** and its dependencies. Node is
**22.23.1**. These are the current source versions, not claims to track the
latest upstream releases.

## Components

| Directory | Role |
| --- | --- |
| `libs/v8` | Resident V8 library, engine glue, allocation ownership and task-local platform selection |
| `libs/node-v8` | ABI bridge between Node and the external V8 engine |
| `libs/uv` | `uv1.library`: native event loop, file I/O, networking, process and task adapters |
| `libs/node` | Native Node runtime, Node 22 patches and npm integration |
| `libs/cef-blink` | Resident CEF/Blink engine archive integration |
| `libs/cef` | Public AROS CEF interface and browser integration |
| `libs/electron` | Electron-compatible API bridge, window/context lifecycle and native desktop integration |
| `tools/ElectronShell` | Application launcher and resident-engine application registration |
| `devs/consoleng` | Console NG terminal device used by command-line applications |
| `tools/dosbash` | AmigaDOS shell adapter for callers expecting a bash-like entry point |
| `build` | Chromium scripts, source pins, package layout and test harnesses |

The Electron layer is an AROS implementation of supported APIs. It is not a
promise of complete upstream Electron compatibility; application behavior must
be checked against the implemented API surface.

## Current progress

The October 21 source refresh brings in V8 heap-ownership and platform fixes,
per-process libuv thread pools, Node path/file-URL and npm integration, file
notifications, native Electron dialogs and notifications, resident-engine
launch registration, and Console NG improvements. Original implementation
commits are preserved in the repository history.

The companion Chromium port has native VHI camera and AHI microphone capture.
A real UGREEN camera and microphone passed combined capture and stop/reopen
checks under QEMU USB passthrough. Preview performance is still limited; this
is not a claim of working Twitch broadcasting or bare-metal capture. See
[validation and limitations](docs/status.md).

Shared-runtime pieces already exist, including `v8.library` and resident
Electron engine registration. The planned consolidation of Chromium, Node and
Electron onto a unified engine with independent application isolation remains
future work, after AmiWest and the Chromium update. Shared library residency
is not equivalent to complete fault or security isolation.

## Building

This is an **ADT AROS source component**, not a standalone CMake or npm project.
Use an ADT tree configured to integrate this repository at `external/aros-web`,
a matching AROS SDK/cross-toolchain, and a patched Chromium checkout. The
ADT build supplies `config/aros.cfg`, the module generator, AROS headers and
libraries. Fetch the parent tree's pinned submodules before building.

Set `CHROMIUM_SRC` to the Chromium **src directory**, not its parent. Source
revisions are recorded in [build/chromium.pin](build/chromium.pin); the patch
repository's `EXPORT.json` additionally records tree IDs, because replaying
patches creates new commit IDs.

From an already configured ADT build directory, representative targets are:

```sh
make -j8 CHROMIUM_SRC=/absolute/path/to/chromium/src workbench-libs-v8
make -j8 CHROMIUM_SRC=/absolute/path/to/chromium/src workbench-libs-uv
make -j8 CHROMIUM_SRC=/absolute/path/to/chromium/src workbench-libs-node
```

V8 and CEF require their matching engine archives and configured output paths;
these commands are not a substitute for preparing those inputs. Node's source
archive is staged in the ADT `PORTSSOURCEDIR` and patched by its fetch target.
See the [build guide](build/README.md),
[CEF/Electron notes](build/docs/cef-electron-build.md), and the module's
`mmakefile.src` for archive and configuration requirements.

## Running and testing

Use packages built for your ADT architecture and matching library set. Do not
mix libraries from unrelated SDKs or builds. Chromium packaging is handled by
`build/scripts/install-layout.sh`; Electron application staging and launch
examples are under `build/tests/electron`.

Native test programs are defined beside the libraries, and guest scripts live
under [build/tests/guest](build/tests/guest/README.md). Compile success, hosted
execution, QEMU execution and physical-hardware testing are different results.
The dated [status document](docs/status.md) states which were actually observed
for the capture milestone; this source refresh does not certify every module
or application on every platform.

For a useful bug report, include the ADT and repository commits, architecture,
launch arguments, application/version, exact reproduction steps, and logs.
For capture failures, also include the camera USB ID and selected AHI input.

## Contributing and licensing

Keep changes within the relevant library or adapter, preserve upstream
provenance, and include a focused test or reproducible validation where useful.
Use the companion patch repository for changes to Chromium and its separate
upstream dependencies.

Licensing follows each source file and imported project's notices. This
repository does not apply one new license over V8, Node, libuv, Chromium or
CEF. No proprietary browser CDM is distributed here.
