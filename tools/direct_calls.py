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

A registration may name its function through a using-alias or a template instantiation spelled without its
namespaces or default arguments (`Map8::Subscript`, C13b): resolve_registered finds it in the map. A forwarder of
a static container wrapper whose body is one member call (`self->SetSize(n, g)`, stlp::VectorAbi / mfc ArrayAbi)
becomes that member call on the caller's container ('unwrap', match_unwrap). Template-ids at a call site are
written through the aliases the site sees (abbreviated).

A call `F(self, nullptr, a, b)` becomes `self->M(a, b)` (`x.M(a, b)` for `&x`, `M(a, b)` for `this`); a renamed
call keeps its object and arguments. The tool adds the include of the target's header to a .cpp call site that
lacks it. The compiler checks the rest: a site that does not compile goes into the skip list.

usage:
  python tools/direct_calls.py --report [--scope GLOB ...] [--verbose]   counts by reason (nothing written)
  python tools/direct_calls.py --apply --scope GLOB [...]                rewrites forwarders declared in GLOB
  (--exclude GLOB [...] leaves the forwarders declared there)
  python tools/direct_calls.py --count                                   forwarders left to rewritten code
  --loose (with --report / --apply) also converts forwarders whose types differ in ways a call can spell
  (match_loose); compile errors at the calls go into --cast-sites or are fixed by hand.
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


_FN_ARG = re.compile(r'&(?:(?:public|private|protected): )?(?:static |virtual )*[^<>,()&]*?'
                     r'(?:__thiscall|__fastcall|__stdcall|__cdecl) ([^<>,()]+(?:<[^()]*?>)?[^<>,()]*)\(')


def collapse_function_arguments(und):
    """`und` with each function (or member function) pointer template argument written as its name: undname prints
    `EachElements<Bytes<12>,0,&void __cdecl gamelists::ReleaseHolder(struct Bytes<12> *)>`, the source
    `EachElements<Bytes<12>, nullptr, ReleaseHolder>` (C13b1)."""
    out, i = [], 0
    while True:
        m = _FN_ARG.search(und, i)
        if not m:
            break
        depth = und.count('<', 0, m.start()) - und.count('>', 0, m.start())
        if depth <= 0:
            out.append(und[i:m.end()])
            i = m.end()
            continue
        j, level = m.end(), 1          # the parameter list's close
        while j < len(und) and level:
            level += {'(': 1, ')': -1}.get(und[j], 0)
            j += 1
        k = re.compile(r'\s*const\b').match(und, j)
        out.append(und[i:m.start()] + '&' + m.group(1).strip())
        i = k.end() if k else j
    out.append(und[i:])
    return ''.join(out)


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
        m = _SIG.match(collapse_function_arguments(und))
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
        self.global_qualify = False           # the call is spelled ::Name (other functions have the name)
        self.redeclare = None                # (place, namespaces): declare the target where the forwarder was
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
        # A declaration starts after the last ; { } forwarder or preprocessor line before it.
        bounds = [m.end() for m in re.finditer(r'[;{}]|' + ORIGINAL.pattern, s.bare)]
        bounds += [m.end() for m in re.finditer(r'(?m)^[ \t]*#[^\n]*', s.code)]
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
            name = m.group(3).replace(' ', '')
            # A registration inside a namespace names a function of it: tried first (then the name as written).
            chain = [n for n in s.namespaces_at(m.start()) if n != "`anonymous namespace'"]
            regs[int(m.group(2), 16)].append(('::'.join(chain + [name]) if chain else name, s, name))
    return regs


PATCHED_RANGES = []   # [start, end) of game code a Rebirth patch overwrites


def kept_shims(sources):
    """address -> why it stays a forwarder."""
    kept = {}
    PATCHED_RANGES.clear()
    for s in sources:
        for rx, why in ((RECORD, 'vector recorder (EI_RECORD)'), (STAND_IN, 'replay stand-in'),
                        (REBIRTH, 'Rebirth-patched function (EI_REBIRTH_IMPLEMENTS)')):
            for m in rx.finditer(s.code):
                kept.setdefault(int(m.group(1), 16), why)
        # The harness replaces some entries (the fixed ISAAC seed 0049F660, the random trace's rand):
        # a direct call would pass its replacement by.
        if s.path.startswith('tests/verify/'):
            consts = {m.group(1): m.group(2) for m in re.finditer(r'\b(k\w+)\s*=\s*0x([0-9A-Fa-f]{8})\b', s.code)}
            for m in re.finditer(r'\bJump(?:To|From)\(\s*(?:0x([0-9A-Fa-f]{8})|(k\w+))\b', s.code):
                at = m.group(1) or consts.get(m.group(2))
                if at:
                    kept.setdefault(int(at, 16), f'the harness replaces its entry ({s.path})')
    with open(os.path.join(ROOT, 'config', 'rebirth_patches.csv'), encoding='utf-8') as f:
        for r in csv.DictReader(f):
            fn = (r.get('function') or '').split('+')[0]
            if re.fullmatch(r'[0-9A-Fa-f]{8}', fn):
                kept.setdefault(int(fn, 16), 'Rebirth-patched function (config/rebirth_patches.csv)')
            # A patch over code outside the function list (function "data") may cover function starts.
            if re.fullmatch(r'[0-9A-Fa-f]{8}', r.get('site') or '') and (r.get('size') or '').isdigit():
                PATCHED_RANGES.append((int(r['site'], 16), int(r['site'], 16) + int(r['size'])))
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


# ------------------------------------------------------------------- aliases and instantiations (C13b)
# A registration may name its function through a using-alias (`using Map8 = stlp::HashtableAbi<...>;` in the
# registering file or a header it includes) or a template instantiation whose arguments are spelled without
# their namespaces or through <cstdint> typedefs. expand_aliases writes the aliases out; the map is then
# searched by the name as written and, failing that, by its qualifier-free form (loose_key) when that is unique.

_ALIAS = re.compile(r'\busing\s+([A-Za-z_]\w*)\s*=\s*([^;{}]+);|\btypedef\s+([^;{}()]+?)\s+([A-Za-z_]\w*)\s*;')
_ALIASES = {}          # qualified alias name -> [(Source, expansion, namespaces)]
_STD_INTS = {'uint32_t': 'unsigned int', 'int32_t': 'int', 'uint16_t': 'unsigned short', 'int16_t': 'short',
             'uint8_t': 'unsigned char', 'int8_t': 'signed char', 'size_t': 'unsigned int',
             'uintptr_t': 'unsigned int', 'intptr_t': 'int', 'ptrdiff_t': 'int', 'uint64_t': 'unsigned __int64',
             'int64_t': '__int64', 'DWORD': 'unsigned long', 'BYTE': 'unsigned char', 'WORD': 'unsigned short'}


_TEMPLATE_ALIASES = {}  # alias templates: qualified name -> [(Source, expansion, namespaces, [parameter names])]
_MEMBER_ALIASES = {}    # aliases in a (non-template) class: `net::trees::CountedValue::Type` -> [(Source, ...)]


def _template_param_names(text):
    """The parameter names of `template <typename K, auto Fn = nullptr>`'s list."""
    names = []
    for p in split_params(text):
        p = p.split('=')[0].strip()
        m = re.search(r'([A-Za-z_]\w*)\s*$', p)
        if m:
            names.append(m.group(1))
    return names


def collect_aliases(sources):
    _ALIASES.clear()
    _TEMPLATE_ALIASES.clear()
    _MEMBER_ALIASES.clear()
    _SEEN_ALIASES.clear()
    _LOOSE.clear()
    for s in sources:
        for _, _, k, n in s.scopes:
            if k == 'ns':
                _NAMESPACES.update(n.split('::'))
    for s in sources:
        if not s.path.startswith('src/'):
            continue
        for m in _ALIAS.finditer(s.bare):
            scope = s.scope_at(m.start())
            if s.in_body(m.start()) or any(k == 'template class' for k, _ in scope):
                continue
            name, exp = (m.group(1), m.group(2)) if m.group(1) else (m.group(4), m.group(3))
            exp = s.code[m.start(2 if m.group(1) else 3):m.end(2 if m.group(1) else 3)]
            exp = re.sub(r'\btypename\s+', '', exp).strip()
            ns = [n for n in s.namespaces_at(m.start()) if n != "`anonymous namespace'"]
            classes = [n for k, n in scope if k == 'class']
            t = re.search(r'\btemplate\s*<([^;{}]*)>\s*$', s.bare[max(0, m.start() - 300):m.start()])
            if t is None and re.search(r'\btemplate\s*<[^;{}]*$', s.bare[max(0, m.start() - 300):m.start()]):
                continue
            q = '::'.join(ns + classes + [name])
            if t is not None:
                if m.group(1) and not classes:
                    _TEMPLATE_ALIASES.setdefault(q, []).append((s, exp, ns, _template_param_names(t.group(1))))
            elif classes:
                _MEMBER_ALIASES.setdefault(q, []).append((s, exp, ns + classes[:-1]))
            else:
                _ALIASES.setdefault(q, []).append((s, exp, ns))


def _visible(cands, src):
    own = [c for c in cands if c[0] is src]
    seen = [c for c in cands if c[0] is not src and c[0].path.endswith('.h') and sees(src, c[0])]
    for group in (own, seen):
        if len({normalize_type(c[1]) for c in group}) == 1:
            return group[0]
    return None


