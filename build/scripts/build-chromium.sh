#!/usr/bin/env bash
set -euo pipefail

SRC=${1:?Chromium checkout required}
OUT=${3:?output directory required}
JOBS=${JOBS:-32}
GN_ARGS=${CHROMIUM_GN_ARGS:?CHROMIUM_GN_ARGS is required}
mkdir -p "$OUT"

if [[ ! -f "$OUT/args.gn" ]]; then
  printf '%s\n' "$GN_ARGS" > "$OUT/args.gn"
fi
cd "$SRC"
if [[ ! -x "$SRC/buildtools/linux64/gn" ]]; then
  echo "Chromium GN is missing; run gclient hooks before the build" >&2
  exit 3
fi
"$SRC/buildtools/linux64/gn" gen "$OUT"
ninja -C "$OUT" -j"$JOBS" chrome
test -x "$OUT/chrome"
