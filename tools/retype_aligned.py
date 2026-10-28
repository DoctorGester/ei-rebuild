"""C14e: the engine's aligned arrays (lib::AlignedArray) to std::vector, a scripted rewrite (one rule per call shape).

Step 1, retype: every `lib::AlignedArray<T>` written in src/game, src/lib, tests/verify/image and tests/vectors becomes
`std::vector<T>` (declarations, aliases, parameters, template arguments), but the qualified static members
(`lib::AlignedArray<T>::Allocate / Free`: raw blocks, ported by hand), the files of SKIP and the declarations of KEEP.
A braced zero initialiser `{nullptr, 0, 0, 0}` goes.
Step 2, fix: the verify build's diagnostics (MSVC /diagnostics:column, ninja -k 0) name every place a std::vector does
not take what the aligned array took; one rule per shape rewrites it (`O` is the vector: `x` for `x.m`, `*p` for
`p->m`):
  x.data[i]           -> x[i]                    (p->data[i] -> (*p)[i])
  x.data              -> x.data()                (in a test: `if (x.data)` -> `if (!x.empty())`, `!x.data` -> x.empty())
  x.size              -> std::ssize(O)           (int on x86, as the field was)
  x.size == 0, <= 0, !x.size                     -> x.empty();   x.size > 0, != 0, a bare test -> !x.empty()
  x.size = 0;  x.size = n;  --x.size;  x.size -= n;  x.size += n;
                      -> x.clear();  x.resize(n);  x.pop_back();  x.resize(x.size() - n);  x.resize(x.size() + n);
  x.capacity          -> static_cast<int>(x.capacity())
  x.growBy = n;       -> (gone: the vector's growth is its own)
  x.SetSize(n[, g]), SetSizeDiscard(n), SetSizeDiscardStep(n, g)   -> x.resize(n)   (n 0: x.clear())
  x.Add(v);           -> x.push_back(v);         (a statement; its index used: manual)
  x.SetAtGrow(i, v)   -> lib::SetAtGrow(O, i, v)     x.SetAtGrowRef(i, p) -> lib::SetAtGrow(O, i, *p)
  x.RemoveAt(i)       -> x.erase(x.begin() + i)      x.RemoveRange(i, n) -> x.erase(x.begin() + i, x.begin() + (i + n))
  x.RemoveAll()       -> x.clear()                   x.Copy(y) -> O = y        x.ElementAt(i) -> O[i]
  x.Construct();      -> in a constructor gone; else std::construct_at(&O);   (the owner built in raw storage)
  x.FreeData();  `if (x.data) lib::FreeAligned32(x.data);`  `lib::FreeAligned32(x.data);`  (and ...::Free(x.data))
                      -> in a destructor gone; else std::destroy_at(&O);
What no rule takes is listed in build/retype_aligned/manual.txt; the sites that depend on the aligned array's quirks
(new slots uninitialised, the growth step, the raw storage the owner is built in) in review.txt.

usage (no VS prompt needed; build/verify configured by build.cmd verify):
  python tools/retype_aligned.py run [--iterations N]     step 1 on the current tree, then the fix builds
  python tools/retype_aligned.py list                      the declarations step 1 would retype
State (the last build's log, manual.txt, review.txt) in build/retype_aligned/.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from c14_strings import (mask, Source, match_forward, match_backward, skip_space, skip_space_back, postfix_start,
                         call_args, apply_edits, read, write, DIAG)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
STATE = os.path.join(ROOT, 'build', 'retype_aligned')
VSROOT = os.environ.get('EI_VSROOT', r'E:\bin\vs\buildtools')

FIX_DIRS = [('src', 'game'), ('src', 'lib'), ('tests', 'verify', 'image'), ('tests', 'vectors')]
# Files another ticket owns while this runs (C14a's): their aligned arrays stay (and so do the declarations they
# reach, KEEP).
SKIP = {
    'src/game/console.cpp', 'src/game/console.h', 'src/game/ui_chat_overlay.cpp', 'src/game/ui_text.cpp',
    'src/game/ui_hud_unit_info.cpp', 'src/game/scene_loader.cpp', 'src/game/scene_loader.h',
    'src/game/ui_item_info.cpp', 'src/game/ui_item_info.h',
    'src/lib/containers.h',
}
# Declarations that stay aligned arrays: (file, declared name). A skipped file's code reaches them.
KEEP = set()

FIELDS = {'data', 'size', 'capacity', 'growBy'}
METHODS = {'SetSize', 'SetSizeDiscard', 'SetSizeDiscardStep', 'Add', 'SetAtGrow', 'SetAtGrowRef', 'RemoveAt',
           'RemoveRange', 'RemoveAll', 'Copy', 'ElementAt', 'Construct', 'FreeData'}
VECTOR = "std::vector<"


def rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def files():
    out = []
    for parts in FIX_DIRS:
        d = os.path.join(ROOT, *parts)
        for name in sorted(os.listdir(d)):
            if name.endswith(('.cpp', '.h', '.inc')):
                p = os.path.normcase(os.path.join(d, name))
                if rel(p) not in SKIP:
                    out.append(p)
    return out


# ---- step 1 -----------------------------------------------------------------------------------------------------

ALIGNED = re.compile(r'\blib::AlignedArray\s*<')
ZERO_INIT = re.compile(r'\s*\{\s*nullptr\s*,\s*0\s*,\s*0\s*,\s*0\s*\}')


def declared_name(masked, start, close):
    """The name an occurrence declares: the alias of `using X = lib::AlignedArray<...>`, else the identifier after
    the closing '>' (past * and &)."""
    before = masked[:start].rstrip()
    m = re.search(r'\busing\s+(\w+)\s*=$', before)
    if m:
        return m.group(1)
    m = re.match(r'[\s*&]*(?:const\s+)?[\s*&]*(\w+)', masked[close + 1:])
    return m.group(1) if m else ''


def find_aligned(text):
    """[(start of `lib::AlignedArray<`, index of its '>', declared name, qualified static use)]."""
    masked = mask(text)
    out = []
    for m in ALIGNED.finditer(masked):
        lt = m.end() - 1
        depth, close = 0, -1
        for j in range(lt, len(masked)):
            if masked[j] == '<':
                depth += 1
            elif masked[j] == '>':
                depth -= 1
                if depth == 0:
                    close = j
                    break
        if close < 0:
            continue
        qualified = masked[skip_space(masked, close + 1):].startswith('::')
        out.append((m.start(), close, declared_name(masked, m.start(), close), qualified))
    return out


def retype_text(path, text):
    """Step 1 on one file: (new text, [(line, name)] retyped)."""
    src = Source(path, text)
    masked = src.masked
    edits, done = [], []
    for (start, close, name, qualified) in find_aligned(text):
        if qualified or (rel(path), name) in KEEP:
            continue
        edits.append((start, masked.index('<', start) + 1, VECTOR))
        done.append((src.line_of(start), name))
        # the zero initialiser right after the declarator
        m = re.match(r'(\s*[*&]*\s*\w+)', masked[close + 1:])
        if m:
            at = close + 1 + m.end()
            z = ZERO_INIT.match(masked, at)
            if z:
                edits.append((at, z.end(), ''))
    new, _ = apply_edits(text, edits)
    return new, done


def ensure_includes(text):
    masked = mask(text)
    for header, needed in (('vector', 'std::vector<'), ('memory', 'std::construct_at')):
        uses = needed in masked or (header == 'memory' and 'std::destroy_at' in masked)
        if not uses or re.search(r'^#include <%s>' % header, text, re.M):
            continue
        line = '#include <%s>\n' % header
        system = list(re.finditer(r'^#include <[^>]+>\n', text, re.M))
        placed = False
        for x in system:
            if x.group(0) > line:
                text = text[:x.start()] + line + text[x.start():]
                placed = True
                break
        if placed:
            continue
        if system:
            text = text[:system[-1].end()] + line + text[system[-1].end():]
            continue
        quoted = list(re.finditer(r'^#include "[^"]+"\n', text, re.M))
        if quoted:
            at = quoted[-1].end()
            text = text[:at] + '\n' + line + text[at:]
        else:
            m = re.search(r'^#pragma once\n', text, re.M)
            at = m.end() if m else 0
            text = text[:at] + '\n' + line + text[at:]
    return text


# ---- step 2 -----------------------------------------------------------------------------------------------------

def parse_diagnostics(output):
    out, seen = [], set()
    for line in output.splitlines():
        m = DIAG.match(line.rstrip())
        if not m or m.group('kind') == 'note':
            continue
        d = dict(m.groupdict())
        d['file'] = os.path.normcase(os.path.abspath(os.path.join(BUILD, d['file'])))
        d['line'], d['col'] = int(d['line']), int(d['col'] or 1)
        key = (d['file'], d['line'], d['col'], d['code'], d['msg'])
        if key not in seen:
            seen.add(key)
            out.append(d)
    return out


MEMBER_MSG = re.compile(r"'std::vector<.*>::(\w+)': non-standard syntax|'(\w+)': is not a member of 'std::vector<")
ACCESS = re.compile(r'(\.|->)\s*(\w+)\b')


def statement_start(m, start):
    """Whether an expression starting at `start` begins a statement."""
    i = skip_space_back(m, start - 1)
    if i < 0 or m[i] in ';{}':
        return True
    if m[i] == ':' and (i == 0 or m[i - 1] != ':'):
        return True
    if re.search(r'\belse$', m[:i + 1]):
        return True
    if m[i] == ')':
        j = match_backward(m, i)
        return re.search(r'\b(if|for|while)\s*$', m[:j]) is not None
    return False


def statement_end(m, end):
    """Whether an expression ending at `end` ends its statement (a ';' follows)."""
    k = skip_space(m, end)
    return k < len(m) and m[k] == ';'


def enclosing_function(m, pos):
    """(name, class name or None) of the function whose body holds `pos`: name '~X' for a destructor."""
    depth = 0
    opens = []
    for j in range(pos - 1, -1, -1):
        c = m[j]
        if c == '}':
            depth += 1
        elif c == '{':
            if depth == 0:
                opens.append(j)
            else:
                depth -= 1
    for k, brace in enumerate(opens):
        i = skip_space_back(m, brace - 1)
        while True:
            w = re.search(r'\b(const|noexcept|override|final)\s*$', m[:i + 1])
            if not w:
                break
            i = skip_space_back(m, w.start() - 1)
        if i < 0 or m[i] != ')':
            continue
        # walk back over a constructor's initialiser list
        while True:
            j = match_backward(m, i)
            if j < 0:
                break
            n = re.search(r'(~?\s*\w+(?:\s*::\s*~?\s*\w+)*)\s*$', m[:j])
            if not n:
                break
            name = re.sub(r'\s+', '', n.group(1))
            b = skip_space_back(m, n.start() - 1)
            if b >= 0 and (m[b] == ',' or (m[b] == ':' and (b == 0 or m[b - 1] != ':'))) and not \
                    re.search(r'\b(public|private|protected)\s*:$', m[:b + 1]):
                i = skip_space_back(m, b - 1)
                if m[i] == ')' or m[i] == '}':
                    if m[i] == '}':
                        i = skip_space_back(m, match_backward(m, i) - 1)
                    continue
                break
            if re.fullmatch(r'(if|for|while|switch|catch|return|sizeof)', name):
                break
            parts = name.split('::')
            owner = None
            for outer in opens[k + 1:]:
                h = re.search(r'\b(?:struct|class)\s+(\w+)[^;{}()]*$', m[:outer])
                if h:
                    owner = h.group(1)
                    break
            if len(parts) >= 2:
                owner = parts[-2]
            return parts[-1], owner
        break
    return None, None


class Fixer:
    def __init__(self, sources):
        self.sources = sources
        self.edits = {}
        self.manual = []
        self.review = []
        self.done = set()

    def add(self, path, a, b, text):
        self.edits.setdefault(path, []).append((a, b, text))

    def note(self, kind, src, pos, text):
        where = '%s:%d' % (rel(src.path), src.line_of(pos))
        (self.manual if kind == 'manual' else self.review).append('%s %s' % (where, text))

    def fix(self, d):
        path = d['file']
        src = self.sources.get(path)
        if src is None:
            return
        msg = d['msg']
        member = None
        mm = MEMBER_MSG.search(msg)
        if mm:
            member = mm.group(1) or mm.group(2)
        elif d['code'] not in ('C2659', 'C2105', 'C2171', 'C2296', 'C2297', 'C2446', 'C2568', 'C2109'):
            return
        line_start = src.line_starts[d['line'] - 1]
        line_end = src.text.find('\n', line_start)
        line_end = len(src.text) if line_end < 0 else line_end
        m = src.masked
        best = None
        for x in ACCESS.finditer(m, line_start, line_end):
            name = x.group(2)
            if member and name != member:
                continue
            if name not in FIELDS and name not in METHODS:
                continue
            after = skip_space(m, x.end())
            called = after < len(m) and m[after] == '('
            if name in FIELDS and called:
                continue
            if name in METHODS and not called:
                continue
            col = x.start() - line_start + 1
            dist = min(abs(col - d['col']), abs(x.start(2) - line_start + 1 - d['col']))
            if dist <= 3 and (best is None or dist < best[0]):
                best = (dist, x)
        if best is None:
            if member:
                self.manual.append('%s:%d %s %s' % (rel(path), d['line'], d['code'], msg[:160]))
            return
        x = best[1]
        key = (path, x.start())
        if key in self.done:
            return
        self.done.add(key)
        self.rewrite(src, x)

    def rewrite(self, src, x):
        m, t = src.masked, src.text
        dot, name = x.group(1), x.group(2)
        a = postfix_start(m, x.start())
        obj = t[a:x.start()].strip()
        arrow = dot == '->'
        O = '*' + obj if arrow else obj            # the vector, as an operand of a function
        P = '(*%s)' % obj if arrow else obj        # the vector, before a postfix operator
        acc = obj + dot                            # `x.` / `p->`
        end = x.end()
        path = src.path
        if name == 'data':
            k = skip_space(m, end)
            if k < len(m) and m[k] == '[':
                self.add(path, a, k, P)
                return
            if self.free_statement(src, a, x):
                return
            test = self.test_context(m, a, end)
            if test is not None:
                self.add(path, test[0], test[1], ('!' if test[2] else '') + acc + 'empty()')
                self.note('review', src, a, 'data test -> empty(): %s' % obj)
                return
            self.add(path, x.start(2), end, 'data()')
            return
        if name == 'size':
            k = skip_space(m, end)
            rest = m[k:k + 3]
            pre = skip_space_back(m, a - 1)
            if m[pre - 1:pre + 1] in ('--', '++') and statement_start(m, pre - 1) and statement_end(m, end):
                op = m[pre - 1:pre + 1]
                semi = skip_space(m, end)
                self.add(path, pre - 1, semi, acc + ('pop_back()' if op == '--' else 'resize(%ssize() + 1)' % acc))
                self.note('review', src, a, 'size %s: %s' % (op, obj))
                return
            if rest[:2] in ('--', '++') and statement_start(m, a):
                op = rest[:2]
                self.add(path, a, k + 2, acc + ('pop_back()' if op == '--' else 'resize(%ssize() + 1)' % acc))
                self.note('review', src, a, 'size %s: %s' % (op, obj))
                return
            asg = re.match(r'(=(?!=)|-=|\+=)\s*', m[k:])
            if asg and statement_start(m, a):
                v0 = k + asg.end()
                semi = v0
                depth = 0
                while semi < len(m) and not (m[semi] == ';' and depth == 0):
                    depth += m[semi] in '([{'
                    depth -= m[semi] in ')]}'
                    semi += 1
                value = t[v0:semi].strip()
                op = asg.group(1)
                if op == '=' and value == '0':
                    rep = acc + 'clear()'
                elif op == '=':
                    rep = acc + 'resize(%s)' % value
                    self.note('review', src, a, 'size = %s: %s' % (value, obj))
                else:
                    rep = acc + 'resize(%ssize() %s %s)' % (acc, op[0], value)
                    if op == '+=':
                        self.note('review', src, a, 'size += %s: %s' % (value, obj))
                self.add(path, a, semi, rep)
                return
            test = self.test_context(m, a, end)
            if test is not None:
                self.add(path, test[0], test[1], ('!' if test[2] else '') + acc + 'empty()')
                return
            self.add(path, a, end, 'std::ssize(%s)' % O)
            return
        if name == 'capacity':
            k = skip_space(m, end)
            if re.match(r'(=(?!=)|-=|\+=|\+\+|--)', m[k:]):
                self.note('manual', src, a, 'capacity written: %s' % obj)
                return
            self.add(path, a, end, 'static_cast<int>(%scapacity())' % acc)
            return
        if name == 'growBy':
            k = skip_space(m, end)
            if re.match(r'=(?!=)', m[k:]) and statement_start(m, a):
                semi = m.index(';', k)
                stop = semi + 1
                # the whole line when the statement is alone on it
                ls = t.rfind('\n', 0, a) + 1
                le = t.find('\n', stop)
                if t[ls:a].strip() == '' and t[stop:le if le >= 0 else len(t)].strip() == '':
                    self.add(path, ls, le + 1 if le >= 0 else len(t), '')
                else:
                    self.add(path, a, stop, '')
                self.note('review', src, a, 'growBy dropped: %s' % obj)
                return
            self.note('manual', src, a, 'growBy read: %s' % obj)
            return
        # methods
        open_ = skip_space(m, end)
        close = match_forward(m, open_)
        spans = call_args(m, open_)
        args = [t[p:q].strip() for p, q in spans if q > p]
        stmt = statement_start(m, a) and statement_end(m, close + 1)

        def paren(e):
            return e if re.fullmatch(r'[\w.:>\-\[\]()]+', e) and '-' not in e.replace('->', '') else '(%s)' % e

        if name in ('SetSize', 'SetSizeDiscard', 'SetSizeDiscardStep'):
            n = args[0]
            self.add(path, a, close + 1, acc + ('clear()' if n == '0' else 'resize(%s)' % n))
            return
        if name == 'Add':
            if not stmt:
                self.note('manual', src, a, 'Add value used: %s' % obj)
                return
            self.add(path, a, close + 1, acc + 'push_back(%s)' % args[0])
            return
        if name in ('SetAtGrow', 'SetAtGrowRef'):
            v = args[1] if name == 'SetAtGrow' else '*' + paren(args[1])
            self.add(path, a, close + 1, 'lib::SetAtGrow(%s, %s, %s)' % (O, args[0], v))
            return
        if name == 'RemoveAt':
            self.add(path, a, close + 1, acc + 'erase(%sbegin() + %s)' % (acc, paren(args[0])))
            return
        if name == 'RemoveRange':
            i, n = args[0], args[1]
            self.add(path, a, close + 1, acc + 'erase(%sbegin() + %s, %sbegin() + %s)' % (
                acc, paren(i), acc, paren('%s + %s' % (i, paren(n)))))
            return
        if name == 'RemoveAll':
            self.add(path, a, close + 1, acc + 'clear()')
            return
        if name == 'Copy':
            if not stmt:
                self.note('manual', src, a, 'Copy value used: %s' % obj)
                return
            self.add(path, a, close + 1, '%s = %s' % (O, args[0]))
            return
        if name == 'ElementAt':
            self.add(path, a, close + 1, '%s[%s]' % (P, args[0]))
            return
        if name in ('Construct', 'FreeData'):
            if not stmt:
                self.note('manual', src, a, '%s value used: %s' % (name, obj))
                return
            self.lifetime(src, a, close + 2 if m[skip_space(m, close + 1)] == ';' else close + 1, obj, arrow,
                          'construct' if name == 'Construct' else 'destroy')
            return

    def lifetime(self, src, a, stop, obj, arrow, what):
        """Replaces the statement [a, stop) (stop past its ';') by the vector's construction / destruction."""
        m, t, path = src.masked, src.text, src.path
        fn, owner = enclosing_function(m, a)
        target = obj if arrow else '&' + obj
        if what == 'destroy' and fn and fn.startswith('~'):
            self.remove_statement(src, a, stop)
            return
        if what == 'construct' and fn and owner and fn == owner:
            self.remove_statement(src, a, stop)
            return
        call = 'std::construct_at' if what == 'construct' else 'std::destroy_at'
        semi = stop - 1 if t[stop - 1] == ';' else stop
        self.add(path, a, semi, '%s(%s)' % (call, target))
        self.note('review', src, a, '%s in %s (%s): %s' % (call, fn, owner, obj))

    def remove_statement(self, src, a, stop):
        t = src.text
        ls = t.rfind('\n', 0, a) + 1
        le = t.find('\n', stop)
        le = len(t) if le < 0 else le
        if t[ls:a].strip() == '' and src.masked[stop:le].strip() == '':
            self.add(src.path, ls, le + 1 if le < len(t) else le, '')
        else:
            self.add(src.path, a, stop, '')

    def free_statement(self, src, a, x):
        """`lib::FreeAligned32(x.data);`, `lib::AlignedArray<T>::Free(x.data);` and their `if (x.data) ...` guard:
        the vector destroyed (gone in a destructor)."""
        m = src.masked
        obj = src.text[a:x.start()].strip()
        arrow = x.group(1) == '->'
        acc = re.escape(re.sub(r'\s+', '', m[a:x.end()]))
        before = m[:a]
        f = re.search(r'(?:\blib::FreeAligned32|\blib::AlignedArray\s*<[^;{}]*>\s*::\s*Free)\s*\(\s*$', before)
        if not f:
            # the guard of such a statement
            g = re.search(r'\bif\s*\(\s*$', before)
            if not g:
                return False
            close = skip_space(m, x.end())
            if close >= len(m) or m[close] != ')':
                return False
            rest = m[close + 1:]
            h = re.match(r'\s*(?:lib::FreeAligned32|lib::AlignedArray\s*<[^;{}]*>\s*::\s*Free)\s*\(\s*([^;]*?)\s*\)\s*;',
                         rest)
            if not h or re.sub(r'\s+', '', h.group(1)) != re.sub(r'\s+', '', m[a:x.end()]):
                return False
            if not statement_start(m, g.start()):
                return False
            self.done.add((src.path, close + 1 + h.start(1) + (len(h.group(1)) - len(h.group(1).lstrip()))))
            self.lifetime(src, g.start(), close + 1 + h.end(), obj, arrow, 'destroy')
            self.mark_inner(src, close + 1, close + 1 + h.end())
            return True
        start = f.start()
        # guarded: `if (x.data) <call>;` - handled from the guard
        g = re.search(r'\bif\s*\(\s*' + acc + r'\s*\)\s*$', m[:start])
        if g:
            return True
        close = match_forward(m, m.index('(', start))
        if not statement_start(m, start) or not statement_end(m, close + 1):
            self.note('manual', src, a, 'free in an expression: %s' % obj)
            return True
        semi = skip_space(m, close + 1)
        self.lifetime(src, start, semi + 1, obj, arrow, 'destroy')
        return True

    def mark_inner(self, src, a, b):
        for x in ACCESS.finditer(src.masked, a, b):
            self.done.add((src.path, x.start()))

    def test_context(self, m, a, end):
        """(start, end, negated) of a truth test of the expression [a, end): `if (e)`, `!e`, `e == 0`, `e > 0`, ...;
        None otherwise. negated: the test is for a non-empty array."""
        k = skip_space(m, end)
        cmp = re.match(r'(==|!=|<=|>=|>|<)\s*(0|nullptr|1)\b', m[k:])
        if cmp:
            op, v = cmp.group(1), cmp.group(2)
            stop = k + cmp.end()
            if (op, v) in (('==', '0'), ('==', 'nullptr'), ('<=', '0'), ('<', '1')):
                return a, stop, False
            if (op, v) in (('!=', '0'), ('!=', 'nullptr'), ('>', '0'), ('>=', '1')):
                return a, stop, True
            return None
        p = skip_space_back(m, a - 1)
        if p >= 0 and m[p] == '!' and (p + 1 >= len(m) or m[p + 1] != '='):
            if k < len(m) and m[k] in ')&|;?,':
                return p, end, False
            return None
        after_ok = k < len(m) and (m[k] == ')' or m.startswith('&&', k) or m.startswith('||', k) or m[k] == '?')
        before_ok = p >= 0 and (m[p] == '(' and re.search(r'\b(if|while)\s*\($', m[:p + 1])
                                or m[p - 1:p + 1] in ('&&', '||'))
        if after_ok and before_ok:
            if m[k] == '?' and m[p] == '(' and not re.search(r'\b(if|while)\s*\($', m[:p + 1]):
                return None
            return a, end, True
        return None


