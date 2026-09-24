#!/usr/bin/env python3
"""Replay the [Chromium:shm] / [Chromium:PA] events of an aros-debug.log and
report every page-allocator event whose range overlaps a shared-memory
region that is live (created, not yet destroyed) at that point, and every
shared-memory region created inside a live page-allocator allocation.

Usage: trace-overlap.py aros-debug.log

A PA event touching a live shared-memory region is the "someone else
protected viz's tile buffer" signature; the converse is the allocator
handing the same pages out twice.
"""
import re
import sys

EVENT = re.compile(r'(shm|PA)\] (\S+) (?:memory|address)=(?:0x)?([0-9a-fA-F]+) len=(\d+)(.*)')


def overlaps(a, alen, b, blen):
    return a < b + blen and b < a + alen


def main(path):
    live_shm = {}   # base -> (length, line)
    live_pa = {}    # base -> (length, line)
    hits = 0
    for n, line in enumerate(open(path, errors='replace'), 1):
        m = EVENT.search(line)
        if not m:
            continue
        kind, what, base, length = m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4))
        if kind == 'shm':
            if what == 'create':
                for pb, (pl, pn) in live_pa.items():
                    if overlaps(base, length, pb, pl):
                        hits += 1
                        print(f'line {n}: shm create {base:#x}+{length:#x} inside PA allocation {pb:#x}+{pl:#x} (line {pn})')
                live_shm[base] = (length, n)
            elif what == 'destroy':
                live_shm.pop(base, None)
            continue
        # PA
        for sb, (sl, sn) in live_shm.items():
            if overlaps(base, length, sb, sl):
                hits += 1
                print(f'line {n}: PA {what} {base:#x}+{length:#x} overlaps live shm {sb:#x}+{sl:#x} (created line {sn})')
        if what.startswith('alloc'):
            live_pa[base] = (length, n)
        elif what == 'free':
            # PA frees by the exposed (trimmed) address; the record covering
            # it releases its whole original range.
            for pb, (pl, pn) in list(live_pa.items()):
                if pb <= base < pb + pl:
                    del live_pa[pb]
    print(f'{hits} overlaps; {len(live_shm)} shm regions and {len(live_pa)} PA allocations live at end')


if __name__ == '__main__':
    main(sys.argv[1])
