"""The coverage list (SV5, docs/notes/standalone-verification.md): which rewritten functions the suite
reaches, and whether a golden run (SV3) or function vectors (SV4) check them without the original exe.

Reach (tests/coverage/reach.txt): which scenarios run each rewritten function's original. Since A-I23 it comes
from runs of the original code (--no-hooks) with `EI_COVER=<file>`: every address the file lists (`addresses`
writes every rewrite's) gets a one-shot INT3 (src/hook/cover.cpp) and the run's ei_hook.log names each one the
run executed (a call the original inlined is not seen, so "reached" is a lower bound for small functions).
Scenarios collected before S21c came from game.exe with our hooks (EI_COVER=hooks: the hook addresses and our
replacements' first bytes); `collect --add` keeps them until their scenarios run again.

    python tools\\coverage.py addresses build\\cover.txt       (the watch list: every rewrite's address)
    set EI_COVER=<full path of that file>
    python tools\\work.py test <ID> [scenarios] --no-hooks --vanilla-only --fresh
    python tools\\coverage.py collect build\\verify\\runs --add  (reach.txt: these runs' scenarios replaced)

    python tools\\coverage.py list                          (writes tests/coverage/coverage.csv from
                                                            reach.txt, the sources, goldens and vectors)
    python tools\\coverage.py who <address or name>          (the scenarios that reach a function)
    python tools\\coverage.py top [N] [--area A]             (the uncovered ones by risk)

`list` needs no games: rerun it after adding a golden, vectors or a rewrite.

Status of a function (coverage.csv):
  golden         a scenario with a vanilla golden reaches it
  vector         no golden scenario reaches it, but replayed vectors check it
  suite          only scenarios without a golden reach it (nothing checks it against the original
                 until those scenarios get goldens)
  unreached      installed, but no scenario reaches it
  not-installed  rewritten, but the hook is not installed on vanilla (a SKIP in the log)
  startup, dead  unreached, and tests/coverage/unreachable.txt says why no scenario can reach it
Risk (for suite and unreached): size in bytes x area weight (game state 3, network / interface /
library 2, drawing and sound 1) x 2 when unreached.
"""
import argparse, csv, glob, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'tests', 'coverage')
REACH = os.path.join(OUT, 'reach.txt')
UNREACHABLE = os.path.join(OUT, 'unreachable.txt')
CSV = os.path.join(OUT, 'coverage.csv')
GOLDEN = os.path.join(ROOT, 'tests', 'golden')
VECTORS = os.path.join(ROOT, 'tests', 'vectors')
INSTALL = 'vanilla'

STATE = ('unit', 'world', 'ai', 'script', 'map', 'combat', 'item', 'player', 'spell', 'party', 'object',
         'quest', 'lever', 'effects', 'trap', 'magic', 'mob', 'torch', 'link', 'logic', 'server', 'sector',
         'random', 'maths', 'quaternion', 'matrix', 'core', 'game', 'class', 'chunk', 'base', 'global',
         'small', 'weather', 'trader', 'acks', 'animation', 'aligned', 'hero', 'camp', 'save', 'zone')
MIDDLE = ('net', 'ui', 'console', 'options', 'text', 'string', 'resource', 'ini', 'registry', 'hash',
          'app', 'win', 'sys', 'lib')
SOUND_DRAW = ('device', 'renderer', 'render', 'texture', 'figure', 'scene', 'object3d', 'terrain', 'camera',
              'sky', 'skin', 'model', 'material', 'dx', 'present', 'clip', 'view', 'particle', 'lightning',
              'ground', 'bink', 'buffered', 'dib', 'sound', 'engine', 'light', 'mesh', 'font', 'movie')


def area_weight(path):
    if not path:
        return 2
    if path.startswith('src/lib/'):
        return 2
    if path.startswith('src/render/'):
        return 1
    stem = os.path.basename(path).split('.')[0]
    head = stem.split('_')[0]
    if head == 'object3d' or stem.startswith('object3d'):
        return 1
    for words, weight in ((STATE, 3), (SOUND_DRAW, 1), (MIDDLE, 2)):
        if head in words:
            return weight
    return 2


def rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def sources(pattern):
    for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
        text = open(path, encoding='utf-8', errors='replace').read()
        yield rel(path), text


def rewrites():
    """address -> (name, file) of every EI_REPLACE / EI_REPLACE_METHOD with a literal address."""
    found = {}
    rx = re.compile(r'\bEI_REPLACE(?:_METHOD|_DISPATCH)?\(\s*0x([0-9A-Fa-f]{8})\s*,\s*&?\s*([^)\s]+)\s*\)')
    for path, text in sources('src/**/*.cpp'):
        for m in rx.finditer(text):
            found.setdefault(int(m.group(1), 16), (m.group(2), path))
    return found


def function_sizes():
    sizes = {}
    text = open(os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc'), encoding='utf-8').read()
    for m in re.finditer(r'\{0x([0-9A-Fa-f]{8}),\s*(\d+)\}', text):
        sizes[int(m.group(1), 16)] = int(m.group(2))
    return sizes


def vector_files():
    """function name -> records, for tests/vectors/data."""
    files = {}
    for path in glob.glob(os.path.join(VECTORS, 'data', '**', '*.vec'), recursive=True):
        data = open(path, 'rb').read(96)
        name = data[32:96].split(b'\0', 1)[0].decode()
        files[name] = files.get(name, 0) + struct.unpack_from('<I', data, 12)[0]
    return files


def recorders():
    """address -> the function name its vectors are written under (tests/vectors/record_*.cpp): the
    first sampler variable the recorder's body names."""
    found = {}
    for _, text in sources('tests/vectors/record_*.cpp'):
        names = {}   # sampler variable -> function name
        for m in re.finditer(r'\bRecorded<\w+>\s+([^;]*);', text):
            names.update(re.findall(r'(\w+)\("([^"]*)"\)', m.group(1)))
        names.update(re.findall(r'\bSampler\s+(\w+)\(\s*"[^"]*"\s*,\s*"([^"]*)"', text))
        def body(function):
            start = re.search(r'\b' + function + r'\([^)]*\)\s*\{', text)
            if not start:
                return ''
            depth, end = 1, start.end()
            while depth and end < len(text):
                depth += {'{': 1, '}': -1}.get(text[end], 0)
                end += 1
            return text[start.end():end]

        for m in re.finditer(r'\bEI_RECORD(?:_OURS)?\(\s*0x([0-9A-Fa-f]{8})\s*,\s*(\w+)\s*,', text):
            words = re.findall(r'\w+', body(m.group(2)))
            used = [w for w in words if w in names]
            if not used:   # a shared helper that names the sampler itself
                used = [w for f in words for w in re.findall(r'\w+', body(f)) if w in names]
            if used:
                found[int(m.group(1), 16)] = names[used[0]]
    return found


def unreachable():
    """address -> (status, reason) from tests/coverage/unreachable.txt."""
    found = {}
    if os.path.exists(UNREACHABLE):
        for line in open(UNREACHABLE, encoding='utf-8'):
            if line.strip() and not line.startswith('#'):
                addr, status, reason = line.split(None, 2)
                found[int(addr, 16)] = (status, reason.strip())
    return found


def golden_scenarios():
    return {os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(GOLDEN, '*', INSTALL))}


# ---- reach.txt ------------------------------------------------------------------------------------
# "# scenarios: a b c ..." then one line per installed hook address: "ADDRESS NAME BITS", BITS a hex
# bitset over the scenarios in that order (bit i = scenario i reaches it). NAME is the installed
# entry's name from the log (a verify wrapper's, where that build had one).

def read_reach():
    if not os.path.exists(REACH):
        sys.exit(f'{rel(REACH)} is missing (restore it from the repository, or collect the whole suite)')
    scenarios, rows = [], {}
    for line in open(REACH, encoding='utf-8'):
        if line.startswith('# scenarios:'):
            scenarios = line.split(':', 1)[1].split()
        elif line.strip() and not line.startswith('#'):
            addr, name, bits = line.split()
            mask = int(bits, 16)
            rows[int(addr, 16)] = (name, {s for i, s in enumerate(scenarios) if mask >> i & 1})
    return scenarios, rows


