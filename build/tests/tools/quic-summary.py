#!/usr/bin/env python3
"""Summarise QUIC health from a Chromium --v=1 log (aros-debug.log).

Usage: quic-summary.py aros-debug.log

Prints the client-side ack delay distribution (time between receiving a
packet and sending the ACK for it - a scheduling-latency signal on AROS),
the smoothed RTT trajectory, PTO retransmission count, write-blocked events
and the media pipeline milestones, so two runs can be compared in one look.
"""
import re
import sys


def pct(values, p):
    if not values:
        return 0
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p))]


def main(path):
    ack = []
    rtt = []
    pto = 0
    blocked = 0
    first_ts = None
    milestones = []
    ts_re = re.compile(r':(\d{4})/(\d{6}\.\d+):')
    for line in open(path, 'rb'):
        line = line.decode('utf-8', 'replace')
        m = ts_re.search(line)
        ts = m.group(2) if m else None
        if ts and first_ts is None:
            first_ts = ts
        m = re.search(r'ack_delay_time: (\d+)', line)
        if m:
            ack.append(int(m.group(1)))
        m = re.search(r'smoothed_rtt\(us\):(\d+)', line)
        if m:
            rtt.append((ts, int(m.group(1))))
        if 'PTO_RETRANSMISSION' in line:
            pto += 1
        if 'write blocked' in line:
            blocked += 1
        for key in ('kStarting -> kPlaying', '"event":"kPlay"',
                    'DEMUXER_UNDERFLOW', 'Trap signal'):
            if key in line:
                milestones.append((ts, key))
    print(f'first log timestamp {first_ts}')
    print(f'ack_delay_time n={len(ack)} p50={pct(ack, .5)} p90={pct(ack, .9)} '
          f'p99={pct(ack, .99)} max={max(ack) if ack else 0} us')
    print(f'PTO_RETRANSMISSION {pto}, write-blocked lines {blocked}')
    if rtt:
        step = max(1, len(rtt) // 12)
        print('smoothed_rtt (ms):', ' '.join(f'{v // 1000}@{t[:6] if t else "?"}'
                                              for t, v in rtt[::step]))
        print(f'smoothed_rtt max {max(v for _, v in rtt) // 1000} ms, '
              f'last {rtt[-1][1] // 1000} ms')
    seen = set()
    for ts, key in milestones:
        if key in ('kStarting -> kPlaying', 'Trap signal') or key not in seen:
            print(f'  {ts} {key}')
        seen.add(key)
    n_under = sum(1 for _, k in milestones if k == 'DEMUXER_UNDERFLOW')
    print(f'DEMUXER_UNDERFLOW {n_under}')


if __name__ == '__main__':
    main(sys.argv[1])