def alias_for(qname, src, members=False):
    """The expansion and its namespaces of alias `qname` as `src` sees it (its own first), or None; with `members`
    also an alias declared in a class (`CountedValue::Type`)."""
    c = _visible(_ALIASES.get(qname, ()), src) or (members and _visible(_MEMBER_ALIASES.get(qname, ()), src))
    return (c[1], c[2]) if c else None


def alias_template_for(qname, src):
    """The expansion, namespaces and parameter names of alias template `qname` as `src` sees it, or None."""
    c = _visible(_TEMPLATE_ALIASES.get(qname, ()), src)
    return (c[1], c[2], c[3]) if c else None


_QUALIFIED = re.compile(r'(?<![\w:])(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*')


def expand_aliases(name, src, namespaces=()):
    """`name` (a registration's function) with the aliases of its scope written out."""
    parts = split_qualified(name)
    scope, last = '::'.join(parts[:-1]), parts[-1]
    if not scope:
        return name
    return expand_text(scope, src, namespaces) + '::' + last


def expand_text(scope, src, namespaces=()):
    """A type's text with the aliases `src` sees written out: plain aliases, alias templates (their arguments put
    in for their parameters) and, inside what an alias wrote, a class's member aliases (`Second::Type`). Names in an
    alias's expansion are looked up in the alias's namespaces too."""
    contexts = [list(namespaces)]

    def lookup(head, find):
        for ns in contexts:
            for pre in [ns[:i] for i in range(len(ns), -1, -1)]:
                a = find('::'.join(list(pre) + [head]))
                if a:
                    return a
        return None

    def learn(ans):
        if ans and list(ans) not in contexts:
            contexts.append(list(ans))

    for _ in range(16):
        changed = False
        # alias templates first: `PairTree<K, CountedValue>`
        for m in _TEMPLATE_ID.finditer(scope):
            head = m.group(0)[:-1].lstrip(':')
            a = lookup(head, lambda q: alias_template_for(q, src))
            if not a:
                continue
            close = _matching_angle(scope, m.end() - 1)
            if close is None:
                continue
            exp, ans, params = a
            args = split_params(scope[m.end():close])
            if len(args) != len(params):
                continue
            subst = dict(zip(params, args))
            body = re.sub(r'(?<![\w:])([A-Za-z_]\w*)\b', lambda p: subst.get(p.group(1), p.group(0)), exp)
            scope = scope[:m.start()] + body + scope[close + 1:]
            learn(ans)
            changed = True
            break
        if changed:
            continue

        def sub(m):
            nonlocal changed
            tok = m.group(0).lstrip(':')
            tparts = tok.split('::')
            for k in range(len(tparts), 0, -1):
                head = '::'.join(tparts[:k])
                a = lookup(head, lambda q: alias_for(q, src, members=k > 1))
                if a:
                    changed = True
                    exp, ans = a
                    learn(ans)
                    return '::'.join([exp] + tparts[k:])
            return m.group(0)
        scope = _QUALIFIED.sub(sub, scope)
        if not changed:
            break
    return scope


def _matching_angle(text, open_at):
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '<':
            depth += 1
        elif text[i] == '>':
            depth -= 1
            if depth == 0:
                return i
    return None


# The type names a call site writes (a template argument, a wrapper's class) through the aliases it sees:
# `stlp::TreeAbi<stlp::map<CRefObject*,net::AttachedId,stlp::TreeLess<CRefObject*>>, ...>` is `trees::Attached`
# where net_connection_trees.h is included. An alias may leave out default template arguments.

_SEEN_ALIASES = {}


def visible_aliases(src, offset):
    """[(name as the site writes it, loose name tree of its type)] of the aliases `src` sees at `offset`."""
    here = [n for n in src.namespaces_at(offset) if n != "`anonymous namespace'"]
    here_q = '::'.join(here).split('::') if here else []
    key = (src.path, tuple(here_q))
    if key in _SEEN_ALIASES:
        return _SEEN_ALIASES[key]
    out = []
    for qname, cands in _ALIASES.items():
        for a_src, exp, ns in cands:
            if a_src is not src and not (a_src.path.endswith('.h') and sees(src, a_src)):
                continue
            ns_q = '::'.join(ns).split('::') if ns else []
            k = 0
            while k < len(ns_q) and k < len(here_q) and ns_q[k] == here_q[k]:
                k += 1
            name = '::'.join(ns_q[k:] + [qname.split('::')[-1]])
            tree = name_tree(loose_key(expand_text(exp, a_src, ns)))
            if tree and any(a is not None for _, a in tree):
                out.append((name, tree))
            break
    _SEEN_ALIASES[key] = out
    return out


_TEMPLATE_ID = re.compile(r'(?<![\w:])[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*<')


def abbreviated(text, src, offset):
    """`text` (a normalised type) with template-ids written through the aliases the site sees."""
    if '<' not in text:
        return text
    aliases = visible_aliases(src, offset)
    if not aliases:
        return text
    spans = []
    for m in _TEMPLATE_ID.finditer(text):
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == '<':
                depth += 1
            elif text[i] == '>':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        spans.append((m.start(), i + 1))
    spans.sort(key=lambda s: s[0] - s[1])
    done = []
    for a, b in spans:
        if any(x <= a < y or x < b <= y for x, y, _ in done):
            continue
        tree = name_tree(loose_key(text[a:b]))
        hits = {n for n, at in aliases if at == tree} or {n for n, at in aliases if prefix_match(at, tree)}
        if len(hits) == 1:
            done.append((a, b, next(iter(hits))))
    for a, b, n in sorted(done, reverse=True):
        text = text[:a] + n + text[b:]
    return text


_NAMESPACES = {'std'}   # every namespace name the sources open (collect_aliases)


def loose_key(name):
    """A normalised name without its namespace qualifiers and with the <cstdint> typedefs written out."""
    t = normalize_type(name)
    t = re.sub(r'\b(?:std::)?(' + '|'.join(_STD_INTS) + r')\b', lambda m: _STD_INTS[m.group(1)], t)
    t = re.sub(r'(?<![\w])([A-Za-z_]\w*)::(?=[A-Za-z_])', lambda m: '' if m.group(1) in _NAMESPACES else m.group(0), t)
    # Template arguments as undname prints them: numbers in decimal, nullptr as 0, `unsigned` as `unsigned int`,
    # a function argument without its `&` (collapse_function_arguments).
    t = re.sub(r'(?<![\w.])0[xX]([0-9A-Fa-f]+)[uUlL]*\b', lambda m: str(int(m.group(1), 16)), t)
    t = re.sub(r'(?<![\w.])([0-9]+)[uUlL]+\b', r'\1', t)
    t = re.sub(r'\bnullptr\b', '0', t)
    t = re.sub(r'\bunsigned(?!\s+(?:int|char|short|long|__int64)\b)', 'unsigned int', t)
    t = re.sub(r'([<,])\s*&\s*(?=[A-Za-z_])', r'\1', t)
    return re.sub(r'\s+', ' ', t)


_LOOSE = {}


def loose_index(decls):
    key = id(decls)
    if _LOOSE.get('key') != key:
        idx = defaultdict(set)
        for q in decls:
            if '<' in q or '::' in q:
                idx[loose_key(q)].add(q)
        _LOOSE.clear()
        _LOOSE.update(key=key, idx=idx)
    return _LOOSE['idx']


def using_namespaces(src):
    """The namespaces a file opens with `using namespace N;` (as lists of names)."""
    return [m.group(1).split('::') for m in re.finditer(r'(?<![\w])using\s+namespace\s+([\w:]+)\s*;', src.bare)]


def resolve_registered(name, src, decls, simple_index):
    """resolve_target for a registration in `src`: the name as written, then with its aliases written out, then
    by its qualifier-free form."""
    t, why = resolve_target(name, decls, simple_index)
    if t is not None or why != 'target not found in the map':
        return t, why, name
    # The aliases of the file's scope, then of the namespaces it opens with `using namespace`.
    expansions = []
    for ns in [[]] + using_namespaces(src):
        expanded = expand_aliases(name, src, ns)
        if expanded != name and expanded not in expansions:
            expansions.append(expanded)
            t2, why2 = resolve_target(expanded, decls, simple_index)
            if t2 is not None or why2 != 'target not found in the map':
                return t2, why2, expanded
    for cand in expansions + [name]:
        hits = loose_index(decls).get(loose_key(cand), set())
        if len(hits) != 1:
            # Default template arguments the registration leaves out (ArrayAbi<Bytes<8>> for
            # ArrayAbi<Bytes<8>, PlainElements<Bytes<8>>>): each argument list written is a prefix of the map's.
            want = name_tree(loose_key(cand))
            hits = {q for q in simple_index.get(split_qualified(normalize_type(cand))[-1], ())
                    if want is not None and prefix_match(want, name_tree(loose_key(q)))}
        if len(hits) == 1:
            q = next(iter(hits))
            t3, why3 = resolve_target(q, decls, simple_index)
            return t3, why3, q
    return None, why, name


