#!/usr/bin/env bash
#
#   Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
#   Stage the Claude Code CLI into an AROS tree as SYS:Developer/ClaudeCode:
#
#     Developer/ClaudeCode/claude_aros_bootstrap.mjs   guest/ClaudeCode
#     Developer/ClaudeCode/runtime/node_modules/@anthropic-ai/claude-code
#                                                     the npm package, unmodified
#     S/Claude-Code-Interactive                       guest/S launcher
#
#   2.1.112 is the last release whose bin is cli.js; later ones ship a native
#   executable per platform.  The bootstrap keeps cli.js unmodified and
#   defers Unix signal listeners, which uv1 does not implement on AROS yet.
#
#   Usage: stage-claude-code.sh <AROS tree> [package tarball]
#     e.g. stage-claude-code.sh <build>/bin/pc-x86_64-smp/AROS
#   Without a tarball the package is fetched with `npm pack`.
#
set -euo pipefail

VERSION=2.1.112
here=$(cd "$(dirname "$0")/.." && pwd)
root=${1:?usage: stage-claude-code.sh <AROS tree> [package tarball]}
tarball=${2:-}
[ -d "$root/Developer" ] || { echo "stage-claude-code: $root has no Developer/" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
if [ -z "$tarball" ]; then
    (cd "$work" && npm pack --silent "@anthropic-ai/claude-code@$VERSION" >/dev/null)
    tarball=$(ls "$work"/anthropic-ai-claude-code-*.tgz)
fi
tar -xzf "$tarball" -C "$work"
got=$(node -p "require('$work/package/package.json').version")
[ "$got" = "$VERSION" ] || { echo "stage-claude-code: tarball is $got, want $VERSION" >&2; exit 1; }

dest=$root/Developer/ClaudeCode
pkg=$dest/runtime/node_modules/@anthropic-ai/claude-code
rm -rf "$pkg"
mkdir -p "$(dirname "$pkg")"
mv "$work/package" "$pkg"
cp "$here/guest/ClaudeCode/claude_aros_bootstrap.mjs" "$dest/"
cp "$here/guest/S/Claude-Code-Interactive" "$root/S/"
echo "stage-claude-code: Claude Code $VERSION in $dest"
