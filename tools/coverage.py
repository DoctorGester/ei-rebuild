"""Source coverage of the suites: which of src/'s functions and lines the scenarios reach (docs/notes/harness.md,
"Source coverage").

usage:
  python tools/coverage.py run <ticket> [--no-build] [run_all arguments]
      build.cmd coverage, then the image and Rebirth suites on that build (work.py test <ticket> --rebirth --golden
      plus the arguments, with EI_BUILD_DIR at build/coverage), then the report
  python tools/coverage.py report [--runs FOLDER] [--top N]
      merges every run's coverage.dat under build/coverage/runs and writes build/coverage/report.md, the HTML view
      build/coverage/html/index.html and build/coverage/coverage.json

What the build records (tests/verify/harness_coverage.h): a flag byte per instrumented edge (MSVC's
/fsanitize-coverage=inline-bool-flag) and, through /Gh's _penter, every function entered. This tool finds the flags'
places in the code from the exe's disassembly (dumpbin: each flag is set by a `mov byte ptr [flag],1`), splits each
function into basic blocks at its jumps and their targets, and reads the lines from the PDB (dbghelp).
- A function is reached when its entry was marked.
- A line is reached when its function was and some of its code lies outside the blocks whose flags stayed clear.
  MSVC leaves blocks without a flag when another block's flag implies them, and its pruning leaves some paths with no
  flag at all (a loop that runs once), so a block without a flag counts as reached with its function. The line counts
  are an upper bound per function; the function counts are exact.
- Only code under src/ counts (headers too), and only functions that kept code of their own: /Ob1 inlines the
  functions marked inline, whose lines then count in their callers. The build keeps unreferenced functions
  (/OPT:NOREF), so dead code shows as unreached.
"""
import argparse
import bisect
import collections
import ctypes
import glob
import html
import json
import os
import pickle
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'coverage')
EXE = os.path.join(BUILD, 'ei-rebuild.exe')
MAGIC = b'EICOV1\0\0'

# The areas of the report, gameplay first (the ticket's order: units, combat, spells, AI, scripts, network, saves).
# A file in a src/game or src/engine folder goes to its folder's area (GAME_FOLDER_AREAS; docs/notes/file-layout.md);
# the other folders of src are areas of their own.
GAME_FOLDER_AREAS = {'saves': 'saves', 'ai': 'AI', 'units': 'units', 'combat': 'combat', 'spells': 'spells and items',
                     'items': 'spells and items', 'script': 'scripts', 'net': 'network', 'world': 'world',
                     'players': 'world', 'map': 'world', 'objects': 'world', 'ui': 'interface', 'app': 'app',
                     'console': 'app', 'mods': 'app', 'sound': 'sound and video', 'effects': 'game drawing',
                     'terrain': 'game drawing', 'models': 'game drawing', 'scene': 'game drawing',
                     'particles': 'game drawing', 'core': 'game core', 'data': 'game core'}
FOLDER_AREAS = {'render': 'renderer', 'platform': 'platform', 'lib': 'lib', 'audio': 'sound and video',
                'video': 'sound and video'}
AREA_ORDER = ['saves', 'AI', 'units', 'combat', 'spells and items', 'scripts', 'network', 'world', 'interface', 'app',
              'game drawing', 'sound and video', 'game core', 'lib', 'renderer', 'platform']
GAMEPLAY = set(AREA_ORDER[:8])


def area_of(rel):
    parts = rel.split('/')
    if len(parts) == 2:   # src/main.cpp
        return 'app'
    if parts[1] not in ('game', 'engine'):
        return FOLDER_AREAS.get(parts[1], 'game core')
    return GAME_FOLDER_AREAS.get(parts[2], 'game core')   # src/game/units/unit.cpp, src/engine/core/ref.cpp


