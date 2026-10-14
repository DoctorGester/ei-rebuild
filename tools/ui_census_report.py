"""Merges the interface drawing census of harness runs (ui_census.csv, tests/verify/ui_census.cpp,
ticket N0) into one table: window x primitive x call site, Rebirth's (addon.dll) code marked apart.

usage: python tools/ui_census_report.py [run folders or ui_census.csv files...]
                                        [--out build/verify/ui_census_merged.csv] [--summary]
  default input: every build/verify/runs/*/ui_census.csv. A run folder is <scenario>-<install>.
  --summary   also print a short per-window summary, the surfaces' frame counts, the readers and the
              changes no hooked call explains (the source of docs/notes/ui-census.md)

Call chains in the CSVs are code addresses: game.exe as is (named from config/functions.csv),
E+offset in ei_hook.dll (named from build/verify/ei_hook.map, and given the game address the
function replaces from the EI_REPLACE lines in src/), A+offset in addon.dll. The call site of a row
is the first frame that is not MFC / library code and not one of the surface helpers (GetDC,
DrawTextIn, the fills, the text extent); the helpers passed on the way are its "via".

Verify wrappers run a rewritten function's original body as well as ours, so a call made from game
code inside a rewritten function (up to the site) is that duplicate: it is counted apart
("verify_dup") and left out of the counts. In a Rebirth run the functions Rebirth patches run their
game code for real (docs/rebirth-patched-functions.txt), so they are not taken for duplicates there.
"""
import argparse, bisect, collections, csv, glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import resolve_crash  # noqa: E402  (load_map)

# The surface helpers a drawing call passes through on its way to GDI (game addresses).
WRAPPERS = {
    0x005FDC00: 'GetDC', 0x005FDC60: 'ReleaseDC', 0x005FDE20: 'DrawTextIn', 0x005FDFA0: 'DrawWrappedText',
    0x005FE370: 'Fill', 0x005FE2F0: 'Clear', 0x005FDCB0: 'FrameRect', 0x005F8340: 'TextExtent',
    0x005F7CA0: 'ScaleRect', 0x005F7E30: 'ScaleBack',
    # the whole-surface pixel helpers (the census's g_uiSurfacePixelObserver rows)
    0x005FD3D0: 'CaptureScreen', 0x005FD440: 'Rgb565Copy', 0x005FE440: 'Greyscale', 0x005FD240: 'PixelAt',
    0x005FD7A0: 'SwapContents',
}
# ei_hook.dll functions that are plumbing, not callers (matched on the undecorated name).
HELPER_NAMES = re.compile(r'GameDC::|SurfaceDC::|CallSlot|SurfaceDrawText|SurfaceDraw::|ui_gdi::|census::|'
                          r'SurfaceGetDC|SurfaceReleaseDC|SurfaceFill|SurfaceClear|SurfaceFrameRect|'
                          r'GameFont::|GamePen::|GameBrush::|ComCall|LockSurface|UnlockSurface|\bBlt\b|'
                          r'Verify|RawBytes|ReadLevels|FillLevels|lambda|CreateFontA|ei::verify|std::|'
                          r'ObservePixels|CUiSurface::(CaptureScreen|Rgb565Copy|Greyscale|SwapContents)')
# The verify wrappers' own pixel helpers (tests/verify): they lock and read surfaces to compare the
# original body with ours, so their reads are not the game's, and a chain that passes one is inside a
# verify wrapper whose bodies run more than once (its counts are high by up to 3-4x).
VERIFY_ONLY = re.compile(r'\b(AccessPixels|CopySurfaceDC|WindowBytes|DiffPixels|FirstDifferentRow|CompareDrawing)\b')
# A whole-surface pixel helper's row (g_uiSurfacePixelObserver) that a verify wrapper's CompareDrawing
# called is still the game's call: the wrapper hooks the game's own call, runs the original body
# (which the observer does not see) and ours once, so ours is that call, not an extra one.
PIXEL_HELPERS = re.compile(r'(CaptureScreen|Rgb565Copy|Greyscale|PixelAt|SwapContents)')
VERIFY_ONLY_PIXEL_HELPER = re.compile(r'(AccessPixels|CopySurfaceDC|WindowBytes|DiffPixels|FirstDifferentRow)')
KINDS_ORDER = ['text', 'draw', 'write', 'read', 'metric', 'state', 'font', 'dc', 'uncaught']


