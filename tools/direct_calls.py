"""Turns calls through EI_ORIGINAL forwarders to rewritten functions into direct calls (ticket C13).

Since S21e our code runs only in the standalone image, where a forwarder's jump to an original address lands on
the jump the image wrote there, i.e. on our function registered for that address. A call through the forwarder is
then the same as a call of that function. This tool finds, for every forwarder whose address has a registration
(EI_REPLACE / _METHOD / _DISPATCH / _SHORT, and with --library EI_REPLACE_STANDALONE), the registered function,
checks that the two declarations agree exactly (types from build/verify/ei_hook.map, undecorated), and rewrites
every call site to call the function directly; the forwarder's declaration goes.

Spelling only. A forwarder is converted only when all of this holds, otherwise it stays and --report says why:
- the declarations agree exactly: the same return and parameter types (typedefs resolved by the compiler), and
  either the same calling convention (free to free, member to member of the same class) or a __fastcall
  forwarder `R F(T* self, void* edx, args...)` (or `R F(T* self)`) for a __thiscall member `R T::M(args...)`;
- the target is not virtual, not a constructor / destructor / operator, and not overloaded; no class derived
  from the target's class declares a member of the same name (it would hide the target at a call site);
- the forwarder's name is used only in calls (no address taken, no __asm use, no other declaration of the name);
- the address is not one of the kept shims: a vector recorder (EI_RECORD), a replay stand-in (ReplayStandIn),
  a Rebirth-patched function (config/rebirth_patches.csv, EI_REBIRTH_IMPLEMENTS), or a decision listed in
  config/direct_calls_skip.csv (address,reason: e.g. a call site that does not compile directly).

A call `F(self, nullptr, a, b)` becomes `self->M(a, b)` (`x.M(a, b)` for `&x`, `M(a, b)` for `this`); a renamed
call keeps its object and arguments. The tool adds the include of the target's header to a .cpp call site that
lacks it. The compiler checks the rest: a site that does not compile goes into the skip list.

usage:
  python tools/direct_calls.py --report [--scope GLOB ...] [--verbose]   counts by reason (nothing written)
  python tools/direct_calls.py --apply --scope GLOB [...]                rewrites forwarders declared in GLOB
  python tools/direct_calls.py --count                                   forwarders left to rewritten code
Needs a current `build.cmd verify` (its map) and undname (the script puts the VS tools on PATH itself).
"""
import argparse
import csv
import fnmatch
import glob
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MAP = os.path.join(ROOT, 'build', 'verify', 'ei_hook.map')
SKIP = os.path.join(ROOT, 'config', 'direct_calls_skip.csv')
UNDNAME_DIRS = glob.glob(r'E:\bin\vs\buildtools\VC\Tools\MSVC\*\bin\Hostx86\x86')

ORIGINAL = re.compile(r'\bEI_ORIGINAL\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*\)')
REGISTRATION = re.compile(r'\bEI_REPLACE(_METHOD|_DISPATCH|_SHORT|_STANDALONE)?\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*,\s*&?\s*([\w:<>, ]+?)\s*\)')
RECORD = re.compile(r'\bEI_RECORD\s*\(\s*0[xX]([0-9A-Fa-f]+)')
STAND_IN = re.compile(r'\bReplayStandIn\s*\(\s*0[xX]([0-9A-Fa-f]+)')
REBIRTH = re.compile(r'\bEI_REBIRTH_IMPLEMENTS\s*\(\s*0[xX]([0-9A-Fa-f]+)')
KEYWORDS_BEFORE_EXPR = {'return', 'else', 'case', 'do', 'throw', 'co_return', 'co_await', 'sizeof', 'decltype'}
DUMMY_TYPES = {'void*', 'int', 'unsigned int', 'void const*'}


# ---------------------------------------------------------------------------------------------- sources

_TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\(?:x[0-9A-Fa-f]+|[0-7]{1,3}|.)|[^\'\\\n])\'', re.S)
_PREPRO = re.compile(r'^[ \t]*#[^\n]*(?:\\\n[^\n]*)*', re.M)


def _blank(text):
    return re.sub(r'[^\n]', ' ', text)


class Source:
    """One file: `raw` as written, `code` with comments blanked, `bare` with strings, characters and
    preprocessor lines blanked too (same offsets), the scopes its braces open."""

    def __init__(self, path, raw):
        self.path = path                 # relative to ROOT, '/' separators
        self.raw = raw
        self.code = _TOKEN.sub(lambda m: _blank(m.group(0)) if m.group(0)[0] == '/' else m.group(0), raw)
        bare = _TOKEN.sub(lambda m: _blank(m.group(0)) if m.group(0)[0] in '/' else
                          m.group(0)[0] + _blank(m.group(0)[1:-1]) + m.group(0)[-1], raw)
        self.bare = _PREPRO.sub(lambda m: _blank(m.group(0)), bare)
        self._scopes = None
        self.edits = []                  # (start, end, text) on raw

    def line_of(self, offset):
        return self.raw.count('\n', 0, offset) + 1

    @property
    def scopes(self):
        if self._scopes is None:
            self._scopes = parse_scopes(self.bare)
        return self._scopes

    def scope_at(self, offset):
        """The (kind, name) chain of braces around `offset`, outermost first."""
        return [(k, n) for (o, c, k, n) in self.scopes if o < offset < c]

    def namespaces_at(self, offset):
        return [n for k, n in self.scope_at(offset) if k == 'ns']

    def in_body(self, offset):
        return any(k == 'other' for k, _ in self.scope_at(offset))


