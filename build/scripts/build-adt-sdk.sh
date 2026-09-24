#!/usr/bin/env bash
set -euo pipefail

ADT=${1:?ADT checkout required}
OUT=${3:?output directory required}
JOBS=${JOBS:-32}
ADT_TARGET=${ADT_TARGET:-pc-x86_64}
ADT_VARIANT=${ADT_VARIANT:-}
TOOLCHAIN=${ADT_TOOLCHAIN_DIR:?ADT_TOOLCHAIN_DIR is required}
PORTS=${ADT_PORTSOURCES_DIR:-$OUT/portssources}
TOOLCHAIN_MODE=${ADT_TOOLCHAIN_MODE:-prebuilt}
mkdir -p "$OUT" "$PORTS"
OUT=$(cd "$OUT" && pwd)
cp "$ADT/configure" "$OUT/configure"
cp "$ADT/mmakefile" "$OUT/mmakefile"
cp "$ADT/scripts/autoconf/config.guess" "$OUT/config.guess"
cp "$ADT/scripts/autoconf/config.sub" "$OUT/config.sub"
mkdir -p "$OUT/scripts/autoconf"
cp "$ADT/scripts/autoconf/config.guess" "$OUT/scripts/autoconf/config.guess"
cp "$ADT/scripts/autoconf/config.sub" "$OUT/scripts/autoconf/config.sub"

cd "$OUT"
configure_args=(
  --srcdir="$ADT"
  --target="$ADT_TARGET"
  --enable-target-variant=smp
  --with-portssources="$PORTS"
  --with-aros-toolchain-install="$TOOLCHAIN"
)
if [[ -n "$ADT_VARIANT" ]]; then
  configure_args+=(--enable-target-variant="$ADT_VARIANT")
fi
if [[ "$TOOLCHAIN_MODE" == prebuilt ]]; then
  configure_args+=(--with-aros-toolchain=yes)
elif [[ "$TOOLCHAIN_MODE" != build ]]; then
  echo "ADT_TOOLCHAIN_MODE must be prebuilt or build" >&2
  exit 2
fi

if [[ ! -f Makefile || "$TOOLCHAIN_MODE" == build && ! -f "$TOOLCHAIN/.installflag-crosstools" ]]; then
  "$ADT/configure" "${configure_args[@]}"
fi
if [[ "$TOOLCHAIN_MODE" == build && ! -f "$TOOLCHAIN/.installflag-crosstools" ]]; then
  make -j"$JOBS" crosstools
  "$ADT/configure" "${configure_args[@]}" --with-aros-toolchain=yes
fi
mkdir -p "$OUT/tools/collect-aros"
cp -a "$ADT/tools/collect-aros/." "$OUT/tools/collect-aros/"
make -j"$JOBS" tools
mkdir -p "$OUT/tools/collect-aros"
cp -a "$ADT/tools/collect-aros/." "$OUT/tools/collect-aros/"
make -j"$JOBS" sdk
printf '%s\n' "$OUT" > "$OUT/sdk-root.txt"
