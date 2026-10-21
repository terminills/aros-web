# V8 library for ADT AROS

`v8.library` integrates the real V8 engine from the patched Chromium checkout
as a resident AROS library. It is no longer the early arithmetic-evaluator
prototype described by older versions of this README.

The module links an AROS-target V8 monolith, supplies platform and ABI glue,
and exposes the AROS library interface in `v8.conf` and `include/`. Allocation
ownership is handled by `v8_allocator.cpp`; `engine/platform-context-aros.c`
selects platform state for the calling task. Node reaches the engine through
`node-v8.library`; Chromium can be configured with `v8_use_aros_library = true`.

## Build inputs

Build inside a configured ADT AROS build tree. `mmakefile.src` reads
`CHROMIUM_SRC` through `build/chromium.mk` and derives `V8_SOURCE` from it.
Prepare the patched source revisions in `build/chromium.pin`, a matching
cross-toolchain/SDK, and the engine build configuration before invoking:

```sh
make -j8 CHROMIUM_SRC=/absolute/path/to/chromium/src workbench-libs-v8
```

Consult `mmakefile.src` for `V8_*` archive/output overrides and engine targets.
A standalone checkout of this directory cannot supply the AROS module
generator, SDK or Chromium build tools. See the repository's
[build guide](../../build/README.md).

## Validation and boundaries

The module defines real-engine, library and symbol tests; `test/` also contains
an allocation-owner regression. Run the relevant native targets with the
same library/toolchain set used by the consumer. Shared residency does not
make arbitrary global engine state safe for independent applications, nor
does it provide process isolation. The broader unified-engine work remains
separate from the current implementation.

Source files and imported V8 code retain their respective license notices.
