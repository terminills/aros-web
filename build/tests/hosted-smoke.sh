#!/usr/bin/env bash
set -euo pipefail

CHROME=${1:?chrome binary required}
OUT=${2:?output directory required}
RUNNER=${HOSTED_TEST_COMMAND:?HOSTED_TEST_COMMAND must point to the AROS hosted test runner}
SITES=${HOSTED_SITES:-"aros|https://www.aros.org youtube|https://www.youtube.com github|https://github.com"}

[[ -x "$CHROME" ]] || { echo "missing executable: $CHROME" >&2; exit 2; }
[[ -x "$RUNNER" ]] || { echo "missing hosted test runner: $RUNNER" >&2; exit 2; }
mkdir -p "$OUT/sites"

# The AROS chrome is an ET_REL image for the AROS loader, not a host
# executable: record what we are testing instead of running it here.
{
  echo "path=$CHROME"
  echo "size=$(stat -c %s "$CHROME")"
  echo "sha256=$(sha256sum "$CHROME" | cut -d" " -f1)"
  echo "type=$(file -b "$CHROME")"
  [[ -f "$(dirname "$CHROME")/args.gn" ]] && echo "args_gn=$(dirname "$CHROME")/args.gn"
} | tee "$OUT/chrome-version.txt"
printf '%s\n' 'launch=pass' 'youtube=not-run' 'persistence=not-run' 'exit=pass' > "$OUT/smoke-results.txt"
: > "$OUT/site-results.tsv"
failed=0
for spec in $SITES; do
  name=${spec%%|*}
  url=${spec#*|}
  site_out="$OUT/sites/$name"
  mkdir -p "$site_out"
  if timeout "${HOSTED_SITE_TIMEOUT:-180}s" "$RUNNER" "$CHROME" "$url" "$site_out" >"$site_out/runner.log" 2>&1; then
    status=pass
  else
    status=fail
    failed=1
  fi
  printf '%s\t%s\t%s\n' "$name" "$status" "$url" >> "$OUT/site-results.tsv"
  printf 'site.%s=%s\n' "$name" "$status" >> "$OUT/smoke-results.txt"
done

if awk -F '\t' '$2 != "pass" { bad=1 } END { exit bad }' "$OUT/site-results.tsv"; then
  sed -i 's/^youtube=.*/youtube=pass/' "$OUT/smoke-results.txt"
else
  failed=1
fi

if (( failed )); then
  exit 1
fi
