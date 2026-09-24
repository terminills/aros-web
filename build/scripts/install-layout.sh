#!/usr/bin/env bash
# Lays a built Chromium out as an AROS application package.
#
#   install-layout.sh <chromium-out-dir> <package-dir>
#
# <package-dir> becomes an arospkg source directory (PKGINFO + files/), whose
# files/ subtree is the on-disk layout Workbench sees under SYS:
#
#   Applications.info                 drawer icon (Gorilla set)
#   Applications/Internet.info        drawer icon (Gorilla set)
#   Applications/Internet/Chromium/
#     Chromium                        the browser (stripped unless NO_STRIP=1)
#     Chromium.info                   Workbench tool icon; CHROME_ARG= tooltypes
#   Applications/Internet/Chromium.info  drawer icon for the drawer itself
#                                     are the launch switches (chrome_exe_main_aura.cc)
#     *.pak, icudtl.dat, locales/     resources next to the binary (DIR_ASSETS)
#     WidevineCdm/                    only with WITH_WIDEVINE=1 (see below)
#
# The profile lives at PROGDIR:Profiles (chrome_paths_aros.cc) unless the
# system has a PROFILE: assign, i.e. on disk next to the application, never
# in T:.  `arospkg mkpkg <package-dir> --out <dir>` turns the result into the
# repository archive; for a hosted or native tree without the Software
# Center, `rsync -a <package-dir>/files/ <SYS root>/` installs it directly.
#
# Environment:
#   AROS_HOSTED_ROOT  an AROS tree root (the dir holding Prefs/Presets) whose
#                     Gorilla presets carry Applications.info and
#                     Applications/Internet.info, and whose build tree's
#                     tools/ilbmtoicon builds the tool icon         [required]
#   ILBMTOICON        override the icon tool (default <root>/../tools/ilbmtoicon,
#                     i.e. bin/<arch>/tools of the build tree)
#   CHROMIUM_SRC      Chromium checkout (default: two levels above <out-dir>)
#   STRIP             cross strip (default x86_64-aros-strip on PATH); NO_STRIP=1
#                     keeps the symbols
#   PKG_ARCH          arospkg Arch (default linux-x86_64)
#   PKG_VERSION       arospkg Version (default from chrome/VERSION)
#   WITH_WIDEVINE=1   copy a bundled WidevineCdm/ too (local installs only;
#                     the CDM is Google's and not redistributable)
set -euo pipefail

OUT_DIR=${1:?chromium out dir required}
PKG=${2:?package dir required}
ROOT=${AROS_HOSTED_ROOT:?AROS_HOSTED_ROOT (AROS tree root with Prefs/Presets) is required}
SRC=${CHROMIUM_SRC:-$(cd "$OUT_DIR/../.." && pwd)}
ILBMTOICON=${ILBMTOICON:-$ROOT/../tools/ilbmtoicon}
ARCH=${PKG_ARCH:-linux-x86_64}
THEME=$SRC/chrome/app/theme/aros
PRESETS=$ROOT/Prefs/Presets/Icons/Gorilla/Default/AROS

[[ -x "$OUT_DIR/chrome" ]] || { echo "missing executable: $OUT_DIR/chrome" >&2; exit 2; }
[[ -x "$ILBMTOICON" ]] || { echo "missing $ILBMTOICON (build the AROS tree's tools)" >&2; exit 2; }
for f in "$PRESETS/Applications.info" "$PRESETS/Applications/Internet.info"; do
  [[ -f "$f" ]] || { echo "missing $f (make icons-wbench-applications in the AROS tree)" >&2; exit 2; }
done
[[ -f "$THEME/Chromium.info.src" && -f "$THEME/Chromium.png" ]] || { echo "missing $THEME/Chromium.{info.src,png}" >&2; exit 2; }
[[ -f "$THEME/ChromiumDrawer.info.src" ]] || { echo "missing $THEME/ChromiumDrawer.info.src" >&2; exit 2; }

