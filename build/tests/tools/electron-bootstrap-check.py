#!/usr/bin/env python3
# Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
# Extract the JavaScript embedded in electron.library's C string literals
# (electron_bridge.c: electron_module_bootstrap; electron_renderer_seam.inc.c:
# the renderer runtime) and syntax-check it with node before a library
# rebuild.  A broken literal otherwise only shows up as a guest-side
# SyntaxError after a ~5 minute hosted run.
#
# usage: electron-bootstrap-check.py <c-source> <array-name> <out.js>
import re
import subprocess
import sys


def extract(source, array):
    text = open(source, encoding="utf-8").read()
    # Drop C comments first: one between the string pieces may end in ';'.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    match = re.search(r"static const char %s\[\]\s*=\s*(.*?);\n" % re.escape(array),
                      text, re.S)
    if match is None:
        raise SystemExit("array %s not found in %s" % (array, source))
    body = match.group(1)
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    js = "".join(pieces)
    # Undo C escapes; the literals use \\ \" \n \t \x1f only.
    return js.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    source, array, out = sys.argv[1:]
    js = extract(source, array)
    with open(out, "w", encoding="utf-8") as handle:
        handle.write(js)
    rc = subprocess.call(["node", "--check", out])
    print("%s: %d chars, node --check rc=%d" % (out, len(js), rc))
    return rc


if __name__ == "__main__":
    sys.exit(main())