def load_game_functions():
    rows = []
    for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'functions.csv'))):
        if r['kind'] == 'eh':
            continue
        rows.append((int(r['address'], 16), int(r['size_est'] or 0), r['name'], r['kind']))
    rows.sort()
    return [r[0] for r in rows], rows


def load_replacements(bare_only=False):
    """EI_REPLACE / EI_REPLACE_METHOD lines: function name -> game address. `bare_only`: the names
    written without a class, as written (no aliases)."""
    out = {}
    short = collections.defaultdict(set)
    pattern = re.compile(r'EI_REPLACE(?:_METHOD)?\(\s*(0x[0-9A-Fa-f]+)\s*,\s*([\w:]+)\s*\)')
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        for m in pattern.finditer(open(path, encoding='utf-8', errors='replace').read()):
            out.setdefault(m.group(2), int(m.group(1), 16))
            short[m.group(2).split('::')[-1]].add(int(m.group(1), 16))
    if bare_only:
        return {name: address for name, address in out.items() if '::' not in name}
    for name, addresses in short.items():   # a bare name only when it is not ambiguous
        if len(addresses) == 1:
            out.setdefault(name, next(iter(addresses)))
    return out


def load_rebirth_patched():
    out = set()
    path = os.path.join(ROOT, 'docs', 'rebirth-patched-functions.txt')
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'([0-9A-F]{8})\s', line)
        if m:
            out.add(int(m.group(1), 16))
    return out


def plain_name(decorated):
    """?Name@Class@ns@@... -> ns::Class::Name (enough to read; no undname needed)."""
    if not decorated.startswith('?'):
        return decorated.lstrip('_').split('@')[0]
    body = decorated[1:]
    if body.startswith('?'):
        return decorated
    parts = body.split('@@')[0].split('@')
    parts = [p for p in parts if p and not p.startswith('?')]
    return '::'.join(reversed(parts)) if parts else decorated


