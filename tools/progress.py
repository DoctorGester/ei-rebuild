"""Prints the "cleanup artefacts" block: the hook-era leftovers that docs/cleanup-plan.md wants to see fall (naked
functions, offset access, raw data addresses, duplicate layouts). tools/dashboard_data.py charts the same counts.

(The rewrite progress it reported first, functions rewritten against config/functions.csv, came from the
registrations and the function-to-original map; C20l removed the last of them: nothing of game.exe's code runs.)
"""
import re
from collections import defaultdict

import srcscan

# ---- cleanup artefacts (docs/cleanup-plan.md, "What is left and what removes it") -------------------------------------
# Every count runs over src/ (*.cpp, *.h) with comments blanked; `sources` is srcscan.load_sources().

def count_matches(sources, pattern):
    """Number of matches of `pattern` over all files."""
    rx = re.compile(pattern, re.M)
    return sum(len(rx.findall(s.code)) for s in sources)


def count_files(sources, pattern):
    """Number of files with at least one match of `pattern`."""
    rx = re.compile(pattern, re.M)
    return sum(1 for s in sources if rx.search(s.code))


def naked_functions(sources):
    """`__declspec(naked)` functions with a body (the declaration reaches `{` before a `;`)."""
    stop = re.compile(r'\{|;')
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
    """(all, with exactly two parameters): functions in the old thiscall-as-fastcall shape `Fn(T* self, void*, ...) {`
    (with or without the `__fastcall` keyword; the second parameter unnamed or called edx / unused), not naked."""
    shim = re.compile(r'\b(?:__fastcall\s+)?(?:\w+::)?\w+\s*\(\s*[^(),]*\*\s*\w*\s*,\s*void\s*\*\s*(?:/\*\s*\w+\s*\*/\s*)?'
                      r'(?:edx|unused)?\s*'
                      r'((?:,[^()]*)?)\)\s*(?:const\s*)?\{')
    extra_parameters = []
    for s in sources:
        for m in shim.finditer(s.code):
            statement_start = re.split(r'[;{}]', s.code[max(0, m.start() - 120):m.start()])[-1]
            if 'naked' not in statement_start:
                extra_parameters.append(m.group(1).strip())
    return len(extra_parameters), sum(1 for rest in extra_parameters if not rest)


STRUCT_DEFINITION = re.compile(
    r'^(?:struct|class)\s+(?:__declspec\(\w+\)\s+)?(\w+(?:::\w+)*)(?:\s+final)?\s*(?::(?!:)[^{;]*)?\{', re.M)


def defined_struct_names(code):
    """The struct/class names that `code` (comments blanked) defines with a body at the start of a line, as written
    (`net::Connection`), but those inside an anonymous namespace: those are the file's own and cannot clash."""
    scopes = None
    names = set()
    for m in STRUCT_DEFINITION.finditer(code):
        scopes = scopes or srcscan.Scopes(code)
        if '' not in (scopes.namespace_at(m.start()) or []):
            names.add(m.group(1))
    return names


def structs_defined_twice(sources):
    """Struct/class names that are defined (with a body, outside an anonymous namespace) in more than one file."""
    files = defaultdict(set)
    for s in sources:
        for name in defined_struct_names(s.code):
            files[name].add(s.path)
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
]


def cleanup_artefacts(sources):
    """The rows of the artefact block, as (label, value text)."""
    def counts(table):
        return [(label, f'{count_matches(sources, pattern):,}') for label, pattern in table]

    shims, shims_two = fastcall_shims(sources)
    return (
        [('naked functions', f'{naked_functions(sources):,}'),
         ('files with #pragma float_control', f"{count_files(sources, r'#\s*pragma\s+float_control'):,}")]
        + counts(RAW_ACCESS_COUNTS)
        + [('files defining their own At()', f'{own_at_helpers(sources):,}'),
           ('game data addresses (distinct)', f'{data_addresses(sources):,}'),
           ('Fn(T* self, void*) shims', f'{shims:,} ({shims_two:,} with exactly two parameters)'),
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
    print_cleanup_artefacts()