_CLASS_HEAD = re.compile(r'\b(class|struct|union)\s+(?:(?:alignas|__declspec)\s*\([^()]*\)\s*)*(\w+)\s*(?:final\s*)?'
                         r'(?::(?P<bases>[^{;()]*(?:\([^()]*\)[^{;()]*)*))?\s*$')
_NS_HEAD = re.compile(r'(?:^|[\s;}])namespace(?:\s+([\w:]+))?\s*$')


def parse_scopes(bare):
    """Every brace pair as (open offset, close offset, kind, name): kind 'ns' (name; an anonymous namespace
    is "`anonymous namespace'"), 'class' (name), 'template class', 'enum', 'extern' or 'other'."""
    out, stack, last = [], [], 0
    for m in re.finditer(r'[{};]', bare):
        ch = m.group(0)
        if ch == ';':
            last = m.end()
            continue
        if ch == '}':
            if stack:
                o, k, n = stack.pop()
                out.append((o, m.start(), k, n))
            last = m.end()
            continue
        head = bare[last:m.start()]
        head = re.sub(r'^\s*(?:(?:public|private|protected)\s*:\s*)+', '', head)
        kind, name = 'other', ''
        ns = _NS_HEAD.search(head)
        cls = _CLASS_HEAD.search(head)
        if ns:
            kind, name = 'ns', ns.group(1) or "`anonymous namespace'"
        elif re.search(r'\bextern\s+"C"\s*$', head):
            kind = 'extern'
        elif re.search(r'\benum\b[^(){};]*$', head):
            kind = 'enum'
        elif cls and '=' not in head[cls.start():] and not re.search(r'\)\s*(?:const\s*)?$', head):
            kind, name = ('template class' if re.search(r'\btemplate\s*<', head) else 'class'), cls.group(2)
        stack.append((m.start(), kind, name))
        last = m.end()
    return sorted(out)


def load_sources():
    paths = []
    for top in ('src', 'tests'):
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            paths += [os.path.join(dirpath, n) for n in names if n.endswith(('.cpp', '.h', '.inc'))]
    out = []
    for full in sorted(paths):
        with open(full, encoding='utf-8', newline='') as f:
            out.append(Source(os.path.relpath(full, ROOT).replace(os.sep, '/'), f.read()))
    return out


# ------------------------------------------------------------------------------------------- the map

_MAP_LINE = re.compile(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\?\S+)\s+[0-9a-f]{8}\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$', re.M)
_SIG = re.compile(r'^(?:(?P<access>public|private|protected): )?(?P<static>static )?(?P<virtual>virtual )?'
                  r'(?P<ret>.*?) ?(?P<cc>__thiscall|__fastcall|__stdcall|__cdecl) (?P<name>[^(]+?)'
                  r'\((?P<params>.*)\)(?P<const>\s*const)?\s*$')


def normalize_type(t):
    t = re.sub(r'\b(?:class|struct|union|enum) ', '', t)
    t = t.replace("`anonymous namespace'::", '')
    t = re.sub(r'\s+', ' ', t).strip()
    return re.sub(r'\s*([*&,<>])\s*', r'\1', t)


def split_params(text):
    depth, cur, out = 0, '', []
    for ch in text:
        if ch in '<([':
            depth += 1
        elif ch in '>)]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [] if out == ['void'] else out


class Sig:
    """One undecorated function declaration from the map."""

    def __init__(self, und, obj):
        self.und, self.obj = und, obj
        m = _SIG.match(und)
        self.ok = bool(m)
        if not m:
            return
        self.access = m.group('access') or ''
        self.static = bool(m.group('static'))
        self.virtual = bool(m.group('virtual'))
        self.ret = normalize_type(m.group('ret'))
        self.cc = m.group('cc')
        self.qname = normalize_type(m.group('name'))       # anonymous namespaces dropped
        self.raw_name = m.group('name')
        self.params = [normalize_type(p) for p in split_params(m.group('params'))]
        self.const = bool(m.group('const'))
        self.member = bool(self.access) and not self.static
        parts = split_qualified(self.qname)
        self.simple = parts[-1]
        self.scope = '::'.join(parts[:-1])
        self.anonymous = "`anonymous namespace'" in self.raw_name
        self.special = (not self.ret and self.member) or self.simple.startswith(('operator', '~')) or \
            (len(parts) > 1 and parts[-2].split('<')[0] == self.simple)

    def key(self):
        return (self.ret, self.cc, tuple(self.params), self.const, self.member, self.static)


def split_qualified(name):
    parts, depth, cur, i = [], 0, '', 0
    while i < len(name):
        ch = name[i]
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        if depth == 0 and name.startswith('::', i):
            parts.append(cur)
            cur = ''
            i += 2
            continue
        cur += ch
        i += 1
    parts.append(cur)
    return parts


