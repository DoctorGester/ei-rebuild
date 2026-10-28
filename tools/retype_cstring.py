"""C14a: every mfc::CString in src/game becomes std::string at once, a scripted whole-tree retype (one rule per
shape), then the verify build's diagnostics fixed by c14_strings.py's rules.

Step `retype` (src/game/*.cpp, *.h; comments and literals are left alone):
  bridge overloads      the CString overloads beside std::string ones go (asset_text's FromAssetText /
                        CharacterCount / CharacterBytes, record_members' CopyMember / AssignMember /
                        DefaultMember / ElementOf / IsPlain); a retyped line equal to another line of the
                        file (an explicit instantiation for both types) goes
  storage left unset    `mfc::CString x(mfc::CString::kUnset)` / `{mfc::CString::kUnset}` -> `std::string x`
                        (the callee assigns now, the string is built empty)
  out parameters        in a function taking `mfc::CString* out` (or `void* out` it builds a CString in):
                        `std::construct_at(out, a)` / `new (out) mfc::CString(a)` -> `*out = a`
                        (`out->clear()` without an argument; `return std::construct_at(out, a);` ->
                        `*out = a; return out;`); a `void*` one becomes `std::string*` in every declaration
  raw storage           `alignas(..) unsigned char s[sizeof(mfc::CString)];` -> `std::string s;`, the calls
                        given `s` get `&s`, and `std::destroy_at(p);` of the pointer they returned goes
  by-value parameters   `mfc::CString x` -> `const std::string& x` (`std::string x` where a definition
                        assigns or changes it: =, +=, a mutating CString member, its address)
  operator+             `mfc::operator+(a, b)` -> `(a + b)`
  the type              every other `mfc::CString` -> `std::string` (members, locals, returns, pointers,
                        references, template arguments, StaticObject globals, casts)
  forward declarations  `class CString;` in namespace mfc goes (an emptied `namespace mfc {}` too)
Step `fix`: the verify build (ninja -k 0 with /diagnostics:column) and c14_strings.py's diagnostic rules
(GetLength -> size(), IsEmpty -> empty(), Format -> str::Format, Mid / Left / Find / trims -> str::, GetBuffer /
ReleaseBuffer -> str::Buffer / CutAtNull, a std::string where a const char* is taken -> c_str(), a CString of
src/lib given where a std::string is taken -> GetString(), ...), repeated until a build adds no edit. What no rule
takes is listed in build/retype_cstring/manual.txt for hand fixing.

usage:
  python tools/retype_cstring.py retype            step 1 over src/game
  python tools/retype_cstring.py fix [--iterations N]
  python tools/retype_cstring.py count             mfc::CString code lines in src/game, src/lib, tests
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c14_strings as c14  # noqa: E402

ROOT = c14.ROOT
GAME = os.path.join(ROOT, 'src', 'game')
STATE = os.path.join(ROOT, 'build', 'retype_cstring')
VCVARS = os.environ.get('EI_VSROOT', r'E:\bin\vs\buildtools') + r'\VC\Auxiliary\Build\vcvars32.bat'

CS = r'mfc::CString'
STD = 'std::string'

# The bridge overloads: CString twins of std::string functions in the same file (whole declarations).
REDUNDANT = {
    'asset_text.h': [
        r'^void FromAssetText\(mfc::CString\* text[^;]*;\n',
        r'^int CharacterCount\(const mfc::CString& text\);\n',
        r'^int CharacterBytes\(const mfc::CString& text, int characters\);\n',
    ],
    'asset_text.cpp': [
        r'^void FromAssetText\(mfc::CString\* text, unsigned codepage\) \{\n(?:.*\n)*?\}\n\n',
        r'^int CharacterCount\(const mfc::CString& text\) \{[^\n]*\}\n\n',
        r'^int CharacterBytes\(const mfc::CString& text, int characters\) \{[^\n]*\}\n\n',
    ],
    'record_members.h': [
        r'^template <>\nstruct ElementOf<mfc::CString> \{\n[^\n]*\n\};\n',
        r'^(?:inline void (?:CopyMember|AssignMember|DefaultMember)\(mfc::CString& [^\n]*\n){3}\n',
        r'^template <>\nstruct IsPlain<mfc::CString> : std::false_type \{\};\n',
    ],
}

MUTATORS = ('MakeLower', 'TrimLeft', 'TrimRight', 'Remove', 'Format', 'FormatV', 'GetBuffer', 'GetBufferSetLength',
            'ReleaseBuffer', 'Empty', 'AssignChar', 'SetAt', 'append', 'clear', 'resize', 'erase', 'insert')


def game_files():
    return [os.path.join(GAME, n) for n in sorted(os.listdir(GAME)) if n.endswith(('.cpp', '.h'))]


class Edits:
    def __init__(self, text):
        self.text = text
        self.m = c14.mask(text)
        self.list = []

    def add(self, a, b, rep):
        self.list.append((a, b, rep))

    def apply(self):
        text, _ = c14.apply_edits(self.text, self.list)
        return text


def code_braces(m):
    """[(open, close)] of the function bodies (the outermost 'code' braces)."""
    kinds = c14.brace_kinds(m)
    out = []
    for o, k in sorted(kinds.items()):
        if k != 'code':
            continue
        if out and out[-1][0] < o < out[-1][1]:
            continue
        out.append((o, c14.match_forward(m, o)))
    return out


def head_params(m, body_open):
    """(name, '(' offset, ')' offset) of the function whose body opens at `body_open`, or None (a constructor
    with an initialiser list gives its last initialiser, which takes no CString parameter)."""
    j = c14.skip_space_back(m, body_open - 1)
    while True:
        w = re.search(r'(?:const|noexcept|override|final)\s*$', m[max(0, j - 40):j + 1])
        if not w:
            break
        j = c14.skip_space_back(m, max(0, j - 40) + w.start() - 1)
    if j < 0 or m[j] != ')':
        return None
    o = c14.match_backward(m, j)
    if o < 0:
        return None
    name = re.search(r'(~?\w+)\s*$', m[max(0, o - 200):o])
    if not name or name.group(1) in ('if', 'while', 'for', 'switch', 'catch', 'return'):
        return None
    return name.group(1), o, j


def split_params(m, o, c):
    """[(a, b)] of the parameters between '(' at o and ')' at c."""
    spans = []
    i = o + 1
    while i < c:
        a, b = c14.argument_span(m, c14.skip_space(m, i))
        if b > a:
            spans.append((a, b))
        j = b
        while j < c and m[j] != ',':
            j += 1
        i = j + 1
    return spans


def mutated(body, name):
    n = re.escape(name)
    if re.search(r'(?<![\w.>:])' + n + r'\s*(?:=(?!=)|\+=|\[)', body):
        return True
    if re.search(r'(?<![&\w])&\s*' + n + r'\b', body):
        return True
    if re.search(r'(?<![\w.>:])' + n + r'\s*\.\s*(?:' + '|'.join(MUTATORS) + r')\b', body):
        return True
    return False


def scan_definitions(path, text):
    """Per definition in `text`: (fn, [(param span, param text)], body span)."""
    m = c14.mask(text)
    out = []
    for (bo, bc) in code_braces(m):
        h = head_params(m, bo)
        if not h:
            continue
        fn, o, c = h
        out.append((fn, o, c, split_params(m, o, c), (bo, bc)))
    return m, out


def declarations(m, fn):
    """[(o, c)] of the parameter lists of `fn`'s declarations and definitions (outside function bodies)."""
    out = []
    pat = re.compile(r'(?<![\w])' + re.escape(fn) + r'\s*\(')
    hits = list(pat.finditer(m))
    ctx = c14.context_kinds(m, [x.start() for x in hits])
    for x in hits:
        if ctx.get(x.start()) == 'code':
            continue
        o = x.end() - 1
        c = c14.match_forward(m, o)
        if c > 0:
            out.append((o, c))
    return out


