"""C14a: the game's strings from MFC's CString to std::string, a scripted rewrite (one rule per call shape).

Step 1, retype: the chosen declarations of `mfc::CString` become `std::string`. Scope `locals`: variables declared
in function bodies of src/game/*.cpp (not the `kUnset` result slots, references, pointers or range-for variables).
Scope `params`: the `const mfc::CString&` parameters of the functions defined in src/game, in the definition and every
declaration of the same name and position (not virtual functions, not an overload set on the string type such as
CopyMember's or the Ops cast views); a parameter no rule carries stays a CString in all of them.
Step 2, fix: the verify build's diagnostics (MSVC with /diagnostics:column) name every place std::string does not
take what CString took; one rule per shape rewrites it (str.h has CString's answers where std::string has none):
  x.GetLength()          -> x.size()                 x.IsEmpty()  -> x.empty()
  x.Empty()              -> x.clear()                x.GetString() -> x.c_str()
  x.AssignChar(c)        -> x.assign(1, c)
  x.Format(...);         -> x = str::Format(...);    (FormatV the same)
  x.Find/ReverseFind/FindOneOf/Mid/Left/Right(...)   -> str::Find(x, ...) ...   (int positions, -1 when absent)
  x.MakeLower/Remove/TrimRight/TrimLeft(...)         -> str::MakeLower(x, ...) ...
  x.GetBuffer(n) -> str::Buffer(x, n); x.ReleaseBuffer(-1) -> str::CutAtNull(x); x.ReleaseBuffer(n) -> x.resize(n);
  x.GetBufferSetLength(n) -> str::SetLength(x, n)
  a std::string where a const char* or a CString value was taken (an argument, a variadic argument, an assignment
  to a CString, an initialisation)   -> e.c_str()   ((e).c_str() for a compound expression)
  a signed / unsigned comparison with a size()        -> static_cast<int>(x.size())
  a CString given to a retyped parameter               -> e.GetString()
  `std::string name = x.GetString();` with x retyped   -> `std::string name = x;`
The fixes reach src/game, src/lib, tests/verify/image and tests/vectors (callers); the build is ninja's whole verify
build (the harness and ei_vectors.exe too).
A variable no rule carries (its address handed to a CString*, bound to a CString&, a method with no rule, a
pointer kept from a temporary) stays a CString: the round starts again from the original files without it.

usage (from a VS x86 prompt; build/verify configured):
  python tools/c14_strings.py run [--scope locals|params] [--files src/game/a.cpp ...] [--rounds N]
  python tools/c14_strings.py list [--files ...]      the declarations step 1 would retype
  python tools/c14_strings.py lifetimes               placement news and destructor calls of CStrings typed by their
                                                      target (std::construct_at / std::destroy_at), before members
State (the excluded variables, the last round's diagnostics) in build/c14_strings/.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
STATE = os.path.join(ROOT, 'build', 'c14_strings')

STD = 'std::string'
STD_TYPE_RE = r"(?:const )?(?:std::string|std::basic_string<char,std::char_traits<char>,std::allocator<char>>)"


# ---- source text ------------------------------------------------------------------------------------------------

def mask(text):
    """`text` with comments and the insides of string and character literals replaced by spaces (newlines and the
    quote characters kept), so brackets and identifiers in them do not count."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if text[k] != '\n':
                    out[k] = ' '
            i = j
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = re.match(r'R"([^(]*)\(', text[i:])
            end = text.find(')' + m.group(1) + '"', i) if m else -1
            if end < 0:
                i += 1
                continue
            end += len(m.group(1)) + 2
            for k in range(i + 2, end - 1):
                if text[k] != '\n':
                    out[k] = ' '
            i = end
        elif c in '"\'':
            if c == "'" and i > 0 and (text[i - 1].isalnum()) and text[i - 1] not in 'uUL8':
                i += 1   # a digit separator (1'000)
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                out[k] = ' '
            i = j + 1
        else:
            i += 1
    return ''.join(out)


class Source:
    def __init__(self, path, text):
        self.path = path
        self.text = text
        self.masked = mask(text)
        self.line_starts = [0]
        for m in re.finditer('\n', text):
            self.line_starts.append(m.end())

    def offset(self, line, col):
        return self.line_starts[line - 1] + col - 1

    def line_of(self, offset):
        lo, hi = 0, len(self.line_starts)
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if self.line_starts[mid] <= offset:
                lo = mid
            else:
                hi = mid
        return lo + 1


OPEN, CLOSE = '([{', ')]}'


def match_forward(s, i):
    """The index of the bracket closing the one at s[i]."""
    depth = 0
    for j in range(i, len(s)):
        if s[j] in OPEN:
            depth += 1
        elif s[j] in CLOSE:
            depth -= 1
            if depth == 0:
                return j
    return -1


def match_backward(s, i):
    """The index of the bracket opening the one at s[i]."""
    depth = 0
    for j in range(i, -1, -1):
        if s[j] in CLOSE:
            depth += 1
        elif s[j] in OPEN:
            depth -= 1
            if depth == 0:
                return j
    return -1


def skip_space_back(s, i):
    while i >= 0 and s[i].isspace():
        i -= 1
    return i


def skip_space(s, i):
    while i < len(s) and s[i].isspace():
        i += 1
    return i


IDENT = re.compile(r'[A-Za-z_]\w*')


def postfix_start(s, end):
    """The start of the postfix expression ending just before `end` (identifiers, ::, ., ->, calls, subscripts;
    a leading * or & is not part of it)."""
    i = skip_space_back(s, end - 1)
    while i >= 0:
        if s[i] in ')]':
            i = match_backward(s, i) - 1
            i = skip_space_back(s, i)
            continue
        if s[i].isalnum() or s[i] == '_':
            j = i
            while j >= 0 and (s[j].isalnum() or s[j] == '_'):
                j -= 1
            k = skip_space_back(s, j)
            if k >= 1 and s[k] == ':' and s[k - 1] == ':':
                i = skip_space_back(s, k - 2)
                continue
            if k >= 0 and s[k] == '.':
                i = skip_space_back(s, k - 1)
                continue
            if k >= 1 and s[k] == '>' and s[k - 1] == '-':
                i = skip_space_back(s, k - 2)
                continue
            if k >= 0 and s[k] == '>' and re.search(r'\b(static_cast|reinterpret_cast|const_cast)\s*<[^<>]*>$', s[:k + 1]):
                # a cast's type: the cast is a postfix expression with its parenthesised operand
                pass
            return j + 1
        if s[i] == '>' and i >= 1 and s[i - 1] == '-':
            i = skip_space_back(s, i - 2)
            continue
        return i + 1
    return 0


