#!/usr/bin/env python3
"""Summarise a Chromium JSON trace (--trace-startup-format=json) per thread.

Usage: trace-summary.py trace.json [--thread NAME] [--bucket SECONDS] [--top N]

Prints, for every thread (or the one matching --thread), busy time per bucket
split by the top-level task names, the longest complete events, and the
netlog request start/end events so a stall can be placed against the network.
Reads the file in one go; a few hundred MB is fine.
"""
import argparse
import collections
import json
import sys


def load(path):
    with open(path, 'rb') as f:
        data = json.load(f)
    return data['traceEvents'] if isinstance(data, dict) else data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--thread', default=None,
                    help='substring of the thread name to report (default: all)')
    ap.add_argument('--bucket', type=float, default=30.0)
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--netlog', action='store_true',
                    help='also dump netlog URL_REQUEST start/end events')
    args = ap.parse_args()

    ev = load(args.trace)
    names = {}
    for e in ev:
        if e.get('ph') == 'M' and e.get('name') == 'thread_name':
            names[(e['pid'], e['tid'])] = e['args']['name']

    t0 = min(e['ts'] for e in ev if 'ts' in e)
    t1 = max(e['ts'] + e.get('dur', 0) for e in ev if 'ts' in e)
    print(f'{len(ev)} events, span {(t1 - t0) / 1e6:.1f} s, {len(names)} named threads')

    # Complete events ('X') carry dur; 'B'/'E' pairs are matched per thread.
    per_thread = collections.defaultdict(list)
    open_stack = collections.defaultdict(list)
    for e in ev:
        ph = e.get('ph')
        key = (e.get('pid'), e.get('tid'))
        if ph == 'X':
            per_thread[key].append((e['ts'], e.get('dur', 0), e['name'], e.get('args')))
        elif ph == 'B':
            open_stack[key].append(e)
        elif ph == 'E' and open_stack[key]:
            b = open_stack[key].pop()
            per_thread[key].append((b['ts'], e['ts'] - b['ts'], b['name'], b.get('args')))

    bucket_us = args.bucket * 1e6
    for key, evs in sorted(per_thread.items(), key=lambda kv: -len(kv[1])):
        tname = names.get(key, '?')
        if args.thread and args.thread not in tname:
            continue
        evs.sort()
        # Only count depth-0 events for busy time so nesting is not double counted.
        busy = collections.defaultdict(lambda: collections.Counter())
        end = -1
        for ts, dur, name, _ in evs:
            if ts < end:
                continue
            end = ts + dur
            busy[int((ts - t0) // bucket_us)][name] += dur
        print(f'\n== {tname} pid={key[0]} tid={key[1]} events={len(evs)}')
        for b in sorted(busy):
            tot = sum(busy[b].values()) / 1e6
            top = ', '.join(f'{n} {d / 1e6:.1f}s' for n, d in busy[b].most_common(4))
            print(f'  [{b * args.bucket:6.0f}s..{(b + 1) * args.bucket:6.0f}s] busy {tot:6.1f}s  {top}')
        print('  longest:')
        for ts, dur, name, a in sorted(evs, key=lambda x: -x[1])[:args.top]:
            src = ''
            if a and isinstance(a, dict):
                src = a.get('src_file') or a.get('src_func') or a.get('url') or ''
                if isinstance(src, str):
                    src = src[-60:]
            print(f'    +{(ts - t0) / 1e6:8.2f}s {dur / 1e6:7.3f}s {name} {src}')

    if args.netlog:
        print('\n== netlog URL_REQUEST events')
        for e in ev:
            if e.get('cat') != 'netlog':
                continue
            n = e.get('name', '')
            if n in ('REQUEST_ALIVE', 'URL_REQUEST_START_JOB', 'HTTP_TRANSACTION_READ_HEADERS',
                     'URL_REQUEST_DELEGATE_RESPONSE_STARTED', 'HTTP_STREAM_REQUEST'):
                a = e.get('args', {}).get('params', {})
                print(f'  +{(e["ts"] - t0) / 1e6:8.2f}s {e.get("ph")} {n} id={e.get("id")} '
                      f'{a.get("url", "")[:100] if isinstance(a, dict) else ""}')


if __name__ == '__main__':
    sys.exit(main())