def source_rel(path, cache={}):
    """src/... (forward slashes) for a PDB file name of ours, None for any other (the compiler's headers, SDL's
    sources, the build folder's generated files). The PDB names the checkout that compiled the object first (the
    compiler cache), so only the part from src/ on counts."""
    if path in cache:
        return cache[path]
    norm = path.replace('\\', '/')
    at = norm.lower().rfind('/src/')
    rel = None
    if at >= 0 and 'third_party' not in norm[:at].lower() and '/build/' not in norm[:at].lower():
        candidate = 'src/' + norm[at + 5:]
        top = candidate.split('/')[1]
        if top in FOLDER_AREAS or top in ('game', 'engine') or candidate == 'src/main.cpp':
            rel = candidate
    cache[path] = rel
    return rel


# ---- The exe: PE header, map file, disassembly, PDB lines ----

def pe_info(path):
    with open(path, 'rb') as f:
        head = f.read(4096)
    nt = struct.unpack_from('<I', head, 0x3C)[0]
    stamp = struct.unpack_from('<I', head, nt + 8)[0]
    image_base = struct.unpack_from('<Q', head, nt + 24 + 24)[0]
    size = struct.unpack_from('<I', head, nt + 24 + 56)[0]
    return stamp, size, image_base


def flag_range(map_path):
    start = end = None
    for line in open(map_path, encoding='latin-1'):
        m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+__sancov\$BoolFlag(Start|End)\s+([0-9a-fA-F]{16})\s', line)
        if m:
            if m.group(1) == 'Start':
                start = int(m.group(2), 16)
            else:
                end = int(m.group(2), 16)
            if start and end:
                break
    if not (start and end):
        sys.exit(f'coverage: no __sancov$BoolFlagStart/End in {map_path} (not a coverage build?)')
    return start, end


def dumpbin():
    root = os.environ.get('EI_VSROOT') or r'E:\bin\vs\buildtools'
    found = sorted(glob.glob(os.path.join(root, 'VC', 'Tools', 'MSVC', '*', 'bin', 'Hostx64', 'x64', 'dumpbin.exe')))
    if not found:
        sys.exit(f'coverage: no dumpbin.exe under {root} (EI_VSROOT names the Build Tools)')
    return found[-1]


INSTRUCTION = re.compile(r'  ([0-9A-F]{16}): (\S+)(?:\s+(.*))?$')
FLAG_STORE = re.compile(r'byte ptr \[([0-9A-F]+)h\],1$')
DIRECT = re.compile(r'([0-9A-F]{16})$')


def disassemble(flag_start, flag_end):
    """The instrumented functions (those that call _penter): a list of dicts with start, end, name, the block
    boundaries and the flags (address, index from __sancov$BoolFlagStart)."""
    process = subprocess.Popen([dumpbin(), '/nologo', '/disasm:nobytes', EXE], stdout=subprocess.PIPE,
                               encoding='latin-1', errors='replace')
    functions = []
    current = None
    after_terminator = False

    def close(at):
        if current is not None and current['entry'] is not None:
            current['end'] = at
            current['bounds'] = sorted(b for b in current['bounds'] if current['start'] <= b < at)
            functions.append(current)

    last = None
    for line in process.stdout:
        if line.startswith('  '):
            m = INSTRUCTION.match(line.rstrip('\n'))
            if not m or current is None:
                continue
            address, mnemonic, operands = int(m.group(1), 16), m.group(2), m.group(3) or ''
            if mnemonic == 'CC' or (len(mnemonic) == 2 and all(c in '0123456789ABCDEF' for c in mnemonic)):
                continue   # padding bytes between functions
            last = address
            if after_terminator:
                current['bounds'].add(address)
                after_terminator = False
            if mnemonic == 'call':
                if operands == '_penter' and current['entry'] is None:
                    current['entry'] = address
            elif mnemonic == 'mov' and operands.startswith('byte ptr ['):
                f = FLAG_STORE.match(operands)
                if f:
                    target = int(f.group(1), 16)
                    if flag_start <= target < flag_end:
                        current['flags'].append((address, target - flag_start))
            elif mnemonic[0] == 'j':
                d = DIRECT.match(operands)
                if d:
                    current['bounds'].add(int(d.group(1), 16))
                after_terminator = True
            elif mnemonic == 'ret':
                after_terminator = True
        elif line.endswith(':\n') and not line.startswith(('Dump of file', 'File Type')):
            close_at = last + 1 if last is not None else None
            if current is not None:
                close(close_at)
            current = {'name': line[:-2].strip(), 'start': None, 'end': None, 'entry': None, 'bounds': set(),
                       'flags': []}
            after_terminator = False
            last = None
        # The label's address comes from its first instruction.
        if current is not None and current['start'] is None and last is not None:
            current['start'] = last
            current['bounds'].add(last)
    if current is not None and last is not None:
        close(last + 1)
    process.wait()
    functions.sort(key=lambda f: f['start'])
    return functions


