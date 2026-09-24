#!/usr/bin/env python3
"""Match a faulting address against the shared-memory and page-allocator
events Chromium's AROS port logs to aros-debug.log.

Usage: trace-match.py aros-debug.log [address ...]

Without addresses, every trap's RDX (the gather_8888 pixel base) is taken
from the "Trap signal" register dumps in the log.  For each address the
events are listed in log order, marking whether the address lies inside the
event's range; the last event before the trap that covered the address is
the one that owned the page when the fault happened.

Events (all only for objects >= 64 KiB):
  [Chromium:shm] create|destroy memory=0x.. len=N refs=N
  [Chromium:PA] alloc-exec|alloc-kernel|free|decommit|set-access address=0x.. len=N protection=0x..
"""
import re
import sys

EVENT = re.compile(r'(shm|PA)\] (\S+) (?:memory|address)=(?:0x)?([0-9a-fA-F]+) len=(\d+)(.*)')
TRAP = re.compile(r'Trap signal')
RDX = re.compile(r'RDX=([0-9A-Fa-f]{16})')


def main(path, addrs):
    lines = open(path, errors='replace').read().splitlines()
    events, traps = [], []
    for n, line in enumerate(lines):
        m = EVENT.search(line)
        if m:
            events.append((n, m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4)), m.group(5).strip()))
            continue
        if TRAP.search(line):
            traps.append(n)
            continue
        m = RDX.search(line)
        if m and traps and n - traps[-1] < 40 and len(addrs) < len(traps):
            addrs.append(int(m.group(1), 16))
    print(f'{len(events)} events, {len(traps)} traps')
    for a in addrs:
        print(f'\n== address {a:#x}')
        owner = None
        for n, kind, what, base, length, rest in events:
            inside = base <= a < base + length
            if inside:
                print(f'  line {n + 1}: [{kind}] {what} {base:#x}+{length:#x} {rest}')
                owner = (kind, what, base, length)
        print(f'  last owner: {owner}')


if __name__ == '__main__':
    main(sys.argv[1], [int(x, 16) for x in sys.argv[2:]])
