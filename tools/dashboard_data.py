"""Collects the numbers behind the progress dashboard, for the working tree and for past commits.

For each sampled first-parent commit of main (and the working tree last) it computes, from that commit's own
files, in groups (GROUP_VERSION below):
  - loc: lines of code per directory of src/ and tests/ (tests/golden holds recorded data, not code, and is left out);
  - cleanup: the live counts of the clean-code ratchet: precision-pinning casts, Ftol calls, per-file container
    structs, scenarios;
  - standalone: the single-byte text sites a UTF-8 switch has to visit (docs/notes/standalone.md);
  - track: tools/progress.py's counts over all of src/ (casts, anonymous namespaces, struct names defined twice);
  - goals: the counts of what the code is moving to: pc24 uses, ordered-sum vector helpers, render::ui canvas calls.
The series that reached 0 for good (the hook era: offset access, VCall, __asm, MFC / STLport / CRT uses, raw and data
addresses, code cells, shims, retired vtables ...) are not measured any more; the build checks hold them at 0.

usage: python tools/dashboard_data.py [--every N] [--out build/dashboard/data.json] [--no-cache] [--refresh GROUP,...]
Reads history through `git cat-file --batch`; nothing is checked out. A rerun keeps every history point it already
measured (a point carries its full commit hash and the version of each group's rule it was measured with), so only
new commits and the working tree are measured: editing a tool does not cost a re-measure. When a group's rule
changes, bump its GROUP_VERSION: the next run measures that group again for every point (the other groups stay);
`--refresh GROUP,...` does the same by hand and `--no-cache` measures everything again.
"""
import argparse, json, os, re, subprocess, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import check_cleanup  # noqa: E402  (the cleanup ratchet's patterns, so both count the same things)
import progress  # noqa: E402  (the cleanup track's counts, so the dashboard and progress.py agree)
import srcscan  # noqa: E402

# The rule version of each group of numbers. A history point keeps the versions it was measured with (`mv`); a group
# whose version differs from the one here is measured again for every point on the next run. Bump a group's version when
# its rule changes (a pattern, a path that is counted or left out), not for an edit that leaves the numbers alone.
# A point from before this table has no `mv`: its four old groups count as version 1.
GROUP_VERSION = {'loc': 2,        # 2: tests/golden (recorded data, 1.15M lines) is not code
                 'cleanup': 1, 'standalone': 1, 'track': 1,
                 'goals': 1}      # new with this table
GROUPS = tuple(GROUP_VERSION)
OLD_GROUPS = ('loc', 'cleanup', 'standalone', 'track')

# progress.py's plain counts that the cleanup track shows, by data.json key.
TRACK = {
    'reinterpret_casts': 'reinterpret_cast',
    'pointer_static_casts': 'static_cast to a pointer type',
    'anonymous_namespaces': 'anonymous namespaces',
}
TRACK_PATTERNS = {key: re.compile(pattern, re.M) for label, pattern in progress.HOOK_ERA_COUNTS
                  for key, name in TRACK.items() if label == name}

# check_cleanup's columns that the dashboard still charts (the others are at 0 and held there by the build check).
RATCHET = {'vector_map_set_structs': 'per-file vector/map/set struct'}

COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
# The x87 remains (C30): casts that pin a precision and the CRT's _ftol.
CLEANUP = {
    'float_casts': re.compile(r'\bstatic_cast<float>'),
    'double_casts': re.compile(r'\bstatic_cast<double>'),
    'ftol_calls': re.compile(r'\bFtol\s*\('),
}

# Single-byte text sites that a UTF-8 switch has to visit (docs/notes/standalone.md).
STANDALONE = {
    'text_case_compares': re.compile(r'\bgamecrt::mbsicmp\b|\b_?strn?icmp\b|\bMakeUpper\b|\bMakeLower\b|\bCompareNoCase\b'
                                     r'|\bCharUpper\w*|\bCharLower\w*'),
    'text_fixed_buffers': re.compile(r'\bchar\s+\w+\[[0-9A-Za-z_]+\]'),
    'text_ansi_calls': re.compile(r'\b(?:CreateFileA|FindFirstFileA|DeleteFileA|GetFileAttributesA|CreateDirectoryA|fopen'
                                  r'|RegQueryValueExA|RegSetValueExA|GetModuleFileNameA|SetWindowTextA|MessageBoxA'
                                  r'|TextOutA|DrawTextA|ExtTextOutA|GetTextExtentPoint32A|GetKeyNameTextA)\b'),
}