class Dbghelp:
    """The PDB's line records through dbghelp (the Debugging Tools' copy when installed, else Windows')."""

    def __init__(self, exe, image_base, size):
        from ctypes import wintypes
        tools = r'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbghelp.dll'
        self.dll = ctypes.WinDLL(tools if os.path.exists(tools) else 'dbghelp')
        d = self.dll
        d.SymSetOptions.argtypes = [wintypes.DWORD]
        d.SymInitializeW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.BOOL]
        d.SymLoadModuleExW.restype = ctypes.c_uint64
        d.SymLoadModuleExW.argtypes = [wintypes.HANDLE, wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                       ctypes.c_uint64, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
        d.SymSetOptions(0x2 | 0x10 | 0x200)   # SYMOPT_UNDNAME, SYMOPT_LOAD_LINES, SYMOPT_FAIL_CRITICAL_ERRORS
        self.handle = wintypes.HANDLE(0x45494356)   # any value of our own: no process is read
        if not d.SymInitializeW(self.handle, os.path.dirname(exe), False):
            sys.exit('coverage: SymInitialize failed')
        self.base = d.SymLoadModuleExW(self.handle, None, exe, None, image_base, size, None, 0)
        if not self.base:
            sys.exit(f'coverage: dbghelp could not load {exe}')

    def undecorate(self, name):
        """A decorated label's name without its parameters (UNDNAME_NAME_ONLY); others stay as they are."""
        if not name.startswith('?'):
            return name
        buffer = ctypes.create_string_buffer(4096)
        if self.dll.UnDecorateSymbolName(name.encode('latin-1'), buffer, len(buffer), 0x1000):
            return buffer.value.decode('latin-1')
        return name

    def lines(self):
        from ctypes import wintypes

        class SRCCODEINFOW(ctypes.Structure):
            _fields_ = [('SizeOfStruct', wintypes.DWORD), ('Key', ctypes.c_void_p), ('ModBase', ctypes.c_uint64),
                        ('Obj', ctypes.c_wchar * 261), ('FileName', ctypes.c_wchar * 261),
                        ('LineNumber', wintypes.DWORD), ('Address', ctypes.c_uint64)]
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, ctypes.POINTER(SRCCODEINFOW), ctypes.c_void_p)
        records = []
        files = {}

        def callback(info, _context):
            i = info.contents
            name = i.FileName
            records.append((i.Address, files.setdefault(name, name), i.LineNumber))
            return True
        self.dll.SymEnumLinesW.argtypes = [wintypes.HANDLE, ctypes.c_uint64, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                           callback_type, ctypes.c_void_p]
        if not self.dll.SymEnumLinesW(self.handle, self.base, None, None, callback_type(callback), None):
            sys.exit('coverage: SymEnumLines failed')
        records.sort()
        return records