def load_map(path=MAP):
    """Normalized qualified name -> [Sig] (one per distinct declaration)."""
    import check_signatures
    os.environ['PATH'] = os.pathsep.join(UNDNAME_DIRS + [os.environ.get('PATH', '')])
    text = open(path, errors='replace').read()
    entries = _MAP_LINE.findall(text)
    und = check_signatures.undecorate({d for d, _ in entries})
    out = defaultdict(list)
    seen = set()
    for dec, obj in entries:
        u = und.get(dec)
        if not u or (u, obj) in seen:
            continue
        seen.add((u, obj))
        s = Sig(u, obj.lower())
        if s.ok:
            out[s.qname].append(s)
    return out


def by_simple(decls):
    out = defaultdict(set)
    for q, sigs in decls.items():
        out[sigs[0].simple].add(q)
    return out


# -------------------------------------------------------------------------------------- the forwarders

class Forwarder:
    def __init__(self, src, address, start, end, text):
        self.src, self.address, self.start, self.end = src, address, start, end
        self.text = text                      # the declaration, comments blanked
        decl = re.sub(r'__declspec\s*\(\s*\w+\s*\)', lambda m: _blank(m.group(0)), text)
        m = re.search(r'([A-Za-z_~][\w:~]*)\s*\(', decl)
        self.name = m.group(1) if m else ''
        self.simple = self.name.split('::')[-1]
        scope = src.scope_at(start)
        self.template = 'template' in decl.split(self.name)[0] or any(k == 'template class' for k, _ in scope)
        self.in_class = [n for k, n in scope if k == 'class']
        chain = [n for k, n in scope if k in ('ns', 'class')]
        self.qname = '::'.join([c for c in chain if c != "`anonymous namespace'"] + [self.name])
        self.extra = []                       # (start, end) more text to delete: the in-class declaration
        self.sig = None
        self.target = None
        self.target_name = ''
        self.kind = ''
        self.sites = []                       # Site
        self.reason = ''


class Site:
    def __init__(self, src, start, name_start, name_end, qualifier, obj):
        self.src = src
        self.start = start            # first character replaced (the qualifier when there is one)
        self.name_start, self.name_end = name_start, name_end
        self.qualifier = qualifier    # 'Cls::' / '::' / ''
        self.obj = obj                # '.', '->' or ''
        self.args = None              # (open paren, close paren, [(start, end)] of each argument)


def find_forwarders(sources):
    out = []
    for s in sources:
        last = 0
        bounds = [m.end() for m in re.finditer(r'[;{}]|' + ORIGINAL.pattern, s.bare)]
        for m in ORIGINAL.finditer(s.code):
            i = max((b for b in bounds if b <= m.start()), default=0)
            start = i + len(s.code[i:m.start()]) - len(s.code[i:m.start()].lstrip())
            text = s.code[start:m.end()]
            text2 = re.sub(r'^(?:(?:public|private|protected)\s*:\s*)+', '', text)
            start += len(text) - len(text2)
            out.append(Forwarder(s, int(m.group(1), 16), start, m.end(), text2))
            last = m.end()
    return out


def registrations(sources, library):
    regs = defaultdict(list)
    for s in sources:
        if not s.path.startswith('src/'):
            continue
        for m in REGISTRATION.finditer(s.code):
            kind = m.group(1) or ''
            if kind == '_STANDALONE' and not library:
                continue
            regs[int(m.group(2), 16)].append((m.group(3).replace(' ', ''), s))
    return regs


def kept_shims(sources):
    """address -> why it stays a forwarder."""
    kept = {}
    for s in sources:
        for rx, why in ((RECORD, 'vector recorder (EI_RECORD)'), (STAND_IN, 'replay stand-in'),
                        (REBIRTH, 'Rebirth-patched function (EI_REBIRTH_IMPLEMENTS)')):
            for m in rx.finditer(s.code):
                kept.setdefault(int(m.group(1), 16), why)
    with open(os.path.join(ROOT, 'config', 'rebirth_patches.csv'), encoding='utf-8') as f:
        for r in csv.DictReader(f):
            fn = (r.get('function') or '').split('+')[0]
            if re.fullmatch(r'[0-9A-Fa-f]{8}', fn):
                kept.setdefault(int(fn, 16), 'Rebirth-patched function (config/rebirth_patches.csv)')
    if os.path.exists(SKIP):
        with open(SKIP, encoding='utf-8') as f:
            for r in csv.DictReader(f):
                kept[int(r['address'], 16)] = 'listed: ' + r['reason']
    return kept


# ----------------------------------------------------------------------------------------- matching

def resolve_forwarder(f, decls):
    sigs = decls.get(normalize_type(f.qname), [])
    if f.src.path.endswith('.cpp'):
        obj = os.path.basename(f.src.path).lower() + '.obj'
        local = [s for s in sigs if s.obj == obj]
        sigs = local or sigs
    if len({s.key() for s in sigs}) != 1:
        return None
    return sigs[0]


