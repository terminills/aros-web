#!/usr/bin/env bash
#
#   Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
#   Stage the "Claude Code for VS Code" extension (an unpacked marketplace
#   VSIX) into an AROS tree so the desktop VS Code staged by stage-vscode.sh
#   loads it as a user extension and runs the CLI under node.library:
#
#     Home/.vscode-oss-dev/extensions/anthropic.claude-code-<ver>/
#         the VSIX's extension/ minus the Linux/macOS/Windows native CLI
#         binaries (resources/native-binary*, 212 MB) and audio-capture;
#         resources/native-binary/claude becomes claude-code-native-shim.cjs
#         so the extension's executable resolution succeeds and hands the
#         shim path to the process wrapper (see below);
#         package.json engines.vscode is relaxed to the staged VS Code's
#         version (the VSIX asks for ^1.94, vscode-aros is 1.92.2) - a local
#         install, so no marketplace check is involved; APIs missing from
#         1.92 show up at activation and are logged by VS Code.
#     Developer/ClaudeCode/claude_aros_sdk_bootstrap.mjs
#         claude-code-sdk-bootstrap.mjs (stdout-silent bootstrap for SDK
#         stream-json mode; needs the existing Developer/ClaudeCode/runtime
#         staged beforehand).
#     Home/.config/code-oss-dev/User/settings.json
#         claudeCode.claudeProcessWrapper = C:Node (the extension then spawns
#         "C:Node <shim> <claude args>") + the same environment the
#         interactive S:Claude-Code-Interactive sets.
#
#   Usage: stage-claude-code-extension.sh <unpacked vsix dir> <AROS tree>
#     <AROS tree> is the AROS/ directory of a build (the one holding Home/).
#
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
VSIX=${1:?usage: stage-claude-code-extension.sh <unpacked vsix dir> <AROS tree>}
AROS=${2:?usage: stage-claude-code-extension.sh <unpacked vsix dir> <AROS tree>}

EXT="$VSIX/extension"
[ -f "$EXT/package.json" ] || { echo "no extension/package.json under $VSIX" >&2; exit 1; }
[ -d "$AROS/Home" ] || { echo "no Home/ in $AROS" >&2; exit 1; }
[ -f "$AROS/Developer/ClaudeCode/runtime/node_modules/@anthropic-ai/claude-code/cli.js" ] ||
    { echo "Developer/ClaudeCode/runtime (Claude Code CLI) is not staged in $AROS" >&2; exit 1; }

ver=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$EXT/package.json")
vscode_ver=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$AROS/Developer/VSCode/package.json")
DST="$AROS/Home/.vscode-oss-dev/extensions/anthropic.claude-code-$ver"

echo "== extension $ver -> $DST (VS Code $vscode_ver)"
rm -rf "$DST"
mkdir -p "$DST"
(cd "$EXT" && find . \( -path ./resources/native-binary -o -path ./resources/native-binaries \
                        -o -path ./resources/audio-capture \) -prune -o -type f -print0) |
while IFS= read -r -d '' rel; do
    d="$DST/$rel"
    mkdir -p "$(dirname "$d")"
    cp "$EXT/$rel" "$d"
done

# engines.vscode: relax to the staged VS Code (recorded in the manifest so
# the change is visible).
python3 - "$DST/package.json" "$vscode_ver" <<'EOF'
import json, sys
p, ver = sys.argv[1], sys.argv[2]
m = json.load(open(p))
want = m.get("engines", {}).get("vscode")
m.setdefault("engines", {})["vscode"] = "^" + ver
m["__aros"] = {"enginesVscodeUpstream": want, "nativeBinary": "resources/native-binary/claude is the AROS node shim"}
json.dump(m, open(p, "w"), indent=2)
print(f"   engines.vscode {want} -> ^{ver}")
EOF

mkdir -p "$DST/resources/native-binary"
cp "$HERE/claude-code-native-shim.cjs" "$DST/resources/native-binary/claude"
chmod +x "$DST/resources/native-binary/claude"

# VS Code loads user extensions from the profile manifest
# extensions/extensions.json, not from the directory listing (run 20: the
# staged directory alone was never scanned).  Register the entry the way
# the install service writes it (location URI + relativeLocation; the
# scanner prefers relativeLocation).
echo "== extensions.json"
python3 - "$AROS/Home/.vscode-oss-dev/extensions/extensions.json" "$ver" <<'EOF'
import json, os, sys, time
p, ver = sys.argv[1], sys.argv[2]
entries = []
if os.path.exists(p):
    try:
        entries = json.load(open(p))
    except Exception:
        entries = []
entries = [e for e in entries if e.get("identifier", {}).get("id") != "anthropic.claude-code"]
rel = f"anthropic.claude-code-{ver}"
entries.append({
    "identifier": {"id": "anthropic.claude-code"},
    "version": ver,
    "location": {"$mid": 1, "scheme": "file",
                 "path": f"/SYS:/Home/.vscode-oss-dev/extensions/{rel}"},
    "relativeLocation": rel,
    "metadata": {"installedTimestamp": int(time.time() * 1000), "pinned": True,
                 "source": "vsix", "isApplicationScoped": False},
})
json.dump(entries, open(p, "w"))
print(f"   {len(entries)} entries")
EOF

echo "== Developer/ClaudeCode/claude_aros_sdk_bootstrap.mjs"
cp "$HERE/claude-code-sdk-bootstrap.mjs" "$AROS/Developer/ClaudeCode/claude_aros_sdk_bootstrap.mjs"

echo "== User/settings.json"
SETTINGS="$AROS/Home/.config/code-oss-dev/User/settings.json"
mkdir -p "$(dirname "$SETTINGS")"
python3 - "$SETTINGS" <<'EOF'
import json, os, sys
p = sys.argv[1]
s = {}
if os.path.exists(p):
    try:
        s = json.load(open(p))
    except Exception:
        s = {}
s["claudeCode.claudeProcessWrapper"] = "C:Node"
s["claudeCode.environmentVariables"] = [
    {"name": "DISABLE_AUTOUPDATER", "value": "1"},
    {"name": "TERM", "value": "xterm-256color"},
    {"name": "BROWSER", "value": "C:OpenURL"},
]
s.setdefault("claudeCode.preferredLocation", "panel")
s.setdefault("extensions.autoUpdate", False)
s.setdefault("extensions.autoCheckUpdates", False)
json.dump(s, open(p, "w"), indent=2)
print("   " + ", ".join(k for k in s if k.startswith("claudeCode.")))
EOF

echo "== staged"
du -sh --apparent-size "$DST" | cut -f1