# What the code is moving to, as uses to move on or to see grow: (key, pattern, files left out: their definitions).
GOALS = [
    # lib/pc24.h: 24-bit-rounded operations on wider operands (docs/notes/float-maths.md, "Wider operands").
    ('pc24_uses', re.compile(r'\bpc24::\w+'), ('src/lib/pc24.',)),
    # The ordered-sum vector helpers of lib/vecmath.h: DotZYX, LengthSquaredXYZ, Sum3::ZXY, DotUnrounded ...
    ('ordered_sums', re.compile(r'\b\w+(?:XYZ|ZYX|ZXY|Unrounded)\b|\bSum3::\w+'), ('src/lib/vecmath.h',)),
    # The game's calls into the native interface canvases (render_ui.h).
    ('ui_canvas_calls', re.compile(r'\brender::ui::\w+'), ('src/render/',)),
]

# The keys each group keeps in data.json; a history point's other keys (the retired series) are dropped.
KEYS = {'cleanup': {'scenarios', *CLEANUP, *RATCHET}, 'standalone': set(STANDALONE),
        'track': {*TRACK_PATTERNS, 'structs_defined_twice'}, 'goals': {key for key, _, _ in GOALS}}


def counted(path):
    """Code of src/ and tests/ and the scenario scripts; tests/golden and tests/saves hold recorded data."""
    if path.startswith(('tests/golden/', 'tests/saves/')) or not path.startswith(('src/', 'tests/')):
        return False
    if path.endswith('.txt'):
        return path.startswith('tests/scenarios/')
    return path.endswith(('.cpp', '.h', '.inc', '.py'))


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
                    d = d.replace(os.sep, '/')
                    if '/golden' in d or d.endswith('/saves'):
                        continue
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


def ratchet_counts(path, text):
    """check_cleanup's counts for one file (its comment stripping and exemptions)."""
    code = check_cleanup.strip_comments(text)
    out = {}
    for key, name in RATCHET.items():
        rx = check_cleanup.PATTERNS.get(name)
        if rx is None or (name in check_cleanup.EXEMPT and path.startswith(check_cleanup.EXEMPT[name])):
            continue
        out[key] = rx(code) if callable(rx) else len(rx.findall(code))
    return out


def file_group(group, path, text):
    """One file's share of one group: (directory, lines) for loc, (counts, struct names defined) for track, else the
    counts."""
    if group == 'loc':
        return '/'.join(path.split('/')[:2]), text.count('\n')
    counts, names = {}, set()
    if group == 'cleanup' and path.startswith('tests/scenarios') and path.endswith('.txt'):
        counts['scenarios'] = 1
    if path.startswith('src/') and path.endswith(('.cpp', '.h')):
        if group == 'track':
            source = srcscan.Source(path, text)
            counts = {key: len(rx.findall(source.code)) for key, rx in TRACK_PATTERNS.items()}
            names = progress.defined_struct_names(source.code)
        elif group == 'goals':
            code = COMMENT.sub('', text)
            counts = {key: len(rx.findall(code)) for key, rx, left_out in GOALS if not path.startswith(left_out)}
        elif not path.startswith(('src/hook/', 'src/standalone/')):
            code = COMMENT.sub('', text)
            if group == 'cleanup':
                counts = {key: len(rx.findall(code)) for key, rx in CLEANUP.items()}
                counts.update(ratchet_counts(path, text))
            elif group == 'standalone' and not path.startswith('src/lib/'):
                counts = {key: len(rx.findall(code)) for key, rx in STANDALONE.items()}
    return (counts, names) if group == 'track' else counts


# Per-file numbers keyed by (blob id, path, group): consecutive commits share most files, so each version of a file is
# scanned once per run instead of once per commit.
FILE_CACHE = {}


