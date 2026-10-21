"""The data seam (S18): every use of game.exe's data by its original address goes through
ei::GameAddress / GameData / GamePointer (src/game/game_data.h), so the standalone image can keep a
section somewhere else than game.exe's address (docs/notes/standalone.md, "Own data").

A literal of the exe's data range (0073B000-007D7000: the import address table, .rdata, .data, its
zero-filled tail .bss, .rsrc) in src/, tests/ or include/ is one of:
  seam       the address argument of GameAddress / GameData<T> / GamePointer<T> / DataId
  helper     the address argument of a function whose every definition hands that parameter to the
             seam (per-file helpers such as GameFloat(std::uintptr_t)); found from the definitions
  id         the value of a constant (constexpr std::uintptr_t kName = 0x...) whose every use is the
             address argument of the seam or a helper
  generated  src/standalone/static_init.cpp / static_exit.cpp: an address argument of the visitor's
             operations, which resolve it (static_init.h)
  raw        anything else: read at game.exe's address whatever the image does
A section is sealed (config/data_seam.csv) once its raw count is 0; the check keeps it there and holds
every other file's raw count per section to the baseline (it may only go down).

usage: python tools/data_seam.py [--check | --update | --list SECTION [--all] | --apply SECTION... [--files F...]]
  (no option)      counts per section
  --check          exit 1 when a sealed section has a raw use or a file has more raw uses than its baseline
  --update         rewrite the baseline in config/data_seam.csv from the current counts (sealed lines kept)
  --list SECTION   the raw uses of one section (--all: every use, with its kind)
  --apply SECTION  rewrites the casts of that section's literals (reinterpret_cast<T*>(0x...) and
                   *reinterpret_cast<T*>(0x...)) and the raw casts in the helpers called with them to the
                   seam, adding the include; 'all' for every section
"""
import argparse, collections, csv, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, 'config', 'data_seam.csv')
TOPS = ('src', 'tests', 'include')

# game.exe 1.06's data sections (tools/pe.py): .rdata 0073B000 (its first 0x820 bytes the import
# address table), .data 00783000 with its zero-filled tail from 0079C000, .rsrc 007CD000; SizeOfImage
# ends at 007D7000.
SECTIONS = [('iat', 0x0073B000, 0x0073B820), ('rdata', 0x0073B820, 0x00783000), ('data', 0x00783000, 0x0079C000),
            ('bss', 0x0079C000, 0x007CD000), ('rsrc', 0x007CD000, 0x007D7000)]
OPENERS = {'GameAddress', 'GameData', 'GamePointer', 'DataId'}
# The visitor operations of the generated static initialisers and exit functions whose first argument is
# a data address, and At (any argument).
GENERATED = ('src/standalone/static_init.cpp', 'src/standalone/static_exit.cpp')
GENERATED_FIRST = {'Store', 'StoreFloat', 'Fill', 'Once', 'NonZero', 'Release', 'Dword', 'Float', 'Double', 'Import'}

LITERAL = re.compile(r'(?<![\w.])0[xX]([0-9A-Fa-f]{1,8})[uUlL]*(?![\w.])')
_TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|R"([^(\s]*)\(.*?\)\1"|"(?:\\.|[^"\\\n])*"'
                    r"|'(?:\\(?:x[0-9A-Fa-f]+|[0-7]{1,3}|.)|[^'\\\n])'", re.S)
CONST_ID = re.compile(r'\b(?:constexpr|const)\s+(?:static\s+)?(?:const\s+)?(?:std::)?(?:uintptr_t|uint32_t)\s+(\w+)\s*'
                      r'(?:\[[^\]]*\])?\s*=')
UINTPTR_PARAM = re.compile(r'^(?:const\s+)?(?:std::)?uintptr_t\s+(\w+)$')
DEFINITION = re.compile(r'\b([A-Za-z_]\w*)\s*(?:<[^<>(){};]*>)?\s*\(([^(){};]*)\)\s*(?:const\s*)?(?:noexcept\s*)?\{')
NOT_FUNCTIONS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'catch', 'decltype'}