def resolve_target(name, decls, simple_index):
    """The Sig of a registered name (as written in the registration), or a reason."""
    norm = normalize_type(name)
    cands = decls.get(norm)
    if not cands:
        parts = split_qualified(norm)
        names = [q for q in simple_index.get(parts[-1], ()) if q == norm or q.endswith('::' + norm)]
        if len(names) != 1:
            return None, 'target not found in the map' if not names else 'target name ambiguous'
        cands = decls[names[0]]
    if len({s.key() for s in cands}) != 1:
        return None, 'target overloaded'
    return cands[0], ''


def match(fsig, tsig):
    """How a call of the forwarder becomes a call of the target: 'rename', 'this' or None. A forwarder
    declared void may stand for a function with a result (the callers cannot read it); a parameter the
    target declares const where the forwarder does not is the same pointer."""
    if tsig.ret != fsig.ret and fsig.ret != 'void':
        return None
    if fsig.member:
        if tsig.member and tsig.cc == fsig.cc and tsig.scope == fsig.scope and same_params(fsig.params, tsig.params) \
                and (tsig.const <= fsig.const):
            return 'rename'
        return None
    if not tsig.member:
        if tsig.cc == fsig.cc and same_params(fsig.params, tsig.params):
            return 'rename'
        return None
    if fsig.cc != '__fastcall' or tsig.cc != '__thiscall' or not fsig.params:
        return None
    self_t = fsig.params[0]
    if self_t == tsig.scope + '*':
        pass
    elif self_t == tsig.scope + ' const*' and tsig.const:
        pass
    else:
        return None
    rest = fsig.params[1:]
    if rest:
        if rest[0] not in DUMMY_TYPES:
            return None
        rest = rest[1:]
    return 'this' if same_params(rest, tsig.params) else None


def same_params(fparams, tparams):
    """Parameter lists that take the same values: equal, or the target adds const to a pointee."""
    if len(fparams) != len(tparams):
        return False
    for f, t in zip(fparams, tparams):
        if f != t and not (t.endswith(('*', '&')) and re.sub(r' const(?=[*&]$)', '', t) == f):
            return False
    return True


# ------------------------------------------------------------------------------------- class graph

class Classes:
    """Class bodies by simple name and who derives from whom (text), for the hiding check."""

    def __init__(self, sources):
        self.bodies = defaultdict(list)       # simple name -> [(src, open, close)]
        self.derived = defaultdict(set)       # base simple name -> derived simple names
        for s in sources:
            for o, c, k, n in s.scopes:
                if k in ('class', 'template class'):
                    self.bodies[n].append((s, o, c))
                    i = max(s.bare.rfind(';', 0, o), s.bare.rfind('{', 0, o), s.bare.rfind('}', 0, o)) + 1
                    head = _CLASS_HEAD.search(s.bare[i:o])
                    if head and head.group('bases'):
                        for b in split_params(head.group('bases')):
                            b = re.sub(r'\b(?:public|private|protected|virtual)\b', '', b).strip()
                            b = split_qualified(b.split('<')[0].strip())[-1]
                            if b:
                                self.derived[b].add(n)

    def all_derived(self, name):
        out, todo = set(), [name]
        while todo:
            for d in self.derived.get(todo.pop(), ()):
                if d not in out:
                    out.add(d)
                    todo.append(d)
        return out

    def declares(self, cls, member):
        rx = re.compile(r'(?<![\w.>])' + re.escape(member) + r'\s*\(')
        return any(rx.search(s.bare, o, c) for s, o, c in self.bodies.get(cls, ()))

    def header_of(self, cls, member):
        """The file defining class `cls` with `member` in its body."""
        hits = [s for s, o, c in self.bodies.get(cls, ())
                if re.search(r'\b' + re.escape(member) + r'\s*\(', s.bare[o:c])]
        files = {s.path for s in hits}
        return hits[0] if len(files) == 1 else None


# ---------------------------------------------------------------------------------------- call sites

_IDENT_END = re.compile(r'[\w]$')


def prev_token(bare, i):
    j = i
    while j > 0 and bare[j - 1] in ' \t\r\n':
        j -= 1
    if j == 0:
        return '', 0
    if bare[j - 1].isalnum() or bare[j - 1] == '_':
        k = j
        while k > 0 and (bare[k - 1].isalnum() or bare[k - 1] == '_'):
            k -= 1
        return bare[k:j], k
    two = bare[j - 2:j]
    if two in ('->', '::', '&&', '||'):
        return two, j - 2
    return bare[j - 1], j - 1


def qualifier_before(bare, i):
    """The `A::B::` chain directly before offset i, as (start, text)."""
    start = i
    while True:
        tok, at = prev_token(bare, start)
        if tok != '::':
            break
        name, nat = prev_token(bare, at)
        if name and (name[0].isalpha() or name[0] == '_') and name not in KEYWORDS_BEFORE_EXPR:
            # template arguments directly before :: (Foo<int>::) are not followed here
            start = nat
        else:
            start = at
            break
    return start, re.sub(r'\s+', '', bare[start:i])


def parse_args(bare, open_paren):
    depth, i, args, arg_start = 0, open_paren, [], open_paren + 1
    while i < len(bare):
        ch = bare[i]
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
            if depth == 0:
                if bare[arg_start:i].strip() or args:
                    args.append((arg_start, i))
                return i, args
        elif ch == ',' and depth == 1:
            args.append((arg_start, i))
            arg_start = i + 1
        i += 1
    return None, None


