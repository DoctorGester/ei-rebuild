"""Writes docs/originals-report.md: every original function that has an EI_ORIGINAL forwarder in
more than one file of src/, with the files and the signatures each file declares.

The report is the work list for merging those forwarders into one declaration per function
(docs/cleanup-plan.md, Phase 0). Source files are only read, never changed. The output is
deterministic: running the script again on unchanged sources gives the same file.

    python tools/gen_originals.py            # writes docs/originals-report.md
    python tools/gen_originals.py --check    # exits 1 if the committed report is out of date
"""
import bisect
import csv
import os
import re
import sys
from collections import Counter, defaultdict

import srcscan
from srcscan import ROOT

REPORT = os.path.join(ROOT, 'docs', 'originals-report.md')

# Where one declaration ends and the next begins: a statement end, a brace, a preprocessor line, or
# the previous forwarder (which has no ';' of its own).
_BOUNDARY = re.compile(r'[;{}]|' + srcscan.ORIGINAL.pattern + r'|^[ \t]*#[^\n]*(?:\\\n[^\n]*)*', re.M)
_REPLACE = re.compile(r'\b(?:EI_REPLACE(?:_METHOD)?|EI_REWRITTEN)\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*,\s*&?([^)]*?)\s*\)')
# ui_hud.cpp's input shims register through EI_REPLACE inside the macro: (address, class, method).
_UI_HANDLER = re.compile(r'\bUI_(?:INPUT|WHEEL)_HANDLER\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)')

STATES = [  # (state, heading, what to do with the duplicates)
    ('rewritten', 'Already rewritten',
     'The function is ported here; callers can call it directly instead of through the original address.'),
    ('library', 'Library code',
     'STLport, MFC or CRT code; goes away with the library layer (cleanup plan, Phase 2).'),
    ('game', 'Not yet ported',
     'Declare once (src/game/originals.h), include everywhere, replace by the port later.'),
]


class Site:
    """One forwarder: where it is and the declaration it forwards for."""

    def __init__(self, source, offset, boundaries):
        self.path = source.path
        self.line = source.line_of(offset)
        self.signature = declaration_before(source, offset, boundaries)

    def shape(self, keep_name=True):
        """The signature without parameter-name comments and spacing; with `keep_name` false also
        without the function's own name (what a merged declaration must agree on)."""
        text = re.sub(r'/\*.*?\*/', '', self.signature)
        if not keep_name:
            text = re.sub(r'[\w:~]+(?=\s*\()', '', text, count=1)
        return re.sub(r'\s+', '', text)


def boundaries_of(source):
    """Offsets just after each declaration boundary of the file."""
    return [m.end() for m in _BOUNDARY.finditer(source.code)]


def declaration_before(source, offset, boundaries):
    """The declaration the forwarder at `offset` finishes, as one line without `//` comments."""
    index = bisect.bisect_left(boundaries, offset) - 1
    start = boundaries[index] if index >= 0 else 0
    # The blanked text skips leading comments; the raw text keeps /*name*/ hints inside the parentheses.
    start += len(source.code[start:offset]) - len(source.code[start:offset].lstrip())
    text = re.sub(r'//[^\n]*', '', source.raw[start:offset])
    text = re.sub(r'__declspec\(naked\)\s*', '', text)
    text = re.sub(r'\(\s+|\s+\)', lambda m: m.group(0).strip(), re.sub(r'\s+', ' ', text)).strip()
    return re.sub(r'^(?:(?:inline|static)\s+)+', '', text)


def read_csv_addresses(name, column='address'):
    with open(os.path.join(ROOT, 'config', name), encoding='utf-8') as f:
        return list(csv.DictReader(f))


def classify(sources):
    """Returns (address -> 'rewritten' | 'library' | 'game', address -> list of (path, hook function))."""
    rewritten = defaultdict(list)
    for s in sources:
        for m in _REPLACE.finditer(s.code):
            rewritten[int(m.group(1), 16)].append((s.path, m.group(2)))
        for m in _UI_HANDLER.finditer(s.code):
            rewritten[int(m.group(1), 16)].append((s.path, m.group(2) + '_' + m.group(3)))
    library = {int(r['address'], 16) for r in read_csv_addresses('library.csv')}
    library |= {int(r['address'], 16) for r in read_csv_addresses('functions.csv') if r['kind'] == 'lib'}

    def state(address):
        if address in rewritten:
            return 'rewritten'
        return 'library' if address in library else 'game'
    return state, rewritten


