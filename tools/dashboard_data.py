"""Collects the numbers behind the progress dashboard, for the working tree and for past commits.

For each sampled first-parent commit of main (and the working tree last) it computes, from that
commit's own files:
  - ported game code: functions and bytes rewritten (EI_REPLACE*, UI_INPUT_HANDLER / UI_WHEEL_HANDLER)
    against config/functions.csv minus library and dead code, overall and per address range;
  - lines of code under src/ (game, lib, render, hook) and tests/;
  - cleanup artefacts: EI_ORIGINAL sites, functions forwarded from several files, At<>/Field<> uses,
    VCall uses, __asm blocks, float_control pragmas, verify wrappers, scenarios;
  - standalone readiness (docs/notes/standalone.md): what still ties the code to the original exe
    (forwarders by target, game data addresses, uses of the exe's MFC / STLport / CRT / imports)
    and the single-byte text sites a UTF-8 switch has to visit.

usage: python tools/dashboard_data.py [--every N] [--out build/dashboard/data.json] [--no-cache]
Reads history through `git cat-file --batch`; nothing is checked out. A rerun takes the numbers of
commits it already measured from the previous data.json (history points carry their full commit hash),
so only new commits and the working tree are measured. The old file is not reused when this script or
tools/check_cleanup.py changed since it was written (their hash is stored in it as `version`).
"""
import argparse, csv, hashlib, io, json, os, re, subprocess, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import check_cleanup  # noqa: E402  (the cleanup ratchet's patterns, so both count the same things)

# check_cleanup's columns that the CLEANUP dict below does not already cover, by data.json key.
RATCHET = {
    'raw_addresses': 'raw game address (0x004..0x007 literal)',
    'at_views': '*At view struct',
    'slot_placeholders': 'placeholder virtual (SlotNN)',
    'byte_arithmetic': 'byte offset arithmetic',
    'container_structs': 'per-file container struct',
    'vector_map_set_structs': 'per-file vector/map/set struct',
    'vector_maths': 'hand-written vector maths',
}


def ratchet_counts(path, text):
    """check_cleanup's counts for one file (its comment stripping, exemptions and registration rule)."""
    code = check_cleanup.strip_comments(text)
    out = {}
    for key, name in RATCHET.items():
        rx = check_cleanup.PATTERNS.get(name)
        if rx is None or (name in check_cleanup.EXEMPT and path.startswith(check_cleanup.EXEMPT[name])):
            continue
        if callable(rx):
            out[key] = rx(code)
            continue
        n = 0
        for m in rx.finditer(code):
            if name.startswith('raw game address') and check_cleanup.REGISTRATION.search(code[max(0, m.start() - 40):m.start()]):
                continue
            n += 1
        out[key] = n
    return out

AREAS = [
    (0x400000, 0x480000, 'Engine base, network, app'),
    (0x480000, 0x4C0000, 'Script runtime, engine misc'),
    (0x4C0000, 0x500000, 'Scripts'),
    (0x500000, 0x540000, 'Units, combat, map objects'),
    (0x540000, 0x5A0000, 'AI, world, server'),
    (0x5A0000, 0x5E0000, 'AI paths, world client'),
    (0x5E0000, 0x640000, 'Interface'),
    (0x640000, 0x6C0000, 'HUD, items, spells, effects'),
    (0x6C0000, 0x740000, 'Game code in the CRT/MFC range'),
]

COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
REPLACE = re.compile(r'EI_REPLACE(?:_METHOD)?\(\s*0x([0-9A-Fa-f]+)')
HANDLER = re.compile(r'UI_(?:INPUT|WHEEL)_HANDLER\(\s*0x([0-9A-Fa-f]+)')
ORIGINAL = re.compile(r'\bEI_ORIGINAL\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*\)')
CLEANUP = {
    'offset_access': re.compile(r'\b(?:At|Field)\s*<'),
    'vcall': re.compile(r'\bVCall\b'),
    'asm_blocks': re.compile(r'\b__asm\b'),
    'float_control': re.compile(r'#\s*pragma\s+float_control'),
}