def postfix_end(s, start):
    """The end of the postfix expression starting at `start`."""
    i = start
    while i < len(s):
        mm = IDENT.match(s, i)
        if mm:
            i = mm.end()
        elif s[i] in '([':
            i = match_forward(s, i) + 1
        else:
            break
        k = skip_space(s, i)
        if k < len(s) and s[k] in '([':
            i = k
            continue
        if s.startswith('::', k) or s.startswith('->', k):
            i = skip_space(s, k + 2)
            continue
        if k < len(s) and s[k] == '.':
            i = skip_space(s, k + 1)
            continue
        break
    return i


def call_args(s, pos):
    """[(a, b)] of the arguments of the call whose name starts at `pos` (the first '(' after it)."""
    k = s.find('(', pos)
    if k < 0:
        return []
    close = match_forward(s, k)
    spans = []
    i = k + 1
    while i < close:
        a, b = argument_span(s, skip_space(s, i))
        spans.append((a, b))
        j = b
        while j < close and s[j] != ',':
            j += 1
        i = j + 1
    return spans


def argument_span(s, start):
    """[start, end) of the argument or initialiser beginning at `start`: up to the comma, closing bracket or ;
    at its own depth."""
    depth = 0
    i = start
    while i < len(s):
        c = s[i]
        if c in OPEN:
            depth += 1
        elif c in CLOSE:
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and c in ',;':
            break
        i += 1
    end = i
    while end > start and s[end - 1].isspace():
        end -= 1
    return start, end


def is_postfix(m):
    """Whether masked expression `m` is one postfix expression (no operator at its own depth)."""
    m = m.strip()
    if not m or m[0] in '*&!-+~':
        return False
    depth = 0
    i = 0
    while i < len(m):
        c = m[i]
        if c in OPEN:
            depth += 1
        elif c in CLOSE:
            depth -= 1
        elif depth == 0:
            if c == '-' and i + 1 < len(m) and m[i + 1] == '>':
                i += 2
                continue
            if c == ':' and i + 1 < len(m) and m[i + 1] == ':':
                i += 2
                continue
            if c in '+-*/%<>=&|^?!~,: ':
                if c == ' ' and False:
                    pass
                if c != ' ':
                    return False
        i += 1
    # a space between two words at the top level (a cast like `const char*`, `new X`) is no postfix expression
    return not re.search(r'\w\s+\w', re.sub(r'\([^()]*\)', '()', m))


def c_str_of(text, masked):
    t = text.strip()
    return t + '.c_str()' if is_postfix(masked) else '(' + t + ').c_str()'


# ---- step 1: declarations ---------------------------------------------------------------------------------------

def brace_kinds(m):
    """{offset of '{': kind} with kind 'code', 'class', 'namespace', 'init'."""
    kinds = {}
    stack = []
    for i, c in enumerate(m):
        if c == '{':
            j = skip_space_back(m, i - 1)
            prev = m[j] if j >= 0 else ';'
            head_start = max(m.rfind(';', 0, i), m.rfind('{', 0, i), m.rfind('}', 0, i)) + 1
            head = m[head_start:i]
            parent = stack[-1][1] if stack else 'namespace'
            word = re.search(r'(\w+)\s*$', m[:i])
            word = word.group(1) if word else ''
            if re.search(r'\bnamespace\b|\bextern\s*"', head):
                kind = 'namespace'
            elif re.search(r'\b(class|struct|union)\b', head) and '(' not in head and '=' not in head:
                kind = 'class'
            elif re.search(r'\benum\b', head):
                kind = 'init'
            elif prev == ')' or word in ('else', 'do', 'try', 'const', 'override', 'noexcept', 'final', 'mutable'):
                kind = 'code'
            elif prev in '=,([{' or word == 'return':
                kind = 'init'
            elif prev in ';}:' or prev == '{':
                kind = 'code' if parent == 'code' else parent
            else:
                kind = 'init'
            stack.append((i, kind))
            kinds[i] = kind
        elif c == '}':
            if stack:
                stack.pop()
    return kinds


DECL = re.compile(r'(?<![\w:])((?:static\s+|const\s+)*)mfc::CString(?![\w:])')