def collect_sites(sources):
    """address -> list of Site, in file order."""
    sites = defaultdict(list)
    boundaries = {}
    for address, source, offset in srcscan.forwarder_sites(sources):
        if source.path not in boundaries:
            boundaries[source.path] = boundaries_of(source)
        sites[address].append(Site(source, offset, boundaries[source.path]))
    return sites


def files_of(sites):
    return sorted({site.path for site in sites})


IDENTICAL, NAMES_DIFFER, TYPES_DIFFER = 'identical', 'same signature, names differ', 'signatures differ'


def agreement(sites):
    """How far the files agree on the declaration."""
    if len({site.shape() for site in sites}) == 1:
        return IDENTICAL
    return NAMES_DIFFER if len({site.shape(keep_name=False) for site in sites}) == 1 else TYPES_DIFFER


def render(sources):
    sites = collect_sites(sources)
    state, rewritten = classify(sources)
    shared = {a: s for a, s in sites.items() if len(files_of(s)) > 1}
    same_file_only = sum(1 for a, s in sites.items() if a not in shared and len(s) > 1)
    by_state = defaultdict(dict)
    for address, address_sites in shared.items():
        by_state[state(address)][address] = address_sites
    agreeing = Counter(agreement(s) for s in shared.values())
    total_sites = sum(len(s) for s in sites.values())

    out = ['# Originals forwarded from more than one file', '',
           'Generated by `tools/gen_originals.py` from `src/` (do not edit; run the script again). The work list for',
           'merging duplicate `EI_ORIGINAL` forwarders into one declaration per function (cleanup plan, Phase 0).', '',
           f'- {total_sites:,} forwarder sites forward to {len(sites):,} distinct original functions.',
           f'- **{len(shared):,}** of those functions are forwarded from more than one file; they are listed below'
           f' ({sum(len(s) for s in shared.values()):,} sites).',
           f'- {same_file_only:,} more are forwarded several times inside a single file (not listed).',
           f'- Of the {len(shared):,}: ' + ', '.join(f'{len(by_state[k])} {title.lower()}' for k, title, _ in STATES) + '.',
           f'- Signatures: {agreeing[IDENTICAL]} identical in every file, {agreeing[NAMES_DIFFER]} the same apart from'
           f' the helper name, {agreeing[TYPES_DIFFER]} differ in return or parameter types or in the calling'
           ' convention (these need a decision before merging).', '']

    crowded = Counter(site.path for s in shared.values() for site in s).most_common(15)
    out += ['Files with the most shared forwarders: ' + ', '.join(f'`{p}` ({n})' for p, n in crowded) + '.', '']

    for key, title, advice in STATES:
        group = by_state[key]
        out += [f'## {title} ({len(group)})', '', advice, '']
        for address in sorted(group):
            address_sites = group[address]
            out.append(f'### 0x{address:08X}: {len(files_of(address_sites))} files, {len(address_sites)} sites,'
                       f' {agreement(address_sites)}')
            if key == 'rewritten':
                out.append('Rewritten as ' + ', '.join(f'`{fn}` ({p})' for p, fn in rewritten[address]) + '.')
            out += [f'- `{site.path}:{site.line}`: `{site.signature}`' for site in address_sites]
            out.append('')
    return '\n'.join(out).rstrip('\n') + '\n'


def main(argv):
    sources = srcscan.load_sources()
    unparsed = sum(s.code.count('EI_ORIGINAL(') for s in sources) - len(srcscan.forwarder_sites(sources))
    if unparsed:
        print(f'warning: {unparsed} EI_ORIGINAL( uses did not parse as EI_ORIGINAL(0x...)', file=sys.stderr)
    report = render(sources)
    if '--check' in argv:
        current = open(REPORT, encoding='utf-8').read() if os.path.exists(REPORT) else ''
        print('docs/originals-report.md is up to date' if current == report else 'docs/originals-report.md is stale')
        return 0 if current == report else 1
    with open(REPORT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)
    head = report.split('\n## ', 1)[0]
    print(head.split('\n\n', 2)[2].rstrip())
    print(f'wrote {os.path.relpath(REPORT, ROOT)} ({report.count(chr(10)):,} lines)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
