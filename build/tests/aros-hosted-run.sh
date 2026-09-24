#!/usr/bin/env bash
# Copyright (C) 2025-2026, The AROS Development Team. All rights reserved.
#
# Hosted-AROS runner for tests/hosted-smoke.sh (HOSTED_TEST_COMMAND).
#
#   aros-hosted-run.sh <chrome> <url> <out-dir>
#
# Stages <chrome> (plus the .pak/locale resources next to it) into the hosted
# AROS tree's Developer/Chromium, boots that tree under X with a generated
# S:User-Startup that opens the browser on <url>, lets it run for
# AROS_RUN_SECONDS, takes a screenshot, shuts AROS down cleanly and collects
# the evidence into <out-dir>:
#
#   aros-debug.log            bootstrap debug log (sysdebug=all); Chromium's
#                             INFO-and-above LOG() lines land here too
#                             (base/logging.cc forwards them to KPutStr on
#                             AROS, one host write per character - VERBOSE
#                             lines are kept off that channel on purpose)
#   chrome.log                Chromium's --log-file (INFO + VERBOSE, --v=1);
#                             the file to feed tools/quic-summary.py
#   bootstrap-stdout.log      AROSBootstrap stdout/stderr + "rc=N"
#   chromium-hosted-result.txt  chrome's shell return code (absent = never
#                             returned before the deadline, i.e. still running)
#   screen.xwd / screen.png   the hosted AROS window at the deadline
#   trap-<n>.png, trap-<n>-more.png  the "Software Failure!" requester of the
#                             n-th trap, before and after its "More..."
#                             gadget was pressed (the parked task's stack)
#   input.txt                 output of the guest-side commands the runner
#                             requested (tools/Inject presses the gadget)
#   AROSBootstrap.conf, S-*.txt  the generated config and scripts
#
# Exit status: 0 when AROS booted, chrome started and AROS shut down on its
# own; non-zero otherwise.  "chrome is still running at the deadline" counts
# as success for a browser (the page stays open); a chrome that returned
# non-zero before the deadline is a failure.
#
# Environment:
#   AROS_HOSTED_ROOT   hosted tree, the dir holding boot/linux/AROSBootstrap
#                      (e.g. .../aros-adt-hosted-clean/bin/linux-x86_64/AROS)  [required]
#   AROS_MEMORY_MB     RAM for AROS (default 8192)
#   AROS_RUN_SECONDS   seconds the browser is left running (default 90)
#   AROS_SETTLE        seconds to wait for Wanderer before opening chrome (default 8)
#   AROS_TICKRATE      hosted timer ticks per VBlank (kernel arg tickrate=N;
#                      the tree's default is 4 = 200 Hz at a 50 Hz VBlank, so
#                      GetSysTime()/clock_gettime() and every timer.device
#                      request are quantised to 5 ms; 20 gives 1 ms).  Unset =
#                      leave the kernel default alone.
#   AROS_LAUNCH_GRACE  seconds after the settle time by which the launch script
#                      must have run, else the boot is judged broken (default 90)
#   AROS_SCREEN_GEOMETRY  WxH of the hosted screen (default 800x600); only a
#                      window named AROS of exactly this size is ever captured
#                      for screen.png / trap-<n>.png -- never the host desktop
#   AROS_PROBE_AT      seconds after launch at which tools/TaskBT dumps every
#                      task's state + backtrace to taskbt-<n>.txt (default "60 300")
#   AROS_PROBE_ARGS    extra TaskBT arguments for every probe, e.g.
#                      "MUTEX _ZZSt16__get_once_mutexvE10once_mutex" (default none)
#   AROS_TOOLS_DIR     where TaskBT lives (default: tools/ next to this script)
#   AROS_PRELUDE       an AROS shell command line run in the chrome shell right
#                      before chrome, its output collected as prelude.txt; the
#                      tools in AROS_TOOLS_DIR are staged next to chrome, e.g.
#                      AROS_PRELUDE="SYS:Developer/Chromium/DirProbe FONTS:TrueType"
#   AROS_NETWORK       "tap": bring AROSTCP up on DEVS:networks/tap.device unit
#                      0 (host tap "aros0", created by setup-aros0-network.sh /
#                      aros0-network.service with NAT) before chrome, static
#                      AROS_NET_IP (10.203.0.2/24) via AROS_NET_GW (10.203.0.1)
#                      with AROS_NET_DNS (1.1.1.1); ifconfig/ping output lands
#                      in net.txt of the evidence dir.  The config is written
#                      into the tree (SYS:System/Network/AROSTCP/db, pointer
#                      saved to ENV: and ENVARC:) so it persists across boots.
#                      Default: no network.
#   AROS_STRACE        when set, AROSBootstrap runs under strace with these
#                      trace expressions (e.g. "openat,truncate,ftruncate"),
#                      output in strace.log of the evidence dir
#   AROS_HOST_PRELOAD  when set, AROSBootstrap runs with this LD_PRELOAD (host
#                      shared objects, colon-separated) - for diagnostic shims
#                      interposing the host libc under the bootstrap only
#   CHROME_FLAGS       override the chrome switches (default: the known-good
#                      single-process software set below).  Overriding drops
#                      --single-process: chrome then spawns child processes,
#                      each reserving its own PartitionAlloc pools, and dies
#                      with ENOMEM CHECKs.  Prefer CHROME_EXTRA_FLAGS.
#   AROS_LAUNCH_VIA    "chrome" (default): the shell script runs chrome itself.
#                      "openurl": tools/OpenURLSetBrowser registers "Chromium"
#                      (path "SYS:Developer/Chromium/chrome $FLAGS \"%u\"") in
#                      openurl.library prefs and the script runs C:OpenURL
#                      "<url>" instead - the Workbench/URL-handler launch path.
#                      OpenURL starts the browser asynchronously, so
#                      chromium-hosted-result.txt holds OpenURL's rc (0) and the
#                      run counts as a pass only if chrome is still alive at the
#                      deadline (the screenshot and chrome.log show that).
#                      "workbench": the script runs C:WBRun on the installed
#                      application, SYS:Applications/Internet/Chromium/Chromium
#                      (scripts/install-layout.sh, rsynced into the tree), i.e.
#                      the icon double-click path: no argv, the switches come
#                      from the icon's CHROME_ARG tooltypes, so CHROME_FLAGS and
#                      <url> are ignored (the tooltypes decide the start page)
#                      and there is no chrome.log unless a tooltype asks for
#                      one.  Asynchronous like openurl: a pass means chrome is
#                      still alive at the deadline.
#   CHROME_EXTRA_FLAGS switches appended to the default set (e.g.
#                      "--autoplay-policy=no-user-gesture-required")
#   AROS_ACTIONS_FILE  a script of timed guest-side actions, one per line:
#                      "<seconds-after-launch> <verb> <args>", where verb is
#                        click <x> <y> [button]   tools/Inject click (screen px)
#                        key <rawcode> [UP|DOWN] [CTRL] [SHIFT] [ALT] [RALT]
#                        snap <name>              screenshot to <name>.png
#                        sh <AROS shell line>     run in the guest (input.txt)
#                      Blank lines and # comments are skipped; lines fire in
#                      file order once their time (relative to the
#                      "aros-hosted-run: opening" marker) has passed, and
#                      actions.txt of the evidence dir records each one.
#                      Used for menu audits and other scripted interaction.
#   AROS_GUEST_ENV_FILE a file of NAME=value lines exported into the guest
#                      shell (SetEnv) before chrome starts, e.g. the Google
#                      OAuth client for Chrome profile sign-in:
#                        GOOGLE_API_KEY=...
#                        GOOGLE_DEFAULT_CLIENT_ID=...
#                        GOOGLE_DEFAULT_CLIENT_SECRET=...
#                      (google_apis reads them through base::Environment;
#                      without an OAuth client HasOAuthClientConfigured() is
#                      false and the profile menu has no sign-in).  The
#                      generated S:Chromium-Hosted-Run-Env is not copied into
#                      the evidence dir.  Unset, the runner uses
#                      ${XDG_CONFIG_HOME:-~/.config}/aros-chromium/guest.env
#                      when that file exists (keeps the OAuth client out of
#                      command lines and logs); AROS_GUEST_ENV_FILE=none
#                      forces a bare guest.
#   AROS_PROFILE_DIR   host directory holding a persistent browser profile
#                      (cookies, logins, prefs).  Unset, chrome gets a fresh
#                      --user-data-dir on T: (RAM) each run and the evidence
#                      dir keeps a profile/ snapshot.  Set, no --user-data-dir
#                      is passed and chrome uses its AROS default,
#                      PROGDIR:Profiles (chrome/common/chrome_paths_aros.cc;
#                      PROFILE:Applications/Chromium once a PROFILE: assign
#                      exists) = SYS:Developer/Chromium/Profiles here.  The
#                      host directory is rsynced into it before the boot and
#                      rsynced back (with deletions) once the guest has shut
#                      down, so a site login survives to the next run and a
#                      tree rebuild.  The evidence dir then gets no profile/
#                      snapshot.  Cookies are readable across runs because the
#                      posix OSCrypt uses a fixed key.
#   AROS_SPOOF_PLATFORM linux|windows: pass --aros-spoof-platform so the
#                      browser presents itself as that OS (User-Agent, UA
#                      client hints, navigator.platform; base/aros/
#                      platform_spoof.h).  For pages that gate on an OS
#                      allow-list and never try on "AROS".  Off by default.
#   DISPLAY/XAUTHORITY X server for the hosted display (required)
set -euo pipefail