# ---- driver -----------------------------------------------------------------------------------------------------

def build():
    os.makedirs(STATE, exist_ok=True)
    cmd = os.path.join(STATE, 'ninja.cmd')
    with open(cmd, 'w') as f:
        f.write('@call "%s\\VC\\Auxiliary\\Build\\vcvars32.bat" >nul\n' % VSROOT)
        f.write('@set PATH=%s\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja;%%PATH%%\n' % VSROOT)
        f.write('@set _MSPDBSRV_ENDPOINT_=ei_%s_verify\n' % os.path.basename(ROOT))
        f.write('@set CL=/diagnostics:column\n')
        f.write('@ninja -C "%s" -k 0\n' % BUILD)
    r = subprocess.run(['cmd', '/c', cmd], capture_output=True, text=True, errors='replace')
    return r.returncode, r.stdout + r.stderr


def run(args):
    os.makedirs(STATE, exist_ok=True)
    paths = files()
    total = 0
    for p in paths:
        text = read(p)
        new, done = retype_text(p, text)
        if new != text:
            write(p, ensure_includes(new))
            total += len(done)
    print('step 1: %d declarations retyped' % total, flush=True)
    manual, review = [], []
    for it in range(args.iterations):
        rc, out = build()
        with open(os.path.join(STATE, 'build.log'), 'w', encoding='utf-8') as f:
            f.write(out)
        diags = [d for d in parse_diagnostics(out) if d['kind'] != 'warning' or d['code'] in ('C4018', 'C4389')]
        sources = {p: Source(p, read(p)) for p in paths}
        fixer = Fixer(sources)
        for d in diags:
            fixer.fix(d)
        count = 0
        for p, edits in fixer.edits.items():
            text, n = apply_edits(read(p), edits)
            count += n
            write(p, ensure_includes(text))
        manual = fixer.manual
        review.extend(fixer.review)
        skipped = sorted({rel(d['file']) for d in diags if rel(d['file']) in SKIP})
        print('  build %d: exit %d, %d diagnostics, %d edits, %d manual%s' % (
            it, rc, len(diags), count, len(manual), (', in skipped files: ' + ' '.join(skipped)) if skipped else ''),
            flush=True)
        if count == 0:
            break
    with open(os.path.join(STATE, 'manual.txt'), 'w', encoding='utf-8') as f:
        f.write(''.join(x + '\n' for x in manual))
    with open(os.path.join(STATE, 'review.txt'), 'w', encoding='utf-8') as f:
        f.write(''.join(x + '\n' for x in review))
    print('done: %d manual, %d to review (build/retype_aligned/)' % (len(manual), len(review)))
    return 0


def list_cmd(args):
    for p in files():
        _, done = retype_text(p, read(p))
        for line, name in done:
            print('%s:%d %s' % (rel(p), line, name))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('command', choices=['run', 'list'])
    ap.add_argument('--iterations', type=int, default=10)
    args = ap.parse_args()
    return list_cmd(args) if args.command == 'list' else run(args)


if __name__ == '__main__':
    sys.exit(main())
