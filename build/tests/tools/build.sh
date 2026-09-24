#!/usr/bin/env bash
# Build the guest-side probe tools against a hosted AROS SDK.
#
#   tools/build.sh <sdk-dir> [toolchain-dir]
#
#   sdk-dir        .../bin/linux-x86_64/AROS/Developer of the hosted tree
#   toolchain-dir  dir holding x86_64-aros-gcc (default: ../../../aros-crosstools)
set -euo pipefail
SDK=${1:?sdk dir (…/AROS/Developer) required}
TOOLCHAIN=${2:-$(dirname "$(readlink -f "$0")")/../../../aros-crosstools}
HERE=$(dirname "$(readlink -f "$0")")
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/TaskBT" "$HERE/TaskBT.c" -ldebug
echo "built $HERE/TaskBT"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall \
  -o "$HERE/DirProbe" "$HERE/DirProbe.c"
echo "built $HERE/DirProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall \
  -o "$HERE/StatProbe" "$HERE/StatProbe.c"
echo "built $HERE/StatProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall \
  -o "$HERE/PoolProbe" "$HERE/PoolProbe.c" -lpthread
echo "built $HERE/PoolProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall \
  -o "$HERE/SqlIoProbe" "$HERE/SqlIoProbe.c"
echo "built $HERE/SqlIoProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall \
  -o "$HERE/NetProbe" "$HERE/NetProbe.c" -lpthread
echo "built $HERE/NetProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall \
  -o "$HERE/UdpProbe" "$HERE/UdpProbe.c" -lpthread
echo "built $HERE/UdpProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/SeekProbe" "$HERE/SeekProbe.c"
echo "built $HERE/SeekProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/Inject" "$HERE/Inject.c"
echo "built $HERE/Inject"
# -nix like Chromium; absolutely linked, so all threads share one StdCBase
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -nix -O1 -g0 -Wall \
  -o "$HERE/ErrnoProbe" "$HERE/ErrnoProbe.c" -lpthread
echo "built $HERE/ErrnoProbe"
# SqlNormalProbe links Chromium's SQLite amalgamation with Chromium's AROS
# configuration (third_party/sqlite/BUILD.gn + *_configuration_flags.gni,
# minus ICU) plus SQLITE_DEBUG so the VFS-level OSTRACE output is available.
SQLITE_AMALGAMATION=${SQLITE_AMALGAMATION:-$HERE/../../../chromium-aros/src/third_party/sqlite/src/amalgamation}
if [[ -f "$SQLITE_AMALGAMATION/sqlite3.c" ]]; then
  SQLITE_DEFS=(
    -DSQLITE_ENABLE_FTS3 -DSQLITE_DISABLE_FTS3_UNICODE -DSQLITE_DISABLE_FTS4_DEFERRED
    -DSQLITE_SECURE_DELETE -DSQLITE_THREADSAFE=1 -DSQLITE_MAX_WORKER_THREADS=0
    -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_OMIT_WAL -DSQLITE_DEFAULT_FILE_PERMISSIONS=0600
    -DSQLITE_DEFAULT_LOCKING_MODE=1 -DSQLITE_DEFAULT_MEMSTATUS=1 -DSQLITE_DEFAULT_PAGE_SIZE=4096
    -DSQLITE_DEFAULT_PCACHE_INITSZ=0 -DSQLITE_LIKE_DOESNT_MATCH_BLOBS -DSQLITE_OMIT_DEPRECATED
    -DSQLITE_OMIT_PROGRESS_CALLBACK -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_USE_ALLOCA
    -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_JSON -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_HAVE_ISNAN
    -DSQLITE_ENABLE_DBPAGE_VTAB -DSQLITE_ENABLE_BATCH_ATOMIC_WRITE -DSQLITE_TEMP_STORE=3
    -DSQLITE_ENABLE_LOCKING_STYLE=0 -DSQLITE_OMIT_ANALYZE -DSQLITE_OMIT_AUTOINIT
    -DSQLITE_OMIT_AUTOMATIC_INDEX -DSQLITE_OMIT_AUTORESET -DSQLITE_OMIT_COMPILEOPTION_DIAGS
    -DSQLITE_OMIT_EXPLAIN -DSQLITE_OMIT_GET_TABLE -DSQLITE_OMIT_INTROSPECTION_PRAGMAS
    -DSQLITE_DEFAULT_LOOKASIDE=0,0 -DSQLITE_OMIT_LOOKASIDE -DSQLITE_OMIT_TCL_VARIABLE
    -DSQLITE_OMIT_REINDEX -DSQLITE_OMIT_TRACE -DSQLITE_OMIT_UPSERT -DSQLITE_OMIT_WINDOWFUNC
    -DHAVE_USLEEP=1 -DSQLITE_DEBUG -DSQLITE_FORCE_OS_TRACE
  )
  "$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -w "${SQLITE_DEFS[@]}" \
    -I"$SQLITE_AMALGAMATION" -c -o "$HERE/sqlite3-aros.o" "$HERE/sqlite3-aros.c"
  # -nix: Chromium links with unix path conversion, so "/T/x" means T:x
  "$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -nix -O1 -g0 -Wall -DSQLITE_DEBUG \
    -I"$SQLITE_AMALGAMATION" -o "$HERE/SqlNormalProbe" "$HERE/SqlNormalProbe.c" \
    "$HERE/sqlite3-aros.o" -lpthread
  echo "built $HERE/SqlNormalProbe"
else
  echo "SqlNormalProbe skipped: no amalgamation at $SQLITE_AMALGAMATION" >&2
fi
# YmmSwitch needs AVX code generation; the rest of the tree is built without it
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O2 -g0 -Wall -mavx \
  -o "$HERE/YmmSwitch" "$HERE/YmmSwitch.c"
echo "built $HERE/YmmSwitch"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/OpenURLSetBrowser" "$HERE/OpenURLSetBrowser.c"
echo "built $HERE/OpenURLSetBrowser"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/WinList" "$HERE/WinList.c"
echo "built $HERE/WinList"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/TtyProbe" "$HERE/TtyProbe.c"
echo "built $HERE/TtyProbe"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/RunErr" "$HERE/RunErr.c"
echo "built $HERE/RunErr"
"$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -O1 -g0 -Wall -Wno-pointer-sign \
  -o "$HERE/FdProbe" "$HERE/FdProbe.c"
echo "built $HERE/FdProbe"
# UvTtyProbe needs libuv's public header (not installed into the SDK)
UV_INCLUDE=${UV_INCLUDE:-$(ls -d "$SDK"/../../Ports/libuv/libuv/libuv-v*/include 2>/dev/null | head -n 1)}
if [[ -n "$UV_INCLUDE" ]]; then
  "$TOOLCHAIN/x86_64-aros-gcc" --sysroot="$SDK" -I"$UV_INCLUDE" -O1 -g0 -Wall -Wno-pointer-sign \
    -o "$HERE/UvTtyProbe" "$HERE/UvTtyProbe.c" -luv1
  echo "built $HERE/UvTtyProbe"
else
  echo "UvTtyProbe skipped: no libuv include dir below Ports" >&2
fi
