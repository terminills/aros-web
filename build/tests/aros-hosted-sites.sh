#!/usr/bin/env bash
# Site sweep over the hosted-AROS runner: one aros-hosted-run.sh boot per URL,
# sequentially, and a summary table at the end.
#
#   aros-hosted-sites.sh <chrome> <out-root> [sites-file]
#
# <sites-file> has one "<name> <url>" per line (blank lines and # comments
# ignored); default: tests/sites.txt next to this script.  Each site gets
# <out-root>/<name>/ (the runner's evidence dir) and <out-root>/<name>.launch.log,
# and <out-root>/summary.md gets a row per site:
#
#   name | url | traps (count of "Trap signal" in aros-debug.log) | chrome_rc |
#   run time | verdict
#
# Verdict: PASS = booted, chrome ran to the deadline, no trap; TRAP = at least
# one Software Failure; EXIT = chrome returned before the deadline; BOOT = the
# runner itself failed.  The sweep never stops on a failure - every site is
# evidence.
#
# Environment: everything aros-hosted-run.sh reads (AROS_HOSTED_ROOT is
# required); AROS_RUN_SECONDS defaults to 150 here and AROS_PROBE_AT to "140"
# (one TaskBT dump shortly before the deadline).

set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CHROME=${1:?chrome binary}
OUTROOT=${2:?out root}
SITES=${3:-$HERE/sites.txt}

export AROS_RUN_SECONDS=${AROS_RUN_SECONDS:-150}
export AROS_PROBE_AT=${AROS_PROBE_AT:-140}

mkdir -p "$OUTROOT"
SUMMARY=$OUTROOT/summary.md
{
  echo "# Site sweep $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  echo "chrome: $CHROME"
  echo "hosted root: ${AROS_HOSTED_ROOT:-unset}, run seconds: $AROS_RUN_SECONDS"
  echo
  echo "| site | url | traps | chrome_rc | wall s | verdict |"
  echo "|---|---|---|---|---|---|"
} > "$SUMMARY"

while read -r name url; do
  [[ -z $name || $name == \#* ]] && continue
  out=$OUTROOT/$name
  t0=$(date +%s)
  echo "=== $name $url ($(date +%H:%M:%S))"
  "$HERE/aros-hosted-run.sh" "$CHROME" "$url" "$out" > "$OUTROOT/$name.launch.log" 2>&1
  rc=$?
  wall=$(( $(date +%s) - t0 ))
  traps=0
  [[ -f $out/aros-debug.log ]] && traps=$(/usr/bin/grep -c "Trap signal" "$out/aros-debug.log")
  crc=$(sed -n 's/^chrome_rc=//p' "$out/run-summary.txt" 2>/dev/null)
  booted=$(sed -n 's/^bootstrap_rc=//p' "$out/run-summary.txt" 2>/dev/null)
  if [[ -z $booted ]]; then verdict=BOOT
  elif (( traps > 0 )); then verdict=TRAP
  elif [[ $crc != running ]]; then verdict=EXIT
  else verdict=PASS
  fi
  echo "| $name | $url | $traps | ${crc:-?} | $wall | **$verdict** (runner rc $rc) |" >> "$SUMMARY"
  echo "    -> $verdict traps=$traps chrome_rc=${crc:-?} ${wall}s"
done < "$SITES"

echo
cat "$SUMMARY"
