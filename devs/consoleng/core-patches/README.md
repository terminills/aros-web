# ConsoleNG core companion patches

This directory carries the independent-window fix in this personal repository.
It is needed alongside `consoleng.device`: separate `CONNG:` opens must create
separate handlers/windows instead of sharing the device node's handler port.
Inherited handles still address their original handler; serial devices retain
their shared-stream behavior.

## Apply to your AROS source

This patch changes `rom/filesys/console_handler/con_handler.c` in the parent
AROS source tree. Apply it there, not inside `aros-web`:

```sh
git -C /path/to/AROS apply --check /path/to/aros-web/devs/consoleng/core-patches/0001-console-independent-consoleng-windows.patch
git -C /path/to/AROS am /path/to/aros-web/devs/consoleng/core-patches/0001-console-independent-consoleng-windows.patch
```

Use a clean branch containing the earlier ConsoleNG core integration. The
verified public base is `a9b44025d59ae4bf7e658a9cc806bffb6e81c979` from
[AROS PR 1763](https://github.com/aros-development-team/AROS/pull/1763).
The fix is already present in public commit `4fd9618699`; do not apply it twice.
This single patch does not replace the preceding handler, ConClip and font
integration. If the check fails on another baseline, inspect the difference
instead of forcing the patch.

From the configured AROS build directory, rebuild `kernel-fs-con`, then follow
that build's normal installation and boot-package regeneration process. An
existing boot package may contain the older `con-handler` even after the module
is rebuilt. Merely updating `consoleng.device` does not replace that handler.

## Provenance and validation

Exported without code changes from `eb85204091cb816e36e0c71de48fbeb79ab26e34`.
The patch applies to the public base above, and the resulting handler is
byte-identical to the source-owner fix. `manifest.json` records hashes.
The source owner's native before/after qualification and core regression checks
are recorded with the upstream delivery; this publication adds no new hardware
or native runtime claim. Original source copyright and licensing are retained.
