#!/usr/bin/env python3
"""Retain the Rust archive closure GN attaches to the V8 monolith link edge.

GN's C++ static archive excludes Rust rlibs. The native library link must keep
them separately, including Temporal and the matching native Rust sysroot.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys

build = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2])
query = subprocess.check_output(
    [os.environ.get("NINJA", "ninja"), "-C", str(build), "-t", "query",
     "obj/v8/libv8_monolith.a"], text=True)
archives = []
for line in query.splitlines():
    value = line.strip().removeprefix("| ").removeprefix("|| ")
    if not value.endswith(".rlib"):
        continue
    archive = (build / value).resolve()
    if not archive.is_file():
        raise SystemExit(f"Missing V8 Rust archive: {archive}")
    if archive not in archives:
        archives.append(archive)
if not archives:
    raise SystemExit("V8 156 Rust archive closure is unexpectedly empty")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("--start-group\n" + "\n".join(shlex.quote(str(p)) for p in archives)
                  + "\n--end-group\n")
print(f"Retained {len(archives)} V8 Rust archives")