VOID_PARAM = re.compile(r'^\s*void\s*\*\s*(\w+)\s*$')
CS_PTR_PARAM = re.compile(r'^\s*mfc::CString\s*\*\s*(\w+)\s*$')
CS_VALUE_PARAM = re.compile(r'^\s*mfc::CString(?:\s+(\w+))?\s*$')


def plan(texts):
    """The decisions that span files: {(fn, index): new param type} for void* out parameters and by-value
    CString parameters."""
    decide = {}
    for path, text in texts.items():
        m, defs = scan_definitions(path, text)
        for (fn, o, c, params, (bo, bc)) in defs:
            body = m[bo:bc]
            for i, (a, b) in enumerate(params):
                p = m[a:b]
                v = VOID_PARAM.match(p)
                if v and re.search(r'new\s*\(\s*' + v.group(1) + r'\s*\)\s*mfc::CString\b', body):
                    decide[(fn, i)] = 'std::string* ' + v.group(1)
                    continue
                cv = CS_VALUE_PARAM.match(p)
                if cv:
                    name = cv.group(1)
                    if name and mutated(body, name):
                        decide[(fn, i)] = 'value'
                    else:
                        decide.setdefault((fn, i), 'cref')
    return decide


def construct_rules(e, bo, bc, name):
    """`std::construct_at(name, a)` / `new (name) mfc::CString(a)` in the body -> assignments."""
    m, t = e.m, e.text
    n = re.escape(name)
    for x in re.finditer(r'std::construct_at\s*\(\s*' + n + r'\s*(,|\))', m[bo:bc]):
        s = bo + x.start()
        o = m.index('(', s)
        c = c14.match_forward(m, o)
        args = t[m.index(name, o) + len(name):c].strip()
        args = args[1:].strip() if args.startswith(',') else args
        assign(e, s, c + 1, name, args)
    for x in re.finditer(r'(?<![\w:])new\s*\(\s*' + n + r'\s*\)\s*mfc::CString\b\s*(\()?', m[bo:bc]):
        s = bo + x.start()
        if x.group(1):
            o = bo + x.end() - 1
            c = c14.match_forward(m, o)
            args = t[o + 1:c].strip()
            end = c + 1
        else:
            args, end = '', bo + x.end()
        assign(e, s, end, name, args)