CHROME=${1:?chrome binary required}
URL=${2:?url required}
OUT=${3:?output directory required}
ROOT=${AROS_HOSTED_ROOT:?AROS_HOSTED_ROOT must point at the hosted AROS tree (dir containing boot/linux/AROSBootstrap)}
MEM=${AROS_MEMORY_MB:-8192}
RUN_SECONDS=${AROS_RUN_SECONDS:-90}
SETTLE=${AROS_SETTLE:-8}
TICKRATE=${AROS_TICKRATE:-}
# --enable-logging (no value) selects LOG_TO_FILE in a release build, and the
# file is the only place VERBOSE lines go.  --enable-logging=stderr instead
# puts INFO+ on the debug channel (interleaved with the kernel's output, the
# thing to use around a crash) but VERBOSE then goes to chrome's stderr,
# which is NIL: under Run.
PROFILE_DIR=${AROS_PROFILE_DIR:-}
if [[ -n $PROFILE_DIR ]]; then
  mkdir -p "$PROFILE_DIR"
  PROFILE_DIR=$(cd "$PROFILE_DIR" && pwd)
  USER_DATA_FLAG=
else
  USER_DATA_FLAG=" --user-data-dir=/T/ChromiumProfile"
fi
FLAGS=${CHROME_FLAGS:-"--single-process --no-sandbox --disable-gpu --no-first-run --no-default-browser-check --enable-logging --log-file=/SYS/Developer/Chromium/chrome.log --v=1 --ozone-platform=aros$USER_DATA_FLAG"}
FLAGS="$FLAGS${CHROME_EXTRA_FLAGS:+ $CHROME_EXTRA_FLAGS}"
SPOOF_PLATFORM=${AROS_SPOOF_PLATFORM:-}
if [[ -n $SPOOF_PLATFORM ]]; then
  case $SPOOF_PLATFORM in
    linux|windows) FLAGS="$FLAGS --aros-spoof-platform=$SPOOF_PLATFORM" ;;
    *) echo "AROS_SPOOF_PLATFORM must be linux or windows, not '$SPOOF_PLATFORM'" >&2; exit 2 ;;
  esac