def name_tree(name):
    """A qualified name as [(identifier, [argument trees] or None)], template arguments parsed; None when it does
    not parse (a function type, a pointer to member)."""
    pos = [0]

    def ident():
        m = re.compile(r'\s*([^<>,:]+?)\s*(?=[<>,]|::|$)').match(name, pos[0])
        if not m:
            return None
        pos[0] = m.end()
        return m.group(1)

    def qualified():
        out = []
        while True:
            i = ident()
            if i is None:
                return None
            args = None
            if name.startswith('<', pos[0]):
                pos[0] += 1
                args = []
                while True:
                    a = qualified()
                    if a is None:
                        return None
                    args.append(a)
                    if name.startswith(',', pos[0]):
                        pos[0] += 1
                        continue
                    if name.startswith('>', pos[0]):
                        pos[0] += 1
                        break
                    return None
            out.append((i, args))
            m = re.compile(r'\s*').match(name, pos[0])
            pos[0] = m.end()
            if name.startswith('::', pos[0]):
                pos[0] += 2
                continue
            # a pointer / reference / const after the name belongs to it
            m = re.compile(r'[\s*&]*(?:const[\s*&]*)*').match(name, pos[0])
            if m.end() > pos[0]:
                out.append((name[pos[0]:m.end()].replace(' ', ''), None))
                pos[0] = m.end()
            return out

    tree = qualified()
    return tree if tree is not None and pos[0] == len(name) else None


def prefix_match(want, have):
    if want is None or have is None or len(want) != len(have):
        return False
    for (wi, wa), (hi, ha) in zip(want, have):
        if wi != hi or (wa is None) != (ha is None):
            return False
        if wa is not None and (len(wa) > len(ha) or not all(prefix_match(a, b) for a, b in zip(wa, ha))):
            return False
    return True


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
    """How a call of the forwarder becomes a call of the target: 'rename', 'this', 'cast' (the object is
    passed as void*: `static_cast<T*>(self)->M(...)`, see rewrite_site) or None. A forwarder
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
    if self_t == tsig.scope + '*' or (self_t == tsig.scope + ' const*' and tsig.const):
        kind = 'this'
    elif self_t == 'void*' or (self_t == 'void const*' and tsig.const):
        kind = 'cast'
    else:
        return None
    rest = fsig.params[1:]
    if rest:
        if rest[0] not in DUMMY_TYPES:
            return None
        rest = rest[1:]
    return kind if same_params(rest, tsig.params) else None


def same_params(fparams, tparams):
    """Parameter lists that take the same values: equal, or the target adds const to a pointee."""
    if len(fparams) != len(tparams):
        return False
    for f, t in zip(fparams, tparams):
        if f != t and not (t.endswith(('*', '&')) and re.sub(r' ?\bconst(?=[*&]$)', '', t) == f):
            return False
    return True


# ------------------------------------------------------------------------------- loose matching (C13a1)
# --loose: a forwarder whose declaration differs from the target's only in ways a call site can spell is
# converted too, the differences written at each call: an object or argument of another pointer type
# through static_cast (void* to T*, a base to a derived class), a narrower integer through static_cast,
# a pointer for a reference dereferenced, a result the caller reads through static_cast back to the
# forwarder's type. Never converted: anything with float / double / bool on one side only (a float's
# bits passed as an integer would be converted), unrelated classes, other parameter counts.

INT_FAMILIES = [{'int', 'unsigned int', 'long', 'unsigned long'}, {'short', 'unsigned short'},
                {'char', 'unsigned char', 'signed char'}]
INT_WIDTH = {t: w for w, fam in zip((4, 2, 1), INT_FAMILIES) for t in fam}
BUILTINS = set(INT_WIDTH) | {'void', 'bool', 'float', 'double', 'long double', '__int64', 'unsigned __int64', 'wchar_t'}
_CLASS_NAME = re.compile(r'[A-Za-z_][\w]*(?:::[A-Za-z_]\w*)*')


def pointer(t):
    """(pointee, const) of a one-level pointer type, else None."""
    if t.endswith('const*'):
        base, const = t[:-6].rstrip(), True
    elif t.endswith('*'):
        base, const = t[:-1], False
    else:
        return None
    if '*' in base or '&' in base or base.endswith('const'):
        return None
    return base, const


def relation(a, b, classes):
    """'up' when class a derives from b, 'down' when b derives from a, else None (simple names)."""
    if not (_CLASS_NAME.fullmatch(a) and _CLASS_NAME.fullmatch(b)) or a in BUILTINS or b in BUILTINS:
        return None
    sa, sb = split_qualified(a)[-1], split_qualified(b)[-1]
    if sa in classes.all_derived(sb):
        return 'up'
    if sb in classes.all_derived(sa):
        return 'down'
    return None


def convert_param(a, b, classes):
    """How a forwarder argument of type a reaches a target parameter of type b: None (as it is),
    ('cast', b) / ('reinterpret', b) (written through that cast), ('deref',) or False (not convertible)."""
    if a == b or same_params([a], [b]):
        return None
    if a in INT_WIDTH and b in INT_WIDTH:
        return None if INT_WIDTH[a] <= INT_WIDTH[b] else ('cast', b)
    if b.endswith('&') and not b.endswith('&&'):
        ref = b[:-1]
        if a in (ref + '*', ref.replace('const', '').strip() + '*') or (a.endswith('const*') and ref.endswith('const')
                                                                       and a[:-6].rstrip() == ref[:-5].rstrip()):
            return ('deref',)
        return False
    pa, pb = pointer(a), pointer(b)
    if not pa or not pb:
        return False
    (ab, ac), (bb, bc) = pa, pb
    if ac and not bc:
        return False
    if bb == 'void':
        return None
    if ab == 'void':
        return False if '<' in bb else ('cast', b)
    if ab in INT_WIDTH and bb in INT_WIDTH and INT_WIDTH[ab] == INT_WIDTH[bb]:
        return ('reinterpret', b)
    r = relation(ab, bb, classes)
    if r == 'up':
        return None
    if r == 'down' and '<' not in bb:
        return ('cast', b)
    return False


def convert_ret(fr, tr, classes):
    """How the target's result reaches a caller that reads it as the forwarder's: None, ('cast', fr) /
    ('reinterpret', fr), or False."""
    if fr == 'void' or fr == tr:
        return None
    if fr in INT_WIDTH and tr in INT_WIDTH and INT_WIDTH[fr] == INT_WIDTH[tr]:
        return None
    pf, pt = pointer(fr), pointer(tr)
    if not pf or not pt:
        return False
    (fb, fc), (tb, tc) = pf, pt
    if tc and not fc:
        return False
    if fb == 'void':
        return None
    if '<' in fb:
        return False
    if tb == 'void':
        return ('cast', fr)
    if fb in INT_WIDTH and tb in INT_WIDTH and INT_WIDTH[fb] == INT_WIDTH[tb]:
        return ('reinterpret', fr)
    r = relation(tb, fb, classes)
    if r == 'up':
        return None
    if r == 'down':
        return ('cast', fr)
    return False


def match_loose(fsig, tsig, classes):
    """('loose', conversions) or (None, why)."""
    conv = {'mode': '', 'self': None, 'args': [], 'ret': None}
    params = list(fsig.params)
    if fsig.member:
        if not tsig.member or tsig.scope != fsig.scope:
            return None, 'member forwarder of another class'
        if fsig.const and not tsig.const:
            return None, 'const forwarder of a non-const target'
        conv['mode'] = 'member'
    elif not tsig.member:
        conv['mode'] = 'free'
    else:
        if fsig.cc != '__fastcall' or tsig.cc != '__thiscall' or not params:
            return None, 'calling conventions'
        conv['mode'] = 'this'
        self_t = params.pop(0)
        if params:
            if params[0] not in DUMMY_TYPES:
                return None, 'no EDX parameter'
            params.pop(0)
        p = pointer(self_t)
        if not p or (p[1] and not tsig.const):
            return None, f'object {self_t}'
        if p[0] != tsig.scope:
            if '<' in tsig.scope or (p[0] != 'void' and relation(p[0], tsig.scope, classes) is None):
                return None, f'object {self_t} for {tsig.scope}'
            conv['self'] = 'cast'
    if len(params) != len(tsig.params):
        return None, 'parameter count'
    for a, b in zip(params, tsig.params):
        c = convert_param(a, b, classes)
        if c is False:
            return None, f'parameter {a} for {b}'
        conv['args'].append(c)
    r = convert_ret(fsig.ret, tsig.ret, classes)
    if r is False:
        return None, f'result {tsig.ret} read as {fsig.ret}'
    conv['ret'] = r
    return 'loose', conv


def spelled(t, src, offset):
    """A normalised type as C++ at a call site."""
    p = pointer(t)
    if p:
        base, const = p
        inner = base if base in BUILTINS else spelled_type(base, src, offset)
        return ('const ' if const else '') + inner + '*'
    return t if t in BUILTINS else spelled_type(t, src, offset)


# ---------------------------------------------------------------- container ABI wrappers (C13b)
# The STLport / MFC instantiations are registered through static wrappers (stlp::VectorAbi<T, E>::Reserve,
# mfc::impl::ArrayAbi<T, E>::SetSize, ...) that take the container as their first parameter and whose body is
# one call of the container's own member: `self->template Reserve<E>(n);`. A call through such a forwarder
# becomes that member call on the caller's container (`list->Reserve<stlp::StringElement>(n)`), when the
# forwarder's container is the wrapper's, or the same template with the element type the wrapper erases
# (mfc::impl::Bytes<N>, stlp::Opaque<N>) written as the real one and the rest equal (then the body may not name
# the wrapper's policy). Parameters the body dereferences (`*value`) are dereferenced at the call; parameters it
# does not read must be plain arguments (nullptr, a literal or a name).

_ERASED_LEAF = re.compile(r'^(?:mfc::impl::Bytes|stlp::Opaque|Bytes|Opaque)<\d+>$')
# Element types a registered instantiation stands for when the caller's container holds another element of
# the same size and kind (vector<unsigned int> for vector<int> or vector<T*>, CList<void*> for CList<T*>,
# vector<char> for vector<unsigned char>): the caller's own instantiation of the member is the same code.
# Never float / double (an x87 copy is not a bit copy) or class types (those are erased leaves above).
_FOUR_BYTE_INTS = {'int', 'unsigned int', 'long', 'unsigned long'}
_RETARGET = {
    'unsigned int': lambda y: y in _FOUR_BYTE_INTS or _plain_pointer(y),
    'void*': lambda y: _plain_pointer(y),
    'char': lambda y: y in ('unsigned char', 'signed char'),
}


def _plain_pointer(text):
    """A data pointer type's text (not a function pointer)."""
    return text.endswith('*') and '(' not in text
