"""What the standalone image reads of game.exe's .rdata and .data (C25): the image_trace.txt files of runs made with
EI_IMAGE_TRACE=1 (tests/verify/harness_image_trace.h), named and grouped.

Each line of a trace is the first access of a run to a nonzero dword of the sections' file bytes (0073B820-0079C000)
in the image's storage: "read" or "write", the game address, the code (H+offset in ei_hook.dll) and up to three
return addresses of ei_hook.dll. A dword is read when some run's first access to it is a read (a write first means
game.exe's value was never needed in that run). The cells (src/standalone/image_cells.inc, image_code_cells.inc) come
from the committed tables, not from game.exe's bytes: they are counted apart.

The reads that are no cells are what the image still takes from game.exe's bytes: the zero-filled trial
(gen_image_cells.py --bytes --zero) keeps exactly config/image_kept.csv's ranges, so every such read must lie in
one (--check), and a run of the image suite on the trial proves the list is enough.

usage: python tools/image_trace.py [RUN_FOLDER ...] [--build build/verify] [--check] [--csv OUT] [--dwords]
  (no folder)  every run folder under <build>/runs that has an image_trace.txt
  --check      exit 1 when a read that is no cell lies outside config/image_kept.csv's ranges; a kept range that
               holds no read is a note (a whole object is kept, also the parts these runs never reach)
  --csv OUT    the reads grouped into ranges, as rows for config/image_kept.csv (owner and what from the readers)
  --dwords     every read dword with its readers, not the ranges
"""
import argparse, collections, csv, glob, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_image_cells  # noqa: E402
from resolve_crash import load_map, short_name, undecorate  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINE = re.compile(r'^(read|write) ([0-9A-F]{8}) (\S+)((?: H\+[0-9A-F]+)*)\s*$')
RDATA_END = 0x00783000
GAP = 0x80   # bytes between two reads of one function that still make one range


class Names:
    """ei_hook.dll's code addresses (H+offset) by the map: the function and its object file."""

    def __init__(self, build):
        path = os.path.join(build, 'ei_hook.map')
        self.syms = load_map(path, code_only=True)
        self.starts = [s for s, _ in self.syms]
        self.objs = {}
        with open(path, errors='replace') as f:
            for line in f:
                m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+[0-9a-fA-F]{8}\s+f?\s*i?\s*(\S+\.obj)\s*$', line)
                if m:
                    self.objs.setdefault(m.group(1), m.group(2))
        self.cache = {}

    def at(self, code):
        """(function, object file) of 'H+offset', or (code, '') outside ei_hook.dll."""
        if not code.startswith('H+'):
            return code, ''
        rva = int(code[2:], 16)
        import bisect
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0:
            return code, ''
        raw = self.syms[i][1]
        if raw not in self.cache:
            self.cache[raw] = short_name(undecorate([raw], name_only=True)[0])
        return self.cache[raw], self.objs.get(raw, '').split(':')[-1]


LIBRARY = re.compile(r'^(memcpy|memmove|memset|memcmp|strlen|strcmp|strncmp|_strcmpi|strcpy|strcat|strchr|strstr|'
                     r'_?mbs\w*|tolower|toupper|is\w+|\w*Lib\w*|libcrt::\w+)$')


NOT_CALLER = re.compile(r'unwindfunclet|dynamic atexit destructor|dynamic initializer|^[0-9A-F]{8}$')


def readers(names, line):
    """The function at the access and the first caller that is no library function, as 'f' or 'f < caller'."""
    code = [line[2]] + line[3].split()
    first, obj = names.at(code[0])
    if not code[0].startswith('H+'):   # in a system DLL (a Win32 call given a string of the data): its caller
        first = f'{first} (system)'
    out = first
    for c in code[1:]:
        fn, o = names.at(c)
        if NOT_CALLER.search(fn):   # a stale stack word: a funclet, a compiler-made atexit thunk
            continue
        if fn != first:
            if not obj or LIBRARY.match(first.split('::')[-1]):
                obj = o
            out = f'{first} < {fn}'
            break
    return out, obj


def traces(folders):
    for folder in folders:
        path = os.path.join(folder, 'image_trace.txt')
        if os.path.exists(path):
            with open(path, errors='replace') as f:
                yield folder, [m.groups() for m in map(LINE.match, f) if m]