def find_locals(src):
    """[(type_start, type_end, [names])] of the local value declarations of mfc::CString in `src`."""
    m = src.masked
    kinds = brace_kinds(m)
    found = []
    # the code-context of every offset, by one sweep
    code_at = {}
    stack = []
    marks = [x.start() for x in DECL.finditer(m)]
    mi = 0
    for i, c in enumerate(m):
        while mi < len(marks) and marks[mi] == i:
            k = next((k for k in reversed(stack) if k != 'init'), 'namespace')
            code_at[i] = k == 'code'
            mi += 1
        if c == '{':
            stack.append(kinds.get(i, 'init'))
        elif c == '}':
            if stack:
                stack.pop()
    for x in DECL.finditer(m):
        if not code_at.get(x.start()):
            continue
        j = skip_space_back(m, x.start() - 1)
        prev = m[j] if j >= 0 else ';'
        if prev not in ';{}:':
            continue
        if prev == ':' and j >= 1 and m[j - 1] == ':':
            continue
        type_start = x.start() + len(x.group(1))
        type_end = x.end()
        k = skip_space(m, type_end)
        name = IDENT.match(m, k)
        if not name:
            continue   # a temporary, a pointer or reference declaration, a cast
        # the declarators up to the ; at this depth
        end = k
        depth = 0
        while end < len(m):
            c = m[end]
            if c in OPEN:
                depth += 1
            elif c in CLOSE:
                depth -= 1
                if depth < 0:
                    break
            elif c == ';' and depth == 0:
                break
            end += 1
        decl = m[k:end]
        if depth < 0 or 'kUnset' in src.text[k:end]:
            continue
        names = []
        inits = []
        pos = 0
        ok = True
        while pos < len(decl):
            nm = IDENT.match(decl, skip_space(decl, pos))
            if not nm:
                ok = False
                break
            names.append(nm.group(0))
            a, b = argument_span(decl, nm.end())
            rest = decl[nm.end():b].strip()
            if rest and rest[0] not in '(={':
                ok = False   # an array, a function declaration's tail, a bit field, a range-for
                break
            if rest.startswith('(') and match_forward(decl, decl.index('(', nm.end())) != b - 1:
                ok = False
                break
            if rest.startswith('('):
                # `name(expr)`: one argument that is no literal reads as `name = expr` (std::string's
                # constructors take a CString's text only through its conversion, which copy-initialisation
                # then asks for by name: GetString())
                o = k + decl.index('(', nm.end())
                inner = m[o + 1:k + b - 1]
                ia, ib = argument_span(inner, skip_space(inner, 0))
                if inner.strip() and ib == len(inner.rstrip()) and '"' not in inner and "'" not in inner:
                    inits.append((o, k + b, ' = ' + src.text[o + 1:k + b - 1].strip()))
            pos = b + 1
        if not ok or not names:
            continue
        # a variable whose address is taken stays a CString: through a cast, a void* parameter or a raw memory call a
        # std::string* compiles and is read as a CString (`reinterpret_cast<StringOps*>(&text)`, ComposeSkin's void*
        # target); the name is looked for in the rest of the file, more than needed
        rest_of_file = m[end:]
        if any(re.search(r'(?<![&\w)\]])&\s*' + re.escape(n) + r'\b(?!\s*(?:\.|->|\[|\())', rest_of_file) for n in names):
            continue
        found.append((type_start, type_end, names, inits))
    return found


PARAM = re.compile(r'(?<![\w:])const\s+(mfc::CString)\s*&\s*(\w+)?')
VIRTUAL_NAME = re.compile(r'\bvirtual\b[^;{}]*?(~?\w+)\s*\(')
OVERRIDE_NAME = re.compile(r'(~?\w+)\s*\([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*\)\s*(?:const\s*)?(?:noexcept\s*)?'
                           r'(?:override|final)\b')


def context_kinds(m, positions):
    """{position: kind of the innermost brace around it ('code', 'class', 'namespace')}."""
    kinds = brace_kinds(m)
    out = {}
    stack = []
    marks = sorted(set(positions))
    mi = 0
    for i, c in enumerate(m):
        while mi < len(marks) and marks[mi] == i:
            out[i] = next((k for k in reversed(stack) if k != 'init'), 'namespace')
            mi += 1
        if c == '{':
            stack.append(kinds.get(i, 'init'))
        elif c == '}':
            if stack:
                stack.pop()
    return out


def enclosing_open(m, pos):
    """The '(' of the parameter list around `pos`, or -1."""
    depth = 0
    for j in range(pos - 1, -1, -1):
        c = m[j]
        if c in CLOSE:
            depth += 1
        elif c in OPEN:
            if depth == 0:
                return j if c == '(' else -1
            depth -= 1
        elif c in ';{}' and depth == 0:
            return -1
    return -1


def find_params(src):
    """[(type_start, type_end, function name, index, parameter name or None, is definition)] of the
    `const mfc::CString&` parameters declared in `src` (outside function bodies)."""
    m = src.masked
    hits = list(PARAM.finditer(m))
    ctx = context_kinds(m, [x.start() for x in hits])
    out = []
    for x in hits:
        if ctx.get(x.start()) == 'code':
            continue
        o = enclosing_open(m, x.start())
        if o < 0:
            continue
        name = re.search(r'(~?\w+)\s*$', m[:o])
        if not name or name.group(1) in ('if', 'while', 'for', 'switch', 'return', 'sizeof', 'decltype'):
            continue
        index = 0
        depth = 0
        for c in m[o + 1:x.start()]:
            if c in OPEN:
                depth += 1
            elif c in CLOSE:
                depth -= 1
            elif c == ',' and depth == 0:
                index += 1
        close = match_forward(m, o)
        after = re.sub(r'^(?:\s|const\b|noexcept\b|override\b|final\b)*', '', m[close + 1:close + 200])
        definition = after[:1] in ('{', ':') and not after.startswith('::')
        out.append((x.start(1), x.end(1), name.group(1), index, x.group(2), definition))
    return out


def virtual_names(texts):
    """The names declared virtual or overriding anywhere (a parameter's type there must not change alone)."""
    names = set()
    for t in texts:
        m = mask(t)
        names |= {x.group(1) for x in VIRTUAL_NAME.finditer(m)}
        names |= {x.group(1) for x in OVERRIDE_NAME.finditer(m)}
    return names


# ---- step 2: diagnostics ----------------------------------------------------------------------------------------

DIAG = re.compile(r'^(?P<file>(?:[A-Za-z]:)?[^():]+(?:\.(?:cpp|h|inl|hpp)))\((?P<line>\d+)(?:,(?P<col>\d+))?\) ?: '
                  r'(?P<kind>error|warning|note|fatal error)(?: (?P<code>C\d+))?: (?P<msg>.*)$')


def parse_diagnostics(output):
    diags = []
    for line in output.splitlines():
        line = line.rstrip()
        m = DIAG.match(line)
        if not m:
            continue
        d = dict(m.groupdict())
        d['file'] = os.path.normcase(os.path.abspath(os.path.join(BUILD, d['file'])))
        d['line'], d['col'] = int(d['line']), int(d['col'] or 1)   # the code generator's warnings have no column
        if d['kind'] == 'note':
            if diags:
                diags[-1]['notes'].append(d)
            continue
        d['notes'] = []
        diags.append(d)
    # one per place and message (a header's diagnostics repeat per translation unit)
    seen = set()
    out = []
    for d in diags:
        key = (d['file'], d['line'], d['col'], d['code'], d['msg'])
        if key in seen:
            continue
        seen.add(key)
        out.append(d)
    return out