_BODY_CALL = re.compile(r'^(return\s+)?self\s*->\s*(?:template\s+)?([A-Za-z_]\w*)\s*(<[^()]*>)?\s*\((.*)\)\s*;$', re.S)
_TYPE_WORDS = {'const', 'void', 'int', 'unsigned', 'char', 'float', 'double', 'bool', 'short', 'long', 'signed'}


def _param_name(text):
    """The name of a parameter declaration, or None when it has none."""
    text = re.sub(r'=.*$', '', text, flags=re.S).strip()
    m = re.search(r'([A-Za-z_]\w*)\s*$', text)
    if not m or m.group(1) in _TYPE_WORDS:
        return None
    before = text[:m.start()].strip()
    if not before or before.endswith('::'):
        return None
    return m.group(1)


def _template_params(src, head_end):
    """Names of the template parameters of the class whose body opens at `head_end`."""
    i = max(src.bare.rfind(';', 0, head_end), src.bare.rfind('}', 0, head_end), src.bare.rfind('{', 0, head_end))
    m = re.search(r'template\s*<(.*)>\s*(?:struct|class)\s+\w+', src.bare[i + 1:head_end], re.S)
    if not m:
        return []
    names = []
    for p in split_params(m.group(1)):
        p = re.sub(r'=.*$', '', p, flags=re.S).strip()
        mm = re.search(r'([A-Za-z_]\w*)$', p)
        names.append(mm.group(1) if mm else '')
    return names


def _template_args(scope):
    """The argument texts of the last template-id of a qualified class name."""
    last = split_qualified(scope)[-1]
    if '<' not in last:
        return []
    return split_params(last[last.index('<') + 1:last.rindex('>')])


