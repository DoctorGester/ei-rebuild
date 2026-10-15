r"""Where two random traces part, draw by draw (ticket SV1g).

usage: python tools/randtrace.py A B [--context N]
    A, B: run folders (a name under build\verify\runs, or a path) or randtrace.txt files, made with
    EI_HARNESS_CLOCK=virtual,randtrace (harness_clock.h). A is the reference (EI_PLAIN=1 or --no-hooks),
    B the run to check (the verify build).

Each traced draw carries the generator's state after it: ISAAC's position in its stream, rand()'s seed.
A verify wrapper that runs both sides puts the generators back in between, so B draws some positions
twice (ours, then the original, or the other way round) and its lines cannot be compared one by one.
This walks A's draws of the main thread in order and looks for each among B's draws of the same
generator position: the first that B makes in no frame or in another frame than A ("frame"), or only
from other callers ("caller", game.exe callers are the originals the wrappers run, so only ei_hook
callers count there), is printed with B's draws around it. ISAAC and rand() are walked separately.
exit code: 0 the same, 1 a difference, 2 a trace missing

usage: python tools/randtrace.py --kept B [A]
    Draws of a verify run that only an original made and that stayed: game.exe callers whose draw the
    generator went on from (the next draw continues the stream) while no rewrite (ei_hook caller) drew
    the same position. Those are originals a verify wrapper ran on live state after (or instead of)
    ours with the generators left as the original moved them (SV1a: 005301B0 from 0052EF90's re-read).
    Callers that A (the plain run) also draws from (originals not rewritten) are left out. One line per
    caller: how many draws stayed, the first frame and position.
"""
import argparse, bisect, collections, os, re, sys

import resolve_crash, run_all

LINE = re.compile(r'^(\d+) (\(other thread\) )?(\S+) ([0-9A-F]{8})( rand\(\)| \(another generator\))? #([0-9A-F]+)$')


def trace_path(spec):
    for path in (spec, os.path.join(run_all.BUILD, 'runs', spec)):
        if os.path.isfile(path):
            return path
        if os.path.isdir(path) and os.path.exists(os.path.join(path, 'randtrace.txt')):
            return os.path.join(path, 'randtrace.txt')
    return None


def read(path):
    """The main thread's draws: (line number, frame, caller, value, generator, position)."""
    draws = []
    with open(path, errors='replace') as f:
        for number, line in enumerate(f, 1):
            m = LINE.match(line.rstrip('\n'))
            if not m or m.group(2) or m.group(5) == ' (another generator)':
                continue
            generator = 'rand' if m.group(5) else 'isaac'
            draws.append((number, int(m.group(1)), m.group(3), m.group(4), generator, int(m.group(6), 16)))
    return draws


class Names:
    """Callers by name: ei_hook+offset from the build's ei_hook.map, game.exe ones from config/functions.csv."""

    def __init__(self):
        self.syms = resolve_crash.load_map(os.path.join(run_all.BUILD, 'ei_hook.map'))
        self.offsets = [s[0] for s in self.syms]
        self.gstarts, self.gnames = resolve_crash.load_game_functions()
        self.cache = {}

    def function(self, caller):
        """The function's name alone."""
        text = self(caller)
        return text[text.index('(') + 1:-1] if '(' in text else text

    def __call__(self, caller):
        if caller not in self.cache:
            if caller.startswith('ei_hook+'):
                i = bisect.bisect_right(self.offsets, int(caller[8:], 16)) - 1
                name = resolve_crash.undecorate([self.syms[i][1]])[0] if i >= 0 else '?'
                name = re.sub(r'^\S+ __\w+ ', '', name)
            else:
                a = int(caller, 16)
                i = bisect.bisect_right(self.gstarts, a) - 1
                name = (self.gnames[i] or f'FUN_{self.gstarts[i]:08x}') if i >= 0 else '?'
            self.cache[caller] = f'{caller} ({name[:90]})'
        return self.cache[caller]


def first_difference(a, b, generator):
    """A's first draw of `generator` that B does not make in the same frame (and from the same ei_hook caller)."""
    seen = collections.defaultdict(list)
    for d in b:
        if d[4] == generator:
            seen[d[5]].append(d)
    for d in a:
        if d[4] != generator:
            continue
        theirs = seen.get(d[5], [])
        frames = [t for t in theirs if t[1] == d[1]]
        if not frames:
            return d, 'frame' if theirs else 'missing', theirs
        if d[2].startswith('ei_hook+') and not any(t[2] == d[2] for t in frames):
            return d, 'caller', theirs
    return None