def static_analysis():
    """The functions with their blocks, flags and lines, kept in build/coverage/static.pickle for the exe it was made
    from."""
    stamp, size, image_base = pe_info(EXE)
    cache = os.path.join(BUILD, 'static.pickle')
    if os.path.exists(cache):
        with open(cache, 'rb') as f:
            data = pickle.load(f)
        if data['stamp'] == stamp and data['size'] == size and os.path.getmtime(cache) >= os.path.getmtime(EXE):
            return data
    print('coverage: reading the code (dumpbin) and the PDB (dbghelp) of the exe', flush=True)
    flag_start, flag_end = flag_range(os.path.join(BUILD, 'ei-rebuild.map'))
    functions = disassemble(flag_start, flag_end)
    symbols = Dbghelp(EXE, image_base, size)
    records = symbols.lines()
    starts = [f['start'] for f in functions]
    for f in functions:
        f['lines'] = []
    for address, path, line in records:
        i = bisect.bisect_right(starts, address) - 1
        if i < 0 or address >= functions[i]['end']:
            continue
        rel = source_rel(path)
        if rel:
            functions[i]['lines'].append((address, rel, line))
    kept = []
    for f in functions:
        if not f['lines']:
            continue
        f['name'] = symbols.undecorate(f['name'])
        # The function's file: the line its _penter call is on (its opening line; code inlined at its start may come
        # from a header).
        at = bisect.bisect_right([a for a, _, _ in f['lines']], f['entry']) - 1
        f['file'] = f['lines'][max(at, 0)][1]
        f['line'] = min(l for _, rel, l in f['lines'] if rel == f['file'])
        f['size'] = f['end'] - f['start']
        kept.append(f)
    data = {'stamp': stamp, 'size': size, 'image_base': image_base, 'flag_start': flag_start - image_base,
            'functions': kept}
    with open(cache, 'wb') as f:
        pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)
    return data


# ---- The runs ----

def dump_paths(runs):
    """Every run's coverage.dat (a pair's peer writes into its run's peer folder)."""
    return sorted(glob.glob(os.path.join(runs, '*', 'coverage.dat')) +
                  glob.glob(os.path.join(runs, '*', 'peer', 'coverage.dat')))


def read_dumps(runs, stamp, size, flag_start):
    flags, slots = set(), set()
    used, stale = [], []
    for path in dump_paths(runs):
        with open(path, 'rb') as f:
            data = f.read()
        if data[:8] != MAGIC or len(data) < 8 + 4 * 6:
            stale.append(path)
            continue
        words = struct.unpack_from(f'<{(len(data) - 8) // 4}I', data, 8)
        if words[0] != stamp or words[1] != size or words[2] != flag_start:
            stale.append(path)
            continue
        at = 4
        n = words[at]
        flags.update(words[at + 1:at + 1 + n])
        at += 1 + n
        n = words[at]
        slots.update(words[at + 1:at + 1 + n])
        used.append(path)
    return flags, slots, used, stale


# ---- Coverage per function, file, area ----

def evaluate(data, flags, slots):
    """Per function: reached, and per line record whether it counts as reached; per file the line sets."""
    image_base = data['image_base']
    files = collections.defaultdict(lambda: {'lines': {}, 'functions': []})
    for f in data['functions']:
        first, last = (f['start'] - image_base) >> 4, (f['end'] - 1 - image_base) >> 4
        entered = any(s in slots for s in range(first, last + 1))
        flagged = any(index in flags for _, index in f['flags'])
        f['reached'] = entered or flagged
        bounds = f['bounds'] + [f['end']]
        # Per block: True reached, False its flag stayed clear, None no flag (counts with the function).
        state = [None] * (len(bounds) - 1)
        for address, index in f['flags']:
            b = bisect.bisect_right(bounds, address) - 1
            if 0 <= b < len(state):
                state[b] = state[b] or index in flags
        records = f['lines']
        reached_lines, all_lines = set(), set()
        for i, (address, rel, line) in enumerate(records):
            end = records[i + 1][0] if i + 1 < len(records) else f['end']
            all_lines.add((rel, line))
            if not f['reached']:
                continue
            b = bisect.bisect_right(bounds, address) - 1
            reached = False
            while 0 <= b < len(state) and bounds[b] < max(end, address + 1):
                if state[b] is not False:
                    reached = True
                    break
                b += 1
            if reached:
                reached_lines.add((rel, line))
        f['line_count'] = len({l for r, l in all_lines if r == f['file']})
        f['reached_line_count'] = len({l for r, l in reached_lines if r == f['file']})
        for rel, line in all_lines:
            entry = files[rel]['lines']
            entry[line] = entry.get(line, False) or (rel, line) in reached_lines
        files[f['file']]['functions'].append(f)
    return files