def find_sites(f, sources, decl_spans):
    """Every use of the forwarder's name, or a reason it cannot be rewritten."""
    local = f.src.path.endswith('.cpp') and '::' not in f.name and not f.in_class
    sites = []
    for s, a, b in _WORDS.get(f.simple, ()):
        if local and s is not f.src:
            continue
        if True:
            if any(sa <= a < sb for sa, sb in decl_spans.get(s.path, ())):
                continue
            j = b
            while j < len(s.bare) and s.bare[j] in ' \t\r\n':
                j += 1
            is_call = j < len(s.bare) and s.bare[j] == '('
            tok, at = prev_token(s.bare, a)
            qstart, qual = (a, '')
            if tok == '::':
                qstart, qual = qualifier_before(s.bare, a)
                tok, at = prev_token(s.bare, qstart)
            obj = tok if tok in ('.', '->') else ''
            declaration = not obj and (tok and (tok[0].isalnum() or tok[0] == '_') and tok not in KEYWORDS_BEFORE_EXPR
                                       or tok in ('*', '&', '>', '~'))
            if declaration and '::' in f.name and not s.in_body(a) and is_call and not f.in_class and not f.extra:
                # the class body's declaration of an out-of-class forwarder definition
                k_open, _ = parse_args(s.bare, j)
                end = k_open + 1 if k_open else b
                rest = s.bare[end:end + 40]
                mm = re.match(r'\s*(?:const\s*)?;', rest)
                cls = [n for k, n in s.scope_at(a) if k == 'class']
                if mm and cls and cls[-1] == f.name.split('::')[-2]:
                    f.extra.append((s, statement_start(s, at if tok else a), end + mm.end()))
                    continue
            if declaration:
                return None, f'another declaration of the name ({f.simple}; {s.path}:{s.line_of(a)})'
            if not is_call:
                return None, f'used other than in a call ({f.simple}; {s.path}:{s.line_of(a)})'
            if s.path.startswith('tests/vectors/'):
                return None, f'called from the vector replay ({s.path}:{s.line_of(a)})'
            site = Site(s, qstart, a, b, qual, obj)
            close, args = parse_args(s.bare, j)
            if close is None:
                return None, f'unbalanced call ({s.path}:{s.line_of(a)})'
            site.args = (j, close, args)
            sites.append(site)
    return sites, ''


def statement_start(s, offset):
    """Start of the declaration statement holding `offset`."""
    i = max(s.bare.rfind(c, 0, offset) for c in ';{}') + 1
    labels = list(re.finditer(r'\b(?:public|private|protected)\s*:', s.bare[i:offset]))
    if labels:
        i += labels[-1].end()
    while i < offset and s.bare[i] in ' \t\r\n':
        i += 1
    return i


# ------------------------------------------------------------------------------------------- rewrite

def is_postfix(expr):
    """True when `expr` binds at least as tightly as a postfix expression (identifiers joined by . -> ::,
    calls, subscripts, named casts): then `expr->M(...)` needs no parentheses."""
    e = expr.strip()
    if not e or not (e[0].isalpha() or e[0] == '_'):
        return False
    i, depth = 0, 0
    while i < len(e):
        ch = e[i]
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif depth == 0:
            if ch == '<':
                head = e[:i].strip()
                if not re.search(r'\b(?:static_cast|reinterpret_cast|const_cast)$', head):
                    return False
                d, k = 0, i
                while k < len(e):
                    if e[k] == '<':
                        d += 1
                    elif e[k] == '>':
                        d -= 1
                        if d == 0:
                            break
                    k += 1
                i = k
            elif e.startswith('->', i) or e.startswith('::', i):
                i += 2
                continue
            elif not (ch.isalnum() or ch in '_.' or ch in ' \t\r\n'):
                return False
            elif ch in ' \t\r\n':
                # spaces only around . -> ::
                before, after = e[:i].rstrip(), e[i:].lstrip()
                if not (before.endswith(('.', '->', '::')) or after.startswith(('.', '->', '::', '(', '['))):
                    return False
        i += 1
    return depth == 0


def spelled_target(tsig, site_src, site_offset):
    """The target's name as written at a call site: qualified past the namespaces the site is in."""
    parts = split_qualified(tsig.qname)
    here = site_src.namespaces_at(site_offset)
    scope = parts[:-1]
    k = 0
    while k < len(scope) and k < len(here) and scope[k] == here[k]:
        k += 1
    return '::'.join(scope[k:] + parts[-1:])


def _trimmed(text, a, b):
    """[a, b) without leading and trailing whitespace of text."""
    while a < b and text[a] in ' \t\r\n':
        a += 1
    while b > a and text[b - 1] in ' \t\r\n':
        b -= 1
    return a, b


def renamed(s, start, name_end, name, close):
    """The call from `start` with its name replaced; argument lines aligned to its parenthesis follow."""
    delta = len(name) - (name_end - start)
    return (start, close + 1, [name, (name_end, close + 1, column(s.raw, name_end) + 1, delta)])


