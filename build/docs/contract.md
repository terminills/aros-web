# Build Contract

ADT target builds use the canonical target names `pc-x86_64`, `raspi-aarch64`,
and `opensbi-riscv64`. `ADT_VARIANT` is optional; set it to `smp` only for a
target that provides that variant. Multi-target builds use isolated target
directories and run sequentially, with `JOBS <= 32` enforced.

Every published run has a manifest containing:

```json
{
  "adt_commit": "...",
  "chromium_commit": "...",
  "patch_queue_sha256": "...",
  "toolchain_sha256": "...",
  "build_flags": "...",
  "jobs": 32,
  "artifact_sha256": "...",
  "tests": {"launch": "pass", "youtube": "pass", "persistence": "pass", "exit": "pass"},
  "site_tests": [{"name": "youtube", "status": "pass", "url": "https://www.youtube.com"}]
}
```

The manifest is written only after the artifact is closed and hashed. A failed
or incomplete build remains in the run directory as evidence and is never
promoted to `nightly/current`.

Hosted testing is a required nightly stage. `HOSTED_TEST_COMMAND` is an
external AROS hosted-runner contract: it receives `chrome`, a URL, and a site
evidence directory, and must return zero only after navigation and the site's
configured checks complete. Each site gets its own log and evidence directory;
failures remain downloadable and are rendered in the GitHub Actions summary.

The ADT artifact is an input, not a mutable checkout. Chromium patches are
applied in a disposable worktree. Build directories are separate from source
directories and are deleted only by the retention job after the manifest is
published.
