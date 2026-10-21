"""Reports rewrite progress: functions replaced in src/.

Counts EI_REPLACE / EI_REPLACE_METHOD registrations against the game functions in
config/functions.csv (sizes from src/hook/function_sizes.inc).

Then prints the "cleanup artefacts" block: the hook-era leftovers that docs/cleanup-plan.md wants to
see fall (forwarders, naked functions, offset access, raw data addresses, duplicate layouts).
"""
import csv, os, re, sys
from collections import defaultdict

import srcscan
from srcscan import ROOT

def main():
    sizes = {}
    for line in open(os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')):
        m = re.match(r'\{0x([0-9A-F]+), (\d+)\}', line)
        if m:
            sizes[int(m.group(1), 16)] = int(m.group(2))
    game = {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'functions.csv')))
            if r['kind'] == 'game'}
    # Library code identified later (STLport/MFC/CRT instantiations named in config/library.csv) is not
    # game code to port: the standalone build links its own runtime.
    library = {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'library.csv')))}
    # Dead code (config/dead_code.csv: nothing in the exe or Rebirth's binaries reaches it) needs no port.
    dead = {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'dead_code.csv')))}
    lib_bytes = sum(sizes.get(a, 0) for a in game & library)
    dead_bytes = sum(sizes.get(a, 0) for a in (game - library) & dead)
    game -= library | dead
    game_bytes = sum(sizes.get(a, 0) for a in game)

    replaced = set()
    for dirpath, _, files in os.walk(os.path.join(ROOT, 'src')):
        for f in files:
            if f.endswith('.cpp'):
                text = open(os.path.join(dirpath, f), encoding='utf-8').read()
                replaced |= {int(a, 16) for a in re.findall(r'(?:EI_REPLACE(?:_METHOD|_SHORT|_DISPATCH)?|EI_REWRITTEN|UI_(?:INPUT|WHEEL)_HANDLER)\(\s*0x([0-9A-Fa-f]+)', text)}

    # Only rewrites of counted game code count; hooks on library or dead code are listed separately.
    other = replaced - game
    replaced &= game
    rb = sum(sizes.get(a, 0) for a in replaced)
    print(f'game functions: {len(game)} ({game_bytes:,} bytes; excluded: library {lib_bytes:,}, dead {dead_bytes:,})')
    print(f'rewritten:      {len(replaced)} ({rb:,} bytes, {100 * rb / game_bytes:.2f}% of game code)'
          f' + {len(other)} library/dead/other')


# ---- cleanup artefacts (docs/cleanup-plan.md, "Hook-era leftovers the plan removes") -------------------------------------
# Every count runs over src/ (*.cpp, *.h) with comments blanked; `sources` is srcscan.load_sources().

def count_matches(sources, pattern):
    """Number of matches of `pattern` over all files."""
    rx = re.compile(pattern, re.M)
    return sum(len(rx.findall(s.code)) for s in sources)


def count_files(sources, pattern):
    """Number of files with at least one match of `pattern`."""
    rx = re.compile(pattern, re.M)
    return sum(1 for s in sources if rx.search(s.code))


def forwarder_counts(sources):
    """(EI_ORIGINAL sites, distinct addresses)."""
    sites = srcscan.forwarder_sites(sources)
    return len(sites), len({address for address, _, _ in sites})


def forwarded_from_several_files(sources):
    """Addresses that have an EI_ORIGINAL forwarder in more than one file."""
    files = defaultdict(set)
    for address, source, _ in srcscan.forwarder_sites(sources):
        files[address].add(source.path)
    return sum(1 for paths in files.values() if len(paths) > 1)


def naked_non_forwarders(sources):
    """`__declspec(naked)` functions with a real body: the declaration reaches `{` before EI_ORIGINAL."""
    stop = re.compile(r'EI_ORIGINAL|\{|;')
    count = 0
    for s in sources:
        for naked in re.finditer(r'__declspec\(naked\)', s.code):
            end = stop.search(s.code, naked.end())
            count += bool(end) and end.group(0) == '{'
    return count


def data_addresses(sources):
    """Distinct literals 0x0073xxxx-0x008Fxxxx: the game's .data / .bss (globals, vtables, strings)."""
    literal = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')
    found = {int(m.group(1), 16) for s in sources for m in literal.finditer(s.code)}
    return len({a for a in found if 0x730000 <= a <= 0x8FFFFF})


def own_at_helpers(sources):
    """Files that define their own raw-offset helper, `T& At(void* base, int offset) {`."""
    return count_files(sources, r'[\w&*>]\s+At\s*\([^()]*\)\s*(?:const\s*)?\{')


