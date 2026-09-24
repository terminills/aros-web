# Chromium for AROS Build

This repository owns the Chromium integration and nightly build pipeline. It
does not modify or vendor Chromium changes into the AROS/ADT source tree.

Each build consumes three pinned inputs:

- an ADT AROS commit and published SDK/runtime artifact;
- a Chromium source revision and patch queue revision;
- a toolchain artifact identified by digest.

The resulting package and manifest are publishable only when the hosted smoke
tests pass.

## Build

```sh
./scripts/nightly-build.sh --config config/nightly.env
```

The builder requires a clean ADT checkout, a Chromium depot checkout, the
matching AROS cross-toolchain, and `-j32` as the maximum parallelism. It writes
all outputs below `artifacts/<run-id>/` and never writes into either source
checkout.

## Stages

1. Validate pinned inputs and clean worktrees.
2. Build or restore the ADT SDK/runtime.
3. Apply the Chromium patch queue in an isolated worktree.
4. Build Chromium with `-j32`.
5. Run hosted launch, navigation, YouTube, persistence, and exit tests.
6. Emit a content-addressed artifact manifest.

See [docs/build-server.md](docs/build-server.md) and
[docs/contract.md](docs/contract.md) for the server and artifact contracts.