class Resolver:
    def __init__(self):
        self.starts, self.rows = load_game_functions()
        map_path = os.path.join(ROOT, 'build', 'verify', 'ei_hook.map')
        self.syms = resolve_crash.load_map(map_path) if os.path.exists(map_path) else []
        self.offsets = [s[0] for s in self.syms]
        self.replaced = load_replacements()
        # A function of ours with a bare name (no class) is a replacement only when EI_REPLACE names it
        # so: the bare aliases of qualified names (Send for SessionBase::Send) would make every
        # function called Send, a lambda in an anonymous namespace among them, the game's.
        self.replaced_bare = load_replacements(bare_only=True)
        self.replaced_addresses = set(self.replaced.values())
        self.our_names = {}
        for name, address in self.replaced.items():
            if '::' in name or address not in self.our_names:
                self.our_names[address] = name
        self.rebirth_patched = load_rebirth_patched()
        self.cache = {}

    def frame(self, token):
        """(label, game address or None, category): category game / lib / ei / helper / addon / other."""
        if token in self.cache:
            return self.cache[token]
        out = (token, None, 'other')
        if re.fullmatch(r'[0-9A-F]{8}', token):
            a = int(token, 16)
            i = bisect.bisect_right(self.starts, a) - 1
            if i >= 0:
                start, size, name, kind = self.rows[i]
                if a < start + max(size, 1) + 16:
                    category = 'lib' if kind in ('lib', 'thunk') else 'game'
                    if start in WRAPPERS:
                        category = 'wrapper'
                    name = self.our_names.get(start, name)
                    out = (f'{start:08X} {name}', start, category)
                else:
                    out = (token, a, 'game')
        elif token.startswith('E+'):
            off = int(token[2:], 16)
            i = bisect.bisect_right(self.offsets, off) - 1
            if i >= 0:
                name = plain_name(self.syms[i][1])
                game = (self.replaced if '::' in name else self.replaced_bare).get(name)
                if game in WRAPPERS:
                    out = (f'{game:08X} {self.our_names.get(game, name)}', game, 'wrapper')
                elif game:
                    out = (f'{game:08X} {self.our_names.get(game, name)}', game, 'ei')
                else:
                    out = (f'ei:{name}', None, 'helper' if HELPER_NAMES.search(name) else 'ei')
        elif token.startswith('A+'):
            out = ('addon+' + token[2:], None, 'addon')
        self.cache[token] = out
        return out

    def site(self, chain, install='vanilla', skipped=frozenset(), primitive=''):
        """(site, via, rebirth, duplicate, verify): the first caller frame (with the rewritten or game
        function it lies in when it is a helper of ours without a game address), the surface
        helpers before it, addon code involved, whether the call ran in a verify wrapper's
        original body, and whether a verify pixel helper is on the stack."""
        tokens = [t for t in chain.split() if t and t != '?']
        via = []
        rebirth = duplicate = False
        only = VERIFY_ONLY_PIXEL_HELPER if PIXEL_HELPERS.match(primitive) else VERIFY_ONLY
        verify = any(only.search(self.frame(t)[0]) for t in tokens)
        site = None
        for i, token in enumerate(tokens):
            label, address, category = self.frame(token)
            if VERIFY_ONLY.search(label):
                continue
            if (re.fullmatch(r'[0-9A-F]{8}', token) and address in self.replaced_addresses and
                    not (install == 'rebirth' and address in self.rebirth_patched) and address not in skipped):
                duplicate = True
            if category == 'addon':
                rebirth = True
                site = label
                break
            if category in ('game', 'ei'):
                site = label
                if address is None:   # one of ours with no game address: name the function it is in
                    for later in tokens[i + 1:i + 4]:
                        outer, outer_address, outer_category = self.frame(later)
                        if (outer_address is not None and outer_category in ('game', 'ei') and
                                not VERIFY_ONLY.search(outer)):
                            site += ' < ' + outer
                            break
                break
            if category == 'wrapper' and WRAPPERS[address] not in via:
                via.append(WRAPPERS[address])
        if site is None:
            site = self.frame(tokens[0])[0] if tokens else '?'
        return site, ' > '.join(via), rebirth, duplicate, verify


def window_names():
    """Window vtable (hex) -> name: the harness's waitwindow names, then class names found beside the
    vtable in src/game."""
    names = {}
    text = open(os.path.join(ROOT, 'tests', 'verify', 'harness.cpp'), encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'\{"(\w+)", \{0x([0-9A-Fa-f]{8}), 0x([0-9A-Fa-f]{8})\}\}', text):
        names[m.group(2).upper()] = m.group(1)
        names[m.group(3).upper()] = m.group(1)
    return names


def class_beside(vtable, cache={}):
    """The class or the comment naming a vtable in src/game: a header comment that gives the vtable
    and the class declared under it, else the comment's own words."""
    if vtable in cache:
        return cache[vtable]
    found = None
    for path in glob.glob(os.path.join(ROOT, 'src', 'game', '*.h')):
        lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
        for i, line in enumerate(lines):
            if vtable not in line.upper() or not line.lstrip().startswith('//') or 'vtable' not in line:
                continue
            for later in lines[i + 1:i + 25]:
                m = re.match(r'\s*(?:class|struct)\s+(\w+)\s*[:{]', later)
                if m:
                    found = m.group(1)
                    break
            if not found:
                words = line.lstrip('/ ').split('(')[0].strip()
                found = words[:48] or None
            break
        if found:
            break
    cache[vtable] = found
    return found


def owner_name(owner, names):
    m = re.match(r'W([0-9A-F]{8})#(-?\d+)(.*)', owner)
    if not m:
        return owner
    vtable = m.group(1)
    name = names.get(vtable) or class_beside(vtable) or 'window'
    return f'{name} [{vtable}] #{m.group(2)}{m.group(3)}'