METHOD = re.compile(r"'(\w+)': is not a member of '" + STD_TYPE_RE + "'")
FROM_STD = re.compile(r"from '" + STD_TYPE_RE + r"'")
FROM_STD_PTR = re.compile(r"from '" + STD_TYPE_RE + r" ?\*")
TO_TYPE = re.compile(r"to '([^']+)'")
NONCONST_TO = re.compile(r"from '" + STD_TYPE_RE + r"' to '(?!const )[^']*CString ?&'")
TO_CHAR_PTR = re.compile(r"argument (\d+) from '" + STD_TYPE_RE + r"' to 'const (?:unsigned )?char ?\*'")
NONCONST_REF = re.compile(r"argument (\d+) from '" + STD_TYPE_RE + r"' to '(?!const )[^']*CString ?&'")
TO_STD = re.compile(r"to '" + STD_TYPE_RE + "'")
CALLEE = re.compile(r'[\w:~]+(?:\s*(?:\.|->)\s*[\w:~]+)*\s*(?:<[^<>;()]*>)?\s*\(')
FROM_CSTRING = re.compile(r"from '(?:const )?mfc::CString'")
CSTRING_TO_STD_ARG = re.compile(r"argument (\d+) from '(?:const )?mfc::CString' to '" + STD_TYPE_RE + " ?&'")

STR_CALL = {'Find', 'ReverseFind', 'FindOneOf', 'Mid', 'Left', 'Right', 'MakeLower', 'Remove', 'TrimRight', 'TrimLeft'}


