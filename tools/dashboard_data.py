"""Collects the numbers behind the progress dashboard, for the working tree and for past commits.

For each sampled first-parent commit of main (and the working tree last) it computes, from that
commit's own files:
  - lines of code under src/ (game, lib, render, hook) and tests/;
  - cleanup artefacts: At<>/Field<> uses, VCall uses, __asm blocks, float_control pragmas, scenarios;
  - standalone readiness (docs/notes/standalone.md): what still ties the code to the original exe
    (game data addresses, uses of the exe's MFC / STLport / CRT / imports)
    and the single-byte text sites a UTF-8 switch has to visit;
  - the cleanup track (tools/progress.py's counts over all of src/, so both tools agree): casts,
    __fastcall shims, game data addresses, struct names defined twice; and from the
    generated tables, game.exe's code cells by group, its pointer cells, the named data ranges (C18)
    and the retired vtables (C19).

usage: python tools/dashboard_data.py [--every N] [--out build/dashboard/data.json] [--no-cache]
Reads history through `git cat-file --batch`; nothing is checked out. A rerun takes the numbers of
commits it already measured from the previous data.json (history points carry their full commit hash),
so only new commits and the working tree are measured. The old file is not reused when this script or
tools/check_cleanup.py changed since it was written (their hash is stored in it as `version`).
"""
import argparse, csv, hashlib, io, json, os, re, subprocess, sys, tokenize
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import check_cleanup  # noqa: E402  (the cleanup ratchet's patterns, so both count the same things)
import progress  # noqa: E402  (the cleanup track's counts, so the dashboard and progress.py agree)
import srcscan  # noqa: E402

# progress.py's plain counts that the cleanup track shows, by data.json key.
TRACK = {
    'reinterpret_casts': 'reinterpret_cast',
    'pointer_static_casts': 'static_cast to a pointer type',
    'anonymous_namespaces': 'anonymous namespaces',
}
TRACK_PATTERNS = {key: re.compile(pattern, re.M) for label, pattern in progress.HOOK_ERA_COUNTS
                  for key, name in TRACK.items() if label == name}
STRUCT_DEFINITION = re.compile(r'^(?:struct|class)\s+(?:__declspec\(\w+\)\s+)?(\w+)(?:\s+final)?\s*(?::[^{;]*)?\{', re.M)
CELL = re.compile(r'^\{DataId\(', re.M)

# check_cleanup's columns that the CLEANUP dict below does not already cover, by data.json key.
RATCHET = {
    'raw_addresses': 'raw game address (0x004..0x007 literal)',
    'at_views': '*At view struct',
    'slot_placeholders': 'placeholder virtual (SlotNN)',
    'byte_arithmetic': 'byte offset arithmetic',
    'container_structs': 'per-file container struct',
    'vector_map_set_structs': 'per-file vector/map/set struct',
    'vector_maths': 'hand-written vector maths',
    'unknown_fields': 'unknown field',
    'unused_fields_accessed': 'unused field accessed',
}


# A literal address directly inside a registration or a function-to-original map line of an old commit (gone since
# C20l) is not counted as a raw address, as check_cleanup did not count it then.
OLD_REGISTRATION = re.compile(r'(?:EI_(?:REWRITTEN|REWRITTEN_SPECIAL|REPLACE|REPLACE_METHOD|REPLACE_SHORT|'
                              r'REPLACE_DISPATCH|REPLACE_STANDALONE)|UI_(?:INPUT|WHEEL)_HANDLER)\s*\(\s*$')


def ratchet_counts(path, text):
    """check_cleanup's counts for one file (its comment stripping and exemptions; old commits' registrations)."""
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
            if name.startswith('raw game address') and OLD_REGISTRATION.search(code[max(0, m.start() - 40):m.start()]):
                continue
            n += 1
        out[key] = n
    return out

COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
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
# Standalone readiness: what ties the image to game.exe at run time (S18k): game.exe's range in the host's
# image, and reads of game.exe's file by the host and by ei_hook.dll in the image.
IMAGE_TIES = [
    ('CMakeLists.txt', re.compile(r'ei_standalone\s+PRIVATE[^\n]*/FIXED')),       # the host at a fixed base
    ('src/standalone/host.cpp', re.compile(r'\bReadWholeFile\b')),                # the host reads game.exe
    ('src/hook/hook.cpp', re.compile(r'\bStandaloneExePath\b')),                  # InstallHooks reads it in the image
    ('src/standalone/image_imports.cpp', re.compile(r'\bEiStandaloneExe\b')),    # imports from game.exe's folder
]
DATA_RANGE = range(0x730000, 0x900000)
DATA_LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')


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


# Per-file numbers keyed by (blob id, path): consecutive commits share most files, so each version
# of a file is scanned once per run instead of once per commit (~3 s per late commit before).
FILE_CACHE = {}


