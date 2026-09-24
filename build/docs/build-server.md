# Build Server

The server is private and reachable only through the existing VPN. Do not
expose the build service, artifact store, SSH, or AROS control endpoints to the
public network.

Required host layout:

```text
/srv/src/adt                 pinned ADT mirror
/srv/src/chromium             Chromium depot checkout
/srv/cache/toolchains         immutable toolchain archives
/srv/build/chromium-aros      disposable per-run worktrees
/srv/artifacts/chromium-aros  manifests and promoted packages
```

`ADT_TARGET` selects the AROS target passed to configure. Supported pipeline
values are `pc-x86_64`, `raspi-aarch64`, and `opensbi-riscv64`; each non-x86 job must
provide a matching cross-toolchain and Chromium GN target configuration. The
pipeline must fail clearly when that toolchain is absent.

Set `ADT_TOOLCHAIN_MODE=build` for a fresh target flavor. This creates the
target's toolchain in its own `ADT_TOOLCHAIN_DIR`, runs `make crosstools`, then
reconfigures the separate target build with `--with-aros-toolchain=yes` before
building the SDK. Nightly jobs must use distinct output and toolchain paths per
target.

The nightly scheduler invokes `scripts/nightly-build.sh` with a checked-in
environment file. A failed stage stops promotion, preserves its log, and
records the input pins. The scheduler must not reuse a failed build directory.

Chromium nightlies require a pinned `depot_tools` checkout. The builder uses
`fetch --nohooks`, `gclient sync --nohooks`, and `gclient runhooks` so DEPS
dependencies and generated build tools are present before applying the local
patch queue. A plain Chromium git clone is not a complete checkout.

The first server implementation can use systemd timers. CI hosting can be
added later without changing the repository contract.