def abi_body(t, classes):
    """The one member call a static wrapper's inline body makes: (member, template argument texts or None,
    [(parameter index, dereferenced)], [parameter names]) or a reason."""
    parts = split_qualified(t.scope)
    cls = parts[-1].split('<')[0]
    bodies = [(s, o, c) for s, o, c in classes.bodies.get(cls, ())
              if '::'.join(n for n in s.namespaces_at(o) if n != "`anonymous namespace'") == '::'.join(parts[:-1])]
    if len(bodies) != 1:
        return 'wrapper class body not found once'
    s, o, c = bodies[0]
    found = [m for m in re.finditer(r'(?<![\w~])' + re.escape(t.simple) + r'\s*\(', s.bare[o:c])]
    found = [m for m in found if re.search(r'\bstatic\b[^;{}]*$', s.bare[o:o + m.start()])]
    if len(found) != 1:
        return 'wrapper overloaded or not inline'
    open_paren = o + found[0].end() - 1
    close, args = parse_args(s.bare, open_paren)
    if close is None:
        return 'wrapper parameters unbalanced'
    names = [_param_name(s.code[a:b]) for a, b in args]
    m = re.match(r'\s*(?:const\s*)?\{', s.bare[close + 1:])
    if not m:
        return 'wrapper not defined inline'
    body_open = close + 1 + m.end() - 1
    depth, i = 0, body_open
    while i < c:
        if s.bare[i] == '{':
            depth += 1
        elif s.bare[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = s.code[body_open + 1:i].strip()
    bm = _BODY_CALL.match(body)
    if not bm or ';' in bm.group(4) or '{' in bm.group(4):
        return 'wrapper body is not one member call'
    if not names or names[0] is None:
        return 'wrapper object unnamed'
    targs = None
    if bm.group(3):
        tparams = _template_params(s, o)
        actual = _template_args(t.scope)
        if not tparams or len(tparams) != len(actual):
            return 'wrapper template arguments'
        subst = dict(zip(tparams, actual))
        targs = []
        for a in split_params(bm.group(3)[1:-1]):
            if a.strip() not in subst:
                return 'wrapper body names other template arguments'
            targs.append(subst[a.strip()])
    order = []
    for a in split_params(bm.group(4)):
        am = re.fullmatch(r'\s*(\*?)\s*([A-Za-z_]\w*)\s*', a)
        if not am or am.group(2) not in names[1:]:
            return 'wrapper body passes more than its parameters'
        order.append((names.index(am.group(2)), bool(am.group(1))))
    return bm.group(2), targs, order, names


def tree_text(tree):
    out = []
    for i, a in tree:
        if out and not re.match(r'[*&]', i):
            out.append('::')
        out.append(i)
        if a is not None:
            out.append('<' + ','.join(tree_text(x) for x in a) + '>')
    return ''.join(out)


def erased_match(want, have, subst):
    """Whether name tree `have` (the caller's type) is `want` (the wrapper's) with erased leaves replaced;
    the replacements go into subst (erased text -> real text)."""
    if want is None or have is None or len(want) != len(have):
        return False
    for (wi, wa), (hi, ha) in zip(want, have):
        if wi != hi or (wa is None) != (ha is None):
            return False
        if wa is None:
            continue
        if len(wa) != len(ha):
            return False
        for x, y in zip(wa, ha):
            xt, yt = tree_text(x), tree_text(y)
            if _ERASED_LEAF.match(xt) or (xt != yt and xt in _RETARGET and _RETARGET[xt](yt)):
                if subst.setdefault(xt, yt) != yt:
                    return False
            elif not erased_match(x, y, subst):
                return False
    return True


def substituted(t, subst):
    """A normalised type with the erased types replaced (`Bytes<8>const*` -> `AreaOffset const*`)."""
    for k, v in subst.items():
        t = re.sub(re.escape(k) + r'(?=\w)', v + ' ', t).replace(k, v)
    return normalize_type(t) if subst else t


def match_unwrap(fsig, tsig, classes):
    """('unwrap', info) for a forwarder of a static container wrapper (see above), or (None, why)."""
    if fsig.member or not tsig.static or fsig.cc != tsig.cc or not fsig.params or not tsig.params:
        return None, 'not a wrapper call'
    if len(fsig.params) != len(tsig.params):
        return None, 'parameter count'
    fself, tself = fsig.params[0], tsig.params[0]
    if not fself.endswith('*') or not tself.endswith('*'):
        return None, 'object not a pointer'
    fbase, tbase = fself[:-1], tself[:-1]
    if tbase.endswith(' const') and not fbase.endswith(' const'):
        tbase = tbase[:-6]
    if fbase.endswith(' const') and not tbase.endswith(' const'):
        return None, 'const object for a non-const wrapper'
    body = abi_body(tsig, classes)
    if isinstance(body, str):
        return None, body
    member, targs, order, names = body
    subst = {}
    if fbase != tbase:
        if not erased_match(name_tree(tbase), name_tree(fbase), subst) or not subst:
            return None, f'object {fself} for {tself}'
        if targs:
            # The member's element policy follows a retargeted element (PlainElement<unsigned int> ->
            # PlainElement<int>); an erased element's policy may be the wrapper's own, so not those.
            if any(_ERASED_LEAF.match(k) for k in subst):
                return None, f'object {fself} for {tself}'
            targs = [substituted(normalize_type(x), subst) for x in targs]
    used = {i for i, _ in order}
    # A parameter is the wrapper's with the element replaced, or (a count, an index) unchanged.
    for i, (a, b) in enumerate(zip(fsig.params[1:], tsig.params[1:]), 1):
        if i in used and not same_params([a], [substituted(b, subst)]) and not same_params([a], [b]):
            return None, f'parameter {a} for {substituted(b, subst)}'
    ret = substituted(tsig.ret, subst)
    if fsig.ret != 'void' and ret != fsig.ret and tsig.ret != fsig.ret:
        return None, f'result {ret} read as {fsig.ret}'
    return 'unwrap', {'member': member, 'targs': targs, 'order': order, 'used': used}


_PLAIN_ARG = re.compile(r'nullptr|NULL|0|-?\d+|[A-Za-z_]\w*|true|false')


def rewrite_unwrap(f, site):
    """rewrite_site for an 'unwrap' forwarder: `F(obj, nullptr, a, &b)` -> `obj->M(a, b)`."""
    s = site.src
    open_paren, close, args = site.args
    if site.obj or site.qualifier not in ('', '::'):
        return None, 'qualified call'
    if len(args) != len(f.sig.params):
        return None, 'argument count differs'
    info = f.unwrap
    for i, (a, b) in enumerate(args):
        if i and i not in info['used'] and not _PLAIN_ARG.fullmatch(s.bare[a:b].strip()):
            return None, f'an argument the member does not take has an effect ({s.bare[a:b].strip()})'
    sa, sb = _trimmed(s.code, *args[0])
    self_code = s.code[sa:sb]
    if '\n' in self_code or s.code[sa:sb] != s.raw[sa:sb]:
        return None, 'object expression spans lines or has a comment'
    name = info['member']
    if info['targs']:
        name += '<' + ', '.join(spelled(normalize_type(x), s, site.start) for x in info['targs']) + '>'
    if self_code.startswith('&') and not self_code.startswith('&&') and is_postfix(self_code[1:]):
        head = [_trimmed(s.code, sa + 1, sb), '.' + name]
    elif is_postfix(self_code):
        head = [(sa, sb), '->' + name]
    else:
        head = ['(', (sa, sb), ')->' + name]
    pieces = head + ['(']
    for k, (i, deref) in enumerate(info['order']):
        ta, tb = _trimmed(s.code, *args[i])
        if k:
            pieces.append(', ')
        text = s.code[ta:tb]
        if not deref:
            pieces.append((ta, tb))
        elif text.startswith('&') and not text.startswith('&&') and is_postfix(text[1:].strip()):
            pieces.append(_trimmed(s.code, ta + 1, tb))
        elif is_postfix(text):
            pieces += ['*', (ta, tb)]
        else:
            pieces += ['*(', (ta, tb), ')']
    pieces.append(')')
    return (site.start, close + 1, pieces), ''


# ------------------------------------------------------------------------------------- class graph

class Classes:
    """Class bodies by simple name and who derives from whom (text), for the hiding check."""

    def __init__(self, sources):
        self.bodies = defaultdict(list)       # simple name -> [(src, open, close)]
        self.derived = defaultdict(set)       # base simple name -> derived simple names
        self.owners = defaultdict(set)        # name followed by ( in a class body -> class simple names
        for s in sources:
            for o, c, k, n in s.scopes:
                if k in ('class', 'template class'):
                    self.bodies[n].append((s, o, c))
                    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', s.bare[o:c]):
                        self.owners[m.group(1)].add(n)
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


_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
_SEES = {}


def sees(a, b):
    """Whether file `a` (a Source) includes file `b`, directly or through other src / tests files."""
    if not _SEES:
        srcs = _FREE_DECLS.get('all', ())
        by_base = defaultdict(list)
        for x in srcs:
            by_base[os.path.basename(x.path)].append(x.path)
        graph = {x.path: {p for inc in _INCLUDE.findall(x.raw) for p in by_base.get(os.path.basename(inc), ())}
                 for x in srcs}
        for x in srcs:
            seen, todo = set(), list(graph[x.path])
            while todo:
                p = todo.pop()
                if p not in seen:
                    seen.add(p)
                    todo += graph.get(p, ())
            _SEES[x.path] = seen
    return b.path in _SEES.get(a.path, ())


def find_sites(f, sources, decl_spans):
    """Every use of the forwarder's name, or a reason it cannot be rewritten. A declaration of the
    forwarder elsewhere (the class body's for an out-of-class definition, a header's prototype of a
    free one) goes into f.extra, to be deleted with it."""
    chain = f.src.scope_at(f.start)
    local = not f.in_class and '::' not in f.name and (
        any(n == "`anonymous namespace'" for _, n in chain) or re.match(r'\s*static\b', f.text))
    namespaces = [n for k, n in chain if k == 'ns']
    sites = []
    for s, a, b in _WORDS.get(f.simple, ()):
        if local and s is not f.src:
            continue
        if f.src.path.endswith('.h') and s is not f.src and not sees(s, f.src) and not sees(f.src, s):
            continue        # a file that cannot see the forwarder's header: the name is another function's there
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
            # std::toupper is not gamecrt::toupper: a qualifier naming another scope is another function.
            owner = qual.rstrip(':').split('::')[-1]
            if owner and owner not in [n for _, n in chain] + f.name.split('::')[:-1]:
                continue
        obj = tok if tok in ('.', '->') else ''
        if obj and not f.in_class and '::' not in f.name:
            continue        # a member of that name, not the free forwarder
        # A word in the target's own namespace is the target's (or hides the forwarder there).
        if f.target is not None and not qual and not obj and not f.target.member:
            tchain = [n for n in split_qualified(f.target.raw_name)[:-1] if n != "`anonymous namespace'"]
            site_named = [n for k, n in s.scope_at(a) if k == 'ns' and n != "`anonymous namespace'"]
            fchain = [n for n in namespaces if n != "`anonymous namespace'"]
            if tchain and tchain != fchain and site_named[:len(tchain)] == tchain:
                continue
        # An unqualified call outside the forwarder's namespace is another function (hook.cpp's
        # fseek is the host CRT's, not gamecrt::fseek), unless a using-directive brings it in.
        named = [n for n in namespaces if n != "`anonymous namespace'"]
        if named and not qual and not obj and not f.in_class and '::' not in f.name:
            site_ns = [n for k, n in s.scope_at(a) if k == 'ns' and n != "`anonymous namespace'"]
            prefix = enclosing_prefix(s, a)
            if site_ns[:len(named)] != named and not (site_ns + prefix)[:len(named)] == named:
                if not re.search(r'\busing\s+(?:namespace\s+)?' + re.escape(namespaces[-1]) + r'\b', s.code):
                    continue    # another function; with a using-directive the call is the forwarder's
        declaration = not obj and (tok and (tok[0].isalnum() or tok[0] == '_') and tok not in KEYWORDS_BEFORE_EXPR
                                   or tok in ('*', '&', '>', '~'))
        if declaration and tok in ('*', '&'):
            # `*Name(...)` / `&Name` after an operator, an opening bracket or a keyword is an expression: a
            # declarator has a type before its * or &.
            before, _ = prev_token(s.bare, at)
            if not before or not (before[0].isalnum() or before[0] == '_' or before in ('>', '*', '&', '::', ')'))                     or before in KEYWORDS_BEFORE_EXPR:
                declaration = False
        if declaration and is_call and not qual and not f.in_class and not s.in_body(a):
            close, _ = parse_args(s.bare, j)
            mm = re.match(r'\s*(?:const\s*)?;', s.bare[close + 1:close + 40]) if close else None
            here = s.scope_at(a)
            classes = [n for k, n in here if k == 'class']
            if '::' in f.name:
                same = bool(classes) and classes[-1] == f.name.split('::')[-2]
            else:
                same = not classes and [n for k, n in here if k == 'ns'] == namespaces
            if mm and same:
                f.extra.append((s, statement_start(s, at), close + 1 + mm.end()))
                continue
        if declaration and not obj and not qual:
            here = s.scope_at(a)
            classes = [n for k, n in here if k == 'class']
            site_named = [n for k, n in here if k == 'ns' and n != "`anonymous namespace'"]
            # (A member forwarder stays: a call through an object of another class with that member name cannot be
            # told from its own, and renaming it breaks the other class's code.)
            if not f.in_class and '::' not in f.name and site_named and site_named != named:
                continue        # a function of that name in another namespace
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


CAST_SITES = set()    # (path, line) of calls that need static_cast<T*> (--cast-sites)


def spelled_type(qname, site_src, site_offset):
    """A class's name as written at a call site: qualified past the namespaces the site is in, its template-ids
    through the aliases the site sees."""
    parts = split_qualified(qname)
    here = site_src.namespaces_at(site_offset)
    k = 0
    while k < len(parts) - 1 and k < len(here) and parts[k] == here[k]:
        k += 1
    return comma_spaced(abbreviated('::'.join(parts[k:]), site_src, site_offset))


def comma_spaced(text):
    """Template arguments as the sources write them: `A<B, C>`."""
    return re.sub(r',(?=\S)', ', ', text)


def spelled_target(tsig, site_src, site_offset):
    """The target's name as written at a call site: qualified past the namespaces the site is in."""
    parts = split_qualified(tsig.qname)
    here = site_src.namespaces_at(site_offset)
    scope = parts[:-1]
    k = 0
    while k < len(scope) and k < len(here) and scope[k] == here[k]:
        k += 1
    if k < len(scope):
        return comma_spaced(abbreviated('::'.join(scope[k:]), site_src, site_offset)) + '::' + parts[-1]
    return parts[-1]


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


_VOID_DECL = re.compile(r'\bvoid\s*(?:const\s*)?\*\s*(?:const\s+)?([A-Za-z_]\w*)\s*[;,)=\[{]')
_VOID_NAMES = {}


def looks_void(expr, src, local=False):
    # An object expression that is void* for sure (it names void) or by its last name: declared as a void*
    # in its file or in a header. Saves a compile round per call (--cast-sites does the rest).
    if 'void' in expr:
        return True
    last = re.search(r'([A-Za-z_]\w*)\s*$', expr)
    if not last or last.group(1) == 'this':
        return False
    if 'headers' not in _VOID_NAMES:
        _VOID_NAMES['headers'] = {n for h in _FREE_DECLS['all'] if h.path.endswith('.h')
                                  for n in _VOID_DECL.findall(h.code)}
    if src.path not in _VOID_NAMES:
        _VOID_NAMES[src.path] = set(_VOID_DECL.findall(src.code))
    return last.group(1) in _VOID_NAMES[src.path] or (not local and last.group(1) in _VOID_NAMES['headers'])


_MEMBER_DEF = re.compile(r'^[A-Za-z_][^;(\n]*?\b([A-Za-z_]\w*)::~?[A-Za-z_]\w*\s*\(', re.M)


def enclosing_class(src, offset):
    # The class of the out-of-class member definition the offset is in (its first line at column 0), or None.
    start = src.raw.rfind('\n}', 0, offset)
    last = None
    for m in _MEMBER_DEF.finditer(src.raw, start + 1 if start >= 0 else 0, offset):
        last = m.group(1)
    return last


_DEF_PREFIX = re.compile(r'^[A-Za-z_][^;(\n]*?\b((?:[A-Za-z_]\w*::)+)~?[A-Za-z_]\w*\s*\(', re.M)


def enclosing_prefix(src, offset):
    # The scopes `a::b::` of the out-of-class definition the offset is in (first line at column 0), as a list.
    start = src.raw.rfind('\n}', 0, offset)
    last = []
    for m in _DEF_PREFIX.finditer(src.raw, start + 1 if start >= 0 else 0, offset):
        last = m.group(1).split('::')[:-1]
    return last


def rewrite_site(f, site):
    """(start, end, pieces) for one call site, or a reason. Pieces are strings and (start, end) ranges of
    the raw text, which are written with the edits inside them (a forwarder call in the arguments)."""
    if f.kind == 'loose':
        return rewrite_loose(f, site)
    if f.kind == 'unwrap':
        return rewrite_unwrap(f, site)
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
        name = spelled_target(t, s, site.start)
        return renamed(s, site.start, site.name_end, '::' + name if f.global_qualify else name, close), ''
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
    own_member = self_code == 'this' and enclosing_class(s, site.start) == f.target.scope.split('::')[-1]
    needs_cast = f.kind == 'cast' and not own_member and (
        not f.unique or (s.path, s.line_of(site.start)) in CAST_SITES or looks_void(self_code, s))
    site.uncast = f.kind == 'cast' and not needs_cast and not own_member
    if needs_cast:
        cls = spelled_type(f.target.scope, s, site.start)
        if '<' in cls:
            return None, 'object passed as void* to a member of a class template'
        const = 'const ' if f.sig.params[0] == 'void const*' else ''
        head = [f'static_cast<{const}{cls}*>(', (sa, sb), ')->' + m]
    elif self_code == 'this':
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


def result_unused(s, site, close):
    """The call is a statement of its own (its result is not read)."""
    tok, _ = prev_token(s.bare, site.start)
    return s.bare[close + 1:].lstrip().startswith(';') and tok in (';', '{', '}', ')', 'else')


_VOID_CAST = re.compile(r'\(\s*(?:const\s+)?void\s*(?:const\s*)?\*\s*\)\s*|'
                        r'(?:static|reinterpret)_cast\s*<\s*(?:const\s+)?void\s*(?:const\s*)?\*\s*>\s*\(')


def unvoided(s, a, b):
    """The range of the argument s.code[a:b] without a cast to void* written for the forwarder: `(void*)x`
    and `static_cast<void*>(x)` -> x."""
    m = _VOID_CAST.match(s.code, a, b)
    if not m:
        return a, b
    if m.group(0).endswith('('):
        close, _ = parse_args(s.bare, m.end() - 1)
        return _trimmed(s.code, m.end(), close) if close == b - 1 else (a, b)
    return (m.end(), b) if is_postfix(s.code[m.end():b].strip()) else (a, b)


def rewrite_loose(f, site):
    """rewrite_site for a 'loose' forwarder: the call with the conversions of f.conv written at it. Casts are
    written where they are needed: a narrowing integer, a pointer for a reference, and an object or argument
    the compiler did not take (its line in --cast-sites) or that is void* for sure."""
    s = site.src
    open_paren, close, args = site.args
    t, conv = f.target, f.conv
    cast_here = (s.path, s.line_of(site.start)) in CAST_SITES
    if conv['mode'] == 'free':
        if site.obj or site.qualifier not in ('', '::'):
            return None, 'free forwarder called as a member or qualified'
        if t.anonymous and s.path != f.target_src_path:
            return None, 'target in an anonymous namespace of another file'
        name = spelled_target(t, s, site.start)
        start, head, rest = site.start, ['::' + name if f.global_qualify else name], args
    elif conv['mode'] == 'member':
        if site.qualifier and normalize_type(site.qualifier[:-2]).split('::')[-1] != t.scope.split('::')[-1]:
            return None, 'qualified member call through another class'
        start, head, rest = site.name_start, [t.simple], args
    else:
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
        m = t.simple
        sa, sb = unvoided(s, sa, sb)
        self_code = s.code[sa:sb]
        own_member = self_code == 'this' and enclosing_class(s, site.start) == t.scope.split('::')[-1]
        if conv['self'] and not own_member and (cast_here or not f.unique or looks_void(self_code, s, True)):
            const = 'const ' if f.sig.params[0].endswith('const*') else ''
            head = [f'static_cast<{const}{spelled_type(t.scope, s, site.start)}*>(', (sa, sb), ')->' + m]
        elif self_code == 'this':
            head = [m]
        elif self_code.startswith('&') and not self_code.startswith('&&') and is_postfix(self_code[1:]):
            head = [_trimmed(s.code, sa + 1, sb), '.' + m]
        elif is_postfix(self_code):
            head = [(sa, sb), '->' + m]
        else:
            head = ['(', (sa, sb), ')->' + m]
        start, rest = site.start, args[2:] if nparams > 2 else []
    if len(rest) != len(conv['args']):
        return None, 'argument count differs'
    pieces = head + ['(']
    for i, ((a, b), c) in enumerate(zip(rest, conv['args'])):
        ta, tb = _trimmed(s.code, a, b)
        if i:
            pieces.append(',')
            if a < ta:
                pieces.append((a, ta))
        narrowing = c and c[0] == 'cast' and c[1] in INT_WIDTH
        if c is None or (c[0] == 'cast' and not narrowing):
            # An argument cast to void* for the forwarder loses that cast; one of another type reaches the
            # target as it is unless the compiler said otherwise (--cast-sites) or it is void* for sure.
            ua, ub = unvoided(s, ta, tb)
            if c and (cast_here or looks_void(s.code[ua:ub], s, True)):
                pieces += [f'static_cast<{spelled(c[1], s, site.start)}>(', (ua, ub), ')']
            else:
                pieces.append((ua, ub))
        elif c[0] in ('cast', 'reinterpret'):
            kind = 'static_cast' if c[0] == 'cast' else 'reinterpret_cast'
            pieces += [f'{kind}<{spelled(c[1], s, site.start)}>(', (ta, tb), ')']
        else:
            text = s.code[ta:tb]
            if text.startswith('&') and not text.startswith('&&') and is_postfix(text[1:].strip()):
                pieces.append(_trimmed(s.code, ta + 1, tb))
            elif is_postfix(text):
                pieces += ['*', (ta, tb)]
            else:
                pieces += ['*(', (ta, tb), ')']
        if tb < b and '\n' in s.raw[tb:b]:
            pieces.append((tb, b))
    pieces.append(')')
    if conv['ret'] and not result_unused(s, site, close):
        kind = 'static_cast' if conv['ret'][0] == 'cast' else 'reinterpret_cast'
        pieces = [f'{kind}<{spelled(conv["ret"][1], s, site.start)}>('] + pieces + [')']
    return (start, close + 1, pieces), ''


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

def analyse(sources, decls, library=False, scope=None, tiers=None, exclude=()):
    tiers = tiers or {}
    simple_index = by_simple(decls)
    regs = registrations(sources, library)
    kept = kept_shims(sources)
    classes = Classes(sources)
    collect_aliases(sources)
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
        f.in_scope = f.in_scope and not any(fnmatch.fnmatch(f.src.path, g) for g in exclude)
        if f.address not in regs:
            f.reason = 'library code' if not library else 'no registration'
            continue
        if f.address in kept:
            f.reason = kept[f.address]
            continue
        if any(a <= f.address < b for a, b in PATCHED_RANGES):
            f.reason = 'Rebirth patch over its code (config/rebirth_patches.csv site)'
            continue
        if len(regs[f.address]) != 1:
            f.reason = 'several registrations'
            continue
        f.target_name, tsrc, plain = regs[f.address][0]
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
        t, why, resolved = resolve_registered(f.target_name, tsrc, decls, simple_index)
        if t is None and plain != f.target_name and why == 'target not found in the map':
            t, why, resolved = resolve_registered(plain, tsrc, decls, simple_index)
            if t is not None:
                f.target_name = plain
        if t is not None:
            f.target_resolved = resolved
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
        f.kind = None
        if t.static and not f.sig.member:
            kind, info = match_unwrap(f.sig, t, classes)
            if kind:
                f.kind, f.unwrap = kind, info
            else:
                f.unwrap_why = info
        if not f.kind:
            f.kind = match(f.sig, t)
        if not f.kind and tiers.get('loose'):
            f.kind, f.conv = match_loose(f.sig, t, classes)
            if not f.kind:
                f.loose_why = f.conv
        if not f.kind:
            f.reason = 'types differ'
            continue
        if f.kind == 'cast' and not tiers.get('cast'):
            f.reason = 'object passed as void* (--cast)'
            continue
        # Without a cast `p->M(...)` reaches T::M only when no other class has an M: then a call
        # through any other type does not compile.
        f.unique = simple_index.get(t.simple, set()) <= {t.qname} and \
            classes.owners.get(t.simple, set()) <= {split_qualified(t.scope)[-1].split('<')[0]}
        others = simple_index.get(t.simple, set()) - {t.qname}
        if t.member:
            cls = split_qualified(t.scope)[-1].split('<')[0]
            hiding = [d for d in classes.all_derived(cls) if classes.declares(d, t.simple)]
            if hiding:
                f.reason = f'a derived class declares the target name ({hiding[0]}::{t.simple})'
                continue
        elif others and not t.scope:
            f.global_qualify = True     # `::Name`: the other functions of that name are not what the call finds
        sites, why = find_sites(f, sources, spans)
        if sites is None:
            f.reason = why
            continue
        f.sites = sites
        if not sites:
            f.kind = 'unused'
    return fwds, classes


_VECTORS = {}


def vector_sources():
    """src/ files the ei_vectors executable links (CMakeLists.txt) -> True, or False when parts of the file are left
    out of it (EI_VECTORS_REPLAY). A call there must keep reaching a target that ei_vectors has."""
    if not _VECTORS:
        with open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8') as fh:
            text = fh.read()
        m = re.search(r'add_executable\(ei_vectors(.*?)\)', text, re.S)
        for path in re.findall(r'\bsrc/\S+\.cpp', m.group(1) if m else ''):
            full = os.path.join(ROOT, path)
            text = ''
            if os.path.exists(full):
                with open(full, encoding='utf-8') as fh:
                    text = fh.read()
            _VECTORS[path] = bool(text) and 'EI_VECTORS_REPLAY' not in text
        _VECTORS.setdefault('', False)
    return _VECTORS


_REPLAY = []


def replay_defines(t):
    """True when ei_vectors defines the target itself (tests/vectors/replay_*.cpp: a replay version, or a stand-in
    that stops the replay naming it, replay_unlinked.cpp), written with its qualified name."""
    if not _REPLAY:
        for path in sorted(glob.glob(os.path.join(ROOT, 'tests', 'vectors', 'replay_*.cpp'))):
            with open(path, encoding='utf-8') as fh:
                _REPLAY.append(Source(path, fh.read()).code)
        _REPLAY.append('')
    rx = re.compile(r'(?<![\w:])' + re.escape(t.qname) + r'\s*\(')
    return any(rx.search(code) for code in _REPLAY)


def plan(fwds, classes):
    """Edits per file for every convertible forwarder in scope; sets f.reason for the ones that fail."""
    edits = defaultdict(list)
    includes = defaultdict(set)
    done = []
    HOISTED.clear()
    for f in fwds:
        if not f.in_scope or f.reason or not f.kind:
            continue
        mine, incs, why = [], set(), ''
        if f.kind == 'unwrap' and f.unwrap['targs'] and match(f.sig, f.target) == 'rename' and f.sites:
            # A member call whose element policy no alias names (`x.Clear<stlp::PairElement<...>>()`) reads worse
            # than the wrapper called through its alias (`trees::Objects::Clear(&x)`): the call keeps its arguments.
            def long_policy(site):
                return any('<' in spelled(normalize_type(x), site.src, site.start) for x in f.unwrap['targs'])
            if any(long_policy(site) for site in f.sites) and \
                    all('<' not in spelled_target(f.target, site.src, site.start) for site in f.sites):
                f.kind = 'rename'
        for site in f.sites:
            e, why = rewrite_site(f, site)
            if e is None:
                where = f'{site.src.path}:{site.src.line_of(site.name_start)}'
                why = why[:-1] + f'; {where})' if why.endswith(')') else f'{why} ({where})'
                break
            mine.append((site.src, e))
            # The classes a template target or an unwrapped call names come from their headers.
            if f.kind == 'unwrap':
                names = ' '.join(f.unwrap['targs'] or ())
            else:
                names = f.target.scope if '<' in f.target.scope else ''
            for name in re.findall(r'[A-Za-z_]\w*', normalize_type(names)):
                hdr = [b[0] for b in classes.bodies.get(name, ()) if b[0].path.endswith('.h')]
                if len({h.path for h in hdr}) != 1 or hdr[0] is site.src or sees(site.src, hdr[0]) or \
                        not site.src.path.endswith('.cpp'):
                    continue
                inc = include_line(site.src.path, hdr[0].path)
                if inc is None:
                    why = f'cannot include {hdr[0].path}'
                    break
                incs.add((site.src, inc))
            if why:
                break
            if f.kind == 'unwrap':
                continue        # the container's own member: declared with the container the call uses
            vectors = vector_sources()
            if site.src.path in vectors and site.src.path != f.target_src_path and \
                    vectors.get(f.target_src_path) is not True and not replay_defines(f.target):
                why = f'ei_vectors links the caller without the target ({site.src.path})'
                break
            if site.src.path == f.target_src_path:
                if not f.target.member and not declared_before(site.src, f.target.simple, site.start):
                    hdr = free_header(f.target, f.target_src_path)
                    if hdr is not None and hdr.path.endswith('.h') and includes_header(site.src, hdr.path):
                        continue        # the file's own header declares the target
                    place = redeclare_place(f, site)
                    if place is not None:
                        f.redeclare = place
                        continue
                    why = f'target declared after the call ({site.src.path}:{site.src.line_of(site.start)})'
                    break
                continue
            t = f.target
            if t.member or t.static:
                hdr = classes.header_of(split_qualified(t.scope)[-1].split('<')[0], t.simple)
            else:
                hdr = None
            if hdr is None and not t.member:
                hdr = free_header(t, f.target_src_path)
            if hdr is None:
                place = redeclare_place(f, site)
                if place is not None:
                    f.redeclare = place
                    continue
                why = f'no header declares the target ({t.qname}; {site.src.path})'
                break
            if hdr.path.endswith('.cpp') and hdr.path != site.src.path:
                place = redeclare_place(f, site)
                if place is not None:
                    f.redeclare = place
                    continue
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
        if f.kind in ('cast', 'loose'):
            for site in f.sites:
                MARKERS[site.src].append((site.start, f.address, getattr(site, 'uncast', False)))
        for src, inc in incs:
            includes[src].add(inc)
            INCLUDED_BY[(src.path, inc.strip())].add(f.address)
        if f.redeclare and f.redeclare[0] == 'here':
            edits[f.src].append(('redeclare', f.start, f.end, f.address, forward_declaration(f, f.redeclare[1])))
        elif f.redeclare:
            # The target is in an enclosing namespace of the forwarder's: declared before the inner one opens.
            text = forward_declaration(f, f.redeclare[1], True)
            if (f.src.path, f.redeclare[0], text) not in HOISTED:      # two forwarders of one function: once
                HOISTED.add((f.src.path, f.redeclare[0], text))
                edits[f.src].append((f.redeclare[0], f.redeclare[0], [text]))
            edits[f.src].append(('delete', f.start, f.end, f.address))
        else:
            edits[f.src].append(('delete', f.start, f.end, f.address))
        for src, a, b in f.extra:
            edits[src].append(('delete', a, b, f.address))
        done.append(f)
    return edits, includes, done


_FREE_DECLS = {}
HOISTED = set()      # (path, offset, text) of the declarations written before a namespace


def declared_before(src, name, offset):
    """Whether `src` declares free function `name` (a type before the name) before `offset`."""
    for m in re.finditer(r'\b' + re.escape(name) + r'\s*\(', src.bare[:offset]):
        tok, _ = prev_token(src.bare, m.start())
        if tok and (tok[0].isalnum() or tok[0] == '_' or tok in ('*', '&')) and tok not in KEYWORDS_BEFORE_EXPR:
            return True
    return False


_WORDS = {}
_HEADERS = {}


def includes_header(src, header_path):
    """Whether `src` has an #include of a file with the header's name."""
    base = os.path.basename(header_path)
    return any(os.path.basename(m) == base for m in re.findall(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', src.raw, re.M))


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


def redeclare_place(f, site):
    """Where the forwarder's declaration becomes a plain declaration of the target (a free function the callers
    cannot see: defined later in the file, or in a .cpp with no header): ('here', namespaces to open) at the
    forwarder's place, or (offset of the line opening the namespace of the forwarder that the target is not in,
    namespaces to open there). None when the callers could not see it, or the forwarder says less than the target."""
    t = f.target
    if f.kind != 'rename' or t.member or t.static or f.in_class or f.sig.ret != t.ret or '<' in t.raw_name:
        return None
    if site.src is f.src:
        if f.start > site.start:
            return None
    elif not f.src.path.endswith('.h'):
        return None
    inner = f.src.namespaces_at(f.start)
    outer = split_qualified(t.raw_name)[:-1]
    k = 0
    while k < len(inner) and k < len(outer) and inner[k] == outer[k]:
        k += 1
    if k == len(inner):
        return 'here', outer[k:]
    opens = sorted(o for (o, c, kind, n) in f.src.scopes if o < f.start < c and kind == 'ns')
    return f.src.raw.rfind('\n', 0, opens[k]) + 1, outer[k:]


def forward_declaration(f, namespaces=(), hoisted=False):
    """The forwarder's declaration as a plain declaration of the target, in `namespaces` when it is in them."""
    raw = f.src.raw
    old = raw[f.start:f.end]
    text = re.sub(r'__declspec\s*\(\s*naked\s*\)\s*', '', old)
    text = re.sub(r'^(?:(?:inline|static)\s+)+', '', ORIGINAL.sub('', text).rstrip())
    text = re.sub(r'\b' + re.escape(f.simple) + r'(?=\s*\()', f.target.simple, text, count=1)
    indent = raw[raw.rfind('\n', 0, f.start) + 1:f.start]
    indent = indent if not indent.strip() and not hoisted else ''
    # Continuation lines aligned to the parameter list follow it.
    old_paren = re.search(re.escape(f.simple) + r'\s*\(', old).end() - 1
    new_paren = re.search(re.escape(f.target.simple) + r'\s*\(', text).end() - 1
    text = reindent(text, column(raw, f.start) + old_paren + 1,
                    len(indent) + new_paren - old_paren - column(raw, f.start))
    if namespaces:
        names = '::'.join(n for n in namespaces if n != "`anonymous namespace'")
        head = 'namespace ' + names + ' {' if names and len(names) == len('::'.join(namespaces)) else 'namespace {'
        return f'{indent}{head}\n{indent}{text};\n{indent}}}\n'
    return indent + text + ';\n'


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
    """Start of the // comment lines directly above the line starting at `ls` (a section heading,
    `// ---`, belongs to what follows it all and stays)."""
    i = ls
    while i > 0:
        p = raw.rfind('\n', 0, i - 1) + 1
        line = raw[p:i].strip()
        if line.startswith('//') and not line.startswith('// ---'):
            i = p
        else:
            break
    return i


_ABOUT_FORWARDER = re.compile(r"through (?:its|the original'?s?|their|the game'?s?) (?:game |original )?address|"
                              r"forwarder|verify wrapper", re.I)


def deletion_span(raw, a, b, deleted, redeclared=False):
    """The text to delete for a declaration at [a, b): whole lines, the comment directly above when it is
    about deleted forwarders only, and one of two blank lines that would meet."""
    a, b = line_span(raw, a, b)
    c = comment_above(raw, a)
    if c < a:
        text = raw[c:a]
        addrs = {int(x, 16) for x in re.findall(r'\b([0-9A-Fa-f]{8})\b', text)}
        if redeclared and not _ABOUT_FORWARDER.search(text):
            pass                # the declaration of the target keeps what the comment says about it
        elif (addrs and addrs <= deleted) or (not addrs and _ABOUT_FORWARDER.search(text)):
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


INCLUDED_BY = defaultdict(set)   # (file, #include line) -> addresses of the forwarders whose calls need it
MARKERS = defaultdict(list)   # src -> (offset, forwarder address, written without a cast) of void* calls
LINE_MAP = {}                 # (path, line after --apply) -> ('path:line' before, address, uncast)


def map_lines(src, ops):
    """Where the calls in MARKERS land after the edits: their lines before and after, in LINE_MAP."""
    raw = src.raw
    top, pos, seen = [], 0, set()
    for o in sorted(ops, key=lambda o: (o[0], -o[1])):
        # render() writes every insertion at a place at once, so one of them stands for all.
        if (o[0] >= pos or o[0] == o[1] == pos) and (o[0], o[1]) not in seen:
            top.append(o)
            seen.add((o[0], o[1]))
            pos = max(pos, o[1])
    for m, address, uncast in MARKERS.get(src, ()):
        delta = 0
        for a, b, _ in top:
            if b <= m and not (a == b == m):
                delta += render(raw, a, b, ops).count('\n') - raw.count('\n', a, b)
        old = raw.count('\n', 0, m) + 1
        LINE_MAP[(src.path, old + delta)] = (f'{src.path}:{old}', address, uncast)


def rewritten(edits, includes, deleted_by_file):
    """path -> new text of every file the edits change."""
    changed = {}
    for src in set(edits) | set(includes):
        raw = src.raw
        deleted = deleted_by_file.get(src.path, set())
        dels, ops = [], []
        for e in edits.get(src, ()):
            if e[0] == 'delete':
                dels.append(deletion_span(raw, e[1], e[2], deleted) + ([],))
            elif e[0] == 'redeclare':
                dels.append(deletion_span(raw, e[1], e[2], deleted, True) + ([e[4]],))
            else:
                ops.append((e[0], e[1], e[2]))
        dels.sort()
        merged = []
        for a, b, pieces in dels:
            if merged and a <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], b)
                merged[-1][2] += pieces
            else:
                merged.append([a, b, list(pieces)])
        for span in merged:
            if not span[2] and span[0] >= 2 and raw[span[0] - 2:span[0]] == '\n\n' and raw[span[1]:span[1] + 1] == '\n':
                span[1] += 1
        ops += [(a, b, pieces) for a, b, pieces in merged]
        for inc in sorted(includes.get(src, ())):
            e = add_include(src, inc)
            if e:
                ops.append((e[0], e[1], [e[2]]))
        new = render(raw, 0, len(raw), ops)
        map_lines(src, ops)
        # A namespace whose forwarders all went goes too.
        empty = re.compile(r'\n\n?namespace(?: \w+)? \{\n\}(?: *//[^\n]*)?\n')
        if len(empty.findall(new)) > len(empty.findall(raw)):
            new = empty.sub('\n', new)
        changed[src.path] = new
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
    ap.add_argument('--exclude', nargs='*', default=[], help='forwarders declared in these GLOBs are left')
    ap.add_argument('--library', action='store_true', help='also forwarders to EI_REPLACE_STANDALONE code')
    ap.add_argument('--verbose', '-v', action='store_true')
    ap.add_argument('--cast', action='store_true', help='also forwarders taking the object as void*')
    ap.add_argument('--loose', action='store_true', help='also forwarders whose types differ in ways a call site '
                    'can spell (casts written at the calls; see match_loose)')
    ap.add_argument('--cast-sites', help='file of path:line calls that need static_cast<T*> (compile errors)')
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
    if args.cast_sites:
        for line in open(args.cast_sites, encoding='utf-8'):
            m = re.match(r'(.+):(\d+)\s*$', line.strip())
            if m:
                CAST_SITES.add((m.group(1).replace('\\', '/'), int(m.group(2))))
    fwds, classes = analyse(sources, decls, args.library, args.scope, {'cast': args.cast, 'loose': args.loose},
                            args.exclude)
    edits, includes, done = plan(fwds, classes) if (args.apply or args.report) else ({}, {}, [])
    scoped = [f for f in fwds if f.in_scope]
    reasons = Counter('listed in config/direct_calls_skip.csv' if f.reason.startswith('listed: ')
                      else re.sub(r' \(.*', '', f.reason) for f in scoped if f.reason)
    print(f'{len(scoped)} forwarders in scope: {len(done)} convertible '
          f'({sum(1 for f in done if f.kind == "unused")} unused, {sum(len(f.sites) for f in done)} call sites), '
          f'{sum(reasons.values())} left')
    for r, n in reasons.most_common():
        print(f'  {n:5}  {r}')
    if args.verbose:
        for f in scoped:
            if f.reason:
                print(f'{f.address:08X} {f.src.path}:{f.src.line_of(f.start)} {f.name} -> {f.target_name}: {f.reason}')
                if f.reason == 'types differ':
                    print(f'      forwarder: {f.sig.und}\n      target:    {f.target.und}')
                    if getattr(f, 'loose_why', ''):
                        print(f'      loose:     {f.loose_why}')
                    if getattr(f, 'unwrap_why', ''):
                        print(f'      wrapper:   {f.unwrap_why}')
        for f in done:
            print(f'{f.address:08X} {f.src.path}:{f.src.line_of(f.start)} {f.name} -> {f.target_name}: '
                  f'convertible ({f.kind}, {len(f.sites)} call sites)')
    if args.apply:
        deleted = defaultdict(set)
        for f in done:
            deleted[f.src.path].add(f.address)
            for src, _, _ in f.extra:
                deleted[src.path].add(f.address)
        changed = apply(edits, includes, deleted)
        print(f'{len(changed)} files changed')
        out = os.path.join(ROOT, 'build', 'C13', 'includes.txt')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'w', encoding='utf-8') as fh:
            fh.writelines(f'{p} {inc} ' + ' '.join(f'{a:08X}' for a in sorted(addrs)) + '\n'
                          for (p, inc), addrs in sorted(INCLUDED_BY.items()))
        if LINE_MAP:
            out = os.path.join(ROOT, 'build', 'C13', 'uncast_calls.txt')
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, 'w', encoding='utf-8') as fh:
                fh.writelines(f'{p}:{n} {old} {a:08X} {"uncast" if u else "cast"}\n'
                              for (p, n), (old, a, u) in sorted(LINE_MAP.items()))
            uncast = sum(1 for _, _, u in LINE_MAP.values() if u)
            print(f'{uncast} calls through void* written without a cast (every void* call in '
                  f'{os.path.relpath(out, ROOT)}). A compile error at one: add its "before" place to '
                  '--cast-sites; a static_cast between unrelated types: list the forwarder in '
                  'config/direct_calls_skip.csv')
        review(done)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