# Standalone readiness: qualified uses of the exe's runtime through src/lib, and text sites that
# assume one byte per character (docs/notes/standalone.md).
STANDALONE = {
    'mfc_strings': re.compile(r'\bmfc::CString\b'),
    'mfc_arrays': re.compile(r'\bmfc::(?:CArray|CStringArray|CPtrArray|CDWordArray|CUIntArray|CByteArray)\b'),
    'mfc_lists': re.compile(r'\bmfc::(?:CTypedPtrList|CList|CPtrList|CObList|CStringList)\b'),
    'mfc_maps': re.compile(r'\bmfc::CMap\w*'),
    'mfc_files': re.compile(r'\bmfc::(?:CFile\w*|CArchive|CMemFile)\b'),
    'stlp_uses': re.compile(r'\bstlp::'),
    'stlp_hash_maps': re.compile(r'\bstlp::hash_(?:map|set)\b'),
    'crt_allocations': re.compile(r'\bgamecrt::operator_(?:new|delete)\b'),
    'crt_other': re.compile(r'\bgamecrt::(?!operator_(?:new|delete)\b)'),
    'import_uses': re.compile(r'\bgameapi::'),
    'text_case_compares': re.compile(r'\bgamecrt::mbsicmp\b|\b_?strn?icmp\b|\bMakeUpper\b|\bMakeLower\b|\bCompareNoCase\b'
                                     r'|\bCharUpper\w*|\bCharLower\w*'),
    'text_fixed_buffers': re.compile(r'\bchar\s+\w+\[[0-9A-Za-z_]+\]'),
    'text_ansi_calls': re.compile(r'\b(?:CreateFileA|FindFirstFileA|DeleteFileA|GetFileAttributesA|CreateDirectoryA|fopen'
                                  r'|RegQueryValueExA|RegSetValueExA|GetModuleFileNameA|SetWindowTextA|MessageBoxA'
                                  r'|TextOutA|DrawTextA|ExtTextOutA|GetTextExtentPoint32A|GetKeyNameTextA)\b'),
}
DATA_LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')
UNHOOKABLE_BYTES = 5   # a hook needs five bytes: shorter functions are written at standalone time


class Tree:
    """Files of one commit (or of the working tree when commit is None)."""

    def __init__(self, commit, batch):
        self.commit, self.batch = commit, batch
        self.blobs = {}   # path -> blob id (commits only), the key of FILE_CACHE
        if commit:
            out = subprocess.run(['git', 'ls-tree', '-r', commit], cwd=ROOT, capture_output=True, text=True).stdout
            for line in out.split('\n'):
                meta, _, path = line.partition('\t')
                if path:
                    self.blobs[path] = meta.split()[-1]
            self.paths = list(self.blobs)
        else:
            self.paths = []
            for top in ('src', 'tests', 'config'):
                for d, _, names in os.walk(os.path.join(ROOT, top)):
                    for n in names:
                        self.paths.append(os.path.relpath(os.path.join(d, n), ROOT).replace(os.sep, '/'))

    def read(self, path):
        if not self.commit:
            p = os.path.join(ROOT, path)
            return open(p, encoding='utf-8', errors='replace').read() if os.path.exists(p) else None
        return self.batch.read(self.blobs.get(path) or f'{self.commit}:{path}')


class Batch:
    def __init__(self):
        self.p = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE)

    def read(self, spec):
        self.p.stdin.write((spec + '\n').encode()); self.p.stdin.flush()
        header = self.p.stdout.readline().decode()
        if header.endswith('missing\n'):
            return None
        size = int(header.split()[2])
        data = self.p.stdout.read(size); self.p.stdout.read(1)
        return data.decode('utf-8', errors='replace')


HEX = re.compile(r'^[0-9A-Fa-f]+$')


