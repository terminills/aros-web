#!/usr/bin/env bash
set -euo pipefail

ADT=${1:?ADRS checkout required}
OUT=${2:?output root required}
JOBS=${JOBS:-32}
TOOLCHAIN_ROOT=${ADT_TOOLCHAIN_ROOT:?ADT_TOOLCHAIN_ROOT is required}
PORTS=${ADT_PORTSOURCES_DIR:?ADT_PORTSOURCES_DIR is required}

if (( JOBS > 32 )); then
  echo "JOBS must be <= 32" >&2
  exit 2
fi

# Targets are deliberately sequential: each target may use 32 compiler jobs,
# but the build server must never multiply that limit across architectures.
targets=(pc-x86_64 raspi-aarch64 opensbi-riscv64)
for target in "${targets[@]}"; do
  target_out="$OUT/$target"
  target_toolchain="$TOOLCHAIN_ROOT/$target"
  mkdir -p "$target_out" "$target_toolchain"
  ADT_TARGET="$target" \
  ADT_TOOLCHAIN_DIR="$target_toolchain" \
  ADT_TOOLCHAIN_MODE="${ADT_TOOLCHAIN_MODE:-build}" \
  ADT_PORTSOURCES_DIR="$PORTS" \
  JOBS="$JOBS" \
    "$(dirname "$0")/build-adt-sdk.sh" "$ADT" "$target" "$target_out"
  printf '%s\n' "$target" > "$target_out/target.txt"
done