class Fixer:
    def __init__(self, sources, decls):
        self.sources = sources          # normcase path -> Source
        self.decls = decls              # normcase path -> [(line, name)]
        self.edits = {}                 # path -> [(start, end, text)]
        self.unfixable = set()          # (path, line of declaration, name)
        self.manual = []

    def add(self, path, start, end, text):
        self.edits.setdefault(path, []).append((start, end, text))

    def variable(self, path, line, name):
        best = None
        for (dl, dn) in self.decls.get(path, []):
            if dn == name and dl <= line and (best is None or dl > best):
                best = dl
        return (path, best, name) if best else None

    def give_up(self, d, src, offset, why):
        """The first retyped variable in the expression at `offset` (up to the end of its argument) stays a
        CString."""
        m = src.masked
        a, b = argument_span(m, skip_space(m, offset))
        for x in IDENT.finditer(m, a, max(b, a + 1)):
            if m[max(0, x.start() - 1)] in '.>' or m.startswith('::', x.end()):
                continue   # a member or a qualifier
            var = self.variable(d['file'], d['line'], x.group(0))
            if var:
                self.unfixable.add(var)
                return
        self.manual.append('%s(%d,%d): %s: %s' % (os.path.relpath(d['file'], ROOT), d['line'], d['col'],
                                                  why, d['msg'][:200]))

    def fix(self, d):
        src = self.sources.get(d['file'])
        if src is None:
            self.manual.append('%s(%d,%d): %s %s' % (d['file'], d['line'], d['col'], d['code'], d['msg'][:200]))
            return
        m, t = src.masked, src.text
        pos = src.offset(d['line'], d['col'])
        code, msg = d['code'], d['msg']
        mm = METHOD.search(msg)
        if code == 'C2039' and mm:
            return self.method(d, src, pos, mm.group(1))
        if code == 'C2662' and re.search(r"pointer from 'const ", msg):
            # a mutating member called through a const view of a temporary CString: the variable stays one
            k = skip_space_back(m, pos - 1)
            end = k - 1 if m[k] == '>' else k
            return self.give_up(d, src, postfix_start(m, end), 'const view')
        if NONCONST_TO.search(msg) and code not in ('C2664', 'C2665'):
            return self.give_up(d, src, pos, 'reference bound')
        if code == 'C4172':
            # a reference to a retyped variable returned as a CString reference binds a temporary: it stays a CString
            r = re.compile(r'\breturn\b').search(m, src.line_starts[d['line'] - 1])
            if r:
                k = skip_space(m, r.end())
                a, b = argument_span(m, k)
                for x in IDENT.finditer(m, a, b):
                    var = self.variable(d['file'], d['line'], x.group(0))
                    if var:
                        self.unfixable.add(var)
                        return
        if code == 'C2084':
            # a retyped parameter's function now has the body of an overload already taking a std::string
            for (dl, dn) in self.decls.get(d['file'], []):
                if dl == d['line']:
                    self.unfixable.add((d['file'], dl, dn))
                    return
        if code == 'C2665':
            for n in d['notes']:
                x = TO_CHAR_PTR.search(n['msg'])
                if x:
                    spans = call_args(m, pos)
                    index = int(x.group(1)) - 1
                    if index < len(spans):
                        a, b = spans[index]
                        return self.add(d['file'], a, b, c_str_of(t[a:b], m[a:b]))
                x = CSTRING_TO_STD_ARG.search(n['msg'])
                if x:
                    # a CString given to a retyped parameter: its characters
                    spans = call_args(m, pos)
                    index = int(x.group(1)) - 1
                    if index < len(spans):
                        a, b = spans[index]
                        expr = t[a:b].strip()
                        return self.add(d['file'], a, b, expr + '.GetString()' if is_postfix(m[a:b])
                                        else '(' + expr + ').GetString()')
        ref = NONCONST_REF.search(msg) or next((NONCONST_REF.search(n['msg']) for n in d['notes']
                                                 if NONCONST_REF.search(n['msg'])), None)
        if FROM_STD_PTR.search(msg) or (ref and code in ('C2664', 'C2665')):
            # an address or a non-const reference: the variable stays a CString
            arg = self.note_col(d, src)
            if arg is None and ref:
                spans = call_args(m, pos)
                index = int(ref.group(1)) - 1
                arg = spans[index][0] if spans and index < len(spans) else None
            if arg is None:
                arg = pos
            k = skip_space(m, arg)
            if m[k] == '&':
                k = skip_space(m, k + 1)
            return self.give_up(d, src, k, 'address or reference')
        if code in ('C2664', 'C2665', 'C2440', 'C2679', 'C2678', 'C4840', 'C2446') and (
                FROM_STD.search(msg) or FROM_CSTRING.search(msg) or code == 'C4840' or 'std::basic_string' in msg
                or "'std::string'" in msg):
            return self.convert(d, src, pos)
        if code in ('C4018', 'C4389'):
            return self.signed(d, src, pos)
        decl = self.declaration_at(d, src)
        if decl and code in ('C2064', 'C2440', 'C2665', 'C2664'):
            # a declaration initialised from a CString: its characters
            a, b = decl
            expr = t[a:b].strip()
            return self.add(d['file'], a, b, expr + '.GetString()' if is_postfix(m[a:b]) else '(' + expr + ').GetString()')
        self.manual.append('%s(%d,%d): %s %s' % (os.path.relpath(d['file'], ROOT), d['line'], d['col'], code, msg[:200]))

    def declaration_at(self, d, src):
        """[a, b) of the single initialiser of a retyped declaration `std::string name(init)` / `= init` on the
        diagnostic's line, when it reads a CString (a name or a call, no literal)."""
        m = src.masked
        ls = src.line_starts[d['line'] - 1]
        le = src.line_starts[d['line']] if d['line'] < len(src.line_starts) else len(m)
        x = re.compile(r'std::string\s+(\w+)\s*([(=])').search(m, ls, le)
        if not x or not any(n == x.group(1) and l == d['line'] for (l, n) in self.decls.get(d['file'], [])):
            return None
        k = skip_space(m, x.end())
        a, b = argument_span(m, k)
        if a >= b or '"' in m[a:b] or (x.group(2) == '(' and m[skip_space(m, b)] != ')'):
            return None
        return a, b

    def note_col(self, d, src):
        for n in d['notes']:
            if n['file'] == d['file'] and ('No user-defined-conversion' in n['msg'] or n['msg'].startswith('Reason:')
                                           or 'Types pointed to are unrelated' in n['msg']):
                return src.offset(n['line'], n['col'])
        return None

    def method(self, d, src, pos, name):
        m, t = src.masked, src.text
        if not t.startswith(name, pos):
            return self.manual.append('%s(%d,%d): method %s not at the column' % (d['file'], d['line'], d['col'], name))
        k = skip_space_back(m, pos - 1)
        if m[k] == '.':
            acc, recv_end = '.', k
        elif m[k] == '>' and m[k - 1] == '-':
            acc, recv_end = '->', k - 1
        else:
            return self.manual.append('%s(%d): %s without a receiver' % (d['file'], d['line'], name))
        rs = skip_space(m, postfix_start(m, recv_end))
        recv = t[rs:recv_end].strip()
        p = skip_space(m, pos + len(name))
        if p >= len(m) or m[p] != '(':
            return self.give_up(d, src, rs, 'method not called')
        q = match_forward(m, p)
        args = t[p + 1:q].strip()
        obj = recv if acc == '.' else ('*' + recv if is_postfix(m[rs:recv_end]) else '*(' + recv + ')')
        lead = recv + acc
        rep = None
        if name == 'GetLength' and not args:
            # size() where only its value is taken (an argument, an initialiser, a return, a test against 0);
            # an int otherwise, so arithmetic and comparisons stay signed
            before = skip_space_back(m, rs - 1)
            after = skip_space(m, q + 1)
            word = re.search(r'(\w+)\s*$', m[:rs])
            assigned = m[before] == '=' and m[before - 1] not in '=!<>+-*/%&|^'
            taken = m[before] in '(,' or assigned or (word and word.group(1) == 'return')
            ends = m[after] in ';),' or re.match(r'(==|!=|>)\s*0(?!\w)', m[after:after + 8])
            rep = lead + 'size()' if taken and ends else 'static_cast<int>(%ssize())' % lead
        elif name == 'IsEmpty' and not args:
            rep = lead + 'empty()'
        elif name == 'Empty' and not args:
            rep = lead + 'clear()'
        elif name == 'GetString' and not args:
            rep = lead + 'c_str()'
            before = skip_space_back(m, rs - 1)
            if (m[before] == '=' and re.search(r'std::string\s+\w+\s*$', m[:before])
                    and m[skip_space(m, q + 1)] == ';'):
                # `std::string name = text.GetString();` with text now a std::string: the copy itself
                mr = m[rs:recv_end].strip()
                rep = recv[1:-1] if mr.startswith('(') and match_forward(mr, 0) == len(mr) - 1 else recv
        elif name == 'AssignChar':
            rep = lead + 'assign(1, %s)' % args
        elif name in ('Format', 'FormatV'):
            before = skip_space_back(m, rs - 1)
            after = skip_space(m, q + 1)
            statement = before < 0 or m[before] in ';{})' or m.endswith('else', 0, before + 1) or (
                m[before] == ':' and m[before - 1] != ':')
            if statement and m[after] == ';':
                rep = '%s = str::%s(%s)' % (obj, name, args)
        elif name in STR_CALL:
            rep = 'str::%s(%s%s)' % (name, obj, ', ' + args if args else '')
        elif name == 'GetBuffer':
            rep = 'str::Buffer(%s, %s)' % (obj, args)
        elif name == 'GetBufferSetLength':
            rep = 'str::SetLength(%s, %s)' % (obj, args)
        elif name == 'ReleaseBuffer':
            rep = 'str::CutAtNull(%s)' % obj if args in ('', '-1') else lead + 'resize(%s)' % args
        if rep is None:
            return self.give_up(d, src, rs, 'no rule for ' + name)
        self.add(d['file'], rs, q + 1, rep)

    def convert(self, d, src, pos):
        m, t = src.masked, src.text
        code, msg = d['code'], d['msg']
        if code in ('C2664', 'C2665'):
            # the argument by its number, in the call whose name the column points at
            x = re.search(r"argument (\d+) from", msg)
            if x and re.match(r"'[^'(]*? \(", msg):
                # a call through a function pointer: the column is the argument's
                spans = [None] * (int(x.group(1)) - 1) + [argument_span(m, skip_space(m, pos))]
            else:
                spans = call_args(m, pos) if x and CALLEE.match(m, pos) else []
            if not x or int(x.group(1)) > len(spans):
                return self.manual.append('%s(%d,%d): %s no argument' % (d['file'], d['line'], d['col'], code))
            a, b = spans[int(x.group(1)) - 1]
        elif code == 'C4840':
            if 'std::basic_string' not in msg:
                return self.manual.append('%s(%d,%d): %s %s' % (d['file'], d['line'], d['col'], code, msg[:200]))
            a, b = argument_span(m, skip_space(m, pos))
        elif code == 'C2440' and ("'type cast'" in msg or "'static_cast'" in msg) and FROM_STD.search(msg):
            # (const char*)x or static_cast<const char*>(x): x.c_str()
            c = re.compile(r'\(\s*(?:const\s+)?char\s*(?:const\s*)?\*\s*\)|static_cast\s*<\s*(?:const\s+)?char\s*'
                           r'(?:const\s*)?\*\s*>\s*\(').match(m, pos)
            if not c:
                # a cast to a class of CString's (a view with more members): the operand stays a CString
                o = m.find('(', m.find('>', pos) if m.startswith('static_cast', pos) else pos)
                if o >= 0:
                    return self.give_up(d, src, skip_space(m, o + 1), 'cast to a CString view')
                return self.manual.append('%s(%d,%d): cast not found' % (d['file'], d['line'], d['col']))
            if m[c.end() - 1] == '(':
                close = match_forward(m, c.end() - 1)
                return self.add(d['file'], pos, close + 1, c_str_of(t[c.end():close], m[c.end():close]))
            e = postfix_end(m, skip_space(m, c.end()))
            return self.add(d['file'], pos, e, c_str_of(t[c.end():e], m[c.end():e]))
        elif code == 'C2440' and ("'initializing'" in msg or "'return'" in msg or "'='" in msg):
            k = pos
            if "'return'" in msg:
                r = re.compile(r'\breturn\b').search(m, pos)
                if not r:
                    return self.manual.append('%s(%d): return not found' % (d['file'], d['line']))
                k = r.end()
            else:
                while k < len(m) and m[k] not in '=(;{':
                    k += 1
                if m[k] in '({':
                    k += 0   # direct initialisation: the argument inside
                k += 1
            a, b = argument_span(m, skip_space(m, k))
            target = TO_TYPE.search(msg).group(1) if TO_TYPE.search(msg) else ''
            if 'char' in target and FROM_STD.search(msg) and not is_simple_name(m[a:b]):
                return self.give_up(d, src, a, 'a pointer kept from a temporary')
        elif code in ('C2679', 'C2678'):
            # binary operator: the right-hand operand
            k = pos
            while k < len(m) and m[k] not in '=;':
                k += 1
            if m[k] != '=':
                return self.manual.append('%s(%d,%d): %s operator' % (d['file'], d['line'], d['col'], code))
            a, b = argument_span(m, skip_space(m, k + 1))
        else:
            return self.manual.append('%s(%d,%d): %s %s' % (d['file'], d['line'], d['col'], code, msg[:200]))
        if a >= b:
            return self.manual.append('%s(%d,%d): empty expression' % (d['file'], d['line'], d['col']))
        if t[a:b].endswith('.c_str()') or t[a:b].endswith('.GetString()'):
            return self.manual.append('%s(%d,%d): already converted' % (d['file'], d['line'], d['col']))
        if FROM_STD.search(msg) or code == 'C4840':
            return self.add(d['file'], a, b, c_str_of(t[a:b], m[a:b]))
        if FROM_CSTRING.search(msg) or TO_STD.search(msg):
            # a CString where a std::string is taken: its characters
            expr = t[a:b].strip()
            return self.add(d['file'], a, b, expr + '.GetString()' if is_postfix(m[a:b]) else '(' + expr + ').GetString()')
        self.manual.append('%s(%d,%d): %s %s' % (d['file'], d['line'], d['col'], code, msg[:200]))

    def signed(self, d, src, pos):
        m, t = src.masked, src.text
        ls = src.line_starts[d['line'] - 1]
        le = src.line_starts[d['line']] if d['line'] < len(src.line_starts) else len(t)
        hits = [x for x in re.finditer(r'\.size\(\)', m[ls:le])]
        if not hits:
            return self.manual.append('%s(%d,%d): %s without size()' % (d['file'], d['line'], d['col'], d['code']))
        for x in hits:
            end = ls + x.end()
            rs = postfix_start(m, ls + x.start())
            if m[skip_space_back(m, rs - 1)] == '(' and t[:rs].rstrip().endswith('static_cast<int>('):
                continue
            self.add(d['file'], rs, end, 'static_cast<int>(%s)' % t[rs:end])


