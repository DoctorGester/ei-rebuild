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
  x.Construct();      -> in a constructor and for a local gone; else std::construct_at(&O);   (the owner built in
                         raw storage: operator new, then its Construct)
  `x.data = nullptr; x.capacity = 0; x.size = 0;` (a run of such lines, any order) -> the same as x.Construct()
  `x.data = ...Allocate(n); x.capacity = n; x.size = n;` (an inlined SetSize)    -> x.resize(n);
  x.FreeData();  `if (x.data) lib::FreeAligned32(x.data);`  `lib::FreeAligned32(x.data);`  (and A::Free(x.data), A
                 the array's type or an alias)
                      -> in a destructor gone, for a local at the end of its block gone; else lib::FreeStorage(O);
  static_cast<T>(x.size) (the diagnostic at the cast)    -> the field rules on the cast's operand
  a size or offset static_assert that fails             -> gone (the owner's layout changed; review.txt lists them)
What no rule takes is listed in build/retype_aligned/manual.txt; the sites that depend on the aligned array's quirks
(new slots uninitialised, the growth step, the raw storage the owner is built in) in review.txt.
SKIP (files another ticket owns), KEEP (declarations those reach) and KEEP_FILES (files whose declarations stay) are
the script's own lists: empty SKIP once C14a has merged and run it again for the rest.

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

FIX_DIRS = [('src', 'game'), ('src', 'lib'), ('src', 'render'), ('src', 'standalone'), ('src', 'hook'),
            ('tests', 'verify', 'image'), ('tests', 'vectors'), ('tools', 'ui_check'), ('tools', 'perf_check')]
# Files another ticket owns while this runs (C14a's): their aligned arrays stay (and so do the declarations they
# reach, KEEP).
SKIP = {
    'src/game/console.cpp', 'src/game/console.h', 'src/game/ui_chat_overlay.cpp', 'src/game/ui_text.cpp',
    'src/game/ui_hud_unit_info.cpp', 'src/game/scene_loader.cpp', 'src/game/scene_loader.h',
    'src/game/ui_item_info.cpp', 'src/game/ui_item_info.h',
    'src/lib/containers.h',
}
# Declarations that stay aligned arrays: (file, declared name). A skipped file's code reaches them.
KEEP = {
    ('src/game/ui.h', 'UiPtrArray'),               # the windows' pointer arrays (ui_chat_overlay, ui_item_info, ...)
    ('src/game/map_object_figures.h', 'FigurePartArray'),   # SceneSkin::BindMeshes (scene_loader.h)
    ('src/game/figure_mesh.h', 'vertices_'), ('src/game/figure_mesh.h', 'normals_'),   # the scene loader fills them
    ('src/game/figure_mesh.h', 'colorsA_'), ('src/game/figure_mesh.h', 'colorsB_'),
    ('src/game/figure_mesh.cpp', 'vertices'), ('src/game/figure_mesh.cpp', 'packets'),   # LoadVertices, NegateBlock
    ('src/game/sky_object.cpp', 'packets'),        # Row over the figure's packets
    ('src/game/figure_part_draw.cpp', 'ColourArray'), ('src/game/figure_part_draw_plain.cpp', 'ColourArray'),
    ('src/game/link_figure.h', 'colours'), ('src/game/link_figure.h', 'speculars'),   # given the mesh's colours
    ('src/game/link_mesh_draw.cpp', 'ColourArray'),
    ('src/game/party.h', 'PartyItemArray'),        # CUiItemStrip::Setup's UiPtrArray, the harness digest's items_.size
    ('src/game/unit.h', 'weapons'), ('src/game/unit.h', 'quickItems'), ('src/game/unit.h', 'armor'),
    ('src/game/unit.h', 'spells'),                 # the party code's PartyItemArray helpers take them too
    ('src/game/ui_camp.h', 'UiItemList'), ('src/game/world.h', 'items'),
    ('src/game/ui_equip_panels.cpp', 'list'), ('src/game/ui_hero_figure.cpp', 'list'),
    ('src/game/ui_hero_screen_click.cpp', 'list'),
    ('src/game/figure.h', 'parts'),                # CAnimationNode::AttachMeshes(FigurePartArray)
}
# Files whose declarations all stay (the scene loader's family: they work on scene_loader.h's arrays); their uses of
# retyped arrays are fixed.
KEEP_FILES = {'src/game/scene_bones.cpp', 'src/game/scene_meshes.cpp', 'src/game/scene_weld.cpp',
              'src/game/skin_rewind.cpp'}

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
        for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
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
        if qualified or (rel(path), name) in KEEP or rel(path) in KEEP_FILES:
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
# The block's free: the library's, or the array's static Free through its own name or an alias of it.
FREE_CALL = r'(?:\blib::FreeAligned32|\b[\w:]+(?:\s*<[^;{}()]*>)?\s*::\s*Free)'


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


CONTROL = re.compile(r'(if|for|while|switch|catch|return|sizeof|alignof|decltype|static_assert)')
NAME_BEFORE = re.compile(r'(~?\s*\w+(?:\s*::\s*~?\s*\w+)*)\s*$')


def function_name(m, brace):
    """The name of the function whose body opens at `brace` ('~X' a destructor; qualified 'A::B'), else None."""
    i = skip_space_back(m, brace - 1)
    while True:
        w = re.search(r'\b(const|noexcept|override|final)\s*$', m[:i + 1])
        if not w:
            break
        i = skip_space_back(m, w.start() - 1)
    if i < 0 or m[i] not in ')}':
        return None
    initialisers = False
    while True:
        j = match_backward(m, i)
        if j < 0:
            return None
        n = NAME_BEFORE.search(m[:j])
        if not n:
            return None
        name = re.sub(r'\s+', '', n.group(1))
        b = skip_space_back(m, n.start() - 1)
        if b >= 0 and m[b] == ',':
            initialisers = True
            i = skip_space_back(m, b - 1)
            continue
        if b >= 0 and m[b] == ':' and (b == 0 or m[b - 1] != ':') and \
                not re.search(r'\b(public|private|protected)\s*:$', m[:b + 1]) and not CONTROL.fullmatch(name):
            initialisers = True
            i = skip_space_back(m, b - 1)
            w = re.search(r'\bnoexcept\s*$', m[:i + 1])
            if w:
                i = skip_space_back(m, w.start() - 1)
            if i < 0 or m[i] != ')':
                return None
            continue
        if m[i] == '}' and not initialisers:
            return None
        return name


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
        name = function_name(m, brace)
        if name is None or CONTROL.fullmatch(name):
            continue
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
    return None, None


def function_start(m, pos):
    """The offset of the body of the function holding `pos` (its outermost function brace), or 0."""
    depth = 0
    best = 0
    for j in range(pos - 1, -1, -1):
        c = m[j]
        if c == '}':
            depth += 1
        elif c == '{':
            if depth == 0:
                name = function_name(m, j)
                if name is not None and not CONTROL.fullmatch(name):
                    best = j
            else:
                depth -= 1
    return best


def is_local(m, pos, obj):
    """Whether `obj` is a vector declared in the function body holding `pos`, before it."""
    if not re.fullmatch(r'\w+', obj):
        return False
    start = function_start(m, pos)
    return re.search(r'\bstd::vector\s*<[^;]*>\s+' + obj + r'\s*[;{=]', m[start:pos]) is not None


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
        if d['code'] in ('C2607', 'C2338'):
            self.layout_assert(src, d)
            return
        if d['code'] == 'C2440' and "from 'overloaded-function'" in msg:
            self.cast_operand(src, d)
            return
        member = None
        mm = MEMBER_MSG.search(msg)
        if mm:
            member = mm.group(1) or mm.group(2)
        elif d['code'] not in ('C2659', 'C2105', 'C2171', 'C2296', 'C2297', 'C2446', 'C2568', 'C2109', 'C2276', 'C2440'):
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

    def cast_operand(self, src, d):
        """`static_cast<T>(x.size)` (the diagnostic at the cast, not the member): the fields in its operand."""
        m = src.masked
        pos = src.offset(d['line'], d['col'])
        c = re.compile(r'\b(?:static|reinterpret)_cast\s*<').search(m, pos, pos + 20)
        if not c:
            return
        lt = c.end() - 1
        depth, j = 0, lt
        while j < len(m):
            depth += m[j] == '<'
            depth -= m[j] == '>'
            if depth == 0:
                break
            j += 1
        open_ = skip_space(m, j + 1)
        if open_ >= len(m) or m[open_] != '(':
            return
        close = match_forward(m, open_)
        for x in ACCESS.finditer(m, open_, close):
            name = x.group(2)
            after = skip_space(m, x.end())
            if name in FIELDS and not (after < len(m) and m[after] == '(') and (src.path, x.start()) not in self.done:
                self.done.add((src.path, x.start()))
                self.rewrite(src, x)

    def field_group(self, src, a, x, obj, arrow, acc):
        """The run of statements around [a, ...) that set the array's fields by hand (one per line): zeroing them all
        is the vector's construction (gone in a constructor and for a local; else std::construct_at), an inlined
        SetSize (`data = ...Allocate(n); capacity = n; size = n;`) is resize(n). False when the run has no `data`
        store (the field rules take each one)."""
        m, t = src.masked, src.text
        key = ''.join(r'\s*\.\s*' if p == '.' else r'\s*->\s*' if p == '->' else re.escape(p)
                      for p in re.split(r'(\.|->)', re.sub(r'\s+', '', acc)) if p)
        pattern = re.compile(r'[ \t]*' + key + r'(data|capacity|size|growBy)\s*=(?!=)\s*([^;\n]*?)\s*;[ \t]*\n')
        ls = t.rfind('\n', 0, a) + 1
        first = pattern.match(m, ls)
        if not first:
            return False
        starts = [ls]
        while True:   # earlier lines
            prev = t.rfind('\n', 0, starts[0] - 1) + 1
            if prev >= starts[0] or not pattern.match(m, prev):
                break
            starts.insert(0, prev)
        stmts = []
        at = starts[0]
        while True:
            g = pattern.match(m, at)
            if not g:
                break
            stmts.append((at, g.end(), g.group(1), t[g.start(2):g.end(2)].strip()))
            at = g.end()
        fields = {f: v for (_, _, f, v) in stmts}
        if 'data' not in fields or len(fields) != len(stmts):
            return False
        begin, stop = stmts[0][0], stmts[-1][1]
        for s0, s1, _, _ in stmts:
            for y in ACCESS.finditer(m, s0, s1):
                self.done.add((src.path, y.start()))
        indent = re.match(r'[ \t]*', t[begin:]).group(0)
        zero = all(v in ('0', 'nullptr', 'NULL') for v in fields.values())
        if zero:
            fn, owner = enclosing_function(m, a)
            if (fn and owner and fn == owner) or is_local(m, a, obj):
                self.add(src.path, begin, stop, '')
                return True
            self.add(src.path, begin, stop, '%sstd::construct_at(%s);\n' % (indent, obj if arrow else '&' + obj))
            self.note('review', src, a, 'std::construct_at (fields zeroed) in %s (%s): %s' % (fn, owner, obj))
            return True
        if 'Allocate(' in fields['data'] and 'size' in fields:
            n = fields['size']
            self.add(src.path, begin, stop, '%s%sresize(%s);\n' % (indent, acc, n))
            self.note('review', src, a, 'inlined SetSize -> resize(%s): %s' % (n, obj))
            return True
        self.note('manual', src, a, 'fields set by hand: %s %s' % (obj, ', '.join('%s = %s' % kv for kv in fields.items())))
        return True

    def layout_assert(self, src, d):
        """A size or offset assert the vector broke (the owner's layout changed): it goes."""
        m = src.masked
        pos = src.offset(d['line'], d['col'])
        line_start = src.line_starts[d['line'] - 1]
        at = m.rfind('static_assert', 0, pos + 1)
        if at < 0 or at < m.rfind(';', 0, pos) or not re.search(r'\b(sizeof|offsetof)\b', m[at:m.find(';', at)]):
            self.manual.append('%s:%d %s %s' % (rel(src.path), d['line'], d['code'], d['msg'][:160]))
            return
        if (src.path, at) in self.done:
            return
        self.done.add((src.path, at))
        close = match_forward(m, m.index('(', at))
        stop = skip_space(m, close + 1) + 1
        self.note('review', src, at, 'layout assert gone: %s' % re.sub(r'\s+', ' ', src.text[at:stop]))
        self.remove_statement(src, at, stop)

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
        if name in FIELDS and statement_start(m, a) and re.match(r'\s*=(?!=)', m[end:]):
            if self.field_group(src, a, x, obj, arrow, acc):
                return
        if name == 'data':
            k = skip_space(m, end)
            if k < len(m) and m[k] == '[':
                self.add(path, a, k, P)
                return
            if self.free_statement(src, a, x):
                return
            amp = skip_space_back(m, a - 1)
            if re.match(r'\s*=(?!=)', m[end:]) or (m[amp] == '&' and m[amp - 1] != '&'):
                self.note('manual', src, a, 'data stored or its address taken: %s' % obj)
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
        local = is_local(m, a, obj)
        if what == 'construct' and ((fn and owner and fn == owner) or local):
            self.remove_statement(src, a, stop)
            return
        # a local freed as its block ends: its destructor does it
        if what == 'destroy' and local and m[skip_space(m, stop):skip_space(m, stop) + 1] == '}':
            self.remove_statement(src, a, stop)
            return
        semi = stop - 1 if t[stop - 1] == ';' else stop
        if what == 'construct':
            # the owner is built in raw storage (operator new, then its Construct)
            self.add(path, a, semi, 'std::construct_at(%s)' % target)
            self.note('review', src, a, 'std::construct_at in %s (%s): %s' % (fn, owner, obj))
            return
        # the block freed, the vector left empty (its owner's destructor, or none, ends it)
        self.add(path, a, semi, 'lib::FreeStorage(%s)' % ('*' + obj if arrow else obj))
        self.note('review', src, a, 'lib::FreeStorage in %s (%s): %s' % (fn, owner, obj))

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
        f = re.search(FREE_CALL + r'\s*\(\s*$', before)
        if not f:
            # the guard of such a statement
            g = re.search(r'\bif\s*\(\s*$', before)
            if not g:
                return False
            close = skip_space(m, x.end())
            if close >= len(m) or m[close] != ')':
                return False
            rest = m[close + 1:]
            h = re.match(r'\s*' + FREE_CALL + r'\s*\(\s*([^;]*?)\s*\)\s*;', rest)
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
