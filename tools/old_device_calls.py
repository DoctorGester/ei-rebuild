"""Lists the old device's calling places from the switch-over's runs (ticket R9b, renderer.md section 9).

Without DirectDraw the device object holds two stand-ins (src/game/device_null.cpp) that log each
method's first call from each calling place ("old device: IDirect3DDevice7::SetRenderState from
game.exe 0050B22E" or "... from ei_hook.dll+1E5BF3"). This collects those lines from run folders,
resolves ei_hook.dll offsets with the build's ei_hook.map (game.exe addresses with
config/functions.csv) and prints them grouped by calling function, with the number of distinct
calling places.

usage: python tools/old_device_calls.py [run folders...]
       (default: every build/verify/runs/*present* folder)
"""
import bisect, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from resolve_crash import ROOT, load_map, load_game_functions, undecorate  # noqa: E402


def main():
    folders = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'build', 'verify', 'runs', '*present*')))
    syms = load_map(os.path.join(ROOT, 'build', 'verify', 'ei_hook.map'))
    offsets = [s[0] for s in syms]
    gstarts, gnames = load_game_functions()
    places = {}   # (method, place) -> (function, set of runs)
    for folder in folders:
        path = os.path.join(folder, 'ei_hook.log')
        if not os.path.exists(path):
            continue
        run = os.path.basename(os.path.normpath(folder))
        for line in open(path, errors='replace'):
            m = re.search(r'old device: (\S+) from (game\.exe ([0-9A-F]{8})|ei_hook\.dll\+([0-9A-F]+))', line)
            if not m:
                continue
            if m.group(3):
                a = int(m.group(3), 16)
                i = bisect.bisect_right(gstarts, a) - 1
                function = gnames[i] or f'FUN_{gstarts[i]:08x}'
                function = f'game.exe {gstarts[i]:08X} {function}'
            else:
                off = int(m.group(4), 16)
                i = bisect.bisect_right(offsets, off) - 1
                function = syms[i][1] if i >= 0 else '?'
            places.setdefault((m.group(1), m.group(2)), [function, set()])[1].add(run)
    names = sorted({f for f, _ in places.values() if f.startswith('?')})
    readable = dict(zip(names, undecorate(names)))
    byfunction = {}
    for (method, place), (function, runs) in places.items():
        byfunction.setdefault(readable.get(function, function), []).append((method, place, runs))
    for function in sorted(byfunction):
        print(function)
        for method, place, runs in sorted(byfunction[function]):
            print(f'    {method:45} {place:24} {len(runs)} run(s)')
    print(f'{len(places)} calling places in {len(byfunction)} functions ({len(folders)} run folders)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