def is_simple_name(m):
    return re.fullmatch(r'\s*[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*\s*', m) is not None


def apply_edits(text, edits):
    """`text` with the non-overlapping `edits` (the first of overlapping ones) applied."""
    edits = sorted(set(edits), key=lambda e: (e[0], -e[1]))
    kept = []
    last_end = -1
    for e in edits:
        if e[0] >= last_end:
            kept.append(e)
            last_end = e[1]
    for s, e, rep in reversed(kept):
        text = text[:s] + rep + text[e:]
    return text, len(kept)


INCLUDE_STR = '#include "lib/str.h"\n'


def ensure_include(text):
    """`text` with "lib/str.h" included where it calls str::, <string> where it names std::string only."""
    masked = mask(text)
    if 'str::' not in masked.replace('std::string', ''):
        if 'std::string' not in masked or re.search(r'^#include <string>', text, re.M):
            return text
        line = '#include <string>\n'
        system = list(re.finditer(r'^#include <[^>]+>\n', text, re.M))
        for x in system:
            if x.group(0) > line:
                return text[:x.start()] + line + text[x.start():]
        if system:
            return text[:system[-1].end()] + line + text[system[-1].end():]
        quoted = list(re.finditer(r'^#include "[^"]+"\n', text, re.M))
        at = quoted[-1].end() if quoted else 0
        return text[:at] + '\n' + line + text[at:]
    if '"lib/str.h"' in text:
        return text
    last = None
    for x in re.finditer(r'^#include "[^"]+"\n', text, re.M):
        last = x
    if last is None:
        return INCLUDE_STR + text
    # keep the quoted includes in order where they are sorted
    block = [x for x in re.finditer(r'^#include "([^"]+)"\n', text, re.M)]
    for x in block:
        if x.group(1) > 'lib/str.h' and x.group(1).startswith('lib/'):
            return text[:x.start()] + INCLUDE_STR + text[x.start():]
    return text[:last.end()] + INCLUDE_STR + text[last.end():]


