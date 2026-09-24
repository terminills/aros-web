#!/usr/bin/env bash
# Append one row to BUILD-LEDGER.md so "when did this break?" is a lookup
# instead of an investigation.
#
#   record-build.sh <artifact> <out-dir> <verdict> [notes...]
#
# verdict: pass | fail | unknown
#
# Records what you cannot reconstruct afterwards: which commit the artifact was
# built from, whether the tree was dirty, and - the one that actually bit us -
# whether the out dir's objects were OLDER than the newest commit. An out dir
# whose objects predate the source is not an incremental build, it is a silent
# bisect over everything in between, so a behaviour change after it cannot be
# attributed to the commit you were testing. That is flagged STALE-OUTDIR here
# rather than left for someone to notice.

set -u

LEDGER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LEDGER="$LEDGER_DIR/BUILD-LEDGER.md"
SRC="${CHROMIUM_AROS_SRC:-$LEDGER_DIR/../chromium-aros/src}"

if [ $# -lt 3 ]; then
    sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 2
fi

artifact="$1"; outdir="$2"; verdict="$3"; shift 3
notes="${*:-}"

[ -f "$artifact" ] || { echo "record-build: no such artifact: $artifact" >&2; exit 1; }

now=$(date -u +"%Y-%m-%d %H:%M:%SZ")
a_mtime=$(date -u -r "$artifact" +"%Y-%m-%d %H:%M")
a_size=$(stat -c %s "$artifact")
a_sha=$(sha256sum "$artifact" | cut -c1-16)

head_sha=$(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo "?")
head_date=$(git -C "$SRC" log -1 --format=%cI 2>/dev/null || echo "?")
dirty=$(git -C "$SRC" status --porcelain --untracked-files=no 2>/dev/null | wc -l)

# The staleness check. Compare the newest object in the out dir against the
# newest commit; if objects are older, the build swept in uncommitted history.
flag=""
if [ -d "$outdir" ]; then
    newest_obj=$(find "$outdir" -name '*.o' -newermt "$head_date" -print -quit 2>/dev/null)
    if [ -z "$newest_obj" ]; then
        flag=" **STALE-OUTDIR**"
    fi
fi
[ "$dirty" -gt 0 ] && flag="$flag **DIRTY($dirty)**"

if [ ! -f "$LEDGER" ]; then
    {
        echo "# Build ledger"
        echo
        echo "Append-only. One row per built-and-tested artifact, so a regression"
        echo "can be dated instead of re-derived. Written by \`tests/record-build.sh\`."
        echo
        echo "\`STALE-OUTDIR\` means the out dir held no object newer than HEAD, i.e."
        echo "that build was a silent bisect over every commit since its objects were"
        echo "made - do not attribute its behaviour to one commit."
        echo
        echo "| recorded (UTC) | artifact | built | size | sha256:16 | HEAD | verdict | notes |"
        echo "| --- | --- | --- | --- | --- | --- | --- | --- |"
    } > "$LEDGER"
fi

printf '| %s | `%s` | %s | %s | `%s` | `%s`%s | **%s** | %s |\n' \
    "$now" "$(basename "$artifact")" "$a_mtime" "$a_size" "$a_sha" \
    "$head_sha" "$flag" "$verdict" "$notes" >> "$LEDGER"

echo "record-build: appended to $LEDGER"
printf '  %s  %s  HEAD=%s%s  verdict=%s\n' "$(basename "$artifact")" "$a_mtime" "$head_sha" "$flag" "$verdict"