def assign(e, s, end, name, args):
    m = e.m
    before = c14.skip_space_back(m, s - 1)
    after = c14.skip_space(m, end)
    rhs = '*%s = %s' % (name, args) if args else '%s->clear()' % name
    if m[before - 5:before + 1] == 'return' and m[after] == ';':
        e.add(before - 5, after + 1, '%s; return %s;' % (rhs, name))
    elif m[before] == '*' and m[before - 1] in '=(&, ':
        # `X& r = *new (out) mfc::CString(a);`
        e.add(before, end, '(%s)' % rhs)
    else:
        e.add(s, end, rhs if not args or m[after] in ';,)' else '(%s)' % rhs)


RAW = re.compile(r'alignas\s*\([^()]*\)\s*(?:unsigned\s+char|char|std::uint8_t)\s+(\w+)\s*\[\s*sizeof\s*\(\s*'
                 r'mfc::CString\s*\)\s*\]\s*;')


def raw_storage(e):
    m, t = e.m, e.text
    for x in RAW.finditer(m):
        s = x.group(1)
        # the enclosing block
        depth, j = 0, x.end()
        while j < len(m):
            if m[j] in '{':
                depth += 1
            elif m[j] == '}':
                if depth == 0:
                    break
                depth -= 1
            j += 1
        block_end = j
        if 'new (' + s + ')' in m[x.end():block_end].replace('new(', 'new ('):
            continue   # built by hand here: a hand fix
        e.add(x.start(), x.end(), 'std::string %s;' % s)
        pointers = []
        for y in re.finditer(r'(?<=[(,])\s*' + re.escape(s) + r'\s*(?=[,)])', m[x.end():block_end]):
            a = x.end() + y.start()
            e.add(a, x.end() + y.end(), ' &' + s if t[a] == ' ' else '&' + s)
            line_start = m.rfind('\n', 0, a) + 1
            p = re.search(r'(\w+)\s*=\s*[^=;]*$', m[line_start:a])
            if p:
                pointers.append(p.group(1))
        for p in pointers:
            for z in re.finditer(r'[ \t]*std::destroy_at\s*\(\s*' + re.escape(p) + r'\s*\)\s*;[ \t]*\n?',
                                 m[x.end():block_end]):
                e.add(x.end() + z.start(), x.end() + z.end(), '')