# ---- driver -----------------------------------------------------------------------------------------------------

def candidate_files(args):
    if args.files:
        return [os.path.normcase(os.path.abspath(os.path.join(ROOT, f))) for f in args.files]
    out = []
    for name in sorted(os.listdir(os.path.join(ROOT, 'src', 'game'))):
        if name.endswith('.cpp'):
            out.append(os.path.normcase(os.path.join(ROOT, 'src', 'game', name)))
    return out


# where a retyped parameter's callers may be: the fixes reach these too (their declarations change only in src/game)
FIX_DIRS = [('src', 'game'), ('src', 'lib'), ('tests', 'verify', 'image'), ('tests', 'vectors')]


def fixable_files():
    out = []
    for parts in FIX_DIRS:
        d = os.path.join(ROOT, *parts)
        for name in sorted(os.listdir(d)):
            if name.endswith(('.cpp', '.h')):
                out.append(os.path.normcase(os.path.join(d, name)))
    return out


def shown(path):
    return path if path == '<param>' else os.path.relpath(path, ROOT)


def game_file(path):
    return os.path.normcase(os.path.join(ROOT, 'src', 'game')) == os.path.dirname(path)


def retype_params(files, originals, excluded, keys):
    """Step 1 for scope `params`: the `const mfc::CString&` parameters of the functions defined in src/game (not
    virtual ones), in their definitions and every declaration of the same name and position, become
    `const std::string&`. {path: [(line, name)]} of the retyped definitions' parameters; `keys` gets
    {(path, line, name): (function, index)}."""
    game = [p for p in files if game_file(p)]
    virtual = virtual_names(originals[p] for p in game)
    found = {p: find_params(Source(p, originals[p])) for p in game}
    chosen = set()
    for p, params in found.items():
        for (a, b, fn, index, pname, definition) in params:
            if definition and pname and fn not in virtual and ('<param>', index, fn) not in excluded:
                chosen.add((fn, index))
    # an overload set on the string type (CopyMember's CString / stlp::string, an Ops cast view): a member of it
    # retyped alone collides with another or loses its callers
    masked = {p: mask(originals[p]) for p in game}
    for (fn, index) in sorted(chosen):
        for p in game:
            for x in re.finditer(r'(?<![\w:])' + re.escape(fn) + r'\s*\(', masked[p]):
                spans = call_args(masked[p], x.start())
                if index < len(spans):
                    a, b = spans[index]
                    param = masked[p][a:b]
                    if re.search(r'std::string\b|string_view|stlp::|(?<!const )mfc::CString\s*&', param):
                        chosen.discard((fn, index))
    decls = {}
    for p, params in found.items():
        src = Source(p, originals[p])
        edits = []
        for (a, b, fn, index, pname, definition) in params:
            if (fn, index) not in chosen:
                continue
            edits.append((a, b, STD))
            if definition and pname:
                line = src.line_of(a)
                decls.setdefault(p, []).append((line, pname))
                keys[(p, line, pname)] = (fn, index)
        text, _ = apply_edits(originals[p], edits)
        if text != originals[p]:
            write(p, ensure_include(text))
    return decls


def read(path):
    with open(path, encoding='utf-8', newline='') as f:
        return f.read()


def write(path, text):
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def retype(files, originals, excluded):
    """Step 1 on the original texts: {path: [(line, name)]} of the retyped declarations."""
    decls = {}
    for path in files:
        src = Source(path, originals[path])
        edits = []
        for (a, b, names, inits) in find_locals(src):
            line = src.line_of(a)
            if any((path, line, n) in excluded for n in names):
                continue
            edits.append((a, b, STD))
            edits.extend(inits)
            decls.setdefault(path, []).extend((line, n) for n in names)
        text, _ = apply_edits(originals[path], edits)
        if text != originals[path]:
            write(path, ensure_include(text))
    return decls


def build():
    env = dict(os.environ)
    env['CL'] = '/diagnostics:column'
    r = subprocess.run(['ninja', '-C', BUILD, '-k', '0'], capture_output=True, text=True, errors='replace', env=env)
    return r.returncode, r.stdout + r.stderr


