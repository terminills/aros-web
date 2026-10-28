# ConsoleNG

ConsoleNG provides an AROS console device, a native Zune preferences editor and
regression probes. It integrates with the native console handler rather than
providing a separate terminal emulator.

See [Preferences](prefs/FEATURES.md), [retained-text navigation](NAVIGATION.md)
and [search](SEARCH.md) for configuration and shortcuts. The test directory
contains host checks and native AROS probes; host results do not establish native
rendering or hardware compatibility.

## Integration status

This aros-web export uses the workbench-devs-consoleng build target and
its preferences subtarget. The device and Preferences build from this layout against an existing x86_64
SMP ADT SDK. Preferences matches the accepted development binary exactly; the
device matches after stripping debug information and normalizing its generated
version date. This is not a fresh full-system build or physical-hardware test.
The development build uses companion core console-handler changes for borrowed
window integration, completion requesters and icon drops. Clipboard read replies
also require the matching ConClip companion. These core changes must be delivered
and validated separately; this module alone is not a complete system update.

The implementation supports a substantial terminal protocol subset, but does not
claim full Alacritty compatibility. Semantic delimiters have a Preferences editor and persisted profiles. Configurable
hints, integration with the OS multi-user defaults, and tab management remain
gaps. See the Preferences feature inventory for qualified behavior and open
acceptance boundaries.

## Building against an existing SDK

When bootstrapping only this module in a configured AROS build, run the setup
and quick targets sequentially. Quick targets assume their prerequisite SDK
and directory setup already exists.

```
make workbench-devs-consoleng-includes-dirs
make -j8 workbench-devs-consoleng-quick
make -j8 workbench-devs-consoleng-prefs-quick
make workbench-devs-consoleng-prefs-startup-script
make workbench-devs-consoleng-prefs-startup-package
```

Do not launch multiple top-level targets concurrently while mmake is generating
new makefiles. Parallel compilation within an initialized target is supported.

## Opening a console

Install the matching core handler and clipboard companions, then run:

```
Execute S:ConsoleNG-Shell
```

The launcher mounts `CONNG:` on demand using `S:ConsoleNG-Mountlist`, with a
64 KiB handler stack, then opens an ordinary native Shell. It does not mount a
console automatically during boot or replace the classic `CON:` device. An
already mounted `CONNG:` is reused; update any older mount definition to use the
same stack size before booting it.

Run `SYS:Prefs/ConsoleNG` for the editor. The package startup restores saved
future-console defaults through the OS's `ENV:`/`ENVARC:` assignments. A read-only
Live CD cannot save to a read-only `ENVARC:`; Use still applies session defaults.

## Core handler

Independent `CONNG:` opens, ConsoleNG paste and line handling need a
con-handler that asks the device for its capabilities
([aros-development-team/AROS#1923](https://github.com/aros-development-team/AROS/pull/1923)),
an aux-handler that passes the device startup unmasked
([#1912](https://github.com/aros-development-team/AROS/pull/1912), merged), and
ConClip 42.4 for whole-clip paste
([#1909](https://github.com/aros-development-team/AROS/pull/1909)).
The device names no core files; it answers `CD_GETCAPS`.

Mixing versions is safe. The capability commands sit above every command this
device has, so its command numbers and the Prefs editor are unchanged and need
no rebuild. An older con-handler does not ask, and treats `CONNG:` as it did
before. An older consoleng.device answers the new commands with `IOERR_NOCMD`,
which a newer con-handler takes as no capabilities: the console works, but
independent windows, raw paste and bracketed paste need this version.

## Source provenance and dependencies

Published from the committed aros-web-layout development subtree at
`7df76f55d62bfa88ac3ae20638d07850d8735748` (October 21, 2026), incorporating
the October 17–21 development work. Public-facing documentation is adapted
from Contrib delivery `29d764d71a8cc002cd8320d0977ca90675e7c3ae`.
Private development evidence notes are not part of this publication.
Original copyright and imported Unicode license/provenance files are retained.

The corresponding Contrib-layout export is proposed in
[Contrib PR 83](https://github.com/aros-development-team/contrib/pull/83).
Required core handler, ConClip and font integration is proposed in
[AROS PR 1763](https://github.com/aros-development-team/AROS/pull/1763).
Do not install both module layouts into one build, and do not assume an older
core handler supplies the new completion/drop/clipboard behavior. Those PRs
remain separate; this update does not change their destination or merge state.

The delivery's native 26-stage integration check passed on the alpha18
candidate. That does not establish full Alacritty parity or physical-laptop
stability. This source publication does not change the frozen demo image.

Publication check: all 177 host regression scripts passed on the imported
implementation. No additional native or physical-hardware run was performed
for this repository update.
