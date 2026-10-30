"""The data seam (S18): every use of game.exe's data by its original address goes through
ei::GameData / GamePointer / DataId (src/game/game_data.h) or the harness's ExeTableId, so that
ei_vectors.exe's replay can hand a stand-in and no code of the image reads game.exe's data by a raw cast
(docs/notes/standalone.md, "Own data").

A literal of the exe's data range (0073B000-007D7000: the import address table, .rdata, .data, its
zero-filled tail .bss, .rsrc) in src/, tests/ or include/ is one of:
  seam       the address argument of GameData<T> / GamePointer<T> / DataId / ExeTableId
  helper     the address argument of a function whose every definition hands that parameter to the
             seam (per-file helpers such as GameFloat(std::uintptr_t)); found from the definitions
  id         the value of a constant (constexpr std::uintptr_t kName = 0x...) whose every use is the
             address argument of the seam or a helper, or the whole value of an alias constant whose every use is
  raw        anything else: read at game.exe's address whatever the image does
Every section is sealed (S18c, S18g): the check fails on any raw use.

usage: python tools/data_seam.py [--check | --list SECTION [--all]]
  (no option)      counts per section
  --check          exit 1 when there is a raw use
  --list SECTION   the raw uses of one section (--all: every use, with its kind)
"""
import argparse, collections, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOPS = ('src', 'tests', 'include')

# game.exe 1.06's data sections (tools/pe.py): .rdata 0073B000 (its first 0x820 bytes the import
# address table), .data 00783000 with its zero-filled tail from 0079C000, .rsrc 007CD000; SizeOfImage
# ends at 007D7000. "data" and "bss" are split at 0079BF70 (the key bindings' action table, 0x200 bytes, the object
# that runs across the file's 0079C000), as S18b moved "bss" alone.
SECTIONS = [('iat', 0x0073B000, 0x0073B820), ('rdata', 0x0073B820, 0x00783000), ('data', 0x00783000, 0x0079BF70),
            ('bss', 0x0079BF70, 0x007CD000), ('rsrc', 0x007CD000, 0x007D7000)]
# ExeTableId: a game.exe table the harness keeps as an id, never read (tests/verify/harness_digest.cpp).
OPENERS = {'GameData', 'GamePointer', 'DataId', 'ExeTableId'}

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
        """name -> {argument index: True when every definition hands it to the seam}, directly or through
        another helper (TraceSlot -> PatchSlot -> GamePointer): the least fixed point, so a function that
        only passes the parameter on to itself is no helper."""
        defs = collections.defaultdict(list)   # (name, index) -> [(path, span, body, param)]
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
                    span = (m.end() - 1, m.end() - 1 + len(body))
                    defs[(name, index)].append((path, span, body, pm.group(1)))
        verdict = collections.defaultdict(dict)
        while True:
            changed = False
            for (name, index), found in defs.items():
                if verdict[name].get(index):
                    continue
                if all(self.routes(body, param, verdict) for _, _, body, param in found):
                    verdict[name][index] = True
                    changed = True
            if not changed:
                break
        self.helper_defs = collections.defaultdict(list)   # (name, index) -> [(path, (start, end), routed, param)]
        for (name, index), found in defs.items():
            for path, span, body, param in found:
                self.helper_defs[(name, index)].append((path, span, self.routes(body, param, verdict), param))
            verdict[name].setdefault(index, False)
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

    def routes(self, body, param, helpers):
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
            if last(callee) in OPENERS or helpers.get(last(callee), {}).get(index):
                continue
            return False
        return True

    def classify(self, path, code, at, end):
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
        # An id is seam when every other mention of its name is a seam or helper argument, or the value of an
        # alias constant (constexpr std::uintptr_t kStreamVtable = ByteStream::kVtableAddress;) whose every mention is.
        self.id_failures = {}
        self.alias_memo = {}
        for name in self.ids:
            homes = {u[0] for u in pending if u[5] == name}
            self.id_failures[name] = self.mention_failures(name, homes, frozenset())
        for use in pending:
            use[4] = 'raw' if self.id_failures[use[5]] else 'id'

    def mention_failures(self, name, homes, stack):
        """The mentions of constant `name` (defined in `homes`) that do not reach the seam."""
        bad = []
        rx = re.compile(r'(?<![\w.])' + re.escape(name) + r'\b')
        # A constant of a .cpp file is seen there only; a header's wherever it is named.
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
                    alias = self.alias_of(code, start, m.end())
                    ok = bool(alias) and alias not in stack and not self.alias_failures(alias, path, stack | {name})
                if not ok:
                    bad.append((path, m.start()))
        return bad

    def alias_of(self, code, start, end):
        """The constant whose whole value is the name at start..end (an alias), or None."""
        head = code[self.statement_start(code, start):start]
        m = re.search(r'(?:constexpr|const)\s+(?:static\s+)?(?:const\s+)?(?:std::)?(?:uintptr_t|uint32_t)\s+(\w+)\s*=\s*$',
                      head)
        if not m or not re.match(r'\s*;', code[end:]):
            return None
        return m.group(1)

    def alias_failures(self, alias, path, stack):
        key = (alias, path if path.endswith('.cpp') else None)
        if key not in self.alias_memo:
            self.alias_memo[key] = self.mention_failures(alias, {path}, stack)
        return self.alias_memo[key]

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


def report(scan):
    _, kinds = scan.counts()
    print(f'{"section":8} {"uses":>6} {"seam":>6} {"helper":>6} {"id":>5} {"raw":>5} {"addresses":>9} {"raw addr":>8}')
    for name, _, _ in SECTIONS:
        k = kinds[name]
        addrs = {u[2] for u in scan.uses if u[3] == name}
        raw_addrs = {u[2] for u in scan.uses if u[3] == name and u[4] == 'raw'}
        print(f'{name:8} {sum(k.values()):6} {k["seam"]:6} {k["helper"]:6} {k["id"]:5} {k["raw"]:5} '
              f'{len(addrs):9} {len(raw_addrs):8}')


def check(scan):
    raw, _ = scan.counts()
    problems = [f'{path}: {n} raw use(s) of {sec} (python tools/data_seam.py --list {sec}): use ei::GameData / '
                f'GamePointer (src/game/game_data.h), or in tests/ ExeTableId (tests/verify/harness_digest.cpp)'
                for (path, sec), n in sorted(raw.items()) if n]
    for p in problems:
        print('data_seam:', p)
    if problems:
        return 1
    print('data_seam: OK (no raw uses)')
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--list')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    scan = Scan()
    if a.list:
        list_uses(scan, a.list, a.all)
        return 0
    if a.check:
        return check(scan)
    report(scan)
    return 0


if __name__ == '__main__':
    sys.exit(main())