def measure(tree, groups=GROUPS):
    """{group: numbers} of one tree for the given groups, or None for the early history without the inputs."""
    if not tree.read('config/functions.csv'):
        return None
    loc, sums, struct_files = defaultdict(int), {g: defaultdict(int) for g in groups}, defaultdict(int)
    for path in tree.paths:
        if not counted(path):
            continue
        text = None
        for group in groups:
            key = (tree.blobs.get(path), path, group)
            f = FILE_CACHE.get(key) if key[0] else None
            if f is None:
                if text is None:
                    text = tree.read(path)
                    if text is None:
                        break
                f = file_group(group, path, text)
                if key[0]:
                    FILE_CACHE[key] = f
            if group == 'loc':
                loc[f[0]] += f[1]
                continue
            if group == 'track':
                f, names = f
                for name in names:
                    struct_files[name] += 1
            for k, n in f.items():
                sums[group][k] += n
    out = {g: dict(sums[g]) for g in groups if g != 'loc'}
    if 'loc' in groups:
        out['loc'] = dict(loc)
    if 'track' in groups:
        out['track']['structs_defined_twice'] = sum(1 for n in struct_files.values() if n > 1)
    return out


def versions_of(point):
    return point.get('mv') or {g: 1 for g in OLD_GROUPS if g in point}


def prune(point):
    """The point without the keys no group measures any more (the retired series and the hook directory)."""
    for group, keys in KEYS.items():
        if group in point:
            point[group] = {k: v for k, v in point[group].items() if k in keys}
    if 'loc' in point:
        point['loc'] = {k: v for k, v in point['loc'].items() if not k.startswith('src/hook')}
    return point


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--every', type=int, default=8, help='sample every Nth first-parent commit')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dashboard', 'data.json'))
    ap.add_argument('--no-cache', action='store_true', help='measure every sampled commit again instead of keeping the previous data.json')
    ap.add_argument('--refresh', default='', help='groups (' + ', '.join(GROUPS) + '; comma separated) to measure again for every point')
    args = ap.parse_args()
    refresh = set(GROUPS) if args.no_cache else {g for g in args.refresh.split(',') if g}
    if refresh - set(GROUPS):
        ap.error('unknown group: ' + ', '.join(sorted(refresh - set(GROUPS))))
    log = subprocess.run(['git', 'log', '--first-parent', '--reverse', '--format=%H %cI %s', 'main'], cwd=ROOT,
                         capture_output=True, text=True).stdout.strip().split('\n')
    commits = [l.split(' ', 2) for l in log]
    sample = commits[::args.every]
    if sample[-1] != commits[-1]:
        sample.append(commits[-1])
    batch = Batch()
    known, skipped = {}, set()
    if os.path.exists(args.out):
        try:
            previous = json.load(open(args.out))
            known = {p['sha']: p for p in previous.get('history', []) if 'sha' in p}
            skipped = set(previous.get('skipped', []))
        except (OSError, ValueError):
            pass
    history, new, again, unmeasured = [], 0, defaultdict(int), []
    for sha, when, subject in sample:
        point = known.get(sha)
        if point is None and sha in skipped and not args.no_cache:
            unmeasured.append(sha)   # a commit without the inputs (early history): not measured again
            continue
        if point is None:
            point, todo = {}, list(GROUPS)
            new += 1
        else:
            have = versions_of(point)
            todo = [g for g in GROUPS if g in refresh or g not in point or have.get(g) != GROUP_VERSION[g]]
            for g in todo:
                again[g] += 1
        if todo:
            got = measure(Tree(sha, batch), todo)
            if got is None:
                unmeasured.append(sha)
                continue
            point = dict(point, **got, mv=dict(versions_of(point), **{g: GROUP_VERSION[g] for g in todo}))
            print(f'{sha[:7]} {when[:16]}  {",".join(todo)}', file=sys.stderr)
        history.append(prune(dict(point, sha=sha, commit=sha[:7], date=when, subject=subject[:80])))
    print(f'{len(sample)} commits: {new} new, {len(history) - new} kept' + ''.join(f'; {g} again for {n}' for g, n in again.items()),
          file=sys.stderr)
    current = measure(Tree(None, batch))
    current.update(commit='working tree', mv=dict(GROUP_VERSION), subject='',
                   date=subprocess.run(['git', 'log', '-1', '--format=%cI'], cwd=ROOT,
                                       capture_output=True, text=True).stdout.strip())
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    json.dump({'current': prune(current), 'history': history, 'skipped': unmeasured}, open(args.out, 'w'), indent=1)
    print(f'wrote {args.out}: {len(history)} history points', file=sys.stderr)


if __name__ == '__main__':
    main()