def write_addresses(args):
    """The watch list for EI_COVER: every rewrite's address and every address reach.txt has."""
    found = set(rewrites())
    if os.path.exists(REACH):
        found |= set(read_reach()[1])
    os.makedirs(os.path.dirname(os.path.abspath(args.file)), exist_ok=True)
    with open(args.file, 'w', encoding='ascii', newline='\n') as f:
        f.write('# tools/coverage.py addresses: the rewritten functions, for EI_COVER (src/hook/cover.cpp)\n')
        f.writelines(f'{a:08X}\n' for a in sorted(found))
    print(f'{len(found)} addresses; wrote {args.file}')


def collect(args):
    """reach.txt from runs of the original made with EI_COVER=<an addresses file>: the <scenario>-vanilla folders
    under args.runs; a pair's second game (peer\\) counts for its scenario."""
    cover_line = re.compile(r'\] cover ([0-9A-F]{8})\b')
    reached = {}
    for folder in sorted(glob.glob(os.path.join(args.runs, f'*-{INSTALL}'))):
        logs = [os.path.join(folder, 'ei_hook.log'), os.path.join(folder, 'peer', 'ei_hook.log')]
        texts = [open(p, encoding='cp1251', errors='replace').read() for p in logs if os.path.exists(p)]
        if not texts or not re.search(r'cover: \d+ address\(es\) from', texts[0]):
            continue
        scenario = os.path.basename(folder)[:-len(INSTALL) - 1]
        reached[scenario] = {int(a, 16) for t in texts for a in cover_line.findall(t)}
    if not reached:
        sys.exit(f'no {INSTALL} run under {args.runs} was made with EI_COVER')
    collected = len(reached)
    names = {a: n for a, (n, _) in rewrites().items()}
    if args.add and os.path.exists(REACH):
        old_scenarios, old = read_reach()
        for s in old_scenarios:
            if s not in reached:
                reached[s] = {a for a, (_, who) in old.items() if s in who}
        for a, (name, _) in old.items():
            names.setdefault(a, name)
    scenarios = sorted(reached)
    with open(REACH, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Written by tools/coverage.py collect from runs of the original (--no-hooks, vanilla) with EI_COVER=<the\n'
                "# rewrites' addresses>; scenarios last collected before S21c came from the hook build (EI_COVER=hooks).\n"
                '# ADDRESS NAME BITS: a rewritten function, and a hex bitset over the scenarios below (bit i: scenario i\n'
                '# reaches it). tools/coverage.py list turns it into coverage.csv.\n')
        f.write('# scenarios: ' + ' '.join(scenarios) + '\n')
        for a in sorted(names):
            mask = sum(1 << i for i, s in enumerate(scenarios) if a in reached[s])
            f.write(f'{a:08X} {names[a]} {mask:x}\n')
    hit = sum(1 for a in names if any(a in r for r in reached.values()))
    print(f'{len(scenarios)} scenarios ({collected} collected now), {len(names)} functions, {hit} reached; wrote {rel(REACH)}')


# ---- the list -------------------------------------------------------------------------------------

def build_rows():
    scenarios, reach = read_reach()
    names = rewrites()
    sizes = function_sizes()
    vec_names = recorders()
    vec_data = vector_files()
    goldens = golden_scenarios() & set(scenarios)
    known = unreachable()
    rows = []
    for a in sorted(set(names) | set(reach)):
        name, path = names.get(a, (None, ''))
        logged, who = reach.get(a, (None, set()))
        name = name or logged
        golden = sorted(who & goldens)
        if a in vec_names:
            records = vec_data.get(vec_names[a], 0)
            vector = f'{records} records' if records else 'no data'
        else:
            records, vector = 0, ''
        if a not in reach:
            status = 'not-installed'
        elif golden:
            status = 'golden'
        elif records:
            status = 'vector'
        elif who:
            status = 'suite'
        else:
            status = known.get(a, ('unreached',))[0]
        size = sizes.get(a, 0)
        risk = 0
        if status in ('suite', 'unreached'):
            risk = size * area_weight(path) * (2 if status == 'unreached' else 1)
        others = sorted(who - set(golden), key=lambda s: (s.startswith('watchdog'), s))
        rows.append({
            'address': f'{a:08X}', 'name': name, 'file': path, 'size': size,
            'status': status, 'risk': int(risk), 'scenarios': len(who), 'golden': ' '.join(golden[:4]) + (f' +{len(golden) - 4}' if len(golden) > 4 else ''),
            'vector': vector, 'reached_by': ' '.join(others[:3]) + (f' +{len(others) - 3}' if len(others) > 3 else ''),
        })
    return scenarios, goldens, rows