if [[ -z ${PKG_VERSION:-} ]]; then
  # chrome/VERSION: MAJOR.MINOR.BUILD.PATCH -> MAJOR.MINOR.BUILD-PATCH (protocol §3.2)
  eval "$(sed -n 's/^\(MAJOR\|MINOR\|BUILD\|PATCH\)=\([0-9]*\)$/V_\1=\2/p' "$SRC/chrome/VERSION")"
  PKG_VERSION="$V_MAJOR.$V_MINOR.$V_BUILD-$V_PATCH"
fi

APP=$PKG/files/Applications/Internet/Chromium
rm -rf "$PKG/files"
mkdir -p "$APP/locales"

# --- icons ---------------------------------------------------------------
cp -f "$PRESETS/Applications.info" "$PKG/files/Applications.info"
cp -f "$PRESETS/Applications/Internet.info" "$PKG/files/Applications/Internet.info"
"$ILBMTOICON" "$THEME/Chromium.info.src" "$THEME/Chromium.png" "$APP/Chromium.info"
# Drawer icon for the Chromium drawer itself. Wanderer only shows an object
# that has its own .info, so without this the drawer is invisible from
# Workbench and the tool icon above cannot be reached without a shell.
"$ILBMTOICON" "$THEME/ChromiumDrawer.info.src" "$THEME/Chromium.png" \
    "$PKG/files/Applications/Internet/Chromium.info"

# --- the browser and its resources ----------------------------------------
if [[ ${NO_STRIP:-0} == 1 ]]; then
  cp -f "$OUT_DIR/chrome" "$APP/Chromium"
else
  STRIP=${STRIP:-$(command -v x86_64-aros-strip || true)}
  [[ -n $STRIP ]] || { echo "no x86_64-aros-strip on PATH; set STRIP or NO_STRIP=1" >&2; exit 2; }
  # AROS executables are ET_REL and get relocated by LoadSeg, so the .rela.*
  # sections must survive; a plain `strip` drops them and the program jumps
  # to 0 at task start.  Same flags as the AROS build (configure.in
  # aros_target_strip_flags).
  "$STRIP" --strip-unneeded -R.comment -o "$APP/Chromium" "$OUT_DIR/chrome"
  if ! readelf -S -W "$APP/Chromium" | /usr/bin/grep -q '\.rela\.text'; then
    echo "stripped $APP/Chromium lost its relocations" >&2; exit 2
  fi
fi
chmod 755 "$APP/Chromium"
for f in "$OUT_DIR"/*.pak "$OUT_DIR"/icudtl.dat; do
  [[ -f "$f" ]] && cp -f "$f" "$APP/"
done
if [[ -d "$OUT_DIR/locales" ]]; then
  cp -f "$OUT_DIR"/locales/*.pak "$APP/locales/"
fi
# A bundled Widevine CDM (bundle_widevine_cdm=true) lives next to the binary
# as WidevineCdm/{manifest.json,_platform_specific/aros_x64/libwidevinecdm.so}.
# That .so is Google's, under its own licence: it is only copied into a
# local install (WITH_WIDEVINE=1), never into a package meant for a pool.
if [[ -d "$OUT_DIR/WidevineCdm" ]]; then
  if [[ ${WITH_WIDEVINE:-0} == 1 ]]; then
    cp -a "$OUT_DIR/WidevineCdm" "$APP/WidevineCdm"
  else
    echo "note: WidevineCdm left out (WITH_WIDEVINE=1 includes it for a local install)"
  fi
fi

# --- package metadata -----------------------------------------------------
cat > "$PKG/PKGINFO" <<EOF
Package: Chromium
Version: $PKG_VERSION
Arch: $ARCH
Summary: Chromium web browser
Description: The Chromium web browser, built for AROS with the aros ozone
 platform.  Installs into SYS:Applications/Internet/Chromium; the Workbench
 icon's CHROME_ARG tooltypes are the launch switches.  The profile is kept
 at PROGDIR:Profiles (or PROFILE:Applications/Chromium on a multi-user
 system).
Section: network
Priority: optional
Homepage: https://www.chromium.org/
Maintainer: The AROS Development Team <aros-dev@aros.org>
Licence: BSD-3-Clause
Reboot: no
EOF

echo "Chromium $PKG_VERSION ($ARCH): $(du -sh "$PKG/files" | cut -f1) in $PKG/files"