def summarise(files):
    rows = []
    for rel, info in files.items():
        functions = info['functions']
        rows.append({
            'file': rel, 'area': area_of(rel),
            'functions': len(functions), 'functions_reached': sum(f['reached'] for f in functions),
            'lines': len(info['lines']), 'lines_reached': sum(info['lines'].values()),
            'unreached_bytes': sum(f['size'] for f in functions if not f['reached']),
        })
    return rows


def pct(a, b):
    return f'{100.0 * a / b:.1f}%' if b else '-'


def totals(rows):
    keys = ('functions', 'functions_reached', 'lines', 'lines_reached', 'unreached_bytes')
    return {k: sum(r[k] for r in rows) for k in keys}


def write_report(data, files, rows, used, stale, top):
    os.makedirs(BUILD, exist_ok=True)
    by_area = collections.defaultdict(list)
    for r in rows:
        by_area[r['area']].append(r)
    functions = [f for f in data['functions'] if f['file'] in files]
    unreached = sorted((f for f in functions if not f['reached']), key=lambda f: -f['size'])
    partial = sorted((f for f in functions if f['reached'] and f['line_count'] - f['reached_line_count'] >= 5),
                     key=lambda f: -(f['line_count'] - f['reached_line_count']))
    t = totals(rows)
    out = []
    w = out.append
    w('# Source coverage of the suites\n')
    w(f'Made by `python tools/coverage.py report` from {len(used)} runs\' coverage.dat (build/coverage/runs; '
      f'{len(stale)} of another build left out), on the coverage build (`build.cmd coverage`: MSVC '
      '/fsanitize-coverage=inline-bool-flag and /Gh). How it counts: tools/coverage.py\'s docstring; HTML view: '
      'build/coverage/html/index.html.\n')
    w('## Totals (src/)\n')
    w(f'- Functions reached: {t["functions_reached"]} of {t["functions"]} ({pct(t["functions_reached"], t["functions"])})')
    w(f'- Lines reached: {t["lines_reached"]} of {t["lines"]} ({pct(t["lines_reached"], t["lines"])}; an upper bound)')
    w(f'- Code in unreached functions: {t["unreached_bytes"] // 1024} KB\n')
    w('## By area (gameplay first)\n')
    w('| area | files | functions reached | lines reached | unreached code |')
    w('|---|---:|---:|---:|---:|')
    for area in AREA_ORDER:
        if area not in by_area:
            continue
        a = totals(by_area[area])
        w(f'| {area} | {len(by_area[area])} | {a["functions_reached"]} / {a["functions"]} '
          f'({pct(a["functions_reached"], a["functions"])}) | {a["lines_reached"]} / {a["lines"]} '
          f'({pct(a["lines_reached"], a["lines"])}) | {a["unreached_bytes"] // 1024} KB |')
    w('\n## Ranked gaps\n')
    w('Per area in the order above: the files with the most unreached lines, then the biggest functions no run '
      'entered (code bytes, lines in their file).\n')
    for area in AREA_ORDER:
        if area not in by_area:
            continue
        w(f'### {area}\n')
        for r in sorted(by_area[area], key=lambda r: -(r['lines'] - r['lines_reached']))[:8]:
            if r['lines'] == r['lines_reached']:
                continue
            w(f'- {r["file"]}: {r["lines"] - r["lines_reached"]} lines unreached of {r["lines"]}; functions '
              f'{r["functions_reached"]} / {r["functions"]}')
        biggest = [f for f in unreached if area_of(f['file']) == area][:10]
        if biggest:
            w('')
            w('| unreached function | file | bytes | lines |')
            w('|---|---|---:|---:|')
            for f in biggest:
                w(f'| `{f["name"]}` | {f["file"]}:{f["line"]} | {f["size"]} | {f["line_count"]} |')
        w('')
    w(f'## The {top} biggest unreached functions\n')
    w('| function | area | file | bytes | lines |')
    w('|---|---|---|---:|---:|')
    for f in unreached[:top]:
        w(f'| `{f["name"]}` | {area_of(f["file"])} | {f["file"]}:{f["line"]} | {f["size"]} | {f["line_count"]} |')
    w(f'\n## Reached functions with the most unreached lines\n')
    w('| function | area | file | lines unreached / lines |')
    w('|---|---|---|---:|')
    for f in partial[:top]:
        w(f'| `{f["name"]}` | {area_of(f["file"])} | {f["file"]}:{f["line"]} | '
          f'{f["line_count"] - f["reached_line_count"]} / {f["line_count"]} |')
    w('\n## Every file\n')
    w('| file | area | functions reached | lines reached |')
    w('|---|---|---:|---:|')
    for r in sorted(rows, key=lambda r: (AREA_ORDER.index(r['area']), r['file'])):
        w(f'| {r["file"]} | {r["area"]} | {r["functions_reached"]} / {r["functions"]} | {r["lines_reached"]} / '
          f'{r["lines"]} ({pct(r["lines_reached"], r["lines"])}) |')
    with open(os.path.join(BUILD, 'report.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    with open(os.path.join(BUILD, 'coverage.json'), 'w', encoding='utf-8') as f:
        json.dump({'totals': t, 'files': rows, 'runs': len(used),
                   'functions': [{'name': f['name'], 'file': f['file'], 'line': f['line'], 'bytes': f['size'],
                                  'lines': f['line_count'], 'lines_reached': f['reached_line_count'],
                                  'reached': f['reached']} for f in functions]}, f, indent=0)
    return t


STYLE = ('body{font:14px sans-serif;margin:1.5em}table{border-collapse:collapse}td,th{padding:2px 8px;'
         'border-bottom:1px solid #ddd;text-align:right}td:first-child,th:first-child{text-align:left}'
         '.bar{display:inline-block;height:9px;background:#5a5}.bg{display:inline-block;width:100px;height:9px;'
         'background:#e66}pre{font:12px monospace;margin:0}.r{background:#dfd}.u{background:#fcc}.n{color:#888}'
         'a{color:#226}')


def bar(a, b):
    width = int(100 * a / b) if b else 0
    return f'<span class="bg"><span class="bar" style="width:{width}px"></span></span>'


def write_html(files, rows):
    folder = os.path.join(BUILD, 'html')
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(folder)
    index = [f'<!doctype html><meta charset="utf-8"><title>Source coverage</title><style>{STYLE}</style>',
             '<h1>Source coverage of the suites</h1><p>build/coverage/report.md has the ranked gaps.</p>']
    t = totals(rows)
    index.append(f'<p>Functions {t["functions_reached"]} / {t["functions"]} '
                 f'({pct(t["functions_reached"], t["functions"])}), lines {t["lines_reached"]} / {t["lines"]} '
                 f'({pct(t["lines_reached"], t["lines"])})</p>')
    by_area = collections.defaultdict(list)
    for r in rows:
        by_area[r['area']].append(r)
    for area in AREA_ORDER:
        if area not in by_area:
            continue
        a = totals(by_area[area])
        index.append(f'<h2>{html.escape(area)}: lines {pct(a["lines_reached"], a["lines"])}, functions '
                     f'{pct(a["functions_reached"], a["functions"])}</h2><table><tr><th>file</th><th>lines</th>'
                     '<th></th><th>functions</th><th>unreached code</th></tr>')
        for r in sorted(by_area[area], key=lambda r: -(r['lines'] - r['lines_reached'])):
            page = r['file'].replace('/', '_') + '.html'
            index.append(f'<tr><td><a href="{page}">{html.escape(r["file"])}</a></td><td>{r["lines_reached"]} / '
                         f'{r["lines"]}</td><td>{bar(r["lines_reached"], r["lines"])}</td><td>'
                         f'{r["functions_reached"]} / {r["functions"]}</td><td>{r["unreached_bytes"]}</td></tr>')
        index.append('</table>')
        for r in by_area[area]:
            write_file_page(folder, r, files[r['file']])
    with open(os.path.join(folder, 'index.html'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(index))


def write_file_page(folder, row, info):
    path = os.path.join(ROOT, row['file'])
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            source = f.read().split('\n')
    except OSError:
        source = []
    marks = info['lines']
    out = [f'<!doctype html><meta charset="utf-8"><title>{html.escape(row["file"])}</title><style>{STYLE}</style>',
           f'<p><a href="index.html">index</a></p><h1>{html.escape(row["file"])}</h1>',
           f'<p>lines {row["lines_reached"]} / {row["lines"]}, functions {row["functions_reached"]} / '
           f'{row["functions"]}</p><table><tr><th>function</th><th>line</th><th>bytes</th><th>lines reached</th>'
           '</tr>']
    for f in sorted(info['functions'], key=lambda f: f['line']):
        cls = 'r' if f['reached'] else 'u'
        out.append(f'<tr class="{cls}"><td><a href="#L{f["line"]}">{html.escape(f["name"])}</a></td>'
                   f'<td>{f["line"]}</td><td>{f["size"]}</td><td>{f["reached_line_count"]} / {f["line_count"]}'
                   '</td></tr>')
    out.append('</table><br>')
    for number, text in enumerate(source, 1):
        cls = 'n' if number not in marks else ('r' if marks[number] else 'u')
        out.append(f'<pre id="L{number}" class="{cls}">{number:5d}  {html.escape(text)}</pre>')
    with open(os.path.join(folder, row['file'].replace('/', '_') + '.html'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


def report(runs, top):
    if not os.path.exists(EXE):
        sys.exit(f'coverage: no {EXE} (build.cmd coverage)')
    data = static_analysis()
    flags, slots, used, stale = read_dumps(runs, data['stamp'], data['size'], data['flag_start'])
    if not used:
        sys.exit(f'coverage: no coverage.dat of this build under {runs}')
    files = evaluate(data, flags, slots)
    rows = summarise(files)
    t = write_report(data, files, rows, used, stale, top)
    write_html(files, rows)
    print(f'coverage: {len(used)} runs ({len(stale)} of another build left out); functions '
          f'{t["functions_reached"]} / {t["functions"]} ({pct(t["functions_reached"], t["functions"])}), lines '
          f'{t["lines_reached"]} / {t["lines"]} ({pct(t["lines_reached"], t["lines"])})')
    print(f'coverage: {os.path.join(BUILD, "report.md")}, {os.path.join(BUILD, "html", "index.html")}')


def run(ticket, build, extra):
    if build:
        code = subprocess.call(['cmd', '/c', os.path.join(ROOT, 'build.cmd'), 'coverage'])
        if code:
            sys.exit(f'coverage: build.cmd coverage failed ({code})')
    for path in dump_paths(os.path.join(BUILD, 'runs')):
        if os.path.isfile(path) and not os.path.islink(path):
            os.remove(path)   # run_all empties the folders it runs in; this drops the others' dumps
    env = dict(os.environ, EI_BUILD_DIR=BUILD)
    subprocess.call([sys.executable, os.path.join(ROOT, 'tools', 'work.py'), 'test', ticket, '--rebirth', '--golden']
                    + extra, env=env)
    report(os.path.join(BUILD, 'runs'), 40)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='command', required=True)
    r = sub.add_parser('run')
    r.add_argument('ticket')
    r.add_argument('--no-build', action='store_true')
    p = sub.add_parser('report')
    p.add_argument('--runs', default=os.path.join(BUILD, 'runs'))
    p.add_argument('--top', type=int, default=40)
    args, extra = ap.parse_known_args(argv)
    if args.command == 'run':
        run(args.ticket, not args.no_build, extra)
    else:
        if extra:
            ap.error(f'unknown arguments {extra}')
        report(args.runs, args.top)


if __name__ == '__main__':
    main(sys.argv[1:])