fi

BOOTSTRAP=$ROOT/boot/linux/AROSBootstrap
[[ -x "$CHROME" ]] || { echo "missing executable: $CHROME" >&2; exit 2; }
# The AROS Shell reads a command line into a LINE_MAX (512) byte buffer and
# runs whatever the tail of an over-long line happens to spell (a 607-byte
# launch line ran `ired "<url>"`, rc 10, and looked like a chrome failure).
# Refuse up front rather than spend a boot on it.
LAUNCH_VIA=${AROS_LAUNCH_VIA:-chrome}
case $LAUNCH_VIA in
  chrome)  LAUNCH_LINE="SYS:Developer/Chromium/chrome $FLAGS \"$URL\"" ;;
  openurl)
    # openurl.library keeps the browser command in a PATH_LEN (256) field
    BROWSER_PATH="SYS:Developer/Chromium/chrome $FLAGS \"%u\""
    if (( ${#BROWSER_PATH} > 255 )); then
      echo "OpenURL browser path is ${#BROWSER_PATH} bytes; openurl.library stores 255. Shorten CHROME_FLAGS." >&2
      exit 2
    fi
    LAUNCH_LINE="C:OpenURL \"$URL\"" ;;
  workbench)
    APP_DIR=$ROOT/Applications/Internet/Chromium
    [[ -x "$APP_DIR/Chromium" && -f "$APP_DIR/Chromium.info" ]] || {
      echo "AROS_LAUNCH_VIA=workbench needs the installed application at $APP_DIR (scripts/install-layout.sh, then rsync files/ into the tree)" >&2; exit 2; }
    LAUNCH_LINE="C:WBRun \"SYS:Applications/Internet/Chromium/Chromium\"" ;;
  shell)
    # Run an arbitrary guest command instead of a browser, reusing this
    # script's boot, networking, trap-capture and evidence collection. Added
    # for the node.library re-invocation work, where the thing under test is
    # "run C:Node three times" rather than anything browser-shaped.
    LAUNCH_LINE=${AROS_SHELL_CMD:?AROS_LAUNCH_VIA=shell needs AROS_SHELL_CMD} ;;
  *) echo "AROS_LAUNCH_VIA must be chrome, openurl, workbench or shell" >&2; exit 2 ;;
