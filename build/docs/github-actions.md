# GitHub Actions

GitHub Actions is the control plane, not the heavy build host.

`validate.yml` runs on GitHub-hosted infrastructure and checks the repository
scripts. `nightly.yml` runs on a private self-hosted runner labelled
`self-hosted`, `linux`, `chromium-aros`. That runner should be reachable only
through the existing VPN and should have `/srv/config/chromium-aros/nightly.env`
and `/srv/artifacts/chromium-aros` mounted locally.

The runner must have at least 32 logical CPUs and must enforce `JOBS=32`. It
must use disposable build directories and must not reuse a failed run.

Promotion is a separate manual workflow. It refuses any manifest unless launch,
YouTube, persistence, and clean exit are all explicitly recorded as `pass`.
