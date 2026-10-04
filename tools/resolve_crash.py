"""Resolves the harness crash log in ei_hook.log to function names.

The harness logs fatal exceptions with the code addresses found on the stack: game.exe addresses
as is, ei_hook.dll ones as ei_hook+offset. This prints each with the nearest ei_hook.map symbol
(undecorated) or the config/functions.csv name for game.exe addresses.

usage: python tools/resolve_crash.py [build/verify]   (defaults to build/verify; last crash only)
"""
import bisect, csv, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_map(path):
    syms = []
    base = 0x10000000
    for line in open(path, errors='replace'):
        m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
        if m:
            base = int(m.group(1), 16)
        m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s', line)
        if m and int(m.group(2), 16) >= base:
            syms.append((int(m.group(2), 16) - base, m.group(1)))
    syms.sort()
    return syms


def load_game_functions():
    starts, names = [], []
    path = os.path.join(ROOT, 'config', 'functions.csv')
    rows = sorted((int(r['address'], 16), r.get('name') or '') for r in csv.DictReader(open(path)))
    for a, n in rows:
        starts.append(a)
        names.append(n)
    return starts, names


def undecorate(names):
    try:
        out = subprocess.run(['undname'] + names, capture_output=True, text=True).stdout
        found = re.findall(r'is :- "(.*)"', out)
        if len(found) == len(names):
            return found
    except OSError:
        pass
    return names


def main():
    # The argument is the folder holding ei_hook.log: build/verify (default) or one run's folder
    # from run_all.py (build/verify/runs/<scenario>-<install>). The map is always the build's.
    logdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'verify')
    build = logdir if os.path.exists(os.path.join(logdir, 'ei_hook.map')) else os.path.join(ROOT, 'build', 'verify')
    lines = open(os.path.join(logdir, 'ei_hook.log'), errors='replace').read().splitlines()
    start = max((i for i, l in enumerate(lines) if 'harness: exception' in l), default=None)
    if start is None:
        print('no crash logged')
        return 1
    syms = load_map(os.path.join(build, 'ei_hook.map'))
    offsets = [s[0] for s in syms]
    gstarts, gnames = load_game_functions()
    out = []
    for line in lines[start:]:
        if line is not lines[start] and not re.search(r'\]\s+(in|at|stack) ', line):
            break
        m = re.search(r'ei_hook\+([0-9A-F]+)', line)
        g = re.search(r'([0-9A-F]{8}) \(game\.exe\)', line)
        if m:
            off = int(m.group(1), 16)
            i = bisect.bisect_right(offsets, off) - 1
            out.append((line, syms[i][1] if i >= 0 else '?', off - syms[i][0] if i >= 0 else 0))
        elif g:
            a = int(g.group(1), 16)
            i = bisect.bisect_right(gstarts, a) - 1
            out.append((line, gnames[i] or f'FUN_{gstarts[i]:08x}', a - gstarts[i]))
        else:
            out.append((line, None, 0))
    names = undecorate([n for _, n, _ in out if n and n.startswith('?')])
    it = iter(names)
    for line, name, delta in out:
        if name is None:
            print(line)
        else:
            print(f'{line}  {next(it) if name.startswith("?") else name}+0x{delta:X}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