def file_stats(path, text):
    """One file's share of measure(): its loc part and lines, cleanup counts, standalone counts and data
    addresses."""
    cleanup, standalone, data = defaultdict(int), defaultdict(int), set()
    track, data_all, structs = {}, set(), set()
    part =('tests/scenarios' if path.startswith('tests/scenarios') else 'tests/verify'
            if path.startswith('tests/') else '/'.join(path.split('/')[:2]))
    if path.startswith('tests/scenarios') and path.endswith('.txt'):
        cleanup['scenarios'] += 1
    if path.endswith(('.cpp', '.h')):
        code = COMMENT.sub('', text)
        if path.startswith('src/'):
            source = srcscan.Source(path, text)
            track = {key: len(rx.findall(source.code)) for key, rx in TRACK_PATTERNS.items()}
            track['fastcall_shims'] = progress.fastcall_shims([source])[0]
            data_all = {a for a in (int(m, 16) for m in DATA_LITERAL.findall(source.code)) if a in DATA_RANGE}
            structs = set(STRUCT_DEFINITION.findall(source.code))
            if not path.startswith(('src/hook/', 'src/standalone/')):
                for k, rx in CLEANUP.items():
                    cleanup[k] += len(rx.findall(code))
                for k, n in ratchet_counts(path, text).items():
                    cleanup[k] += n
                data |= {a for a in (int(m, 16) for m in DATA_LITERAL.findall(code)) if 0x730000 <= a <= 0x8FFFFF}
                if not path.startswith('src/lib/'):
                    for k, rx in STANDALONE.items():
                        standalone[k] += len(rx.findall(code))
    return part, text.count('\n'), dict(cleanup), dict(standalone), data, track, data_all, structs


def measure(tree):
    if not tree.read('config/functions.csv'):   # the early history
        return None
    loc, cleanup = defaultdict(int), defaultdict(int)
    standalone, data = defaultdict(int), set()
    track, data_all, struct_files = defaultdict(int), set(), defaultdict(int)
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
        part, lines, counts, stand, data_here, track_here, data_all_here, structs_here = f
        for k, n in track_here.items():
            track[k] += n
        data_all |= data_all_here
        for name in structs_here:
            struct_files[name] += 1
        loc[part] += lines
        for k, n in counts.items():
            cleanup[k] += n
        for k, n in stand.items():
            standalone[k] += n
        data |= data_here

    standalone['data_addresses'] = len(data)
    standalone['image_ties'] = sum(1 for path, rx in IMAGE_TIES if rx.search(COMMENT.sub('', tree.read(path) or '')))
    track.update(data_addresses=len(data_all), structs_defined_twice=sum(1 for n in struct_files.values() if n > 1))
    track.update(generated_tables(tree))
    return {'loc': dict(loc), 'cleanup': dict(cleanup), 'standalone': dict(standalone), 'track': dict(track)}


def generated_tables(tree):
    """The image's tables of game.exe's data (S18b / S18i, C18, C19): code cells by group (none since C20), pointer
    cells, named ranges and retired vtables; a commit without a table leaves its keys out."""
    out = {}
    code_cells = tree.read('src/standalone/image_code_cells.inc')
    if code_cells:
        groups, group = defaultdict(int), ''
        for line in code_cells.splitlines():
            if line.startswith('// '):
                group = line
            elif line.startswith('{DataId('):
                kind = ('code_cells_eh' if 'C++ EH' in group else 'code_cells_vtable' if 'vtable' in group
                        else 'code_cells_static_init' if 'initialiser list' in group else 'code_cells_other')
                groups[kind] += 1
        out.update(groups, code_cells=sum(groups.values()))
    pointer_cells = tree.read('src/standalone/image_cells.inc')
    if pointer_cells:
        out['pointer_cells'] = len(CELL.findall(pointer_cells))
        out.setdefault('code_cells', 0)   # retired in C20: game.exe's data keeps no code pointer in the image
    # The retired vtables: their own csv until C20 batch 6 folded it into config/vtables.csv (the rows with a class).
    retired = tree.read('config/retired_vtables.csv')
    if retired is None and 'class' in (tree.read('config/vtables.csv') or '').partition('\n')[0].split(','):
        retired = tree.read('config/vtables.csv')
    if retired is not None:
        out['retired_vtables'] = sum(1 for r in csv.DictReader(io.StringIO(retired)) if r.get('class', '-') != '')
    for key, path in (('named_ranges', 'config/named_data.csv'),):
        text = tree.read(path)
        if text is not None:
            out[key] = sum(1 for _ in csv.DictReader(io.StringIO(text)))
    return out


def metrics_version():
    """Hash of the code that decides the numbers; a cache made by other code is not reused. Comments,
    blank lines and indentation-only changes do not count, so editing an explanation keeps the cache."""
    h = hashlib.sha1()
    for name in ('dashboard_data.py', 'check_cleanup.py', 'progress.py', 'srcscan.py'):
        with open(os.path.join(ROOT, 'tools', name), 'rb') as f:
            for tok in tokenize.tokenize(f.readline):
                if tok.type not in (tokenize.COMMENT, tokenize.NL, tokenize.NEWLINE, tokenize.INDENT,
                                    tokenize.DEDENT, tokenize.ENCODING):
                    h.update(tok.string.encode() + b'\0')
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
                print(f'{sha[:7]} {when[:16]}', file=sys.stderr)
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