def read_runs(paths):
    files = []
    for p in paths or [os.path.join(ROOT, 'build', 'verify', 'runs')]:
        if os.path.isdir(p):
            direct = os.path.join(p, 'ui_census.csv')
            files += [direct] if os.path.exists(direct) else glob.glob(os.path.join(p, '*', 'ui_census.csv'))
        elif os.path.exists(p):
            files.append(p)
    runs = []
    for f in sorted(files):
        folder = os.path.basename(os.path.dirname(f))
        lines = open(f, encoding='cp1251', errors='replace').read().splitlines()
        header = lines[0] if lines and lines[0].startswith('#') else ''
        m = re.search(r'install=(\w+)', header)
        install = m.group(1) if m else folder.rsplit('-', 1)[-1]
        scenario = folder.rsplit('-', 1)[0] if '-' in folder else folder
        rows = list(csv.DictReader([l for l in lines if not l.startswith('#')]))
        runs.append((scenario, install, rows, skipped_hooks(os.path.dirname(f))))
    return runs


def skipped_hooks(folder):
    """Game addresses whose hook the run left out ("SKIP <name> at <address>: ..." in its
    ei_hook.log: patched by EI_Plugin.asi, by a Rebirth patch we do not implement, or by another
    mod). Only the game code runs there, so it is not a verify wrapper's second run (CChatWindow::
    Update 006095A0 under Rebirth, which EI_Plugin.asi patches)."""
    out = set()
    log = os.path.join(folder, 'ei_hook.log')
    if os.path.exists(log):
        for line in open(log, encoding='cp1251', errors='replace'):
            m = re.search(r'\bSKIP \S+ at ([0-9A-F]{8}):', line)
            if m:
                out.add(int(m.group(1), 16))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inputs', nargs='*')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'verify', 'ui_census_merged.csv'))
    ap.add_argument('--summary', action='store_true')
    args = ap.parse_args()
    runs = read_runs(args.inputs)
    if not runs:
        print('no ui_census.csv found')
        return 1
    res = Resolver()
    names = window_names()

    merged = collections.OrderedDict()
    surfaces = collections.defaultdict(lambda: collections.Counter())
    surface_runs = collections.defaultdict(set)
    for scenario, install, rows, skipped in runs:
        for r in rows:
            owner = owner_name(r['owner'], names)
            if r['kind'] == 'surface':
                stats = dict(kv.split('=') for kv in r['detail'].split(';') if '=' in kv)
                c = surfaces[(owner, r['size'], install)]
                c['sessions'] += int(r['count'])
                c['drawn'] += int(r['frames'])
                for k, v in stats.items():
                    c[k] = max(c[k], int(v)) if k == 'maxrun' else c[k] + int(v)
                surface_runs[(owner, r['size'], install)].add(scenario)
                continue
            site, via, rebirth, duplicate, verify = res.site(r['chain'], install, skipped, r['primitive'])
            if verify:
                site += ' [in verify]'
            key = (owner, '', r['primitive'], r['kind'], site, via)
            e = merged.setdefault(key, {'vanilla': 0, 'rebirth': 0, 'other': 0, 'dup': 0, 'frames': 0,
                                        'scenarios': set(), 'details': [], 'addon': False})
            if duplicate:
                e['dup'] += int(r['count'])
            else:
                e[install if install in ('vanilla', 'rebirth') else 'other'] += int(r['count'])
            e['frames'] = max(e['frames'], int(r['frames']))
            e['scenarios'].add(scenario)
            e['addon'] |= rebirth
            for d in r['detail'].split(' | '):
                if d and d not in e['details'] and len(e['details']) < 6:
                    e['details'].append(d)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['owner', 'size', 'primitive', 'kind', 'site', 'via', 'rebirth', 'vanilla_count', 'rebirth_count',
                    'verify_dup', 'max_frames_in_a_run', 'scenarios', 'details'])
        for (owner, size, prim, kind, site, via), e in sorted(merged.items()):
            mark = 'addon' if e['addon'] else ('rebirth only' if e['rebirth'] and not e['vanilla'] else '')
            w.writerow([owner, size, prim, kind, site, via, mark, e['vanilla'], e['rebirth'], e['dup'], e['frames'],
                        ' '.join(sorted(e['scenarios'])), ' | '.join(e['details'])])
    print(f'{len(runs)} runs, {len(merged)} rows -> {args.out}')
    if not args.summary:
        return 0

    # Per owner: primitives (draw kinds) with counts and their sites.
    print('\n== drawing per surface (text / draw / write kinds) ==')
    by_owner = collections.defaultdict(list)
    for (owner, size, prim, kind, site, via), e in merged.items():
        if kind in ('text', 'draw', 'write') and not (kind == 'write' and site.endswith('[in verify]')):
            by_owner[(owner, size)].append((prim, site, via, e))
    for (owner, size), items in sorted(by_owner.items()):
        print(f'{owner} {size}')
        prims = collections.defaultdict(lambda: [0, 0, set(), False])
        for prim, site, via, e in items:
            p = prims[prim]
            p[0] += e['vanilla']
            p[1] += e['rebirth']
            p[2].add(site + (f' (via {via})' if via else ''))
            p[3] |= e['addon']
        for prim, (v, rb, sites, addon) in sorted(prims.items()):
            if not v and not rb:
                continue   # only verify wrappers' second calls
            print(f'    {prim:24} vanilla {v:7} rebirth {rb:7}{"  ADDON" if addon else ""}')
            for s in sorted(sites)[:6]:
                print(f'        {s}')
            if len(sites) > 6:
                print(f'        ... {len(sites) - 6} more')

    print('\n== metrics (text measurement, device caps) by DC and site ==')
    metrics = collections.Counter()
    for (owner, size, prim, kind, site, via), e in merged.items():
        if kind == 'metric':
            metrics[(prim, owner if owner.startswith('(') else 'surface DC', site, via)] += e['vanilla'] + e['rebirth']
    for (prim, owner, site, via), n in metrics.most_common():
        print(f'    {n:8} {prim:24} {owner:28} {site}{" (via " + via + ")" if via else ""}')

    print('\n== fonts, pens and brushes made ==')
    for (owner, size, prim, kind, site, via), e in sorted(merged.items()):
        if kind == 'font':
            print(f'    {prim:20} {e["vanilla"]:5} {e["rebirth"]:5} {site}  {" | ".join(e["details"])}')

    print('\n== readers (not the per-frame blit 005FDA80, not ReleaseDC\'s empty lock, not verify helpers) ==')
    verify_reads = collections.Counter()
    for (owner, size, prim, kind, site, via), e in sorted(merged.items()):
        if kind == 'read' and site.endswith('[in verify]'):
            verify_reads[owner] += e['vanilla'] + e['rebirth']
        elif kind == 'read' and not site.startswith('005FDA80') and 'ReleaseDC' not in via:
            print(f'    {owner} {size}: {prim} by {site}{" (via " + via + ")" if via else ""} '
                  f'v{e["vanilla"]} r{e["rebirth"]}  {" | ".join(e["details"][:2])}')

    if verify_reads:
        print('    verify helpers\' reads: ' + ', '.join(f'{o} {n}' for o, n in verify_reads.most_common()))

    print('\n== changes no hooked call explains ==')
    for (owner, size, prim, kind, site, via), e in sorted(merged.items()):
        if kind == 'uncaught':
            print(f'    {owner} {size}: {prim} v{e["vanilla"]} r{e["rebirth"]} last session from {site} '
                  f'({" ".join(sorted(e["scenarios"]))})')

    print('\n== surfaces: frames alive / drawn / longest run of drawn frames ==')
    for (owner, size, install), c in sorted(surfaces.items()):
        every = ' EVERY FRAME' if c['alive'] >= 30 and c['maxrun'] >= 30 else ''
        print(f'    {install:8} {owner} {size}: alive {c["alive"]} drawn {c["drawn"]} changed {c["changed"]} '
              f'maxrun {c["maxrun"]} sessions {c["sessions"]} uncaught {c["uncaught_session"]}/{c["uncaught_frame"]}'
              f'{every}  [{" ".join(sorted(surface_runs[(owner, size, install)]))}]')
    return 0


if __name__ == '__main__':
    sys.exit(main())
