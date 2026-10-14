"""Time per function from a profiled harness run (EI_PROFILE=1, src/hook/profile.cpp).

usage: python tools/profile_report.py <run folder> [--top N] [--min-ms MS]

The run folder holds profile.txt and ei_hook.log. The run is cut at the harness steps in the log;
for every stretch of at least --min-ms the report lists where the main thread was (self: the
function it was in; total: functions anywhere on its stack, scanned, so a stale return address can
add a little). game.exe names come from config/functions.csv, ei_hook's from its .map file,
other modules show by name only.
"""
import argparse
import bisect
import collections
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ms_of_day(stamp):
    h, m, s = stamp.split(':')
    return (int(h) * 3600 + int(m) * 60) * 1000 + float(s) * 1000


class Symbols:
    def __init__(self, modules, map_path):
        self.modules = sorted(modules)   # (base, size, name)
        rows = list(csv.reader(open(os.path.join(ROOT, 'config', 'functions.csv'), encoding='utf-8')))[1:]
        self.game = sorted((int(r[0], 16), r[2]) for r in rows)
        self.game_starts = [a for a, _ in self.game]
        self.own, preferred = [], 0x10000000
        if map_path and os.path.exists(map_path):
            for line in open(map_path, encoding='latin-1'):
                m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
                if m:
                    preferred = int(m.group(1), 16)
                m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+f?\s', line)
                if m and int(m.group(2), 16):
                    self.own.append((int(m.group(2), 16), self.pretty(m.group(1))))
        self.own.sort()
        self.own_starts = [a for a, _ in self.own]
        self.own_base = next((b for b, s, n in self.modules if n.lower().endswith('ei_hook.dll')), None)
        self.preferred = preferred
        self.cache = {}

    @staticmethod
    def pretty(name):
        if name.startswith('?'):
            parts = name[1:].split('@@')[0].split('@')
            if parts and parts[0].startswith('?'):
                parts[0] = {'?0': '{ctor}', '?1': '{dtor}'}.get(parts[0][:2], parts[0])
            return '::'.join(reversed([p for p in parts if p]))
        return name.lstrip('_')

    def module(self, a):
        for base, size, name in self.modules:
            if base <= a < base + size:
                return base, name
        return None, '?'

    def name(self, a):
        if a in self.cache:
            return self.cache[a]
        result = None
        if 0x00401000 <= a < 0x00800000:
            i = bisect.bisect_right(self.game_starts, a) - 1
            result = 'game:' + (self.game[i][1] if i >= 0 else hex(a))
        else:
            base, module = self.module(a)
            if base is not None and base == self.own_base:
                v = a - base + self.preferred
                i = bisect.bisect_right(self.own_starts, v) - 1
                result = 'ours:' + (self.own[i][1] if i >= 0 else hex(a))
            else:
                result = os.path.basename(module)
        self.cache[a] = result
        return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run')
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--min-ms', type=float, default=400)
    args = ap.parse_args()
    start, modules, samples = None, [], []
    for line in open(os.path.join(args.run, 'profile.txt')):
        if line.startswith('S '):
            parts = line.split()
            samples.append((float(parts[1]), int(parts[2], 16), [int(p, 16) for p in parts[3:]]))
        elif line.startswith('M '):
            _, base, size, path = line.rstrip('\n').split(' ', 3)
            modules.append((int(base, 16), int(size, 16), path))
        elif line.startswith('T '):
            start = ms_of_day(line.split()[1])
    build = os.path.dirname(os.path.dirname(os.path.abspath(args.run)))
    symbols = Symbols(modules, os.path.join(build, 'ei_hook.map'))
    # The harness steps, as times since the profile started.
    marks = [(0.0, 'start (WinMain)')]
    for line in open(os.path.join(args.run, 'ei_hook.log'), encoding='latin-1'):
        m = re.match(r'\[(\d\d:\d\d:\d\d\.\d+)\] harness: (.*)', line)
        if m and not m.group(2).startswith(('saves and', 'steps from')) and re.match(r'\d+ steps', m.group(2)) is None:
            marks.append((ms_of_day(m.group(1)) - start, m.group(2)[:90]))
    end = samples[-1][0] if samples else 0
    marks.append((end, 'end of samples'))
    print(f'{len(samples)} samples over {end / 1000:.1f} s')
    for (t0, label), (t1, _) in zip(marks, marks[1:]):
        inside = [s for s in samples if t0 <= s[0] < t1]
        print(f'{t0 / 1000:7.2f} s  {t1 - t0:8.0f} ms  {label}')
        if t1 - t0 < args.min_ms or not inside:
            continue
        own, total = collections.Counter(), collections.Counter()
        for _, eip, callers in inside:
            leaf = symbols.name(eip)
            own[leaf] += 1
            names = {leaf} | {symbols.name(c) for c in callers}
            total.update(names)
        n = len(inside)
        print(f'      self ({n} samples):')
        for name, count in own.most_common(args.top):
            print(f'        {100 * count / n:5.1f}%  {name}')
        print('      total:')
        for name, count in total.most_common(args.top * 2):
            print(f'        {100 * count / n:5.1f}%  {name}')


if __name__ == '__main__':
    sys.exit(main())
