# node-v8.library + deps/v8 excision — validated recipe (2026-07-25)

The `node ◄─► node-v8.library ◄─► v8.library` seam. Node compiles against V8's
PUBLIC headers but does NOT compile the V8 engine; it links v8.library's thin
trampoline linklib and resolves at OpenLibrary time. Scoped + de-risked by recon.

## Why it's tractable (recon findings)
- `libv8.a` = **21 KB** (the trampoline/resolver linklib, NOT the 155 MB
  engine). Node links this; V8 symbols resolve to v8.library at load.
  Path: `aros-opal-pc-x86_64-smp/bin/pc-x86_64-smp/AROS/Developer/lib/libv8.a`
- Node's own `src/*.cc` reference internal/torque V8 headers **0 times** — only
  the public V8 API. So the header surface is stable + version-matched (12.4.254).
- v8.library header surface (point Node's V8 include dirs HERE, drop deps/v8/src):
  - public:   `chromium-aros/src/v8/include`
  - generated: `chromium-aros/src/out/aros-v8-library/gen/v8/include`
  - (torque-generated dir exists too if any transitive need surfaces)

## The cut — it's a SUPPORTED build mode, not hand-surgery (recon 2026-07-25)
Node's ENTIRE V8-engine dependency is gated behind ONE variable. `node.gypi:91-96`:
```
[ 'node_use_bundled_v8=="true"', {
  'dependencies': [
    'tools/v8_gypfiles/v8.gyp:v8_snapshot',      # <- pulls v8_base_without_compiler etc.
    'tools/v8_gypfiles/v8.gyp:v8_libplatform',
  ],
}],
```
Those TWO deps transitively pull the whole engine (v8_base_without_compiler @
v8.gyp:1069, v8_compiler, cppgc, torque_*, the NativeHandle wall). So:

1. **`./configure --without-bundled-v8`** — a SUPPORTED, official flag
   (`configure.py:1739`: `node_use_bundled_v8 = b(not options.without_bundled_v8)`;
   default true at `common.gypi:20`). Setting it false makes Node stop depending
   on v8_snapshot + v8_libplatform → the entire engine compile is skipped, AND
   the torque/mksnapshot HOST cross-cliff evaporates. This is the SAME path
   Chromium's & Electron's build use to embed Node on a shared V8 — a real,
   upstream mode, not a hack. (My earlier "no --shared-v8 flag" grepped the wrong
   string; the flag is `--without-bundled-v8`, and it drives the gypi var above.)
2. KEEP header-only targets: `v8_headers`, `v8_config_headers`, `v8_version` —
   Node's 155 src files still compile against the public V8 API.
3. REWIRE include dirs to v8.library's header surface (the two paths above).
4. LINK: add the 21 KB `libv8.a` trampoline to the `node` target's libraries so
   the public v8:: symbols resolve to v8.library at runtime.
5. Re-run configure to regenerate makefiles after setting the var.

## Expected signal
- Node's own objects COMPILE (header surface present) — proves the seam.
- LINK surfaces undefined `v8::` symbols the trampoline doesn't cover =
  **exactly the node-v8.library override-table entries** (the Node-floated V8
  patch delta). That link error list IS the shim's work-list.

## node-v8.library shim (this dir, future)
Two-tier per [[aros-node-library-electron]]: runtime override table (symbol
deltas) + shim headers (inline/API deltas). Built STATIC then genmodule-engulfed
into node-v8.library (AROS .library = LVO module, NOT a .so).

State: build via the `local/node` scaffold + `local/node/aros-compat` armory
(host/target hygiene fixes already captured). Recon reached this fork after 11
build iterations (host tools link, 149 steps, 111 target .o).