def section_of(value):
    for name, lo, hi in SECTIONS:
        if lo <= value < hi:
            return name
    return None


def blank(text):
    """Comments blanked to spaces, string and character literal contents to spaces (quotes kept):
    the same offsets and lines as the text."""
    def sub(m):
        t = m.group(0)
        if t.startswith('/'):
            return re.sub(r'[^\n]', ' ', t)
        return t[0] + re.sub(r'[^\n]', ' ', t[1:-1]) + t[-1]
    return _TOKEN.sub(sub, text)


def files():
    out = []
    for top in TOPS:
        for d, _, names in os.walk(os.path.join(ROOT, top)):
            for n in names:
                if n.endswith(('.cpp', '.h', '.inc')):
                    full = os.path.join(d, n)
                    out.append(os.path.relpath(full, ROOT).replace(os.sep, '/'))
    return sorted(out)


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8', errors='replace', newline='') as f:
        return f.read()


def skip_back_ws(code, i):
    while i >= 0 and code[i] in ' \t\r\n':
        i -= 1
    return i


def callee_before(code, paren):
    """The name called with the '(' at `paren` (template arguments skipped, qualifiers and d. kept)."""
    j = skip_back_ws(code, paren - 1)
    if j >= 0 and code[j] == '>':
        depth = 0
        while j >= 0:
            if code[j] == '>':
                depth += 1
            elif code[j] == '<':
                depth -= 1
                if depth == 0:
                    break
            elif code[j] in ';{}':
                return None
            j -= 1
        j = skip_back_ws(code, j - 1)
    end = j + 1
    while j >= 0 and (code[j].isalnum() or code[j] in '_:.'):
        j -= 1
    name = code[j + 1:end]
    return name or None


def call_of(code, at):
    """(callee, argument index, '(' offset) of the call whose argument list holds offset `at` at its top level."""
    depth, commas, i = 0, 0, at - 1
    while i >= 0:
        c = code[i]
        if c in ')]':
            depth += 1
        elif c in '([':
            if depth == 0:
                if c == '[':
                    return None
                return callee_before(code, i), commas, i
            depth -= 1
        elif c == ',' and depth == 0:
            commas += 1
        elif c in ';{}' and depth == 0:
            return None
        i -= 1
    return None


def last(name):
    return re.split(r'::|\.', name)[-1] if name else name


CASTS = ('std::uintptr_t', 'uintptr_t', 'std::uint32_t', 'static_cast')


def argument_owner(code, at):
    """(callee, argument index) of the call that takes the expression starting at `at` as an argument,
    looking through std::uintptr_t(...) wrappers; the expression must begin the argument."""
    j = skip_back_ws(code, at - 1)
    while True:
        if j < 0 or code[j] not in '(,':
            return None
        owner = call_of(code, j + 1)
        if not owner or owner[0] is None:
            return None
        callee, index, paren = owner
        if callee in CASTS or last(callee) == 'uintptr_t':
            # A functional cast around the expression: what takes the cast?
            start = paren - 1
            start = skip_back_ws(code, start)
            if code[start] == '>':   # static_cast<...>
                callee_start = start
                depth = 0
                while callee_start >= 0:
                    if code[callee_start] == '>':
                        depth += 1
                    elif code[callee_start] == '<':
                        depth -= 1
                        if depth == 0:
                            break
                    callee_start -= 1
                start = callee_start - 1
            while start >= 0 and (code[start].isalnum() or code[start] in '_:'):
                start -= 1
            if index != 0:
                return None
            j = skip_back_ws(code, start)
            continue
        return callee, index


