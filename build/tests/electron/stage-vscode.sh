#!/usr/bin/env bash
#
#   Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
#   Stage the VS Code desktop tree (vscode-aros, compiled to out/) into an
#   AROS tree as SYS:Developer/VSCode for ElectronShell:
#
#     Tools/ElectronShell --main-script=SYS:Developer/VSCode/out/main.js
#
#   Everything is hardlinked (cp -al) so re-staging after a compile costs no
#   disk and out/ edits made by hand (write new file + mv) stay isolated.
#
#   What goes in:
#     out/ package.json product.json      the workbench + main process
#     node_modules/<runtime closure>      package.json "dependencies" and
#                                         their transitive deps, one entry
#                                         per line in vscode-runtime-deps.txt
#                                         (nested entries ride along with
#                                         their parent package)
#     extensions/<built-ins>              without node_modules/src/test -
#                                         grammars, themes and compiled
#                                         out/ or dist/ code; extensions
#                                         whose deps are missing fail to
#                                         activate, which VS Code reports
#
#   Usage: stage-vscode.sh <vscode-aros dir> <AROS tree Developer dir>
#     e.g. stage-vscode.sh ~/src/vscode-aros <build>/bin/linux-x86_64/AROS/Developer
#
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=${1:?usage: stage-vscode.sh <vscode-aros dir> <AROS tree Developer dir>}
DEV=${2:?usage: stage-vscode.sh <vscode-aros dir> <AROS tree Developer dir>}
DST="$DEV/VSCode"
DEPS="$HERE/vscode-runtime-deps.txt"

[ -f "$SRC/out/main.js" ] || { echo "no compiled out/ in $SRC" >&2; exit 1; }
[ -d "$DEV" ] || { echo "no Developer dir at $DEV" >&2; exit 1; }
[ -f "$DEPS" ] || { echo "missing $DEPS" >&2; exit 1; }

mkdir -p "$DST"

# out/ + the two manifests: refresh only what changed (cp -al refuses to
# overwrite, so replace file by file when the link target differs).
# Source maps are 840 MB of the 1.4 GB this used to stage - 60% of a VS Code
# on the ISO, for debug metadata nothing reads unless devtools is open. They
# stay in $SRC (this is a hardlink farm, so dropping the staged name keeps the
# original) and can be staged with STAGE_SOURCE_MAPS=1 when a minified frame
# in a tester's report actually needs symbolising.
STAGE_SOURCE_MAPS=${STAGE_SOURCE_MAPS:-0}
skip_file() {
    [ "$STAGE_SOURCE_MAPS" = 1 ] && return 1
    case $1 in *.map) return 0 ;; *) return 1 ;; esac
}

link_tree() {
    local from=$1 to=$2 rel
    while IFS= read -r -d '' rel; do
        skip_file "$rel" && continue
        local s="$from/$rel" d="$to/$rel"
        if [ -e "$d" ] && [ "$s" -ef "$d" ]; then continue; fi
        mkdir -p "$(dirname "$d")"
        rm -f "$d"
        ln "$s" "$d"
    done < <(cd "$from" && find . -type f -print0)
}

echo "== out/"
link_tree "$SRC/out" "$DST/out"
for f in package.json product.json; do
    [ -e "$DST/$f" ] && [ "$SRC/$f" -ef "$DST/$f" ] || { rm -f "$DST/$f"; ln "$SRC/$f" "$DST/$f"; }
done

echo "== node_modules (runtime closure)"
mkdir -p "$DST/node_modules"
n=0
while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    case "$rel" in
        node_modules/*/node_modules/*|node_modules/@*/*/node_modules/*) continue ;;
    esac
    pkg=${rel#node_modules/}
    if [ -d "$DST/node_modules/$pkg" ]; then continue; fi
    mkdir -p "$(dirname "$DST/node_modules/$pkg")"
    cp -al "$SRC/$rel" "$DST/node_modules/$pkg"
    n=$((n + 1))
done < "$DEPS"
echo "   $n packages linked"

echo "== extensions (built-ins, no src/test; node_modules = runtime closure)"
mkdir -p "$DST/extensions"
: > "$DST/extensions/.missing-deps.txt"
e=0
p=0
for ext in "$SRC"/extensions/*/; do
    name=$(basename "$ext")
    [ -f "$ext/package.json" ] || continue
    if [ ! -d "$DST/extensions/$name" ]; then
        mkdir -p "$DST/extensions/$name"
        (cd "$ext" && find . \( -name node_modules -o -name src -o -name test -o -name .vscode \) -prune -o -type f -print0) |
        while IFS= read -r -d '' rel; do
            skip_file "$rel" && continue
            d="$DST/extensions/$name/$rel"
            mkdir -p "$(dirname "$d")"
            ln "$ext/$rel" "$d"
        done
        e=$((e + 1))
    fi
    # Runtime deps only (git needs `which`, most need @vscode/extension-telemetry);
    # the closure walker keeps devDependencies out.  A package resolved from
    # extensions/node_modules or the repo root is staged into the extension's
    # own node_modules so the staged tree resolves it the same way.
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        case "$rel" in
            extensions/$name/node_modules/*) to="$DST/$rel" ;;
            *) to="$DST/extensions/$name/node_modules/${rel##*node_modules/}" ;;
        esac
        [ -d "$to" ] && continue
        mkdir -p "$(dirname "$to")"
        cp -al "$SRC/$rel" "$to"
        p=$((p + 1))
    done < <(node "$HERE/extension-runtime-deps.js" "$SRC" "$name" 2>>"$DST/extensions/.missing-deps.txt")
done
echo "   $e extensions linked, $p dependency packages linked"
if [ -s "$DST/extensions/.missing-deps.txt" ]; then
    echo "   unresolved runtime deps (see $DST/extensions/.missing-deps.txt):"
    sort -u "$DST/extensions/.missing-deps.txt" | head -20
fi

echo "== staged: $DST"
du -sh --apparent-size "$DST" | cut -f1