def follows(d, n):
    """Whether draw n continues the generator from draw d's state."""
    if d[4] == 'isaac':
        return n[5] == d[5] + 1
    return n[5] == (d[5] * 0x343FD + 0x269EC3) & 0xFFFFFFFF


def kept(path_b, path_a):
    b = read(path_b)
    plain_callers = {d[2] for d in read(path_a)} if path_a else set()
    names = Names()
    found = 0
    for generator in ('isaac', 'rand'):
        draws = [d for d in b if d[4] == generator]
        rewritten = {d[5] for d in draws if d[2].startswith('ei_hook+')}
        stayed = collections.OrderedDict()
        for d, n in zip(draws, draws[1:]):
            if d[2].startswith('ei_hook+') or d[2] in plain_callers or d[5] in rewritten or not follows(d, n):
                continue
            stayed.setdefault(d[2], []).append(d)
        for caller, ds in stayed.items():
            found += 1
            print(f'{generator}: {names(caller)}: {len(ds)} draw(s) stayed, first in frame {ds[0][1]} '
                  f'(line {ds[0][0]}, #{ds[0][5]:X})')
        # Draws taken back: a position that two different rewritten functions draw. A wrapper put the
        # generator back behind ours' draw (and did not set it to ours' state afterwards), so the
        # next function drew the same numbers again.
        by_position = collections.defaultdict(list)
        for d in draws:
            if d[2].startswith('ei_hook+'):
                by_position[d[5]].append(d)
        undone = collections.OrderedDict()
        for position, ds in by_position.items():
            functions = []
            for d in ds:
                f = names.function(d[2])
                if f not in functions:
                    functions.append(f)
            if len(functions) > 1:
                undone.setdefault(tuple(functions), []).append(ds[0])
        for functions, ds in undone.items():
            found += 1
            print(f'{generator}: drawn again by another function: {" then ".join(functions)}: {len(ds)} position(s), '
                  f'first in frame {ds[0][1]} (line {ds[0][0]}, #{ds[0][5]:X})')
    if not found:
        print('no original draw stayed without a rewrite drawing the same position')
    return 1 if found else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('a')
    parser.add_argument('b', nargs='?')
    parser.add_argument('--context', type=int, default=12, help="B's lines shown before and after (default 12)")
    parser.add_argument('--kept', action='store_true', help='original draws that stayed in one verify run')
    args = parser.parse_args()
    if args.kept:
        pb, pa = trace_path(args.a), trace_path(args.b) if args.b else None
        if not pb or (args.b and not pa):
            print(f'no random trace: {args.a if not pb else args.b}')
            return 2
        return kept(pb, pa)
    if not args.b:
        parser.error('two runs to compare, or --kept')
    pa, pb = trace_path(args.a), trace_path(args.b)
    if not pa or not pb:
        print(f'no random trace: {args.a if not pa else args.b}')
        return 2
    a, b = read(pa), read(pb)
    with open(pa, errors='replace') as f:
        lines_a = f.read().splitlines()
    with open(pb, errors='replace') as f:
        lines_b = f.read().splitlines()
    names = Names()
    result = 0
    for generator in ('isaac', 'rand'):
        found = first_difference(a, b, generator)
        count = sum(1 for d in a if d[4] == generator)
        if not found:
            print(f'{generator}: B makes all {count} of A\'s draws in the same frames')
            continue
        result = 1
        d, why, theirs = found
        callers = sorted({t[2] for t in theirs if t[1] == d[1]})
        print(f'{generator}: A line {d[0]}, frame {d[1]}, {names(d[2])} {d[3]} #{d[5]:X}: '
              + {'missing': 'B never draws it',
                 'frame': 'B draws it in frame(s) ' + ', '.join(sorted({str(t[1]) for t in theirs})),
                 'caller': f'B draws it {len(theirs)} time(s), in that frame only from '
                           + ', '.join(names(c) for c in callers)}[why])
        for n in range(max(1, d[0] - args.context), min(len(lines_a), d[0] + args.context) + 1):
            m = LINE.match(lines_a[n - 1])
            print(f'  A {n}: {lines_a[n - 1]}' + (f'  {names(m.group(3))}' if m else ''))
        # B's lines around its first draw of the position (or its last draw up to A's frame).
        before = [t for t in b if t[1] <= d[1]]
        at = theirs[0][0] if theirs else before[-1][0] if before else 1
        for n in range(max(1, at - args.context), min(len(lines_b), at + args.context) + 1):
            m = LINE.match(lines_b[n - 1])
            print(f'  B {n}: {lines_b[n - 1]}' + (f'  {names(m.group(3))}' if m else ''))
    return result


if __name__ == '__main__':
    sys.exit(main())
