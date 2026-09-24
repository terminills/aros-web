# Guest-side scripts for the hosted runner

Copies of the S: scripts and node programs the `AROS_LAUNCH_VIA=shell` runs
execute inside the hosted clean-ADT guest (`AROS_SHELL_CMD='NewShell CONNG:
FROM S:<script>'`).  Install into the guest tree with

    R=.../aros-adt-hosted-clean/bin/linux-x86_64/AROS
    cp tests/guest/S/* $R/S/ ; cp tests/guest/ClaudeCode/* $R/Developer/ClaudeCode/

| script | pairs with | what it proves |
| --- | --- | --- |
| S/UV-Process-Test | actions/uv-process-test.txt | uv_spawn lifecycle (C:UVSpawnDiagTest, every return value printed) |
| S/Node-Spawn-Smoke + ClaudeCode/spawn-smoke.mjs | actions/node-spawn-smoke.txt | node child_process: execFile pipes/exit code, PATH miss, detached spawn, cwd, and the `C:OpenURL <OAuth URL>` hand-off Claude Code's login makes |
| S/OpenURL-Long | actions/openurl-long.txt | `C:OpenURL "<430-char URL>"` straight from the Shell (dos ReadArgs + Shell line-length fixes) |
| S/Claude-Code-Interactive | actions/claude-interactive-login.txt | the real Claude Code TUI: theme prompt, login method, browser hand-off |
| S/OpenURL-Reuse | actions/openurl-reuse.txt | Chromium's CHROMIUM command port: registers the browser with `PORT CHROMIUM`, starts it once via `C:OpenURL`, then a second `C:OpenURL` and the node smoke's OAuth hand-off must land as tabs in the running browser (a second chrome process CHECK-fails on 8 GB) |
| S/Node-Resident-Regress | actions/node-resident-regress.txt | node.library/uv1.library expunge paths (aros-adt-src d3411393): library flush from the closer task (expunge allowed) and from a foreign task (`Run Avail FLUSH`, vetoed, stays resident) with `C:Node` reopening the library after each; run with `AROS_SHELL_CMD='Execute S:Node-Resident-Regress'`; Execute feeds the script into the calling shell so a redirect on it captures nothing - the resident-70 console snapshot carries the NODE1..4_RC=0 / NODE4_EVAL_OK lines |

Results land in `out/<run>/` (aros-debug.log, timed snapshots) and, for the
node smoke, `SYS:Developer/ClaudeCode/spawn-state.json` in the guest.