def addresses(text, column='address'):
    # Rows that are not addresses (a conflict marker committed by mistake once) are skipped.
    if not text:
        return set()
    return {int(r[column], 16) for r in csv.DictReader(io.StringIO(text)) if HEX.match(r.get(column) or '')}


# Per-file numbers keyed by (blob id, path): consecutive commits share most files, so each version
# of a file is scanned once per run instead of once per commit (~3 s per late commit before).
FILE_CACHE = {}


def file_stats(path, text):
    """One file's share of measure(): its loc part and lines, rewritten addresses, one EI_ORIGINAL
    target per site, cleanup counts, standalone counts and data addresses."""
    replaced, origins, cleanup, standalone, data = set(), [], defaultdict(int), defaultdict(int), set()
    part = ('tests/scenarios' if path.startswith('tests/scenarios') else 'tests/verify'
            if path.startswith('tests/') else '/'.join(path.split('/')[:2]))
    if path.startswith('tests/scenarios') and path.endswith('.txt'):
        cleanup['scenarios'] += 1
    if path.endswith(('.cpp', '.h')):
        code = COMMENT.sub('', text)
        if path.startswith('src/'):
            replaced |= {int(a, 16) for a in REPLACE.findall(code)}
            replaced |= {int(a, 16) for a in HANDLER.findall(code)}
            for a in ORIGINAL.findall(code):
                cleanup['forwarder_sites'] += 1
                origins.append(int(a, 16))
            if not path.startswith('src/hook/'):
                for k, rx in CLEANUP.items():
                    cleanup[k] += len(rx.findall(code))
                for k, n in ratchet_counts(path, text).items():
                    cleanup[k] += n
                data |= {a for a in (int(m, 16) for m in DATA_LITERAL.findall(code)) if 0x730000 <= a <= 0x8FFFFF}
                if not path.startswith('src/lib/'):
                    for k, rx in STANDALONE.items():
                        standalone[k] += len(rx.findall(code))
        else:
            cleanup['verify_wrappers'] += len(re.findall(r'\bEI_VERIFY\s*\(', code))
    return part, text.count('\n'), replaced, origins, dict(cleanup), dict(standalone), data


def measure(tree):
    sizes = {}
    for line in (tree.read('src/hook/function_sizes.inc') or '').splitlines():
        m = re.match(r'\{0x([0-9A-F]+), (\d+)\}', line)
        if m:
            sizes[int(m.group(1), 16)] = int(m.group(2))
    funcs = tree.read('config/functions.csv')
    if not funcs or not sizes:
        return None
    game = {int(r['address'], 16) for r in csv.DictReader(io.StringIO(funcs)) if r['kind'] == 'game'}
    game -= addresses(tree.read('config/library.csv')) | addresses(tree.read('config/dead_code.csv'))

    replaced, loc, cleanup = set(), defaultdict(int), defaultdict(int)
    standalone, data = defaultdict(int), set()
    origin_files = defaultdict(set)
    for path in tree.paths:
        if not path.endswith(('.cpp', '.h', '.inc', '.txt', '.py')):
            continue
        if not (path.startswith('src/') or path.startswith('tests/')):
            continue
        key = (tree.blobs.get(path), path)
        f = FILE_CACHE.get(key) if key[0] else None
        if f is None:
            text = tree.read(path)
            if text is None:
                continue
            f = file_stats(path, text)
            if key[0]:
                FILE_CACHE[key] = f
        part, lines, replaced_here, origins, counts, stand, data_here = f
        loc[part] += lines
        replaced |= replaced_here
        for a in origins:
            origin_files[a].add(path)
        for k, n in counts.items():
            cleanup[k] += n
        for k, n in stand.items():
            standalone[k] += n
        data |= data_here
    cleanup['forwarded_from_several_files'] = sum(1 for f in origin_files.values() if len(f) > 1)

    total = sum(sizes.get(a, 0) for a in game)
    done = game & replaced
    # Forwarders by what they reach: game code still original (goes with the port), code already
    # rewritten (call it directly), and everything else (the exe's library code, to be our own).
    forwarded = set(origin_files)
    patched = {int(m, 16) for m in re.findall(r'^\s*(?:0x)?([0-9A-Fa-f]{8})\b',
                                              tree.read('docs/rebirth-patched-functions.txt') or '', re.M)}
    left = game - done
    standalone.update(
        forwarder_addresses=len(forwarded), forwarders_to_unported=len(forwarded & left),
        forwarders_to_rewritten=len(forwarded & replaced), forwarders_to_library=len(forwarded - left - replaced),
        data_addresses=len(data), unported_functions=len(left),
        unported_unhookable=sum(1 for a in left if sizes.get(a, 0) < UNHOOKABLE_BYTES),
        unported_rebirth_patched=len(left & patched))
    areas = []
    for lo, hi, name in AREAS:
        g = {a for a in game if lo <= a < hi}
        d = g & done
        areas.append({'name': name, 'range': f'{lo:06X}-{hi:06X}', 'functions': len(g), 'ported_functions': len(d),
                      'bytes': sum(sizes.get(a, 0) for a in g), 'ported_bytes': sum(sizes.get(a, 0) for a in d)})
    return {'functions': len(game), 'ported_functions': len(done), 'bytes': total,
            'ported_bytes': sum(sizes.get(a, 0) for a in done), 'areas': areas, 'loc': dict(loc),
            'cleanup': dict(cleanup), 'standalone': dict(standalone)}