class Scan:
    def __init__(self):
        self.paths = files()
        self.code = {p: blank(read(p)) for p in self.paths}
        self.helpers = self.find_helpers()
        self.uses = []   # (path, offset, value, section, kind, detail)
        self.ids = {}    # name -> list of (path, offset)
        self.scan()

    def find_helpers(self):
        """name -> {argument index: True when every definition hands it to the seam}."""
        verdict = collections.defaultdict(dict)
        self.helper_defs = collections.defaultdict(list)   # (name, index) -> [(path, (start, end), routed, param)]
        for path, code in self.code.items():
            for m in DEFINITION.finditer(code):
                name = m.group(1)
                if name in NOT_FUNCTIONS or name in OPENERS:
                    continue
                params = [p.strip() for p in m.group(2).split(',')] if m.group(2).strip() else []
                body = self.body(code, m.end() - 1)
                for index, p in enumerate(params):
                    pm = UINTPTR_PARAM.match(re.sub(r'\s*=.*$', '', p))
                    if not pm:
                        continue
                    routed = self.routes(body, pm.group(1))
                    span = (m.end() - 1, m.end() - 1 + len(body))
                    self.helper_defs[(name, index)].append((path, span, routed, pm.group(1)))
                    verdict[name][index] = verdict[name].get(index, True) and routed
        return verdict

    @staticmethod
    def body(code, brace):
        depth = 0
        for i in range(brace, len(code)):
            if code[i] == '{':
                depth += 1
            elif code[i] == '}':
                depth -= 1
                if depth == 0:
                    return code[brace:i + 1]
        return code[brace:]

    def routes(self, body, param):
        """The parameter reaches memory only through the seam (or another helper's seam argument)."""
        p = re.escape(param)
        uses = [m.start() for m in re.finditer(r'\b' + p + r'\b', body)]
        if not uses:
            return False
        for at in uses:
            owner = argument_owner(body, at)
            if not owner:
                return False
            callee, index = owner
            if last(callee) in OPENERS:
                continue
            return False
        return True

    def classify(self, path, code, at, end):
        if path in GENERATED:
            owner = call_of(code, at)
            if owner and owner[0]:
                callee, index, _ = owner
                op = last(callee)
                if callee.startswith('d.') and ((op in GENERATED_FIRST and index == 0) or op == 'At'
                                                or (op == 'Thiscall' and index == 1)):
                    return 'generated', callee
        owner = argument_owner(code, at)
        if owner:
            callee, index = owner
            name = last(callee)
            if name in OPENERS and index == 0:
                return 'seam', callee
            if self.helpers.get(name, {}).get(index):
                return 'helper', callee
            if name.startswith('EI_'):
                return 'registration', callee
        # The value of a constant: classified once its uses are known.
        head = code[self.statement_start(code, at):at]
        m = None
        for m in CONST_ID.finditer(head):
            pass
        if m and ';' not in head[m.end():]:
            return 'id?', m.group(1)
        return 'raw', None

    def scan(self):
        pending = []
        for path in self.paths:
            code = self.code[path]
            for m in LITERAL.finditer(code):
                value = int(m.group(1), 16)
                sec = section_of(value)
                if not sec:
                    continue
                kind, detail = self.classify(path, code, m.start(), m.end())
                use = [path, m.start(), value, sec, kind, detail]
                self.uses.append(use)
                if kind == 'id?':
                    pending.append(use)
                    self.ids.setdefault(detail, [])
        # An id is seam when every other mention of its name is a seam or helper argument.
        self.id_failures = {}
        for name in self.ids:
            bad = []
            rx = re.compile(r'(?<![\w.])' + re.escape(name) + r'\b')
            # A constant of a .cpp file is seen there only; a header's wherever it is named.
            homes = {u[0] for u in pending if u[5] == name}
            where = self.paths if any(not h.endswith('.cpp') for h in homes) else sorted(homes)
            for path in where:
                code = self.code[path]
                if name not in code:
                    continue
                for m in rx.finditer(code):
                    if self.defines(code, m.start(), name):
                        continue
                    start = m.start()
                    while start >= 2 and code[start - 2:start] == '::':   # Class::kName
                        k = start - 2
                        while k > 0 and (code[k - 1].isalnum() or code[k - 1] == '_'):
                            k -= 1
                        start = k
                    owner = argument_owner(code, start)
                    ok = False
                    if owner:
                        callee, index = owner
                        n = last(callee)
                        ok = (n in OPENERS and index == 0) or bool(self.helpers.get(n, {}).get(index))
                    if not ok:
                        bad.append((path, m.start()))
            self.id_failures[name] = bad
        for use in pending:
            use[4] = 'raw' if self.id_failures[use[5]] else 'id'

    @staticmethod
    def statement_start(code, at):
        """After the ';' or block '{' / '}' before `at` (an initialiser's braces do not end a statement)."""
        i = at - 1
        while i >= 0:
            c = code[i]
            if c == ';':
                return i + 1
            if c in '{}':
                j = skip_back_ws(code, i - 1)
                if c == '}' or j < 0 or code[j] not in '=,{(':
                    if c == '}' and j >= 0 and code[j] in ',{':   # the end of a nested initialiser
                        i -= 1
                        continue
                    return i + 1
            i -= 1
        return 0

    @staticmethod
    def line_start(code, at):
        return code.rfind('\n', 0, at) + 1

    @staticmethod
    def defines(code, at, name):
        """`at` is the name in its own constant definition."""
        head = code[Scan.line_start(code, at):at + len(name)]
        return re.search(r'(?:constexpr|const)\s+(?:static\s+)?(?:const\s+)?(?:std::)?(?:uintptr_t|uint32_t)\s+' +
                         re.escape(name) + r'$', head) is not None

    def line(self, path, offset):
        return self.code[path].count('\n', 0, offset) + 1

    def counts(self):
        """{(path, section): raw count} and {section: Counter(kind)}."""
        raw = collections.Counter()
        kinds = collections.defaultdict(collections.Counter)
        for path, _, value, sec, kind, _ in self.uses:
            kinds[sec][kind] += 1
            if kind == 'raw':
                raw[(path, sec)] += 1
        return raw, kinds