def write_list(args):
    scenarios, goldens, rows = build_rows()
    with open(CSV, 'w', encoding='utf-8', newline='') as f:
        f.write('# Written by tools/coverage.py list (SV5): per rewritten function, how the suite reaches it and what\n'
                '# checks it without the original exe. status: golden / vector / suite (only scenarios without a golden)\n'
                '# / unreached / not-installed (vanilla). risk: for suite and unreached, see tools/coverage.py.\n'
                '# golden: vanilla golden scenarios that reach it (first 4); reached_by: other scenarios (first 3).\n')
        w = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator='\n')
        w.writeheader()
        w.writerows(rows)
    counts = {}
    for r in rows:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    print(f'{len(rows)} rewritten functions over {len(scenarios)} scenarios ({len(goldens)} with a golden): ' +
          ', '.join(f'{counts.get(s, 0)} {s}' for s in ('golden', 'vector', 'suite', 'unreached', 'startup', 'dead', 'not-installed')))
    gaps = [r for r in rows if r['vector'] == 'no data']
    if gaps:
        print('vector checks without data: ' + ', '.join(f"{r['name']} ({r['status']})" for r in gaps))
    print(f'wrote {rel(CSV)}')


def who(args):
    scenarios, reach = read_reach()
    names = rewrites()
    goldens = golden_scenarios()
    key = args.function.upper().removeprefix('0X')
    hits = [a for a in reach if f'{a:08X}' == key.zfill(8)] or \
           [a for a in reach if args.function.lower() in (names.get(a, (reach[a][0],))[0] or '').lower()]
    if not hits:
        sys.exit(f'no installed hook matches {args.function}')
    for a in sorted(hits)[:20]:
        name = names.get(a, (reach[a][0],))[0]
        found = sorted(reach[a][1])
        tagged = [s + ('*' if s in goldens else '') for s in found]
        print(f'{a:08X} {name}: {len(found)} scenario(s){": " if found else ""}{" ".join(tagged)}')
    if any(reach[a][1] & goldens for a in hits):
        print('(* has a vanilla golden)')


def top(args):
    _, _, rows = build_rows()
    pick = [r for r in rows if r['risk'] and (not args.area or args.area in r['file'])]
    pick.sort(key=lambda r: -r['risk'])
    for r in pick[:args.count]:
        print(f"{r['risk']:7d} {r['status']:9s} {r['address']} {r['name'][:46]:46s} {r['size']:5d} "
              f"{r['file'][9:] if r['file'].startswith('src/game/') else r['file']}"
              + (f"  [{r['reached_by']}]" if r['reached_by'] else ''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('list')
    a = sub.add_parser('addresses')
    a.add_argument('file', help='where to write the watch list (EI_COVER names it)')
    c = sub.add_parser('collect')
    c.add_argument('runs', help='folder of runs (build\\verify\\runs)')
    c.add_argument('--add', action='store_true', help='keep the scenarios of reach.txt these runs do not have')
    w = sub.add_parser('who')
    w.add_argument('function', help='address (hex) or part of a name')
    t = sub.add_parser('top')
    t.add_argument('count', nargs='?', type=int, default=40)
    t.add_argument('--area', help='only files whose path contains this')
    args = ap.parse_args()
    {'list': write_list, 'addresses': write_addresses, 'collect': collect, 'who': who, 'top': top}[args.cmd](args)


if __name__ == '__main__':
    main()