def plus(e, start, o, c, spans):
    """`mfc::operator+(a, b)` at `start` ('(' at o, ')' at c) -> `a + b`, in brackets unless it is a whole
    argument, a whole right-hand side or a return value."""
    m = e.m
    (a1, b1), (a2, b2) = spans
    before = m[c14.skip_space_back(m, start - 1)]
    after = m[c14.skip_space(m, c + 1)]
    bare = before in '(,=' and after in '),;'   # inside brackets, an argument or a whole right-hand side
    bare = bare or (m[:start].rstrip().endswith('return') and after == ';')
    e.add(start, o + 1, '' if bare else '(')
    e.add(b1, a2, ' + ')
    if bare:
        e.add(c, c + 1, '')


def retype_text(path, text, decide):
    base = os.path.basename(path)
    for pat in REDUNDANT.get(base, []):
        text = re.sub(pat, '', text, flags=re.M)
    e = Edits(text)
    m = e.m
    # storage left unset
    for x in re.finditer(r'\(\s*mfc::CString::kUnset\s*\)|\{\s*mfc::CString::kUnset\s*\}', m):
        e.add(x.start(), x.end(), '')
    # out parameters, by-value parameters (definitions)
    _, defs = scan_definitions(path, text)
    for (fn, o, c, params, (bo, bc)) in defs:
        for i, (a, b) in enumerate(params):
            p = m[a:b]
            pp = CS_PTR_PARAM.match(p)
            if pp:
                construct_rules(e, bo, bc, pp.group(1))
            v = VOID_PARAM.match(p)
            if v and decide.get((fn, i), '').startswith('std::string*'):
                construct_rules(e, bo, bc, v.group(1))
    # parameter types in every declaration of the functions decided
    names = {fn for (fn, i) in decide}
    for fn in names:
        if fn not in m:
            continue
        for (o, c) in declarations(m, fn):
            for i, (a, b) in enumerate(split_params(m, o, c)):
                d = decide.get((fn, i))
                p = m[a:b]
                if not d:
                    continue
                if d.startswith('std::string*') and VOID_PARAM.match(p):
                    e.add(a, b, 'std::string* ' + VOID_PARAM.match(p).group(1))
                elif CS_VALUE_PARAM.match(p):
                    name = CS_VALUE_PARAM.match(p).group(1)
                    ty = 'std::string' if d == 'value' else 'const std::string&'
                    e.add(a, b, ty + (' ' + name if name else ''))
    raw_storage(e)
    # mfc::operator+(a, b) -> (a + b)
    for x in re.finditer(r'mfc::operator\+\s*\(', m):
        o = x.end() - 1
        c = c14.match_forward(m, o)
        spans = split_params(m, o, c)
        if len(spans) == 2:
            plus(e, x.start(), o, c, spans)
    text = e.apply()
    # nested operator+ calls: once more until none is left
    while re.search(r'mfc::operator\+\s*\(', c14.mask(text)):
        e = Edits(text)
        x = re.search(r'mfc::operator\+\s*\(', e.m)
        o = x.end() - 1
        c = c14.match_forward(e.m, o)
        spans = split_params(e.m, o, c)
        if len(spans) != 2:
            break
        plus(e, x.start(), o, c, spans)
        text = e.apply()
    # the type
    e = Edits(text)
    for x in re.finditer(r'(?<![\w:])mfc::CString(?![\w:])', e.m):
        e.add(x.start(), x.end(), STD)
    text = e.apply()
    # forward declarations
    text = re.sub(r'^namespace mfc \{ class CString; \}\n', '', text, flags=re.M)
    text = re.sub(r'^(namespace mfc \{\n(?:(?!\}).*\n)*?)class CString;\n', r'\1', text, flags=re.M)
    text = re.sub(r'^namespace mfc \{\n\}(?: // namespace mfc)?\n\n?', '', text, flags=re.M)
    # a retyped line equal to another line of the file (an explicit instantiation for both types)
    lines = text.split('\n')
    seen = {}
    keep = []
    for line in lines:
        s = line.strip()
        if s.startswith('template ') and s.endswith(';') and s in seen and s not in ('',):
            continue
        seen[s] = True
        keep.append(line)
    text = '\n'.join(keep)
    return text