def load_config():
    sealed, baseline = set(), {}
    if os.path.exists(CONFIG):
        with open(CONFIG, newline='') as f:
            for r in csv.DictReader(line for line in f if not line.startswith('#')):
                if r['file'] == '*sealed*':
                    sealed.add(r['section'])
                else:
                    baseline[(r['file'], r['section'])] = int(r['raw'])
    return sealed, baseline


def write_config(sealed, raw):
    with open(CONFIG, 'w', newline='') as f:
        f.write('# The data seam (tools/data_seam.py, S18): raw uses of game.exe\'s data per file and section, which\n'
                '# may only go down; a "*sealed*" line marks a section that must have none.\n')
        w = csv.writer(f, lineterminator='\n')
        w.writerow(['file', 'section', 'raw'])
        for s in sorted(sealed):
            w.writerow(['*sealed*', s, 0])
        for (path, sec), n in sorted(raw.items()):
            if n:
                w.writerow([path, sec, n])


def report(scan):
    raw, kinds = scan.counts()
    sealed, _ = load_config()
    print(f'{"section":8} {"uses":>6} {"seam":>6} {"helper":>6} {"id":>5} {"gen":>5} {"raw":>5} {"addresses":>9} {"raw addr":>8}')
    for name, _, _ in SECTIONS:
        k = kinds[name]
        addrs = {u[2] for u in scan.uses if u[3] == name}
        raw_addrs = {u[2] for u in scan.uses if u[3] == name and u[4] == 'raw'}
        print(f'{name:8} {sum(k.values()):6} {k["seam"]:6} {k["helper"]:6} {k["id"]:5} {k["generated"]:5} {k["raw"]:5} '
              f'{len(addrs):9} {len(raw_addrs):8}{"  sealed" if name in sealed else ""}')