esac
if (( ${#LAUNCH_LINE} > 500 )); then
  # Trees without aros-adt-src eeb40c5f (Shell readLine bounded only by
  # memory) drop lines over LINE_MAX 512 silently, RC=0 and nothing runs. On
  # trees with it this is fine, so warn instead of refusing.
  echo "warning: launch line is ${#LAUNCH_LINE} bytes; a Shell without eeb40c5f (LINE_MAX 512) drops it silently" >&2
fi
[[ -x "$BOOTSTRAP" ]] || { echo "missing hosted bootstrap: $BOOTSTRAP" >&2; exit 2; }
[[ -n "${DISPLAY:-}" ]] || { echo "DISPLAY is not set" >&2; exit 2; }
# The x11gfx.hidd fails silently ("Invalid MIT-MAGIC-COOKIE-1 key", no
# driver) when the display is not reachable with this DISPLAY/XAUTHORITY,
# and a display-less boot leaves a Shell running without a console; probe
# the server with the same environment before spending a boot on it.
if ! xset -display "$DISPLAY" q >/dev/null 2>&1; then
  echo "cannot open X display $DISPLAY (XAUTHORITY=${XAUTHORITY:-unset})" >&2
  exit 2
fi
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

# One hosted instance at a time on this box.
if pgrep -x AROSBootstrap -u "$(id -u)" -a | grep -q -- "$BOOTSTRAP"; then
  echo "an AROSBootstrap from $ROOT is already running; refusing to start another" >&2
  exit 3
fi

# --- stage chrome + resources -------------------------------------------
STAGE=$ROOT/Developer/Chromium
mkdir -p "$STAGE"
ln -f "$CHROME" "$STAGE/chrome" 2>/dev/null || cp -f "$CHROME" "$STAGE/chrome"
SRC_DIR=$(dirname "$CHROME")
for f in "$SRC_DIR"/*.pak "$SRC_DIR"/icudtl.dat; do
  [[ -e "$f" ]] || continue
  cmp -s "$f" "$STAGE/$(basename "$f")" || cp -f "$f" "$STAGE/"
done
if [[ -d "$SRC_DIR/locales" ]]; then
  mkdir -p "$STAGE/locales"
  cp -u "$SRC_DIR"/locales/*.pak "$STAGE/locales/" 2>/dev/null || true
fi
# A bundled Widevine CDM (bundle_widevine_cdm=true) lives next to chrome as
# WidevineCdm/{manifest.json,_platform_specific/aros_x64/libwidevinecdm.so};
# chrome looks it up under DIR_ASSETS.  Absent from the out dir = absent
# from the stage, so a build without it does not run with a stale one.
rm -rf "$STAGE/WidevineCdm"
if [[ -d "$SRC_DIR/WidevineCdm" ]]; then
  rsync -a "$SRC_DIR/WidevineCdm/" "$STAGE/WidevineCdm/"
  echo "widevine: $(du -sh "$STAGE/WidevineCdm" | cut -f1) staged"
fi
rm -rf "$STAGE/chromium-hosted-result.txt" "$STAGE"/taskbt-*.txt "$STAGE/prelude.txt" "$STAGE/openurl.txt" "$STAGE/profile" "$STAGE/chrome.log" \
       "$STAGE"/input-request* "$STAGE/input-running" "$STAGE/input.txt"
# A persistent profile is worked on in place at chrome's default location
# (PROGDIR:Profiles; emul.handler does not follow host symlinks out of the
# tree) and written back after the run.  A stale Profiles from an aborted run
# is replaced, never merged.
rm -rf "$STAGE/Profiles"
if [[ -n $PROFILE_DIR ]]; then
  rsync -a "$PROFILE_DIR/" "$STAGE/Profiles/"
  echo "profile: $PROFILE_DIR ($(du -sh "$STAGE/Profiles" | cut -f1)) at PROGDIR:Profiles"
fi

# Task backtrace probes: tools/TaskBT (built from tools/TaskBT.c with the SDK
# gcc) dumps every task's state, signal masks and a symbolised backtrace of
# its saved context; run it AROS_PROBE_AT seconds after chrome is launched so
# a stall shows what each task is waiting on.
TOOLS=${AROS_TOOLS_DIR:-$(dirname "$(readlink -f "$0")")/tools}
PROBE_AT=${AROS_PROBE_AT:-"60 300"}
PROBE_ARGS=${AROS_PROBE_ARGS:-}
have_taskbt=no
for t in "$TOOLS"/*; do
  [[ -x "$t" && -f "$t" && "$t" != *.sh && "$t" != *.c ]] || continue
  ln -f "$t" "$STAGE/$(basename "$t")" 2>/dev/null || cp -f "$t" "$STAGE/$(basename "$t")"
  [[ $(basename "$t") == TaskBT ]] && have_taskbt=yes
done
PRELUDE=${AROS_PRELUDE:-}
prelude_line=
[[ -n $PRELUDE ]] && prelude_line="$PRELUDE >\"SYS:Developer/Chromium/prelude.txt\""
GUEST_ENV_FILE=${AROS_GUEST_ENV_FILE:-}
if [[ -z $GUEST_ENV_FILE ]]; then
  default_env="${XDG_CONFIG_HOME:-$HOME/.config}/aros-chromium/guest.env"
  [[ -r $default_env ]] && GUEST_ENV_FILE=$default_env
elif [[ $GUEST_ENV_FILE == none ]]; then
  GUEST_ENV_FILE=
fi
env_line=
rm -f "$ROOT/S/Chromium-Hosted-Run-Env"
if [[ -n $GUEST_ENV_FILE ]]; then
  [[ -r $GUEST_ENV_FILE ]] || { echo "AROS_GUEST_ENV_FILE not readable: $GUEST_ENV_FILE" >&2; exit 2; }
  {
    while IFS= read -r line || [[ -n $line ]]; do
      [[ -z $line || $line == \#* ]] && continue
      [[ $line == *=* ]] || { echo "bad env line (NAME=value): $line" >&2; exit 2; }
      printf 'SetEnv %s "%s"\n' "${line%%=*}" "$(printf '%s' "${line#*=}" | sed 's/"/*"/g')"
    done < "$GUEST_ENV_FILE"
  } > "$ROOT/S/Chromium-Hosted-Run-Env"
  env_line="Execute S:Chromium-Hosted-Run-Env"
  echo "$(/usr/bin/grep -c . "$ROOT/S/Chromium-Hosted-Run-Env") guest env vars from $GUEST_ENV_FILE"
fi
openurl_line=
if [[ $LAUNCH_VIA == openurl ]]; then
  [[ -x "$TOOLS/OpenURLSetBrowser" ]] || { echo "AROS_LAUNCH_VIA=openurl needs $TOOLS/OpenURLSetBrowser (tools/build.sh)" >&2; exit 2; }
  # the %u form makes openurl.library start the command itself; PORT CHROMIUM
  # makes it hand the URL to an already running chrome instead (the browser's
  # ARexx-style command port, chrome/browser/process_singleton_aros.cc)
  openurl_line="SYS:Developer/Chromium/OpenURLSetBrowser Chromium \"$(printf '%s' "$BROWSER_PATH" | sed 's/"/*"/g')\" PORT CHROMIUM >\"SYS:Developer/Chromium/openurl.txt\""
fi

# Networking: AROSTCP resolves its config dir from ENV:AROSTCP/Config.  ENV:
# is RAM and gone at the next boot, ENVARC: is the persistent copy that the
# stock Startup-Sequence copies into ENV: at every boot, so the pointer is
# saved to both (SetEnv SAVE) and the config itself is written into the
# tree's own SYS:System/Network/AROSTCP/db (the stack's default location):
# once configured, the hosted tree comes up networked on any later boot, with
# or without this runner.  The stack is started the way S:startnet does it,
# and net.txt records what the guest saw.
NETWORK=${AROS_NETWORK:-}
NET_IP=${AROS_NET_IP:-10.203.0.2}
NET_MASK=${AROS_NET_MASK:-255.255.255.0}
NET_GW=${AROS_NET_GW:-10.203.0.1}
NET_DNS=${AROS_NET_DNS:-1.1.1.1}
net_lines=
rm -f "$STAGE/net.txt"
case $NETWORK in
  "") ;;
  tap)
    TCP=$ROOT/System/Network/AROSTCP
    [[ -x "$TCP/C/AROSTCP" && -f "$ROOT/Devs/Networks/tap.device" ]] \
      || { echo "AROS_NETWORK=tap: $TCP/C/AROSTCP or Devs/Networks/tap.device missing in $ROOT" >&2; exit 2; }
    [[ -c /dev/net/tun ]] || { echo "AROS_NETWORK=tap: /dev/net/tun missing on the host" >&2; exit 2; }
    if ! ip link show aros0 >/dev/null 2>&1; then
      echo "AROS_NETWORK=tap: host tap aros0 does not exist (run setup-aros0-network.sh as root)" >&2
      exit 2
    fi
    DB=$TCP/db
    cat > "$DB/interfaces" <<EOF
# generated by aros-hosted-run.sh (AROS_NETWORK=tap)
net0 DEV=DEVS:networks/tap.device UNIT=0 IP=$NET_IP NETMASK=$NET_MASK UP
EOF
    cat > "$DB/static-routes" <<EOF
DEFAULT GATEWAY $NET_GW
EOF
    cat > "$DB/netdb-myhost" <<EOF
HOST $NET_IP arosbox.arosnet arosbox
HOST $NET_GW gateway.arosnet gateway
NAMESERVER $NET_DNS
DOMAIN arosnet
EOF
    cat > "$DB/general.config" <<EOF
USELOOPBACK=YES
DEBUGSANA=NO
USENS=SECOND
GATEWAY=NO
HOSTNAME=arosbox.arosnet
LOG FILTERFILE=5
GUI PANEL=MUI
OPENGUI=NO
EOF
    # SetVar() only Open()s ENV:<name> / ENVARC:<name>, it does not create
    # the AROSTCP/ directory in either (a stock tree has no ENVARC:AROSTCP),
    # so make both first or the SetEnv silently fails and the stack reads its
    # all-commented default interfaces file.
    net_lines=$(cat <<EOF
If NOT EXISTS ENV:AROSTCP
  MakeDir ENV:AROSTCP
EndIf
If NOT EXISTS ENVARC:AROSTCP
  MakeDir ENVARC:AROSTCP
EndIf
SetEnv SAVE AROSTCP/Config "SYS:System/Network/AROSTCP/db"
Echo "AROSTCP/Config=\$AROSTCP/Config" >"SYS:Developer/Chromium/net.txt"
Run >NIL: <NIL: SYS:System/Network/AROSTCP/C/AROSTCP
WaitForPort AROSTCP
Echo "AROSTCP port wait rc=\$RC" >>"SYS:Developer/Chromium/net.txt"
Wait 2
C:ifconfig net0 >>"SYS:Developer/Chromium/net.txt"
C:ping -c 3 $NET_GW >>"SYS:Developer/Chromium/net.txt"
C:ping -c 2 $NET_DNS >>"SYS:Developer/Chromium/net.txt"
Echo "--- T:Log/Syslog" >>"SYS:Developer/Chromium/net.txt"
Type T:Log/Syslog >>"SYS:Developer/Chromium/net.txt"
EOF
    )
    ;;
  *) echo "unknown AROS_NETWORK=$NETWORK (supported: tap)" >&2; exit 2 ;;
esac

# --- generated AROS-side scripts -------------------------------------------
# Startup-Sequence executes S:User-Startup before Wanderer opens; keep it
# non-blocking so the desktop comes up, and do the waiting in the launcher.
cat > "$ROOT/S/User-Startup" <<EOF
; generated by chromium-aros-build/tests/aros-hosted-run.sh — removed after the run
Run >NIL: <NIL: Execute S:Chromium-Hosted-Run-Launch
EOF
probe_line=
[[ $have_taskbt == yes ]] && probe_line="Run >NIL: <NIL: Execute S:Chromium-Hosted-Run-Probe"
cat > "$ROOT/S/Chromium-Hosted-Run-Launch" <<EOF
; wait for Wanderer, arm the deadline (and the task probes), listen for
; host requests, open the browser in its own console
Wait $SETTLE
Run >NIL: <NIL: Execute S:Chromium-Hosted-Run-Deadline
$probe_line
Run >NIL: <NIL: Execute S:Chromium-Hosted-Run-Input
NewShell "CON:40/40/1080/520/Chromium (hosted run)/CLOSE" FROM "S:Chromium-Hosted-Run"
EOF
# Host -> guest command channel.  The staging dir is a host directory, so a
# script the host drops there is visible to the guest at once; this loop
# runs it and removes it.  Used to press requester gadgets: the hosted AROS
# window under Xwayland gets no XTEST input (Xwayland routes it through the
# libei portal, which nobody has granted), so the guest injects the click
# itself with tools/Inject.  The loop's Wait LoadSegs C:Wait once a second
# while chrome runs, which doubles as a canary for memory chrome hands back
# to exec in a bad state (net-26: pages returned without execute permission
# trapped the next program loaded into them at its first instruction).
# The request runs in its own Shell: an Execute nested in this loop leaves
# the loop's script stream where "Skip loop BACK" no longer finds the label,
# and the loop died after its first request (net-28).  The request script
# deletes itself when done, which is what guest_run waits for.  FailAt keeps
# the loop alive when Rename fails because a previous request is still
# running (a script aborts at RC 10 by default).
cat > "$ROOT/S/Chromium-Hosted-Run-Input" <<EOF
FailAt 21
Lab loop
Wait 1
If EXISTS SYS:Developer/Chromium/input-request
  Rename SYS:Developer/Chromium/input-request SYS:Developer/Chromium/input-running
  Run >NIL: <NIL: Execute SYS:Developer/Chromium/input-running
EndIf
Skip loop BACK
EOF
profile_reset_line='Delete "T:ChromiumProfile" ALL QUIET >NIL:'
[[ -n $PROFILE_DIR ]] && profile_reset_line=
cat > "$ROOT/S/Chromium-Hosted-Run" <<EOF
; a shell script aborts at RC >= 10 by default; keep going so the browser's
; return code (which may be anything) still reaches the result file
FailAt 255
Echo "aros-hosted-run: opening $URL"
; the stock ADT Startup-Sequence has no Developer: assign; an unassigned
; volume would raise a blocking "please insert" requester
Assign Developer: SYS:Developer
$profile_reset_line
$net_lines
$env_line
$prelude_line
$openurl_line
$LAUNCH_LINE
Set chromerc \$RC
Echo "\$chromerc" >"SYS:Developer/Chromium/chromium-hosted-result.txt"
Echo "chrome returned \$chromerc"
Wait 600
EOF
# The ephemeral profile lives on T: (RAM) and dies with the guest; snapshot it
# into the staging dir just before Shutdown so on-disk state (SQLite files,
# prefs) can be inspected on the host.  The browser is still running, so it is
# a point-in-time copy, not a consistent one.  A persistent profile is already
# on the host disk and is rsynced back by the runner after the boot ends.
profile_snapshot_line='Copy "T:ChromiumProfile" "SYS:Developer/Chromium/profile" ALL QUIET >NIL:'
[[ -n $PROFILE_DIR ]] && profile_snapshot_line=
cat > "$ROOT/S/Chromium-Hosted-Run-Deadline" <<EOF
Wait $RUN_SECONDS
$profile_snapshot_line
Shutdown
EOF
if [[ $have_taskbt == yes ]]; then
  {
    echo "; task backtrace probes at ${PROBE_AT}s after launch"
    last=0
    for at in $PROBE_AT; do
      (( at > last )) || continue
      echo "Wait $(( at - last ))"
      echo "SYS:Developer/Chromium/TaskBT DEPTH 48 ${PROBE_ARGS} >\"SYS:Developer/Chromium/taskbt-${at}.txt\""
      last=$at
    done
  } > "$ROOT/S/Chromium-Hosted-Run-Probe"
  cp "$ROOT/S/Chromium-Hosted-Run-Probe" "$OUT/S-Chromium-Hosted-Run-Probe.txt"
fi

CONF=$OUT/AROSBootstrap.conf
{
  echo "logfile $OUT/aros-debug.log"
  echo "memory $MEM"
  echo "arguments unattendedalerts${TICKRATE:+ tickrate=$TICKRATE}"
  grep -E '^module ' "$ROOT/boot/linux/AROSBootstrap.conf"
} > "$CONF"
cp "$ROOT/S/User-Startup" "$OUT/S-User-Startup.txt"
cp "$ROOT/S/Chromium-Hosted-Run-Launch" "$OUT/S-Chromium-Hosted-Run-Launch.txt"
cp "$ROOT/S/Chromium-Hosted-Run" "$OUT/S-Chromium-Hosted-Run.txt"
cp "$ROOT/S/Chromium-Hosted-Run-Deadline" "$OUT/S-Chromium-Hosted-Run-Deadline.txt"
cp "$ROOT/S/Chromium-Hosted-Run-Input" "$OUT/S-Chromium-Hosted-Run-Input.txt"

cleanup_tree() {
  rm -f "$ROOT/S/User-Startup" "$ROOT/S/Chromium-Hosted-Run-Launch" \
        "$ROOT/S/Chromium-Hosted-Run" "$ROOT/S/Chromium-Hosted-Run-Deadline" \
        "$ROOT/S/Chromium-Hosted-Run-Probe" "$ROOT/S/Chromium-Hosted-Run-Input"
  # A runaway Shell (display-less boot) creates junk-named files in SYS:S;
  # report anything that appeared during the run rather than deleting blindly.
  local junk
  junk=$(find "$ROOT/S" -maxdepth 1 -type f -newer "$CONF" 2>/dev/null | cat -v)
  [[ -z $junk ]] || echo "WARNING: files created in $ROOT/S during the run:"$'\n'"$junk" | tee -a "$OUT/bootstrap-stdout.log" >&2
}

# --- boot -----------------------------------------------------------------
: > "$OUT/aros-debug.log"
(
  cd "$ROOT"
  if [[ -n ${AROS_HOST_PRELOAD:-} ]]; then
    export LD_PRELOAD=$AROS_HOST_PRELOAD
    echo "host preload: $LD_PRELOAD"
  fi
  if [[ -n ${AROS_STRACE:-} ]]; then
    exec strace -f -tt -o "$OUT/strace.log" -e trace="$AROS_STRACE" -e signal=none \
      "$BOOTSTRAP" -c "$CONF" sysdebug=all
  fi
  exec "$BOOTSTRAP" -c "$CONF" sysdebug=all
) > "$OUT/bootstrap-stdout.log" 2>&1 &
BOOT_PID=$!
echo "$BOOT_PID" > "$OUT/bootstrap.pid"
echo "AROSBootstrap pid $BOOT_PID, deadline ${RUN_SECONDS}s (+${SETTLE}s settle)"

# Wait for the AROS-side deadline, but bail out early when the boot has
# visibly gone wrong: the display driver failed to attach, or the launch
# script never ran (no "opening" marker within LAUNCH_GRACE of the settle
# time — a boot without a display leaves the Shell reading garbage, and it
# creates files in SYS:S while doing so).
LAUNCH_GRACE=${AROS_LAUNCH_GRACE:-90}

# Capture the hosted AROS window as $OUT/<name>.xwd + .png; leaves the window
# id in SCREEN_WID for input injection.
SCREEN_WID=
SCREEN_GEOMETRY=${AROS_SCREEN_GEOMETRY:-800x600}
snap_screen() {
  local name=$1 w geo
  # Under rootless Xwayland GetImage on the root window (and on the window
  # manager's frame windows) fails with BadMatch; capture the hosted AROS
  # client window instead.  The x11 hidd sets no _NET_WM_PID, so find the
  # candidates by name and keep the first that reads.  The hidd's window is
  # titled "AROS" and is exactly the guest screen size (AROS_SCREEN_GEOMETRY,
  # 800x600): a name match alone also catches the user's own windows (a chat
  # or browser tab named "AROS.dev"), and a root-window fallback captures the
  # whole host desktop -- both leaked private host content into a run's
  # evidence once.  Nothing but a guest-sized window is ever captured; when
  # none reads, the .xwd stays empty and no .png is written.
  : > "$OUT/$name.xwd"
  for w in $(xdotool search --onlyvisible --name '^AROS$' 2>/dev/null) \
           $(xdotool search --onlyvisible --name '^AROS' 2>/dev/null); do
    geo=$(xdotool getwindowgeometry "$w" 2>/dev/null | awk '/Geometry/{print $2}')
    [[ $geo == "$SCREEN_GEOMETRY" ]] || continue
    if xwd -display "$DISPLAY" -id "$w" -silent > "$OUT/$name.xwd" 2>/dev/null && [[ -s "$OUT/$name.xwd" ]]; then
      SCREEN_WID=$w
      break
    fi
    : > "$OUT/$name.xwd"
  done
  if [[ ! -s "$OUT/$name.xwd" ]]; then
    echo "snap_screen $name: no ${SCREEN_GEOMETRY} window named AROS on $DISPLAY; not captured"
    rm -f "$OUT/$name.xwd"
    return 0
  fi
  if [[ -s "$OUT/$name.xwd" ]]; then
    # PIL has no XWD reader; go through netpbm.
    xwdtopnm "$OUT/$name.xwd" 2>/dev/null | python3 -c '
import sys
from PIL import Image
Image.open(sys.stdin.buffer).save(sys.argv[1])' "$OUT/$name.png" 2>/dev/null || true
  fi
}

# Run AROS Shell lines inside the guest (see S:Chromium-Hosted-Run-Input);
# waits for the guest to pick the request up.  Their output collects in
# $STAGE/input.txt.
guest_run() {
  local i
  printf '%s >>SYS:Developer/Chromium/input.txt\nDelete SYS:Developer/Chromium/input-running QUIET\n' "$@" > "$STAGE/input-request.tmp"
  mv "$STAGE/input-request.tmp" "$STAGE/input-request"
  for i in $(seq 1 20); do
    [[ -e "$STAGE/input-request" || -e "$STAGE/input-running" ]] || return 0
    sleep 0.5
  done
  echo "guest did not run the request within 10s: $*"
  return 1
}

# A trap or a recoverable alert parks the failing task in a "Software
# Failure!" / "Recoverable Alert!" requester; TaskBT cannot unwind a parked
# task, but the requester's "More..." gadget shows its stack, and the full
# requester's "Log" gadget (leftmost, where More... was) writes the whole
# alert -- registers, disassembly, every stack frame -- to the debug log as
# a "*** Logged alert:" block.  Capture the requester, press More... (from
# inside the guest), capture the stack, press Log, copy the block out.
# Traps are logged by the kernel; alerts have no log line of their own, so
# match the exec checks that raise them.
FAILURE_PATTERN='\[KRN\] Trap signal|called on a not initialized semaphore|called in supervisor mode'
traps_seen=0
logged_alerts_seen=0
capture_trap() {
  local n=$1 xy logged
  sleep 4
  snap_screen "trap-$n"
  xy=$(python3 "$TOOLS/requester-more.py" "$OUT/trap-$n.png" 2>/dev/null || true)
  if [[ -z $xy || -z $SCREEN_WID ]]; then
    echo "trap $n: no requester found on screen (trap-$n.png)"
    return
  fi
  guest_run "SYS:Developer/Chromium/Inject click ${xy}" || true
  sleep 3
  snap_screen "trap-$n-more"
  echo "trap $n: requester at ${xy}, stack in trap-$n-more.png"
  xy=$(python3 "$TOOLS/requester-more.py" "$OUT/trap-$n-more.png" 2>/dev/null || true)
  [[ -n $xy ]] || return
  guest_run "SYS:Developer/Chromium/Inject click ${xy}" || true
  sleep 2
  logged=$(grep -a -c '^\*\*\* Logged alert:' "$OUT/aros-debug.log" 2>/dev/null || true)
  if (( ${logged:-0} > logged_alerts_seen )); then
    logged_alerts_seen=$logged
    # The block runs from the marker to the next kernel/shell log line.
    awk '/^\*\*\* Logged alert:/{p=1; buf=""} p{ if (buf != "" && /^\[/) {p=0} else buf = buf $0 "\n" } END{printf "%s", buf}' \
      "$OUT/aros-debug.log" > "$OUT/trap-$n-stack.txt"
    echo "trap $n: Log pressed at ${xy}, full stack in trap-$n-stack.txt"
  else
    echo "trap $n: Log pressed at ${xy} but no new logged alert appeared"
  fi
}

# Timed guest-side actions (AROS_ACTIONS_FILE): loaded up front so a
# malformed script fails before the boot, fired from the loop below once
# the launch marker has been seen and the action's offset has passed.
ACTIONS_FILE=${AROS_ACTIONS_FILE:-}
action_at=() action_cmd=()
if [[ -n $ACTIONS_FILE ]]; then
  [[ -r $ACTIONS_FILE ]] || { echo "AROS_ACTIONS_FILE not readable: $ACTIONS_FILE" >&2; exit 2; }
  while IFS= read -r line || [[ -n $line ]]; do
    line=${line#"${line%%[![:space:]]*}"}
    [[ -z $line || $line == \#* ]] && continue
    [[ $line =~ ^([0-9]+)[[:space:]]+(click|key|snap|sh)[[:space:]]+(.+)$ ]] \
      || { echo "bad action line: $line" >&2; exit 2; }
    action_at+=("${BASH_REMATCH[1]}")
    action_cmd+=("${BASH_REMATCH[2]} ${BASH_REMATCH[3]}")
  done < "$ACTIONS_FILE"
  cp -f "$ACTIONS_FILE" "$OUT/actions.script"
  echo "${#action_at[@]} timed actions from $ACTIONS_FILE"
fi
next_action=0
launch_at=
run_action() {
  local cmd=$1 verb=${1%% *} args=${1#* }
  case $verb in
    click|key) guest_run "SYS:Developer/Chromium/Inject $verb $args" || true ;;
    snap)      snap_screen "$args" ;;
    sh)        guest_run "$args" || true ;;
  esac
  echo "$(( elapsed - launch_at ))s: $cmd" >> "$OUT/actions.txt"
}

abort_reason=
t0=$SECONDS
elapsed=0
while (( elapsed < SETTLE + RUN_SECONDS - 5 )); do
  sleep 1; elapsed=$(( SECONDS - t0 ))     # wall clock, not iterations
  kill -0 "$BOOT_PID" 2>/dev/null || break
  if [[ -z $launch_at ]] && grep -q -a "aros-hosted-run: opening" "$OUT/aros-debug.log" 2>/dev/null; then
    launch_at=$elapsed
  fi
  while [[ -n $launch_at ]] && (( next_action < ${#action_at[@]} )) \
        && (( elapsed - launch_at >= action_at[next_action] )); do
    run_action "${action_cmd[next_action]}"
    next_action=$(( next_action + 1 ))
  done
  if grep -q -a -m1 -E "Invalid MIT-MAGIC-COOKIE|HW__AddDriver: Failed to instantiate Driver Object" "$OUT/aros-debug.log" 2>/dev/null; then
    abort_reason="display driver failed to attach (see aros-debug.log)"; break
  fi
  if (( elapsed > SETTLE + LAUNCH_GRACE )) && ! grep -q -a "aros-hosted-run: opening" "$OUT/aros-debug.log" 2>/dev/null; then
    abort_reason="launch script did not run within $(( SETTLE + LAUNCH_GRACE ))s"; break
  fi
  traps=$(grep -a -c -E "$FAILURE_PATTERN" "$OUT/aros-debug.log" 2>/dev/null || true)
  traps=${traps:-0}
  if (( traps > traps_seen )); then
    traps_seen=$traps
    capture_trap "$traps" | tee -a "$OUT/bootstrap-stdout.log"
  fi
done
if [[ -n $abort_reason ]]; then
  echo "aborting: $abort_reason; killing pid $BOOT_PID" | tee -a "$OUT/bootstrap-stdout.log"
  kill "$BOOT_PID" 2>/dev/null || true
  sleep 3
  kill -9 "$BOOT_PID" 2>/dev/null || true
fi

# Screenshot just before the AROS-side deadline fires. A requester that
# appeared without a matching log line (an alert raised from a code path the
# pattern above does not cover) is still on screen here; walk its stack too.
if kill -0 "$BOOT_PID" 2>/dev/null; then
  snap_screen screen
  xy=$(python3 "$TOOLS/requester-more.py" "$OUT/screen.png" 2>/dev/null || true)
  if [[ -n $xy && -n $SCREEN_WID && ! -e "$OUT/trap-$traps_seen-more.png" ]]; then
    guest_run "SYS:Developer/Chromium/Inject click ${xy}" || true
    sleep 3
    snap_screen "screen-more"
    echo "final screen: requester at ${xy}, stack in screen-more.png" | tee -a "$OUT/bootstrap-stdout.log"
  fi
fi

# Wait for the clean Shutdown; kill by exact pid on overrun.
rc=124
for _ in $(seq 1 120); do
  if ! kill -0 "$BOOT_PID" 2>/dev/null; then
    wait "$BOOT_PID" && rc=0 || rc=$?
    break
  fi
  sleep 1
done
if [[ $rc -eq 124 ]]; then
  echo "AROS did not shut down within the grace period; killing pid $BOOT_PID" | tee -a "$OUT/bootstrap-stdout.log"
  kill "$BOOT_PID" 2>/dev/null || true
  sleep 3
  kill -9 "$BOOT_PID" 2>/dev/null || true
fi
echo "rc=$rc" >> "$OUT/bootstrap-stdout.log"
cleanup_tree

# --- verdict --------------------------------------------------------------
[[ -f "$STAGE/chromium-hosted-result.txt" ]] && cp "$STAGE/chromium-hosted-result.txt" "$OUT/"
[[ -f "$STAGE/net.txt" ]] && cp "$STAGE/net.txt" "$OUT/"
for f in "$STAGE"/taskbt-*.txt "$STAGE/prelude.txt" "$STAGE/openurl.txt" "$STAGE/chrome.log" "$STAGE/input.txt"; do [[ -e "$f" ]] && mv -f "$f" "$OUT/"; done
rm -f "$STAGE"/input-request* "$STAGE/input-running"
[[ -d "$STAGE/profile" ]] && { rm -rf "$OUT/profile"; mv -f "$STAGE/profile" "$OUT/profile"; }
if [[ -n $PROFILE_DIR && -d "$STAGE/Profiles" ]]; then
  # the guest is down, so this copy is consistent; --delete drops files chrome
  # removed (journals, expired cache entries)
  rsync -a --delete "$STAGE/Profiles/" "$PROFILE_DIR/"
  rm -rf "$STAGE/Profiles"
  echo "profile: written back to $PROFILE_DIR" | tee -a "$OUT/bootstrap-stdout.log"
fi
chrome_rc=running
[[ -f "$OUT/chromium-hosted-result.txt" ]] && chrome_rc=$(tr -d '[:space:]' < "$OUT/chromium-hosted-result.txt")
if [[ $LAUNCH_VIA == workbench ]]; then
  # WBRun returns as soon as Workbench has started the program, so its rc says
  # nothing about the browser; the run passes when the Chromium task reached
  # the deadline without trapping.
  chrome_rc=running
  grep -l -- '- Chromium$' "$OUT"/trap-*-stack.txt >/dev/null 2>&1 && chrome_rc=trap
  echo "$chrome_rc" > "$OUT/chromium-hosted-result.txt"
fi
started=no
grep -q "aros-hosted-run: opening" "$OUT/aros-debug.log" 2>/dev/null && started=yes
{
  echo "bootstrap_rc=$rc"
  echo "chrome_started=$started"
  echo "chrome_rc=$chrome_rc"
  echo "url=$URL"
  echo "memory_mb=$MEM"
  echo "run_seconds=$RUN_SECONDS"
} | tee "$OUT/run-summary.txt"

[[ $rc -eq 0 ]] || exit 1
[[ $chrome_rc == running || $chrome_rc == 0 ]] || exit 1
exit 0