def retype(args):
    texts = {p: c14.read(p) for p in game_files()}
    decide = plan(texts)
    changed = 0
    for p, text in texts.items():
        if 'CString' not in text:
            continue
        new = retype_text(p, text, decide)
        if new != text:
            new = c14.ensure_include(new)
            c14.write(p, new)
            changed += 1
    print('%d files retyped; %d parameter decisions' % (changed, len(decide)))
    for k, v in sorted(decide.items()):
        if v != 'cref':
            print('  %s #%d: %s' % (k[0], k[1], v))


def build():
    env = dict(os.environ)
    env['CL'] = '/diagnostics:column'
    cmd = 'call "%s" >nul && ninja -C "%s" -k 0' % (VCVARS, c14.BUILD)
    r = subprocess.run('cmd /s /c "%s"' % cmd, capture_output=True, text=True, errors='replace', env=env)
    return r.returncode, r.stdout + r.stderr


def fix(args):
    # a template's element (`T`, a container of std::string) where a const char* is taken: c_str() too
    c14.FROM_STD = re.compile(r"from '(?:" + c14.STD_TYPE_RE + r"|T)'")
    os.makedirs(STATE, exist_ok=True)
    files = c14.fixable_files()
    for it in range(args.iterations):
        rc, out = build()
        with open(os.path.join(STATE, 'build.log'), 'w', encoding='utf-8') as f:
            f.write(out)
        diags = [d for d in c14.parse_diagnostics(out) if d['kind'] != 'note']
        sources = {p: c14.Source(p, c14.read(p)) for p in files}
        fixer = c14.Fixer(sources, {}, {})
        for d in diags:
            fixer.fix(d)
        count = 0
        for p, edits in fixer.edits.items():
            text, n = c14.apply_edits(c14.read(p), edits)
            count += n
            c14.write(p, c14.ensure_include(text))
        with open(os.path.join(STATE, 'manual.txt'), 'w', encoding='utf-8') as f:
            f.write(''.join(x + '\n' for x in fixer.manual))
        print('build %d: exit %d, %d diagnostics, %d edits, %d manual' % (it, rc, len(diags), count,
                                                                           len(fixer.manual)), flush=True)
        if (rc == 0 and not diags) or count == 0:
            return 0 if rc == 0 else 1
    return 1


def count(args):
    for d in ('src/game', 'src/lib', 'tests/vectors', 'tests/verify'):
        lines = files = 0
        for base, _, names in os.walk(os.path.join(ROOT, d)):
            for n in names:
                if not n.endswith(('.cpp', '.h')):
                    continue
                m = c14.mask(c14.read(os.path.join(base, n)))
                k = len([l for l in m.split('\n') if re.search(r'\bCString\b', l)])
                lines += k
                files += k > 0
        print('%-14s %4d CString code lines in %d files' % (d, lines, files))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('command', choices=['retype', 'fix', 'count'])
    ap.add_argument('--iterations', type=int, default=12)
    args = ap.parse_args()
    return {'retype': retype, 'fix': fix, 'count': count}[args.command](args)


if __name__ == '__main__':
    sys.exit(main() or 0)