def check(scan):
    raw, _ = scan.counts()
    sealed, baseline = load_config()
    problems = []
    for (path, sec), n in sorted(raw.items()):
        if sec in sealed:
            problems.append(f'{path}: {n} raw use(s) of the sealed section {sec} (python tools/data_seam.py --list {sec})')
        elif n > baseline.get((path, sec), 0):
            problems.append(f'{path}: {n} raw use(s) of {sec}, the baseline allows {baseline.get((path, sec), 0)}: '
                            f'use ei::GameData / GamePointer / GameAddress (src/game/game_data.h)')
    for p in problems:
        print('data_seam:', p)
    if problems:
        return 1
    print(f'data_seam: OK ({sum(raw.values())} raw uses left; sealed: {", ".join(sorted(sealed)) or "none"})')
    return 0


def list_uses(scan, section, every):
    for path, offset, value, sec, kind, detail in scan.uses:
        if sec == section and (every or kind == 'raw'):
            line = read(path).split('\n')[scan.line(path, offset) - 1].strip()
            extra = f' [{kind}{" " + detail if detail else ""}]' if every else ''
            print(f'{path}:{scan.line(path, offset)}: {value:08X}{extra}  {line[:140]}')
    for name, bad in sorted(scan.id_failures.items()):
        if bad and any(u[5] == name and u[3] == section for u in scan.uses):
            for path, offset in bad[:5]:
                print(f'  id {name} used outside the seam at {path}:{scan.line(path, offset)}')


# ---- --apply ------------------------------------------------------------------------------------

def match_angle(text, lt):
    depth = 0
    for i in range(lt, len(text)):
        if text[i] == '<':
            depth += 1
        elif text[i] == '>':
            depth -= 1
            if depth == 0:
                return i
        elif text[i] in ';{}':
            return None
    return None


def rewrite_casts(raw_text, code, wanted):
    """Rewrites reinterpret_cast<TYPE>(ARG) where ARG (optionally in std::uintptr_t(...)) is `wanted`(arg)
    true. Returns the new text and the count."""
    out, pos, n = [], 0, 0
    for m in re.finditer(r'reinterpret_cast\s*<', code):
        if m.start() < pos:
            continue
        gt = match_angle(code, m.end() - 1)
        if gt is None:
            continue
        type_text = raw_text[m.end():gt].strip()
        k = gt + 1
        while k < len(code) and code[k] in ' \t':
            k += 1
        if k >= len(code) or code[k] != '(':
            continue
        am = re.match(r'\(\s*(?:std::uintptr_t\s*\(\s*([^()]*?)\s*\)|([^()]*?))\s*\)', code[k:])
        if not am:
            continue
        arg = (am.group(1) if am.group(1) is not None else am.group(2)).strip()
        if not wanted(arg, m.start()):
            continue
        arg_text = raw_text[k:k + am.end()]
        arg_text = re.match(r'\(\s*(?:std::uintptr_t\s*\(\s*(.*?)\s*\)|(.*?))\s*\)$', arg_text, re.S)
        arg_raw = (arg_text.group(1) if arg_text.group(1) is not None else arg_text.group(2)).strip()
        end = k + am.end()
        start = m.start()
        j = skip_back_ws(code, start - 1)
        # A unary '*' (not a multiplication: nothing that ends an operand before it) on the cast itself
        # (no postfix operator after it, which would bind first).
        k2 = end
        while k2 < len(code) and code[k2] in ' \t':
            k2 += 1
        postfix = code.startswith(('[', '->', '(', '.'), k2)
        j2 = skip_back_ws(code, j - 1)
        operand = j2 >= 0 and (code[j2].isalnum() or code[j2] in '_)]') and not re.search(
            r'\b(?:return|case|else|const|auto)$', code[:j2 + 1])
        deref = j >= 0 and code[j] == '*' and not postfix and not operand
        if type_text.endswith('*'):
            pointee = type_text[:-1].rstrip()
            if deref:
                # unary '*' only: a '*' after an operand would be a multiplication by a pointer (invalid)
                start = j
                new = f'ei::GameData<{pointee}>({arg_raw})'
            else:
                new = f'ei::GamePointer<{pointee}>({arg_raw})'
        else:
            new = f'reinterpret_cast<{type_text}>(ei::GameAddress({arg_raw}))'
        out.append(raw_text[pos:start])
        out.append(new)
        pos = end
        n += 1
    out.append(raw_text[pos:])
    return ''.join(out), n