def owner_file(obj, files):
    if not obj:
        return '?'
    base = obj[:-4] if obj.endswith('.obj') else obj
    hits = files.get(base, [])
    return hits[0] if len(hits) == 1 else base


def source_files():
    files = collections.defaultdict(list)
    for top in ('src', 'tests'):
        for path in glob.glob(os.path.join(ROOT, top, '**', '*.cpp'), recursive=True):
            files[os.path.basename(path)].append(os.path.relpath(path, ROOT).replace('\\', '/'))
    return files


def text_at(img, a, n):
    raw = bytes(img[a - gen_image_cells.BASE:a - gen_image_cells.BASE + n])
    return ''.join(chr(b) if 32 <= b < 127 else '.' for b in raw)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folders', nargs='*')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'verify'))
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--csv')
    ap.add_argument('--dwords', action='store_true')
    a = ap.parse_args()
    folders = a.folders or sorted(glob.glob(os.path.join(a.build, 'runs', '*')))
    names = Names(a.build)
    cells = gen_image_cells.committed_cells()
    read = collections.defaultdict(lambda: [set(), set(), set()])   # dword -> readers, owners, runs
    runs = 0
    for folder, lines in traces(folders):
        runs += 1
        for line in lines:
            if line[0] != 'read':
                continue
            who, obj = readers(names, line)
            entry = read[int(line[1], 16)]
            entry[0].add(who)
            entry[1].add(obj)
            entry[2].add(os.path.basename(folder))
    plain = {d: v for d, v in read.items() if d not in cells}
    print(f'image_trace: {runs} run(s) traced; {len(read)} dword(s) read, {len(read) - len(plain)} of them cells, '
          f'{len(plain)} from game.exe\'s bytes')
    files = source_files()
    img = None
    if a.csv or not a.check:
        import exe_path
        img = gen_image_cells.image(exe_path.find_exe())
    # Ranges: read dwords up to GAP bytes apart that share a reading function (a table read at its rows' fields,
    # a string), the span from the first to the last.
    def functions(d):
        return {w.split(' < ')[0] for w in plain[d][0]}

    ranges = []
    for d in sorted(plain):
        if ranges and d - ranges[-1][1] <= GAP and functions(d) & ranges[-1][3]:
            ranges[-1][1] = d + 4
            ranges[-1][2].append(d)
            ranges[-1][3] |= functions(d)
        else:
            ranges.append([d, d + 4, [d], functions(d)])
    ranges = [r[:3] for r in ranges]
    if a.dwords:
        for d in sorted(plain):
            who, objs, rs = plain[d]
            print(f'{d:08X} {len(rs):3} run(s)  ' + '; '.join(sorted(who))[:200])
    elif not a.check:
        for b, e, ds in ranges:
            who = sorted({w for d in ds for w in plain[d][0]})
            print(f'{b:08X}+{e - b:<5} {"rdata" if b < RDATA_END else "data "} "{text_at(img, b, min(e - b, 24))}"  '
                  + '; '.join(who)[:220])
    problems = 0
    if a.check:
        kept = gen_image_cells.kept_ranges()
        for d in sorted(plain):
            if not any(k <= d < k + n for k, n, _, _, _ in kept):
                print(f'image_trace: {d:08X} is read ({"; ".join(sorted(plain[d][0]))[:160]}) but not in config/image_kept.csv')
                problems += 1
        for k, n, _, _, what in kept:
            if not any(k <= d < k + n for d in plain):
                print(f'image_trace: note: config/image_kept.csv {k:08X}+{n} ({what[:60]}) holds no read of these runs')
        print(f'image_trace: {problems} problem(s)')
    if a.csv:
        with open(a.csv, 'w', newline='') as f:
            w = csv.writer(f, lineterminator='\n')
            w.writerow(['address', 'size', 'owner', 'kind', 'what'])
            for b, e, ds in ranges:
                owners = sorted({owner_file(o, files) for d in ds for o in plain[d][1]})
                who = sorted({x for d in ds for x in plain[d][0]})
                kind = ('rdata ' if b < RDATA_END else 'data ') + ('text' if all(
                    32 <= c < 127 or c in (0, 9, 10, 13) for c in img[b - gen_image_cells.BASE:e - gen_image_cells.BASE])
                    else 'values')
                w.writerow([f'{b:08X}', e - b, owners[0] if len(owners) == 1 else 'several files: ' + ' '.join(owners),
                            kind, '; '.join(who)])
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