def rewrite_site(f, site):
    """(start, end, pieces) for one call site, or a reason. Pieces are strings and (start, end) ranges of
    the raw text, which are written with the edits inside them (a forwarder call in the arguments)."""
    s = site.src
    open_paren, close, args = site.args
    if f.kind == 'rename':
        t = f.target
        if t.member:
            if site.qualifier and normalize_type(site.qualifier[:-2]).split('::')[-1] != t.scope.split('::')[-1]:
                return None, 'qualified member call through another class'
            return renamed(s, site.name_start, site.name_end, t.simple, close), ''
        if t.anonymous and s.path != f.target_src_path:
            return None, 'target in an anonymous namespace of another file'
        if site.obj:
            return None, 'free forwarder called as a member'
        return renamed(s, site.start, site.name_end, spelled_target(t, s, site.start), close), ''
    # 'this': F(self, edx, rest...) -> self->M(rest...)
    if site.obj or site.qualifier not in ('', '::'):
        return None, 'qualified call'
    nparams = len(f.sig.params)
    if len(args) != nparams:
        return None, 'argument count differs'
    sa, sb = _trimmed(s.code, *args[0])
    self_code = s.code[sa:sb]
    if '\n' in self_code or s.code[sa:sb] != s.raw[sa:sb]:
        return None, 'object expression spans lines or has a comment'
    if nparams >= 2:
        dummy = s.bare[args[1][0]:args[1][1]].strip()
        if not re.fullmatch(r'nullptr|0|NULL|[A-Za-z_]\w*', dummy):
            return None, f'EDX argument with an effect ({dummy})'
    rest = [_trimmed(s.code, args[2][0], close)[0], close] if nparams > 2 else None
    m = f.target.simple
    if self_code == 'this':
        head = [m]
    elif self_code.startswith('&') and not self_code.startswith('&&') and is_postfix(self_code[1:]):
        head = [_trimmed(s.code, sa + 1, sb), '.' + m]
    elif is_postfix(self_code):
        head = [(sa, sb), '->' + m]
    else:
        head = ['(', (sa, sb), ')->' + m]
    if not rest:
        return (site.start, close + 1, head + ['()']), ''
    width = sum(len(p) if isinstance(p, str) else p[1] - p[0] for p in head) + 1
    old_col = column(s.raw, rest[0])
    delta = column(s.raw, site.start) + width - old_col
    return (site.start, close + 1, head + ['(', (rest[0], close, old_col, delta), ')']), ''


def column(raw, offset):
    return offset - (raw.rfind('\n', 0, offset) + 1)


def reindent(text, old_col, delta):
    """Continuation lines of a call's arguments aligned at or past `old_col` move by `delta`."""
    if not delta or '\n' not in text:
        return text

    def shift(m):
        n = len(m.group(1))
        return '\n' + ' ' * max(0, n + delta) if n >= old_col else m.group(0)
    return re.sub(r'\n( *)(?=\S)', shift, text)


def include_line(site_path, header_path):
    a, b = os.path.dirname(site_path), os.path.dirname(header_path)
    if a == b:
        return os.path.basename(header_path)
    if header_path.startswith('src/'):
        return header_path[4:]
    return None


def add_include(src, inc):
    """Edit adding `#include "inc"` among the file's quoted includes (sorted place), or None when present."""
    lines = src.raw.split('\n')
    if any(re.match(r'\s*#\s*include\s+"' + re.escape(inc) + '"', l) for l in lines):
        return None
    quoted = [i for i, l in enumerate(lines) if re.match(r'\s*#\s*include\s+"', l)]
    if not quoted:
        return False
    same_kind = [i for i in quoted if ('/' in lines[i].split('"')[1]) == ('/' in inc)] or quoted
    at = None
    for i in same_kind:
        if lines[i].split('"')[1] > inc:
            at = i
            break
    if at is None:
        at = same_kind[-1] + 1
    offset = sum(len(l) + 1 for l in lines[:at])
    return (offset, offset, f'#include "{inc}"\n')


# ------------------------------------------------------------------------------------------- driver

