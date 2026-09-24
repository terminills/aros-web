#!/usr/bin/env bash
# Puts the Electron apps on the Workbench, so they can be started by
# double-clicking instead of by typing a shell line.
#
# Lays out, in the AROS tree given as $1:
#
#   Applications/Development.info               drawer icon (stock Gorilla
#                                               "Developer" art from Presets)
#   Applications/Development/VS Code            AmigaDOS launcher script
#   Applications/Development/VS Code.info       PROJECT icon, DefaultTool
#                                               C:IconX
#   Applications/Development/GitHub Desktop[.info]
#   Applications/Development/VS Code + GitHub Desktop[.info]
#                                               both apps, one engine
#
# The apps themselves stay where they are installed (SYS:Developer/VSCode,
# SYS:Developer/GitHubDesktop) and one SYS:Tools/ElectronShell serves all of
# them; only the scripts and icons are added here.  Wanderer shows an object
# only if it has its own .info, which is why the drawer needs one too.
#
#   stage-app-icons.sh <AROS tree> [icon build dir]
#
# Environment:
#   ILBMTOICON  the icon tool (default: <tree>/../tools/ilbmtoicon, i.e. the
#               build tree's own bin/<arch>/tools)
#   PRESETS     Gorilla preset icons (default:
#               <tree>/Prefs/Presets/Icons/Gorilla/Default/AROS)
set -euo pipefail

TREE=${1:?AROS tree required (the directory holding C/, Libs/, Applications/)}
WORK=${2:-$(mktemp -d)}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

ILBMTOICON=${ILBMTOICON:-$TREE/../tools/ilbmtoicon}
PRESETS=${PRESETS:-$TREE/Prefs/Presets/Icons/Gorilla/Default/AROS}

[[ -d $TREE/Tools ]] || { echo "not an AROS tree: $TREE" >&2; exit 2; }
[[ -x $ILBMTOICON ]] || { echo "missing $ILBMTOICON (build the AROS tree's tools)" >&2; exit 2; }
[[ -f $PRESETS/Developer.info ]] || { echo "missing $PRESETS/Developer.info" >&2; exit 2; }

DST=$TREE/Applications/Development
mkdir -p "$DST"

# The images, from each app's own artwork.
"$HERE/make-app-icons.py" "$WORK"

# Drawer icons: Applications itself may not have one yet on a tree that never
# staged Chromium, and the new drawer needs its own.
[[ -f $TREE/Applications.info ]] || cp -f "$PRESETS/Applications.info" "$TREE/Applications.info"
cp -f "$PRESETS/Developer.info" "$TREE/Applications/Development.info"

stage() { # <script name> <icon basename>
    local script=$1 icon=$2
    cp -f "$HERE/scripts/$script" "$DST/$script"
    chmod 644 "$DST/$script"
    "$ILBMTOICON" "$HERE/$icon.info.src" "$WORK/$icon.png" "$DST/$script.info"
    echo "  $DST/$script"
}

stage "VS Code" VSCode
stage "GitHub Desktop" GitHubDesktop
stage "VS Code + GitHub Desktop" BothApps

echo "staged Workbench launchers in $DST"
