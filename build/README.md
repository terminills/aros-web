# Building Chromium and the AROS web libraries

The build scripts support a Linux host cross-compiling for ADT AROS. They need
an already prepared Chromium source tree, an architecture-matched ADT SDK and
cross-toolchain, and Chromium's GN/Ninja dependencies. They do not by themselves
produce a complete SDK or validate the entire runtime stack.

## Inputs

- Prepare Chromium and its dependency repositories using
  [chromium-aros-patches](https://github.com/terminills/chromium-aros-patches).
  Read its `PINS`, `EXPORT.json` and development-snapshot notes.
- Use the exact ADT SDK/runtime and compiler chosen for your target. The native
  capture build used an x86-64 SMP SDK and GCC 15.2 cross-toolchain. Hosted AROS
  and native pc-x86_64 are different targets; do not substitute one sysroot for
  the other.
- Build/install the matching `v8.library` and the other AROS library dependencies
  required by the configuration. See the parent [README](../README.md) and the
  relevant `libs/*/mmakefile.src`.
- `chromium.pin` records original development commit IDs. A queue replay can have
  different commit IDs with identical trees; check the companion `EXPORT.json`.

## Native browser

Copy [config/native-browser.gn.example](config/native-browser.gn.example) into
your private output directory as `args.gn`, replacing the toolchain and sysroot
paths. This example is derived from the October 21 native capture configuration,
with proprietary CDM integration disabled. It is not a new certified build.

From this `build` directory:

```sh
export CHROMIUM_SRC=/absolute/path/to/chromium/src
export CHROMIUM_OUT=/absolute/path/to/private/chromium-output
mkdir -p "$CHROMIUM_OUT"
cp config/native-browser.gn.example "$CHROMIUM_OUT/args.gn"
# Edit the two absolute paths in args.gn before building.
export CHROMIUM_GN_ARGS="$(cat "$CHROMIUM_OUT/args.gn")"
JOBS=8 ./scripts/build-chromium.sh "$CHROMIUM_SRC" pc-x86_64 "$CHROMIUM_OUT"
```

`build-chromium.sh` runs GN and Ninja's `chrome` target. It preserves an existing
`args.gn`; changing `CHROMIUM_GN_ARGS` does not overwrite that file. Keep build
outputs separate from anyone else's active build.

## Package for Workbench

```sh
export AROS_HOSTED_ROOT=/absolute/path/to/adt-build/bin/pc-x86_64-smp/AROS
export PKG_ARCH=pc-x86_64
export CHROMIUM_SRC=/absolute/path/to/chromium/src
export STRIP=/absolute/path/to/x86_64-aros-strip
./scripts/install-layout.sh "$CHROMIUM_OUT" /absolute/path/to/new/package
```

Despite its historical name, `AROS_HOSTED_ROOT` points here to the **target**
AROS directory containing `Prefs/Presets`. The script also needs the built
`ilbmtoicon` host tool; set `ILBMTOICON` if its default location differs. The
package destination's `files/` directory is regenerated, so use a dedicated
output directory. `NO_STRIP=1` retains the original executable.

The result contains `PKGINFO` and a `files/` tree with the browser, resources,
Workbench icons and an All CPUs launcher. Match its architecture and library
set to the receiving system. The script does not distribute a proprietary CDM
by default; do not enable `WITH_WIDEVINE` for public redistribution.

## CEF, Electron and Node

The native browser example has `enable_cef = false`. CEF/Blink uses a different
GN closure and archive build; see [CEF/Electron build notes](docs/cef-electron-build.md)
and the consuming `libs/cef-blink/mmakefile.src`. Those notes include historical
hosted paths and configuration pitfalls, not a portable one-command build.
Node 22.23.1 uses its own fetch/configure/build targets and AROS ABI adapters.

## Validation

Library test targets are defined beside each module. Guest scripts live under
[tests/guest](tests/guest/README.md); application staging examples are under
`tests/electron`. Record source commits, SDK/compiler revisions, runtime hashes,
launch arguments and the actual environment alongside results.

The [local capture fixture](tests/capture/README.md) provides a manual camera
and microphone preview with frame counts and RMS readings.

The [dated status](../docs/status.md) records the camera/microphone result and
its limits. A completed source export, patch replay or cross-build does not
establish bare-metal behavior or full Electron compatibility.

## Nightly harness

`nightly-build.sh`, `config/nightly.env.example` and the server/contract notes
are an older configurable pipeline. The sample contains historical revisions
and site-specific artifact paths; it is **not ready to reproduce this snapshot**.
Its single `patches/series` input is not an automatic adapter for the companion
multi-repository queues. Prepare and validate current pins, dependency queues,
toolchain digests and the external hosted tester before operating it.

See [server notes](docs/build-server.md) and [artifact contract](docs/contract.md)
for that pipeline's intended layout. Use the explicit native-browser workflow
above for this source snapshot.