def add_include(text, path):
    if re.search(r'#include\s+"(?:game/)?game_data\.h"', text) or path == 'src/game/game_data.h':
        return text
    includes = list(re.finditer(r'^#include[^\n]*\n', text, re.M))
    line = '#include "game/game_data.h"\n'
    if path.startswith('src/game/'):
        line = '#include "game_data.h"\n'
    if not includes:
        pragma = re.search(r'^#pragma once[^\n]*\n', text, re.M)
        at = pragma.end() if pragma else 0
        return text[:at] + ('\n' if pragma else '') + line + text[at:]
    quoted = [m for m in includes if '"' in m.group(0)]
    at = (quoted or includes)[-1].end()
    return text[:at] + line + text[at:]


def apply(scan, sections, only):
    wanted_sections = {s for s, _, _ in SECTIONS} if 'all' in sections else set(sections)
    # helpers called with a literal of the sections: their raw casts of the parameter
    called = set()
    for path, offset, value, sec, kind, detail in scan.uses:
        if sec not in wanted_sections:
            continue
        owner = argument_owner(scan.code[path], offset)
        if owner and owner[0]:
            called.add((last(owner[0]), owner[1]))
    helper_params = collections.defaultdict(list)   # path -> [((body start, end), param)]
    for (name, index), defs in scan.helper_defs.items():
        if (name, index) in called:
            for path, span, routed, param in defs:
                if not routed:
                    helper_params[path].append((span, param))
    total = 0
    for path in scan.paths:
        if only and path not in only:
            continue
        if path in GENERATED:
            continue
        text = read(path)
        code = scan.code[path]

        def literal_wanted(arg):
            m = LITERAL.fullmatch(arg)
            return bool(m) and section_of(int(m.group(1), 16)) in wanted_sections

        spans = helper_params.get(path, [])

        def param_wanted(arg, at):
            # the helper's own parameter, inside that helper's body only
            return any(lo <= at < hi and arg == p for (lo, hi), p in spans)

        # Constants of the sections (ids) used outside the seam: their casts too.
        ids = {name for name, bad in scan.id_failures.items() if any(b[0] == path for b in bad)
               and any(u[5] == name and u[3] in wanted_sections for u in scan.uses)}

        def id_wanted(arg):
            m = re.match(r'(?:\w+::)*(\w+)\b', arg)
            return bool(m) and m.group(1) in ids

        new, n = rewrite_casts(text, code, lambda a, at: literal_wanted(a) or param_wanted(a, at) or id_wanted(a))
        if n:
            new = add_include(new, path)
            with open(os.path.join(ROOT, path), 'w', encoding='utf-8', newline='') as f:
                f.write(new)
            total += n
            print(f'{path}: {n}')
    print(f'{total} cast(s) rewritten')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--list')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--apply', nargs='+')
    ap.add_argument('--files', nargs='+')
    a = ap.parse_args()
    scan = Scan()
    if a.apply:
        apply(scan, a.apply, set(a.files or []))
        return 0
    if a.list:
        list_uses(scan, a.list, a.all)
        return 0
    if a.update:
        sealed, _ = load_config()
        raw, _ = scan.counts()
        write_config(sealed, raw)
        report(scan)
        return 0
    if a.check:
        return check(scan)
    report(scan)
    return 0


if __name__ == '__main__':
    sys.exit(main())