def metrics_version():
    """Hash of the code that decides the numbers; a cache made by other code is not reused."""
    h = hashlib.sha1()
    for name in ('dashboard_data.py', 'check_cleanup.py'):
        h.update(open(os.path.join(ROOT, 'tools', name), 'rb').read())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--every', type=int, default=8, help='sample every Nth first-parent commit')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dashboard', 'data.json'))
    ap.add_argument('--no-cache', action='store_true', help='measure every sampled commit again instead of reusing the previous data.json')
    args = ap.parse_args()
    log = subprocess.run(['git', 'log', '--first-parent', '--reverse', '--format=%H %cI %s', 'main'], cwd=ROOT,
                         capture_output=True, text=True).stdout.strip().split('\n')
    commits = [l.split(' ', 2) for l in log]
    sample = commits[::args.every]
    if sample[-1] != commits[-1]:
        sample.append(commits[-1])
    batch = Batch()
    version = metrics_version()
    known, skipped = {}, set()
    if not args.no_cache and os.path.exists(args.out):
        try:
            previous = json.load(open(args.out))
            if previous.get('version') == version:
                known = {p['sha']: p for p in previous.get('history', []) if 'sha' in p}
                skipped = set(previous.get('skipped', []))
        except (OSError, ValueError):
            pass
    history, measured, unmeasured = [], 0, []
    for sha, when, subject in sample:
        if sha in known:
            m = known[sha]
        elif sha in skipped:
            m = None
        else:
            m = measure(Tree(sha, batch))
            measured += 1
            if m:
                print(f'{sha[:7]} {when[:16]} {100 * m["ported_bytes"] / m["bytes"]:.1f}%', file=sys.stderr)
        if m:
            m = dict(m, sha=sha, commit=sha[:7], date=when, subject=subject[:80])
            history.append(m)
        else:
            unmeasured.append(sha)   # a commit without the inputs (early history): not measured again
    print(f'{measured} commits measured, {len(sample) - measured} reused from {os.path.basename(args.out)}', file=sys.stderr)
    current = measure(Tree(None, batch))
    current.update(commit='working tree', date=subprocess.run(['git', 'log', '-1', '--format=%cI'], cwd=ROOT,
                   capture_output=True, text=True).stdout.strip(), subject='')
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    json.dump({'version': version, 'current': current, 'history': history, 'skipped': unmeasured},
              open(args.out, 'w'), indent=1)
    print(f'wrote {args.out}: {len(history)} history points', file=sys.stderr)


if __name__ == '__main__':
    main()