def fastcall_shims(sources):
    """(all, with exactly two parameters): hook shims `__fastcall Fn(T* self, void*, ...) {`, not naked."""
    shim = re.compile(r'\b__fastcall\s+(?:\w+::)?\w+\s*\(\s*[^(),]*\*\s*\w*\s*,\s*void\s*\*\s*\w*\s*'
                      r'((?:,[^()]*)?)\)\s*(?:const\s*)?\{')
    extra_parameters = []
    for s in sources:
        for m in shim.finditer(s.code):
            statement_start = re.split(r'[;{}]', s.code[max(0, m.start() - 120):m.start()])[-1]
            if 'naked' not in statement_start:
                extra_parameters.append(m.group(1).strip())
    return len(extra_parameters), sum(1 for rest in extra_parameters if not rest)


def structs_defined_twice(sources):
    """Top-level struct/class names that are defined (with a body) in more than one file."""
    definition = re.compile(r'^(?:struct|class)\s+(?:__declspec\(\w+\)\s+)?(\w+)(?:\s+final)?\s*(?::[^{;]*)?\{', re.M)
    files = defaultdict(set)
    for s in sources:
        for m in definition.finditer(s.code):
            files[m.group(1)].add(s.path)
    return sum(1 for paths in files.values() if len(paths) > 1)


# Plain "how often does this appear" counts: (label, regex).
RAW_ACCESS_COUNTS = [
    ('__asm blocks', r'\b__asm\b'),
    ('unknown_XXX fields', r'\bunknown_[0-9A-Fa-f]+\b'),
    ('At<> uses', r'\bAt<'),
    ('Field<> uses', r'\bField<'),
    ('ei::VCall uses', r'\bVCall\s*<'),
]
HOOK_ERA_COUNTS = [
    ('reinterpret_cast', r'\breinterpret_cast\b'),
    # Casts to a pointer type (C21): typing debt (void* parameters, base-class fields); the numeric
    # static_casts mark x87 rounding points and stay.
    ('static_cast to a pointer type', r'\bstatic_cast\s*<[^<>();]*\*\s*(?:const\s*)?>'),
    ('constexpr uintptr_t kX = 0x00...', r'\bconstexpr\s+(?:std::)?uintptr_t\s+k\w+\s*=\s*0[xX]00'),
    ('*At view structs', r'^[ \t]*(?:struct|class)\s+\w+At\b[^;{]*\{'),
    ('anonymous namespaces', r'\bnamespace\s*\{'),
    ('mfc:: uses', r'\bmfc::'),
    ('stlp:: uses', r'\bstlp::'),
    ('gamecrt:: uses', r'\bgamecrt::'),
    ('gameapi:: uses', r'\bgameapi::'),
    # The interface input shims (UI_INPUT_HANDLER / UI_WHEEL_HANDLER, ui_hud.cpp) register through
    # EI_REPLACE inside the macro, so each use counts as one registration like the replaced-function
    # count above.
    ('EI_REPLACE registrations', r'\b(?:EI_REPLACE(?:_METHOD|_SHORT|_DISPATCH)?|(?<!#define )UI_(?:INPUT|WHEEL)_HANDLER)\s*\('),
]


def cleanup_artefacts(sources):
    """The rows of the artefact block, as (label, value text)."""
    def counts(table):
        return [(label, f'{count_matches(sources, pattern):,}') for label, pattern in table]

    sites, distinct = forwarder_counts(sources)
    shims, shims_two = fastcall_shims(sources)
    return (
        [('EI_ORIGINAL forwarders', f'{sites:,} sites, {distinct:,} distinct addresses'),
         ('forwarded from several files', f'{forwarded_from_several_files(sources):,} addresses'),
         ('naked, not forwarders', f'{naked_non_forwarders(sources):,}'),
         ('files with #pragma float_control', f"{count_files(sources, r'#\s*pragma\s+float_control'):,}")]
        + counts(RAW_ACCESS_COUNTS)
        + [('files defining their own At()', f'{own_at_helpers(sources):,}'),
           ('game data addresses (distinct)', f'{data_addresses(sources):,}'),
           ('__fastcall Fn(T* self, void*) shims', f'{shims:,} ({shims_two:,} with exactly two parameters)'),
           ('struct names defined in several files', f'{structs_defined_twice(sources):,}')]
        + counts(HOOK_ERA_COUNTS))


def print_cleanup_artefacts():
    sources = srcscan.load_sources()
    lines = sum(s.raw.count('\n') for s in sources)
    print()
    print(f'cleanup artefacts (src/*.cpp, *.h: {len(sources)} files, {lines:,} lines)')
    for label, value in cleanup_artefacts(sources):
        print(f'  {label + ":":<42}{value}')


if __name__ == '__main__':
    status = main()
    print_cleanup_artefacts()
    sys.exit(status)