def analyse(sources, decls, library=False, scope=None):
    simple_index = by_simple(decls)
    regs = registrations(sources, library)
    kept = kept_shims(sources)
    classes = Classes(sources)
    fwds = find_forwarders(sources)
    spans = defaultdict(list)
    for f in fwds:
        spans[f.src.path].append((f.start, f.end))
    names = {f.simple for f in fwds}
    _WORDS.clear()
    for s in sources:
        for m in re.finditer(r'[A-Za-z_]\w*', s.bare):
            if m.group(0) in names:
                _WORDS.setdefault(m.group(0), []).append((s, m.start(), m.end()))
    name_count = Counter((f.src.path if f.src.path.endswith('.cpp') else '', f.simple) for f in fwds)
    for f in fwds:
        f.in_scope = scope is None or any(fnmatch.fnmatch(f.src.path, g) for g in scope)
        if f.address not in regs:
            f.reason = 'library code' if not library else 'no registration'
            continue
        if f.address in kept:
            f.reason = kept[f.address]
            continue
        if len(regs[f.address]) != 1:
            f.reason = 'several registrations'
            continue
        f.target_name, tsrc = regs[f.address][0]
        if f.template or not f.name:
            f.reason = 'template forwarder'
            continue
        if name_count[(f.src.path if f.src.path.endswith('.cpp') else '', f.simple)] > 1:
            f.reason = 'forwarder name declared twice'
            continue
        f.sig = resolve_forwarder(f, decls)
        if f.sig is None:
            sites, why = find_sites(f, sources, spans)
            if sites == []:
                f.kind = 'unused'
            else:
                f.reason = why or 'not in the map (overloaded or not compiled) but called'
            continue
        t, why = resolve_target(f.target_name, decls, simple_index)
        if t is None:
            f.reason = why
            continue
        f.target = t
        f.target_src_path = tsrc.path
        if t.virtual:
            f.reason = 'target virtual'
            continue
        if t.special:
            f.reason = 'target constructor / destructor / operator'
            continue
        f.kind = match(f.sig, t)
        if not f.kind:
            f.reason = 'types differ'
            continue
        others = simple_index.get(t.simple, set()) - {t.qname}
        if t.member:
            cls = split_qualified(t.scope)[-1].split('<')[0]
            hiding = [d for d in classes.all_derived(cls) if classes.declares(d, t.simple)]
            if hiding:
                f.reason = f'a derived class declares the target name ({hiding[0]}::{t.simple})'
                continue
        elif others and not t.scope:
            f.reason = f'other functions have the target name ({t.simple})'
            continue
        sites, why = find_sites(f, sources, spans)
        if sites is None:
            f.reason = why
            continue
        f.sites = sites
        if not sites:
            f.kind = 'unused'
    return fwds, classes


def plan(fwds, classes):
    """Edits per file for every convertible forwarder in scope; sets f.reason for the ones that fail."""
    edits = defaultdict(list)
    includes = defaultdict(set)
    done = []
    for f in fwds:
        if not f.in_scope or f.reason or not f.kind:
            continue
        mine, incs, why = [], set(), ''
        for site in f.sites:
            e, why = rewrite_site(f, site)
            if e is None:
                where = f'{site.src.path}:{site.src.line_of(site.name_start)}'
                why = why[:-1] + f'; {where})' if why.endswith(')') else f'{why} ({where})'
                break
            mine.append((site.src, e))
            if site.src.path == f.target_src_path:
                continue
            t = f.target
            if t.member or t.static:
                hdr = classes.header_of(split_qualified(t.scope)[-1].split('<')[0], t.simple)
            else:
                hdr = None
            if hdr is None and not t.member:
                hdr = free_header(t, f.target_src_path)
            if hdr is None:
                why = f'no header declares the target ({t.qname}; {site.src.path})'
                break
            if hdr.path.endswith('.cpp') and hdr.path != site.src.path:
                why = f'target declared in its .cpp only ({t.qname} in {hdr.path}; {site.src.path})'
                break
            if hdr.path != site.src.path and site.src.path.endswith('.cpp'):
                inc = include_line(site.src.path, hdr.path)
                if inc is None:
                    why = f'cannot include {hdr.path}'
                    break
                incs.add((site.src, inc))
        if why:
            f.reason = why
            continue
        for src, e in mine:
            edits[src].append(e)
        for src, inc in incs:
            includes[src].add(inc)
        edits[f.src].append(('delete', f.start, f.end, f.address))
        for src, a, b in f.extra:
            edits[src].append(('delete', a, b, f.address))
        done.append(f)
    return edits, includes, done


_FREE_DECLS = {}


_WORDS = {}
_HEADERS = {}


def free_header(t, defining):
    """The header declaring free function `t` (the one next to its defining .cpp first), as a Source."""
    key = (t.qname, defining)
    if key not in _HEADERS:
        rx = re.compile(r'(?<![\w.>:])' + re.escape(t.simple) + r'\s*\(')
        srcs = {s.path: s for s in _FREE_DECLS.get('all', ())}
        first = os.path.splitext(defining)[0] + '.h'
        hits = [s for s in srcs.values() if s.path.endswith('.h') and t.simple in s.bare and rx.search(s.bare)]
        hits.sort(key=lambda s: s.path != first)
        _HEADERS[key] = hits[0] if hits else srcs.get(defining)
    return _HEADERS[key]


def line_span(raw, a, b):
    """Widen [a, b) to whole lines when only whitespace (and a trailing // comment) is around it."""
    ls = raw.rfind('\n', 0, a) + 1
    le = raw.find('\n', b)
    le = len(raw) if le < 0 else le
    before, after = raw[ls:a], raw[b:le]
    if before.strip() == '' and (after.strip() == '' or after.strip().startswith('//')):
        return ls, min(le + 1, len(raw))
    return a, b


def comment_above(raw, ls):
    """Start of the // comment lines directly above the line starting at `ls`."""
    i = ls
    while i > 0:
        p = raw.rfind('\n', 0, i - 1) + 1
        if raw[p:i].strip().startswith('//'):
            i = p
        else:
            break
    return i


_ABOUT_FORWARDER = re.compile(r"through (?:its|the original'?s?|their|the game'?s?) (?:game |original )?address|"
                              r"forwarder|verify wrapper", re.I)


