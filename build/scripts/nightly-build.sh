#!/usr/bin/env bash
set -euo pipefail

CONFIG=${1:---config}
if [[ "$CONFIG" == "--config" ]]; then
  CONFIG=${2:-config/nightly.env}
fi
source "$CONFIG"
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

: "${ADT_REPO:?}" "${ADT_COMMIT:?}" "${CHROMIUM_COMMIT:?}" "${CHROMIUM_DEPOT_TOOLS:?}"
: "${BUILD_ROOT:?}" "${ARTIFACT_ROOT:?}"
JOBS=${JOBS:-32}
(( JOBS <= 32 )) || { echo "JOBS must not exceed 32" >&2; exit 2; }

RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
RUN_DIR="$ARTIFACT_ROOT/runs/$RUN_ID"
WORK="$BUILD_ROOT/$RUN_ID"
mkdir -p "$RUN_DIR" "$WORK"
exec > >(tee "$RUN_DIR/nightly.log") 2>&1

sha256_file() { sha256sum "$1" | awk '{print $1}'; }
git_clean_at() {
  local dir=$1 commit=$2
  git -C "$dir" fetch --quiet --depth=1 origin "$commit"
  git -C "$dir" reset --hard --quiet "$commit"
  git -C "$dir" clean -ffdqx
  [[ -z "$(git -C "$dir" status --porcelain)" ]]
}

echo "run=$RUN_ID"
echo "adt=$ADT_COMMIT chromium=$CHROMIUM_COMMIT jobs=$JOBS"

ADT="$WORK/adt"
CHROMIUM="$WORK/chromium/src"
git clone --quiet "$ADT_REPO" "$ADT"
git_clean_at "$ADT" "$ADT_COMMIT"

if [[ -n "${CHROMIUM_DEPOT_TOOLS:-}" && -x "$CHROMIUM_DEPOT_TOOLS/fetch" ]]; then
  mkdir -p "$WORK/chromium"
  (
    cd "$WORK/chromium"
    PATH="$CHROMIUM_DEPOT_TOOLS:$PATH" fetch --nohooks --no-history chromium
    PATH="$CHROMIUM_DEPOT_TOOLS:$PATH" gclient sync --nohooks --revision "src@$CHROMIUM_COMMIT"
    PATH="$CHROMIUM_DEPOT_TOOLS:$PATH" gclient runhooks
  )
else
  echo "CHROMIUM_DEPOT_TOOLS with fetch is required for Chromium nightlies" >&2
  exit 2
fi
git_clean_at "$CHROMIUM" "$CHROMIUM_COMMIT"

PATCH_QUEUE="$ROOT/$PATCH_QUEUE"
PATCH_SHA=$(sha256sum "$PATCH_QUEUE" | awk '{print $1}')
if [[ -s "$PATCH_QUEUE" ]]; then
  while IFS= read -r patch; do
    [[ -z "$patch" || "$patch" == \#* ]] && continue
    git -C "$CHROMIUM" am --3way "$ROOT/$patch"
  done < "$PATCH_QUEUE"
fi

echo "stage=inputs" > "$RUN_DIR/stages"
echo "adt_head=$(git -C "$ADT" rev-parse HEAD)" >> "$RUN_DIR/stages"
echo "chromium_head=$(git -C "$CHROMIUM" rev-parse HEAD)" >> "$RUN_DIR/stages"
echo "patch_queue_sha256=$PATCH_SHA" >> "$RUN_DIR/stages"

echo "stage=build" >> "$RUN_DIR/stages"
ADT_TOOLCHAIN_DIR=${ADT_TOOLCHAIN_DIR:-$WORK/toolchain}
ADT_PORTSOURCES_DIR=${ADT_PORTSOURCES_DIR:-$WORK/portssources}
export ADT_TOOLCHAIN_DIR ADT_PORTSOURCES_DIR
export ADT_TARGET=${ADT_TARGET:-pc-x86_64}
export ADT_VARIANT=${ADT_VARIANT:-}
"$ROOT/scripts/build-adt-sdk.sh" "$ADT" "$ADT_TARGET" "$WORK/adt-build"

export CHROMIUM_GN_ARGS
: "${CHROMIUM_GN_ARGS:?CHROMIUM_GN_ARGS must be set in nightly.env}"
"$ROOT/scripts/build-chromium.sh" "$CHROMIUM" pc-x86_64 "$WORK/chromium-build"

echo "stage=tests" >> "$RUN_DIR/stages"
"$ROOT/tests/hosted-smoke.sh" "$WORK/chromium-build/chrome" "$RUN_DIR"

PACKAGE="$RUN_DIR/chromium-aros-$RUN_ID.tar.zst"
tar -C "$WORK/chromium-build" -I zstd -cf "$PACKAGE" .
ARTIFACT_SHA=$(sha256_file "$PACKAGE")
python3 "$ROOT/scripts/write-manifest.py" "$RUN_DIR/manifest.json" \
  "$ADT" "$CHROMIUM" "$PATCH_SHA" "$ARTIFACT_SHA" "$JOBS"
ln -sfn "runs/$RUN_ID" "$ARTIFACT_ROOT/nightly/current"
echo "published=$RUN_DIR/manifest.json"