def run(args):
    os.makedirs(STATE, exist_ok=True)
    files = candidate_files(args) if args.scope == 'locals' else fixable_files()
    files = sorted(set(files) | set(fixable_files()))
    originals = {p: read(p) for p in files}
    excluded_path = os.path.join(STATE, 'excluded.json')
    excluded = set()
    if os.path.exists(excluded_path) and not args.fresh:
        excluded = {(p if p == '<param>' else os.path.normcase(os.path.join(ROOT, p)), l, n)
                    for p, l, n in json.load(open(excluded_path))}
    try:
        for rnd in range(args.rounds):
            for p in files:
                if read(p) != originals[p]:
                    write(p, originals[p])
            keys = {}
            if args.scope == 'params':
                decls = retype_params(files, originals, excluded, keys)
            else:
                decls = retype(candidate_files(args), originals, excluded)
            print('round %d: %d declarations retyped in %d files, %d excluded' % (
                rnd, sum(len(v) for v in decls.values()), len(decls), len(excluded)), flush=True)
            new_unfixable = set()
            manual = []
            for it in range(args.iterations):
                rc, out = build()
                with open(os.path.join(STATE, 'build.log'), 'w', encoding='utf-8') as f:
                    f.write(out)
                diags = [d for d in parse_diagnostics(out) if d['kind'] != 'note']
                sources = {p: Source(p, read(p)) for p in files}
                fixer = Fixer(sources, decls)
                for d in diags:
                    fixer.fix(d)
                # a parameter stays a CString in its function's every definition and declaration
                new_unfixable |= {('<param>',) + keys[v][::-1] if v in keys else v for v in fixer.unfixable}
                manual = fixer.manual
                with open(os.path.join(STATE, 'manual.txt'), 'w', encoding='utf-8') as f:
                    f.write(''.join(x + '\n' for x in manual))
                with open(os.path.join(STATE, 'keep.txt'), 'w', encoding='utf-8') as f:
                    f.write(''.join('%s:%d %s\n' % (shown(p), l, n) for p, l, n in sorted(new_unfixable)))
                count = 0
                for p, edits in fixer.edits.items():
                    text, n = apply_edits(read(p), edits)
                    count += n
                    write(p, text)
                for p in files:
                    text = read(p)
                    if text == originals[p]:
                        continue
                    fixed = ensure_include(text)
                    if fixed != text:
                        write(p, fixed)
                print('  build %d: exit %d, %d diagnostics, %d edits, %d variables to keep, %d manual' % (
                    it, rc, len(diags), count, len(fixer.unfixable), len(manual)), flush=True)
                if rc == 0 and not diags:
                    break
                if count == 0:
                    break
            if new_unfixable - excluded:
                excluded |= new_unfixable
                json.dump(sorted((shown(p), l, n) for p, l, n in excluded),
                          open(excluded_path, 'w'), indent=0)
                continue
            with open(os.path.join(STATE, 'manual.txt'), 'w', encoding='utf-8') as f:
                f.write('\n'.join(manual) + '\n')
            print('done: %d manual diagnostics (build/c14_strings/manual.txt)' % len(manual))
            return 0 if not manual else 1
        print('rounds exhausted')
        return 1
    except KeyboardInterrupt:
        return 2


PLACEMENT = re.compile(r'(?<![\w:])(?:::)?new\s*\(')
DTOR = re.compile(r'(\.|->)\s*~CString\s*\(\s*\)')


def lifetimes(text):
    """`text` with CString lifetimes typed by their target: `new (p) mfc::CString(args)` -> std::construct_at(p, args),
    `x.~CString()` -> std::destroy_at(&x), `p->~CString()` -> std::destroy_at(p); retyping the target then changes
    what is built and destroyed with it (a placement new of a CString on a std::string compiles). A target that is raw
    storage (a void* out parameter, a byte array) does not compile as construct_at's: those keep their placement new
    by hand (5 in batch 2)."""
    m = mask(text)
    edits = []
    for x in PLACEMENT.finditer(m):
        o = x.end() - 1
        c = match_forward(m, o)
        t = re.compile(r'\s*mfc::CString\b').match(m, c + 1)
        if not t:
            continue
        target = text[o + 1:c].strip()
        if target == 'this':
            continue   # a view class of CString's building its base: the type is the base's, not this's
        end = t.end()
        k = skip_space(m, end)
        args = ''
        if k < len(m) and m[k] == '(':
            q = match_forward(m, k)
            args = text[k + 1:q].strip()
            end = q + 1
        elif k < len(m) and m[k] == '{':
            continue
        edits.append((x.start(), end, 'std::construct_at(%s%s)' % (target, ', ' + args if args else '')))
    for x in DTOR.finditer(m):
        if m[skip_space_back(m, postfix_start(m, x.start()) - 1)] == '>':
            continue   # `reinterpret_cast<mfc::CString*>(p)->~CString()`: typed by its cast already
        if x.group(1) == '.':
            rs = postfix_start(m, x.start())
            obj = text[rs:x.start()].strip()
            edits.append((rs, x.end(), 'std::destroy_at(&%s)' % obj))
        else:
            rs = postfix_start(m, x.start())
            obj = text[rs:x.start()].strip()
            edits.append((rs, x.end(), 'std::destroy_at(%s)' % obj))
    text, n = apply_edits(text, edits)
    if n and not re.search(r'^#include <memory>', text, re.M):
        system = list(re.finditer(r'^#include <[^>]+>\n', text, re.M))
        line = '#include <memory>\n'
        at = next((s.start() for s in system if s.group(0) > line), system[-1].end() if system else None)
        if at is None:
            quoted = list(re.finditer(r'^#include "[^"]+"\n', text, re.M))
            at = quoted[-1].end() if quoted else 0
            line = '\n' + line
        text = text[:at] + line + text[at:]
    return text, n


def lifetimes_cmd(args):
    total = 0
    for p in fixable_files():
        if os.path.dirname(p) == os.path.normcase(os.path.join(ROOT, 'src', 'lib')):
            continue   # CString's own implementation and the byte-offset element policies
        text, n = lifetimes(read(p))
        if n:
            write(p, text)
            total += n
            print('%s: %d' % (shown(p), n))
    print(total, 'lifetimes typed')


def list_cmd(args):
    files = candidate_files(args)
    total = 0
    for p in files:
        src = Source(p, read(p))
        for (a, b, names, _) in find_locals(src):
            total += len(names)
            if args.verbose:
                print('%s:%d: %s' % (os.path.relpath(p, ROOT), src.line_of(a), ', '.join(names)))
    print(total, 'declarations')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('command', choices=['run', 'list', 'lifetimes'])
    ap.add_argument('--files', nargs='*')
    ap.add_argument('--scope', choices=['locals', 'params'], default='locals')
    ap.add_argument('--rounds', type=int, default=8)
    ap.add_argument('--iterations', type=int, default=12)
    ap.add_argument('--fresh', action='store_true', help='forget the excluded variables of earlier runs')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    if args.command == 'lifetimes':
        return lifetimes_cmd(args)
    if args.command == 'list':
        return list_cmd(args)
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