def deletion_span(raw, a, b, deleted):
    """The text to delete for a declaration at [a, b): whole lines, the comment directly above when it is
    about deleted forwarders only, and one of two blank lines that would meet."""
    a, b = line_span(raw, a, b)
    c = comment_above(raw, a)
    if c < a:
        text = raw[c:a]
        addrs = {int(x, 16) for x in re.findall(r'\b([0-9A-Fa-f]{8})\b', text)}
        if (addrs and addrs <= deleted) or (not addrs and _ABOUT_FORWARDER.search(text)):
            a = c
        else:
            KEPT_COMMENTS.append(text.strip().splitlines()[-1].strip())
    return a, b


KEPT_COMMENTS = []


def render(raw, a, b, ops):
    """raw[a:b] with the edits inside it; an edit's (start, end) pieces are rendered the same way."""
    out, pos = [], a
    for o in sorted((o for o in ops if a <= o[0] and o[1] <= b), key=lambda o: (o[0], -o[1])):
        s, e, pieces = o
        if s < pos:
            if e <= pos:
                continue        # inside an earlier edit: written with that edit's pieces
            raise SystemExit(f'overlapping edits at offset {s}')
        out.append(raw[pos:s])
        for p in pieces:
            if isinstance(p, str):
                out.append(p)
                continue
            text = render(raw, p[0], p[1], [x for x in ops if x is not o])
            out.append(reindent(text, p[2], p[3]) if len(p) == 4 else text)
        pos = e
    out.append(raw[pos:b])
    return ''.join(out)


def apply(edits, includes, deleted_by_file):
    """Writes every changed file; returns their paths."""
    out = rewritten(edits, includes, deleted_by_file)
    for path, text in out.items():
        with open(os.path.join(ROOT, path), 'w', encoding='utf-8', newline='') as fh:
            fh.write(text)
    return sorted(out)


def rewritten(edits, includes, deleted_by_file):
    """path -> new text of every file the edits change."""
    changed = {}
    for src in set(edits) | set(includes):
        raw = src.raw
        deleted = deleted_by_file.get(src.path, set())
        dels, ops = [], []
        for e in edits.get(src, ()):
            if e[0] == 'delete':
                dels.append(deletion_span(raw, e[1], e[2], deleted))
            else:
                ops.append((e[0], e[1], e[2]))
        dels.sort()
        merged = []
        for a, b in dels:
            if merged and a <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], b)
            else:
                merged.append([a, b])
        for span in merged:
            if span[0] >= 2 and raw[span[0] - 2:span[0]] == '\n\n' and raw[span[1]:span[1] + 1] == '\n':
                span[1] += 1
        ops += [(a, b, []) for a, b in merged]
        for inc in sorted(includes.get(src, ())):
            e = add_include(src, inc)
            if e:
                ops.append((e[0], e[1], [e[2]]))
        changed[src.path] = render(raw, 0, len(raw), ops)
    return changed


def review(done):
    """What a person still reads after --apply: comments kept above a deleted forwarder and comments that
    name a deleted forwarder."""
    for text in KEPT_COMMENTS:
        print(f'kept comment above a deleted forwarder: {text}')
    names = {f.simple for f in done}
    if not names:
        return
    rx = re.compile(r'\b(' + '|'.join(sorted(map(re.escape, names))) + r')\b')
    for s in load_sources():
        for m in rx.finditer(s.raw):
            if s.code[m.start()] == ' ':
                print(f'{s.path}:{s.line_of(m.start())}: comment names {m.group(1)}')


def to_rewritten_count(fwds):
    return sum(1 for f in fwds if f.reason != 'library code')


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--count', action='store_true')
    ap.add_argument('--scope', nargs='*')
    ap.add_argument('--library', action='store_true', help='also forwarders to EI_REPLACE_STANDALONE code')
    ap.add_argument('--verbose', '-v', action='store_true')
    ap.add_argument('--map', default=MAP)
    args = ap.parse_args(argv)
    sources = load_sources()
    _FREE_DECLS['all'] = sources
    if args.count:
        fwds = find_forwarders(sources)
        regs = registrations(sources, False)
        print(f'{len(fwds)} forwarders, {sum(1 for f in fwds if f.address in regs)} to rewritten code')
        return 0
    decls = load_map(args.map)
    fwds, classes = analyse(sources, decls, args.library, args.scope)
    edits, includes, done = plan(fwds, classes) if (args.apply or args.report) else ({}, {}, [])
    scoped = [f for f in fwds if f.in_scope]
    reasons = Counter(re.sub(r' \(.*', '', f.reason) for f in scoped if f.reason)
    print(f'{len(scoped)} forwarders in scope: {len(done)} convertible '
          f'({sum(1 for f in done if f.kind == "unused")} unused, {sum(len(f.sites) for f in done)} call sites), '
          f'{sum(reasons.values())} left')
    for r, n in reasons.most_common():
        print(f'  {n:5}  {r}')
    if args.verbose:
        for f in scoped:
            if f.reason:
                print(f'{f.address:08X} {f.src.path}:{f.src.line_of(f.start)} {f.name} -> {f.target_name}: {f.reason}')
    if args.apply:
        deleted = defaultdict(set)
        for f in done:
            deleted[f.src.path].add(f.address)
            for src, _, _ in f.extra:
                deleted[src.path].add(f.address)
        changed = apply(edits, includes, deleted)
        print(f'{len(changed)} files changed')
        review(done)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
