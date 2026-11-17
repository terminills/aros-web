#!/usr/bin/env python3
"""Retain the Rust archive closure GN attaches to a native engine link edge.

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
target = sys.argv[3] if len(sys.argv) > 3 else "obj/v8/libv8_monolith.a"
query = subprocess.check_output(
    [os.environ.get("NINJA", "ninja"), "-C", str(build), "-t", "query",
     target], text=True)
archives = []
for line in query.splitlines():
    value = line.strip().removeprefix("| ").removeprefix("|| ")
    if not value.endswith(".rlib"):
        continue
    archive = (build / value).resolve()
    if not archive.is_file():
        raise SystemExit(f"Missing engine Rust archive: {archive}")
    if archive not in archives:
        archives.append(archive)
if not archives:
    raise SystemExit("Engine Rust archive closure is unexpectedly empty")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("--start-group\n" + "\n".join(shlex.quote(str(p)) for p in archives)
                  + "\n--end-group\n")
print(f"Retained {len(archives)} Rust archives for {target}")
