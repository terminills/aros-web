#!/usr/bin/env python3
"""Generate exact-build V8 import addresses without debug symbol enumeration."""
from pathlib import Path
import argparse
import hashlib
import re


def generate(text):
    names = []
    for line in text.splitlines():
        name = line.strip()
        if not name or name.startswith('#'):
            continue
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name):
            raise ValueError('invalid ELF function name: ' + name)
        names.append(name)
    if not names:
        raise ValueError('empty V8 import manifest')
    names = sorted(set(names))
    lines = ['/* Generated exact-build V8 import addresses; do not edit. */',
             '/* Manifest SHA256: ' + hashlib.sha256(text.encode()).hexdigest() + ' */']
    # Some manifest entries belong to older Chromium consumers. Weak references
    # leave absent optional imports NULL; lookup retains the normal failure path.
    for i, name in enumerate(names):
        lines.append(f'extern void v8_exact_import_{i}(void) __asm__("{name}") __attribute__((weak));')
    lines.append('static const struct V8SymbolEntry v8ExactImports[] = {')
    for i, name in enumerate(names):
        lines.append(f'    {{"{name}", (APTR)&v8_exact_import_{i}}},')
    lines.append('};\n')
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = generate(args.manifest.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != output:
        args.output.write_text(output)
