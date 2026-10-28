"""C14b: the game's STLport vectors and lists to std::vector / std::list, a scripted rewrite (one rule per call shape).

Step 1, retype: every `stlp::vector<E>` / `stlp::list<E>` of src/ and tests/ becomes `std::vector<E>` / `std::list<E>`
unless its element E is one an instantiation copies or destroys by a policy of its own (DENY_VECTOR / DENY_LIST: the
game records of recordvec, the counted and held pointers, the reference holders) or is a template parameter; the
library itself (src/lib/stlp*), the original layouts of the vector recorders and C14a's files are left (SKIP).
A `new (&x) stlp::vector<E>()` becomes std::construct_at(&x).
Step 2, fix: the verify build's diagnostics (MSVC with /diagnostics:column) name every place the std container does not
take what the STLport one took; one rule per shape rewrites it:
  x.ConstructEmpty() / l.Construct()      -> std::construct_at(&x)
  x.FreeStorage() (trivial elements)      -> std::destroy_at(&x)        l.Destroy() / l.Release<E>() the same
  x.CopyConstruct<E>(y)                   -> std::construct_at(&x, y)    x.AssignVector<E>(y) -> x = y
  x.EraseRange<E>(x.begin(), x.end())     -> x.clear()                   other ranges -> x.erase(a, b)
  x.ResizeDefault<E>(n) / Resize<E>(n, v) -> x.resize(n) / x.resize(n, v)
  x.Reserve<E>(n) / PushBack<E>(v) / PushDefault<E>() -> x.reserve(n) / x.push_back(v) / x.emplace_back()
  x.InsertOverflow<E>(x.Finish(), v, 1)   -> x.push_back(v)
  x.FillInsert<E>(end, n, v) / RangeInsert<E>(end or begin, a, b) -> x.insert(x.end(), n, v) / x.insert(pos, a, b)
  x.Start() != x.Finish()                 -> !x.empty()   (== : x.empty())
  x.Finish()[-1] -> x.back()     --x.Finish(); -> x.pop_back();     x.Finish() - x.Start() -> std::ssize(x)
  x.Start()[i] -> x[i]           x.Start() -> x.data()             x.Finish() -> x.data() + x.size()
  x.Capacity() -> x.capacity()
  l.First() / l.End() / l.Empty() -> l.begin() / l.end() / l.empty()     l.PushBack(v) -> l.push_back(v)
  l.head->next -> l.begin()      l.head->prev -> std::prev(l.end())       l.head -> l.end()
  l.Clear<E>() -> l.clear()
  std::list<E>::Node                       -> std::list<E>::iterator (a `Node*` declarator loses its `*`)
  n->data -> *n (n->data.m -> n->m)        n->next / n->prev -> std::next(n) / std::prev(n)   n = n->next -> ++n
  std::list<E>::Insert(l.End(), v)        -> l.insert(l.end(), v)
  an iterator where a pointer was taken    -> x.data() / x.data() + x.size()
The policy E of a rewritten call must be one with the std container's semantics (stlp::PlainElement,
stlp::ObjectElement of std::string / mfc::CString, or an alias of one); any other leaves the site to the hand.
What no rule takes is listed in build/retype_containers/manual.txt (place and message) for the hand.

usage (from a VS x86 prompt; build/verify configured):
  python tools/retype_containers.py run [--iterations N]     step 1 on the files, then step 2 until nothing changes
  python tools/retype_containers.py retype                   step 1 only
  python tools/retype_containers.py list                     the declarations step 1 retypes and keeps
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c14_strings as c14   # noqa: E402  (the source helpers and the diagnostics parser)

ROOT = c14.ROOT
STATE = os.path.join(ROOT, 'build', 'retype_containers')

# Files step 1 leaves alone: the STLport library itself, the vector recorders' copies of game.exe's layouts, and the
# files C14a's worker converts (their string arrays).
SKIP = [
    r'src/lib/stlp[^/]*',
    r'tests/vectors/record_stats\.cpp',
    r'src/game/record_members\.h', r'src/game/record_lifetime\.h',   # the policies' helpers (std overloads by hand)
    r'src/game/console[^/]*', r'src/game/ui_chat_overlay\.cpp', r'src/game/ui_text[^/]*',
    r'src/game/ui_hud_unit_info\.cpp', r'src/game/scene_loader[^/]*', r'src/game/ui_item_info[^/]*',
]

# Elements copied or destroyed by a policy of their own instantiation (record_vectors.h, record_lists.h,
# stlp_vector_refs.h, stlp_list_refs.h and the files' own policies): these containers stay STLport's for now.
DENY_COMMON = {
    'T', 'U', 'R', 'Record', 'Text', 'Ref', 'Pair',
    'RefPtr', 'RefEntry', 'TimedEntry',
}
DENY_VECTOR = DENY_COMMON | {
    # recordvec (record_vectors.h)
    'itemdb::MaterialRecordHead', 'MaterialRecordHead', 'itemdb::WeaponRecord', 'WeaponRecord',
    'itemdb::ArmorRecord', 'ArmorRecord', 'itemdb::QuickItemRecord', 'QuickItemRecord',
    'itemdb::QuestItemRecord', 'QuestItemRecord', 'itemdb::LootItemRecord', 'LootItemRecord',
    'itemdb::SpellRecord', 'SpellRecord', 'itemdb::SpellModifierRecord', 'SpellModifierRecord',
    'itemdb::SpellListRecord', 'SpellListRecord',
    'UnitPrototypeRecord', 'UnitNpcRecord', 'UnitModelRecord', 'UnitLevelRecord', 'SkillRecord', 'SkillLevelRecord',
    'FiguredUnitRecord', 'DressedUnitRecord', 'PrintRecord', 'PrintRecord2', 'SpeechRecord', 'EnvironmentSpeech',
    'acks::EnvironmentSpeech', 'UnitAcks', 'ModelAcks', 'ExtraAcks', 'acks::UnitAcks', 'acks::ModelAcks',
    'acks::ExtraAcks', 'Query', 'net::Query', 'PartyManagerEntry', 'LeverRecord', 'Replicated', 'net::Replicated',
    'CEngineObject*', 'HudPortraitSlot', 'SaveEntry', 'MpAddressEntry', 'TradeRow', 'TradeRecord', 'OptionBinding',
    'ZoneTableEntry', 'QuestRecord', 'QuestBriefingRecord', 'SpellRecipe',
    # counted pointers (stlp::CountedPtrElement) and the files' own record policies
    'ClassTemplate*', 'PartyRecord*', 'CRefObject*', 'MpHeroRecord', 'BriefingSpeaker',
    # the vector recorders' game.exe layouts
    'OriginalSkillRecord', 'OriginalSkillLevelRecord',
}
DENY_LIST = DENY_COMMON | {
    'BloodPrint', 'Footprint', 'GroundPrint', 'TitleLine', 'TradeRecord', 'net::TradeRecord', 'SentItem',
    'SentRecord', 'SentPacket', 'ReliableChunk', 'net::ReliableChunk', 'FreedIds', 'CMemoryStream', 'LinkArchive',
    'recordlist::LinkArchive', 'ChatLine', 'TextSource',
    # held engine objects (stlp::HeldObject, HeldSpell): a reference while listed
    'CMapObject*', 'CSpell*',
}

# Element policies with the std containers' semantics (an alias of one is resolved through ALIAS in the file).
STD_POLICY = re.compile(r'^(?:stlp::)?(?:PlainElement<.*>|ObjectElement<(?:std::string|mfc::CString|Text)>|'
                        r'Element<.*>)$')


def norm(path):
    return os.path.normcase(os.path.abspath(path))


def rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def skipped(path):
    r = rel(path)
    return any(re.fullmatch(p, r) for p in SKIP)


def source_files():
    out = []
    for top in ('src', 'tests'):
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            for name in names:
                if name.endswith(('.cpp', '.h', '.inc')):
                    p = norm(os.path.join(dirpath, name))
                    if not skipped(p):
                        out.append(p)
    return sorted(out)


def read(path):
    return c14.read(path)


def write(path, text):
    c14.write(path, text)


# ---- step 1 ------------------------------------------------------------------------------------------------------

TYPE = re.compile(r'(?<![\w:])stlp::(vector|list)\s*<')


def angle_end(s, i):
    """The index just after the '>' closing the '<' at s[i]."""
    depth = 0
    j = i
    while j < len(s):
        c = s[j]
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
            if depth == 0:
                return j + 1
        elif c in '(;{':
            if c == '(':
                j = c14.match_forward(s, j)
                if j < 0:
                    return -1
            else:
                return -1
        j += 1
    return -1


def element_of(text):
    e = re.sub(r'\s+', ' ', text.strip())
    e = re.sub(r'^const ', '', e)
    e = re.sub(r'\s*\*', '*', e)
    return e


def allowed(kind, element):
    deny = DENY_VECTOR if kind == 'vector' else DENY_LIST
    if element in deny:
        return False
    if element.startswith('typename ') or re.search(r'(?:^|::)Node\*$', element) or element.startswith('stlp::'):
        return False
    return True


def retype_text(text):
    """Step 1 on `text`: (new text, [(kind, element, kept)])."""
    m = c14.mask(text)
    edits = []
    seen = []
    for x in TYPE.finditer(m):
        lt = m.index('<', x.start())
        end = angle_end(m, lt)
        if end < 0:
            continue
        kind = x.group(1)
        element = element_of(text[lt + 1:end - 1])
        ok = allowed(kind, element)
        seen.append((kind, element, ok))
        if ok:
            edits.append((x.start(), x.start() + len('stlp::'), 'std::'))
    text, _ = c14.apply_edits(text, edits)
    # placement news of an empty container
    text = re.sub(r'(?<![\w:])new\s*\(\s*&\s*([^()]*?)\s*\)\s*std::(?:vector|list)\s*<[^;()]*>\s*\(\s*\)',
                  lambda mm: 'std::construct_at(&%s)' % mm.group(1), text)
    return text, seen


SYSTEM_INCLUDE = re.compile(r'^#include <([^>]+)>\n', re.M)


def ensure_includes(text):
    """<vector> / <list> / <memory> / <iterator> where the text names what they declare."""
    m = c14.mask(text)
    need = []
    if 'std::vector<' in m:
        need.append('vector')
    if 'std::list<' in m:
        need.append('list')
    if re.search(r'std::(construct_at|destroy_at)\b', m):
        need.append('memory')
    if re.search(r'std::(ssize|next|prev|distance)\b', m):
        need.append('iterator')
    for header in need:
        if re.search(r'^#include <%s>' % header, text, re.M):
            continue
        line = '#include <%s>\n' % header
        system = list(SYSTEM_INCLUDE.finditer(text))
        placed = False
        for x in system:
            if x.group(1) > header:
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
            text = text[:quoted[-1].end()] + '\n' + line + text[quoted[-1].end():]
        else:
            pragma = re.search(r'^#pragma once\n', text, re.M)
            at = pragma.end() if pragma else 0
            text = text[:at] + '\n' + line + text[at:]
    return text


def retype(files):
    report = []
    changed = 0
    for p in files:
        t = read(p)
        n, seen = retype_text(t)
        report.extend((rel(p),) + s for s in seen)
        if n != t:
            write(p, ensure_includes(n))
            changed += 1
    return changed, report


# ---- step 2 ------------------------------------------------------------------------------------------------------

STD_CONTAINER = r"std::(?:vector|list)<.*>"
NOT_MEMBER = re.compile(r"'(\w+)': is not a member of '(?:const )?(std::(vector|list)<(.*)>|"
                        r"std::_(?:Vector|List)_(?:const_)?iterator<.*>)'")
NOT_MEMBER_ANY = re.compile(r"'(\w+)': is not a member of '(?:const )?(std::[^']*)'")
FROM_ITER = re.compile(r"from '(?:const )?std::_(Vector|List)_(?:const_)?iterator<")
ELEMENT_TRIVIAL = re.compile(r'^(?:const )?(?:(?:unsigned |signed )?(?:char|short|int|long|__int64)|float|double|bool|'
                             r'(?:std::)?u?int\d+_t|(?:std::)?uintptr_t|(?:std::)?size_t|BOOL|DWORD)$|\*$')
# A static_assert of a size or an offset on one line (the containing class's layout changed with the container).
# The sentinel of a list freed by hand after its clear (the destructor inlined).
DEALLOCATE_HEAD = re.compile(r'stlp::deallocate\s*\(\s*(.+?)\s*(\.|->)\s*head\s*,\s*sizeof\s*\([^;]*\)\s*\)')
# The destructors of the STLport instantiations and helpers called on a container: (address).
DESTROY_REF = re.compile(r'records::DestroyVector\s*\(\s*([^;()]+?)\s*\)')
DESTROY_CALL = re.compile(r'(?:'
                          r'stlp::(?:VectorAbi|ListAbi)\s*<[^;()]*>\s*::\s*(?:Destruct|DestroyBase)\s*\(\s*)'
                          r'([^;()]+?)\s*\)')
# A vector's storage freed by hand (~_Vector_base inlined): (receiver, operator).
DEALLOCATE_STORAGE = re.compile(r'stlp::deallocate\s*\(\s*([^;()]+?)\s*(\.|->)\s*(?:begin|Start|data)\s*\(\s*\)\s*,'
                                r'[^;]*EndOfStorage[^;]*\)')
LAYOUT_ASSERT = re.compile(r'^[ \t]*static_assert\s*\((?:[^;\n]*\b(?:sizeof|offsetof)\s*\()[^;\n]*\)\s*;[ \t]*(?://[^\n]*)?\n', re.M)


class Fixer:
    def __init__(self, sources):
        self.sources = sources
        self.edits = {}
        self.manual = []

    def add(self, path, a, b, text):
        self.edits.setdefault(path, []).append((a, b, text))

    def hand(self, d, why):
        self.manual.append('%s(%d,%d): %s: %s' % (rel(d['file']), d['line'], d['col'], why, d['msg'][:220]))

    def fix(self, d):
        src = self.sources.get(d['file'])
        if src is None:
            return
        msg = d['msg']
        mm = NOT_MEMBER.search(msg)
        if mm:
            return self.member(d, src, mm)
        x = FROM_ITER.search(msg)
        if x and x.group(1) == 'Vector':
            return self.iterator_to_pointer(d, src)
        line = src.line_starts[d['line'] - 1]
        line_end = src.masked.find('\n', line)
        for pattern in (DESTROY_REF, DESTROY_CALL, DEALLOCATE_STORAGE):
            y = pattern.search(src.masked, line, line_end)
            if y:
                recv = src.text[y.start(1):y.end(1)].strip()
                if pattern is DESTROY_REF:
                    addr = '&' + recv
                elif pattern is DESTROY_CALL:
                    addr = recv
                    recv = recv[1:].strip() if recv.startswith('&') else '*' + recv
                else:
                    addr = '&' + recv if y.group(2) == '.' else recv
                return self.lifetime(d, src, y.start(), y.end(), 'std::destroy_at(%s)' % addr, recv, 'destroy')
        y = DEALLOCATE_HEAD.search(src.masked, line, line_end)
        if y:
            recv = src.text[y.start(1):y.end(1)].strip()
            addr = '&' + recv if y.group(2) == '.' else recv
            return self.lifetime(d, src, y.start(), y.end(), 'std::destroy_at(%s)' % addr, recv, 'destroy')
        if d['code'] == 'C2338' or 'static assertion failed' in msg:
            # the assertion that failed: the static_assert starting on its line or the one it continues
            m = src.masked
            start = src.line_starts[d['line'] - 1]
            for _ in range(4):
                x = re.match(r'[ \t]*static_assert\s*\(', m[start:])
                if x or start == 0:
                    break
                start = m.rfind('\n', 0, start - 1) + 1
            if x:
                close = c14.match_forward(m, start + x.end() - 1)
                semi = c14.skip_space(m, close + 1)
                body = m[start:close]
                if close > 0 and m[semi] == ';' and re.search(r'\b(?:sizeof|offsetof)\s*\(', body):
                    end = m.find('\n', semi)
                    return self.add(d['file'], start, len(m) if end < 0 else end + 1, '')
        self.hand(d, 'no rule')

    # the member access `.name` / `->name` nearest the diagnostic's column on its line
    def access_at(self, src, d, name):
        m = src.masked
        line_start = src.line_starts[d['line'] - 1]
        line_end = m.find('\n', line_start)
        line_end = len(m) if line_end < 0 else line_end
        col = src.offset(d['line'], d['col'])
        best = None
        for x in re.finditer(r'(\.|->|::)\s*(' + re.escape(name) + r')\b', m[line_start:line_end]):
            pos = line_start + x.start(2)
            dist = abs(pos - col)
            if best is None or dist < best[0]:
                best = (dist, line_start + x.start(), line_start + x.end(1), pos, x.group(1))
        if best is None:
            return None
        return best[1:]   # (op start, op end, name start, op)

    def member(self, d, src, mm):
        name, owner = mm.group(1), mm.group(2)
        found = self.access_at(src, d, name)
        if not found:
            return self.hand(d, 'member not found on the line')
        op_start, op_end, name_start, op = found
        t, m = src.text, src.masked
        if owner.startswith('std::_'):
            return self.iterator_member(d, src, name, op_start, name_start, op)
        kind = mm.group(3)
        element = element_of(mm.group(4).split(',std::allocator')[0])
        if op == '::':
            return self.static_member(d, src, kind, element, name, op_start, name_start)
        rs = c14.postfix_start(m, op_start)
        recv = t[rs:op_start].strip()
        recv_m = m[rs:op_start].strip()
        name_end = name_start + len(name)
        k = c14.skip_space(m, name_end)
        policy = None
        after = k
        if k < len(m) and m[k] == '<':
            e = angle_end(m, k)
            if e < 0:
                return self.hand(d, 'template arguments')
            policy = t[k + 1:e - 1].strip()
            after = c14.skip_space(m, e)
        call = after < len(m) and m[after] == '('
        args = []
        close = after
        if call:
            close = c14.match_forward(m, after)
            args = [t[a:b].strip() for a, b in c14.call_args(m, after)]
            close += 1
        if policy is not None and not self.std_policy(src, policy):
            return self.hand(d, 'policy ' + policy)
        dot = op
        obj = recv if dot == '.' else '*' + recv
        addr = '&' + recv if dot == '.' else recv
        sel = recv + dot

        def put(text, a=rs, b=None):
            self.add(d['file'], a, close if b is None else b, text)

        def same_recv(expr, member):
            e = re.sub(r'\s+', '', expr)
            return e in (re.sub(r'\s+', '', recv) + dot + member + '()',)

        if kind == 'vector':
            if name == 'ConstructEmpty':
                return self.lifetime(d, src, rs, close, 'std::construct_at(%s)' % addr, recv, 'construct')
            if name in ('FreeStorage',):
                loop = self.destroy_loop(m, rs, recv, dot)
                if loop is not None:
                    return self.lifetime(d, src, loop, close, 'std::destroy_at(%s)' % addr, recv, 'destroy')
                if ELEMENT_TRIVIAL.search(element):
                    return self.lifetime(d, src, rs, close, 'std::destroy_at(%s)' % addr, recv, 'destroy')
                return self.hand(d, 'free of non-trivial elements')
            if name == 'CopyConstruct':
                return put('std::construct_at(%s, %s)' % (addr, args[0]))
            if name == 'AssignVector':
                return put('%s = %s' % (obj, args[0]))
            if name == 'EraseRange':
                if len(args) == 2 and same_recv(args[0], 'begin') and same_recv(args[1], 'end'):
                    return put(sel + 'clear()')
                return put(sel + 'erase(%s, %s)' % (args[0], args[1]))
            if name == 'ResizeDefault':
                return put(sel + 'resize(%s)' % args[0])
            if name == 'Resize':
                return put(sel + 'resize(%s)' % ', '.join(args))
            if name == 'Reserve':
                return put(sel + 'reserve(%s)' % args[0])
            if name == 'PushBack':
                return put(sel + 'push_back(%s)' % args[0])
            if name == 'PushDefault':
                return put(sel + 'emplace_back()')
            if name == 'Capacity':
                return put(sel + 'capacity()')
            if name == 'InsertOverflow':
                if len(args) == 3 and args[2] == '1' and (same_recv(args[0], 'Finish') or same_recv(args[0], 'end')):
                    return put(sel + 'push_back(%s)' % args[1])
                return self.hand(d, 'InsertOverflow not at the end')
            if name == 'FillInsert':
                if same_recv(args[0], 'Finish') or same_recv(args[0], 'end'):
                    return put(sel + 'insert(%send(), %s, %s)' % (sel, args[1], args[2]))
                return self.hand(d, 'FillInsert not at the end')
            if name == 'RangeInsert':
                for member, std in (('Finish', 'end'), ('end', 'end'), ('Start', 'begin'), ('begin', 'begin')):
                    if same_recv(args[0], member):
                        return put(sel + 'insert(%s%s(), %s, %s)' % (sel, std, args[1], args[2]))
                return self.hand(d, 'RangeInsert not at an end')
            if name in ('Start', 'Finish', 'EndOfStorage'):
                return self.ends(d, src, recv, dot, name, rs, close)
            return self.hand(d, 'vector member ' + name)
        # list
        if name == 'Construct' and not args:
            return self.lifetime(d, src, rs, close, 'std::construct_at(%s)' % addr, recv, 'construct')
        if name in ('Destroy', 'Release') and not args:
            return self.lifetime(d, src, rs, close, 'std::destroy_at(%s)' % addr, recv, 'destroy')
        if name == 'ClearWith' and len(args) == 1:
            x = re.fullmatch(r'\[[^\]]*\]\s*\(\s*[\w:<>\s]+\*\s*(\w*)\s*\)\s*\{\s*(.*?)\s*\}', args[0], re.S)
            body = x.group(2) if x else None
            v = re.escape(x.group(1)) if x and x.group(1) else None
            if x and (body == '' or v and re.fullmatch(r'(?:' + v + r'\s*->\s*~\w+\s*\(\s*\)|std::destroy_at\s*\(\s*'
                                                     + v + r'\s*\))\s*;', body)):
                return put(sel + 'clear()')
            return self.hand(d, 'ClearWith with its own destruction')
        if name == 'CopyConstruct':
            return put('std::construct_at(%s, %s)' % (addr, args[0]))
        if name == 'Clear' and not args:
            return put(sel + 'clear()')
        if name == 'First':
            return put(sel + 'begin()')
        if name == 'End':
            return put(sel + 'end()')
        if name == 'Empty':
            return put(sel + 'empty()')
        if name == 'PushBack':
            return put(sel + 'push_back(%s)' % args[0])
        if name == 'head' and not call:
            k2 = c14.skip_space(m, name_end)
            for link, text in (('next', sel + 'begin()'), ('prev', 'std::prev(%send())' % sel)):
                x = re.match(r'->\s*' + link + r'\b', m[k2:])
                if x:
                    return put(text, rs, k2 + x.end())
            return put(sel + 'end()', rs, name_end)
        return self.hand(d, 'list member ' + name)

    def lifetime(self, d, src, a, b, text, recv, what):
        """A construction or destruction of the container `recv` by hand: dropped where the compiler now does it (a
        local of the function; a member, `x` or `this->x`, in its class's constructor or destructor), else `text`."""
        m = src.masked
        name = re.fullmatch(r'(?:this\s*->\s*)?(\w+)', recv.strip())
        drop = False
        if name:
            heads = enclosing_heads(m, a)
            function = next((h for h in heads if h[1] == 'function'), None)
            if function and local_declared(m, function[0], a, name.group(1)):
                drop = True
            elif function and not local_declared(m, function[0], a, name.group(1)):
                header = function[2]
                if what == 'destroy' and re.search(r'~\s*\w+\s*\(\s*\)\s*(?:noexcept\s*)?(?:override\s*)?$', header):
                    drop = True
                if what == 'construct' and re.search(r'(?:^|[\s:])(\w+)::\1\s*\(', header) or \
                        what == 'construct' and function[3] and re.match(r'\s*(?:explicit\s+)?' + re.escape(function[3]) + r'\s*\(', header):
                    drop = True
        if not drop:
            return self.add(d['file'], a, b, text)
        # the statement and, when it is alone on its line, the line
        e = c14.skip_space(m, b)
        if e < len(m) and m[e] == ';':
            b = e + 1
        line_start = m.rfind('\n', 0, a) + 1
        line_end = m.find('\n', b)
        line_end = len(m) if line_end < 0 else line_end
        if not m[line_start:a].strip() and not m[b:line_end].strip():
            return self.add(d['file'], line_start, line_end + 1, '')
        return self.add(d['file'], a, b, '')

    def destroy_loop(self, m, rs, recv, dot):
        """The start of a loop just before `rs` destroying each element of `recv` (its storage freed next), or None."""
        head = m[max(0, rs - 600):rs]
        base = rs - len(head)
        r = re.escape(recv.strip())
        d = re.escape(dot)
        first = r'(?:Start|begin|data)\s*\(\s*\)'
        last = r'(?:Finish|end)\s*\(\s*\)|data\s*\(\s*\)\s*\+\s*' + r + d + r'size\s*\(\s*\)'
        pointer = (r'for\s*\(\s*[\w:<>\s]+\*\s*(\w+)\s*=\s*' + r + d + first + r'\s*;\s*\1\s*!=\s*' + r + d +
                   r'(?:' + last + r')\s*;\s*\+\+\s*\1\s*\)\s*\{?\s*(?:std::destroy_at\s*\(\s*\1\s*\)|\1\s*->\s*~\w+\s*\(\s*\))'
                   r'\s*;\s*\}?\s*$')
        ranged = (r'for\s*\(\s*[\w:<>\s]+&\s*(\w+)\s*:\s*' + r + r'\s*\)\s*\{?\s*std::destroy_at\s*\(\s*&\s*\1\s*\)\s*;\s*\}?\s*$')
        for pattern in (pointer, ranged):
            for x in re.finditer(r'for\s*\(', head):
                if re.match(pattern, head[x.start():]):
                    return base + x.start()
        return None

    def std_policy(self, src, policy):
        p = re.sub(r'\s+', '', policy)
        if STD_POLICY.match(p):
            return True
        # a local alias: `using IdElement = stlp::PlainElement<int>;`
        x = re.search(r'\busing\s+' + re.escape(p) + r'\s*=\s*([^;]+);', src.masked)
        if x:
            return bool(STD_POLICY.match(re.sub(r'\s+', '', src.text[x.start(1):x.end(1)])))
        return False

    def ends(self, d, src, recv, dot, name, rs, close):
        t, m = src.text, src.masked
        sel = recv + dot
        obj = recv if dot == '.' else '*' + recv
        other = {'Start': 'Finish', 'Finish': 'Start'}.get(name)
        tail = m[close:close + 200]
        head = m[max(0, rs - 200):rs]
        if name == 'Finish':
            pushed = self.inlined_push_back(d, src, recv, dot, rs)
            if pushed:
                return
            # x.Finish() = x.begin(); (erase(begin, end) inlined)
            x = re.match(r'\s*=\s*' + re.escape(recv_m(recv)) + re.escape(dot) + r'(?:begin|Start)\s*\(\s*\)\s*;', tail)
            if x:
                return self.add(d['file'], rs, close + x.end(), sel + 'clear();')
        if other:
            # x.Start() != x.Finish() / x.Finish() != x.Start() (and ==)
            x = re.match(r'\s*(!=|==)\s*' + re.escape(recv_m(recv)) + re.escape(dot) + other + r'\(\)', tail)
            if x:
                text = ('!' if x.group(1) == '!=' else '') + sel + 'empty()'
                return self.add(d['file'], rs, close + x.end(), text)
        if name == 'Finish':
            x = re.match(r'\[\s*-\s*1\s*\]', tail)
            if x:
                return self.add(d['file'], rs, close + x.end(), sel + 'back()')
            y = re.search(r'--\s*$', head)
            if y and re.match(r'\s*;', tail):
                return self.add(d['file'], rs - (len(head) - y.start()), close, sel + 'pop_back()')
            x = re.match(r'\s*-\s*' + re.escape(recv_m(recv)) + re.escape(dot) + r'Start\(\)', tail)
            if x:
                return self.add(d['file'], rs, close + x.end(), 'std::ssize(%s)' % obj)
        if re.match(r'\s*(?:=(?!=)|\+=|-=|\+\+|--)', tail) or re.search(r'(?:\+\+|--)\s*$', head):
            return self.hand(d, name + ' written')
        if name == 'Start':
            x = re.match(r'\[', tail)
            if x:
                return self.add(d['file'], rs, close, obj if dot == '.' else '(%s)' % obj)
            return self.add(d['file'], rs, close, sel + 'data()')
        if name == 'Finish':
            return self.add(d['file'], rs, close, '(%sdata() + %ssize())' % (sel, sel)
                            if re.match(r'\s*(?:\[|->|\.)', tail) else '%sdata() + %ssize()' % (sel, sel))
        return self.hand(d, name)

    def inlined_push_back(self, d, src, recv, dot, rs):
        """push_back inlined: `if (x.Finish() != x.EndOfStorage()) { construct at Finish; ++x.Finish(); } else
        { x.InsertOverflow<E>(x.Finish(), v, 1); }` (either branch first) -> `x.push_back(v);`."""
        t, m = src.text, src.masked
        k = c14.skip_space_back(m, rs - 1)
        if k < 0 or m[k] != '(':
            return False
        j = c14.skip_space_back(m, k - 1)
        if m[j - 1:j + 1] != 'if' or (j >= 2 and (m[j - 2].isalnum() or m[j - 2] == '_')):
            return False
        if_start = j - 1
        cond_end = c14.match_forward(m, k)
        r = re.escape(recv_m(recv)) + re.escape(dot)
        cond = re.sub(r'\s+', '', m[k + 1:cond_end])
        if not re.fullmatch(r + r'Finish\(\)(!=|==)' + r + r'EndOfStorage\(\)', cond):
            return False
        b1 = c14.skip_space(m, cond_end + 1)
        if m[b1] != '{':
            return False
        e1 = c14.match_forward(m, b1)
        x = re.match(r'\s*else\s*\{', m[e1 + 1:])
        if not x:
            return False
        b2 = e1 + 1 + x.end() - 1
        e2 = c14.match_forward(m, b2)
        overflow = m[b1:e1 + 1] if '==' in cond else m[b2:e2 + 1]
        y = re.search(r'InsertOverflow\s*(<[^;]*>)?\s*\(', overflow)
        if not y:
            return False
        base = (b1 if '==' in cond else b2) + y.end() - 1
        args = [t[a:b].strip() for a, b in c14.call_args(m, base)]
        if len(args) != 3 or args[2] != '1':
            return False
        policy = y.group(1)
        if policy and not self.std_policy(src, policy[1:-1]):
            return False
        self.add(d['file'], if_start, e2 + 1, '%s%spush_back(%s);' % (recv, dot, args[1]))
        return True

    def static_member(self, d, src, kind, element, name, op_start, name_start):
        t, m = src.text, src.masked
        name_end = name_start + len(name)
        if name == 'Node' and kind == 'list':
            # the node type: an iterator (a pointer declarator after it loses its `*`)
            k = c14.skip_space(m, name_end)
            end = name_end
            if k < len(m) and m[k] == '*':
                end = k + 1
            return self.add(d['file'], op_start, end, '::iterator' + (' ' if end != name_end else ''))
        if name == 'Insert' and kind == 'list':
            k = c14.skip_space(m, name_end)
            policy = None
            if k < len(m) and m[k] == '<':
                e = angle_end(m, k)
                policy = t[k + 1:e - 1]
                k = c14.skip_space(m, e)
            if policy and not self.std_policy(src, policy):
                return self.hand(d, 'policy ' + policy)
            if m[k] != '(':
                return self.hand(d, 'Insert')
            close = c14.match_forward(m, k)
            args = [t[a:b].strip() for a, b in c14.call_args(m, k)]
            start = c14.postfix_start(m, op_start)   # the qualified type name
            start = self.type_start(m, op_start)
            x = re.fullmatch(r'(.*?)\s*(\.|->)\s*(?:End|end)\s*\(\s*\)', args[0]) or \
                re.fullmatch(r'(.*?)\s*(\.|->)\s*head', args[0])
            if not x or len(args) != 2:
                return self.hand(d, 'Insert at a node')
            sel = x.group(1) + x.group(2)
            return self.add(d['file'], start, close + 1, '%sinsert(%send(), %s)' % (sel, sel, args[1]))
        return self.hand(d, 'static ' + name)

    def type_start(self, m, op_start):
        """The start of the qualified template name before `::` at op_start (`std::list<T>`)."""
        i = c14.skip_space_back(m, op_start - 1)
        if m[i] == '>':
            depth = 0
            while i >= 0:
                if m[i] == '>':
                    depth += 1
                elif m[i] == '<':
                    depth -= 1
                    if depth == 0:
                        break
                i -= 1
            i -= 1
        while i >= 0 and (m[i].isalnum() or m[i] in '_:'):
            i -= 1
        return i + 1

    def iterator_member(self, d, src, name, op_start, name_start, op):
        t, m = src.text, src.masked
        rs = c14.postfix_start(m, op_start)
        recv = t[rs:op_start].strip()
        name_end = name_start + len(name)
        if op != '->':
            return self.hand(d, 'iterator member through .')
        if name == 'data':
            k = c14.skip_space(m, name_end)
            x = re.match(r'(\.|->)\s*(\w+)', m[k:])
            if x and x.group(1) == '.':
                return self.add(d['file'], rs, k + x.start(2), recv + '->')
            if x and x.group(1) == '->':
                return self.add(d['file'], rs, k + x.start(2), '(*%s)->' % recv)
            return self.add(d['file'], rs, name_end, '(*%s)' % recv)
        if name in ('next', 'prev'):
            head = m[max(0, rs - 120):rs]
            x = re.search(r'(\w+)\s*=\s*$', head)
            if name == 'next' and x and x.group(1) == recv:
                return self.add(d['file'], rs - (len(head) - x.start()), name_end, '++' + recv)
            if name == 'prev' and x and x.group(1) == recv:
                return self.add(d['file'], rs - (len(head) - x.start()), name_end, '--' + recv)
            return self.add(d['file'], rs, name_end, 'std::%s(%s)' % (name, recv))
        return self.hand(d, 'iterator member ' + name)

    def iterator_to_pointer(self, d, src):
        t, m = src.text, src.masked
        line_start = src.line_starts[d['line'] - 1]
        line_end = m.find('\n', line_start)
        seg = m[line_start:line_end]
        hits = list(re.finditer(r'(\.|->)\s*(begin|end)\s*\(\s*\)', seg))
        if not hits:
            return self.hand(d, 'iterator to pointer: no begin/end')
        for x in hits:
            a = line_start + x.start()
            rs = c14.postfix_start(m, a)
            recv = t[rs:a].strip()
            sel = recv + x.group(1)
            text = sel + 'data()' if x.group(2) == 'begin' else '%sdata() + %ssize()' % (sel, sel)
            self.add(d['file'], rs, line_start + x.end(), text)


def enclosing_heads(m, pos):
    """The braces enclosing `pos`, innermost first: (offset of '{', kind, head text, class name) with kind 'function'
    for a function body (its head ends with `)` and qualifiers, or a constructor's initialiser list), 'class' for a
    class body, 'block' for the rest."""
    out = []
    depth = 0
    i = pos - 1
    while i >= 0:
        c = m[i]
        if c == '}':
            depth += 1
        elif c == '{':
            if depth == 0:
                start = max(m.rfind(';', 0, i), m.rfind('}', 0, i), m.rfind('{', 0, i)) + 1
                head = m[start:i].strip()
                kind = 'block'
                cls = None
                if re.search(r'\b(class|struct|union)\s+\w+[^()]*$', head):
                    kind = 'class'
                else:
                    j = c14.skip_space_back(m, i - 1)
                    while True:   # qualifiers after the parameter list
                        x = re.search(r'\b(?:const|noexcept|override|final|mutable)\s*$', m[max(0, j - 12):j + 1])
                        if not x:
                            break
                        j = c14.skip_space_back(m, j - len(x.group(0).rstrip()))
                    if j >= 0 and m[j] == ')':
                        o = c14.match_backward(m, j)
                        k = c14.skip_space_back(m, o - 1)
                        word = re.search(r'(\w+)$', m[max(0, k - 40):k + 1])
                        if k >= 0 and m[k] == ']':
                            kind = 'block'   # a lambda
                        elif word and word.group(1) in ('if', 'for', 'while', 'switch', 'catch'):
                            kind = 'block'
                        else:
                            kind = 'function'
                out.append([i, kind, head, cls])
            else:
                depth -= 1
        i -= 1
    # a function inside a class body: its class's name, for an inline constructor
    for k, h in enumerate(out):
        if h[1] == 'function':
            for outer in out[k + 1:]:
                if outer[1] == 'class':
                    x = re.search(r'\b(?:class|struct|union)\s+(\w+)', outer[2])
                    h[3] = x.group(1) if x else None
                    break
            h[2] = h[2].split('\n')[-1] if False else h[2]
    return [tuple(h) for h in out]


def local_declared(m, body, pos, name):
    """Whether `name` is declared as a local object (not a reference or pointer) in the function body from `body`
    up to `pos`."""
    seg = m[body:pos]
    pattern = (r'(?:^|[;{}])\s*(?:const\s+)?(?:[\w:]+(?:\s*<[^;{}()]*>)?(?:\s*::\s*\w+)*)\s+' + re.escape(name) +
               r'\s*(?:;|=|\{|\()')
    return re.search(pattern, seg) is not None


def recv_m(recv):
    return re.sub(r'\s+', '', recv)


ITER_ALIAS = re.compile(r'\busing\s+(\w+)\s*=\s*(?:typename\s+)?([\w:]+)\s*(?:<[^;]*>)?\s*::\s*iterator\s*;')
LIST_ALIAS = re.compile(r'\busing\s+(\w+)\s*=\s*std::list\s*<')


def scope_end(m, pos):
    """The end of the brace scope enclosing `pos` (the file's end at namespace or file level of a header)."""
    depth = 0
    for i in range(pos, len(m)):
        if m[i] == '{':
            depth += 1
        elif m[i] == '}':
            depth -= 1
            if depth < 0:
                return i
    return len(m)


def alias_pointers(files):
    """A list node alias that became an iterator (`using X = std::list<T>::iterator;`, once `...::Node`): its
    pointer declarators were node pointers, iterators by value now (`X*` -> `X`, `X**` -> `X*`). A header's
    alias (not named Node) in every file, a local one up to the end of its scope."""
    texts = {p: read(p) for p in files}
    global_names = set()
    local = {}
    lists = {'std::list'}
    for p, t in texts.items():
        lists.update(x.group(1) for x in LIST_ALIAS.finditer(c14.mask(t)))
    for p, t in texts.items():
        m = c14.mask(t)
        for x in ITER_ALIAS.finditer(m):
            if x.group(2) not in lists:
                continue
            name = x.group(1)
            if p.endswith('.h') and name != 'Node':
                global_names.add(name)
            else:
                local.setdefault(p, []).append((name, x.end(), scope_end(m, x.end())))
    changed = 0
    for p, t in texts.items():
        m = c14.mask(t)
        edits = []
        spans = [(n, 0, len(m)) for n in global_names] + local.get(p, [])
        for name, a, b in spans:
            for x in re.finditer(r'(?<![\w:])' + re.escape(name) + r'(\s*)\*', m[a:b]):
                if a + x.start() > 0 and m[a + x.start() - 1] == '.':
                    continue
                edits.append((a + x.end() - 1, a + x.end(), ''))
        if edits:
            n, _ = c14.apply_edits(t, edits)
            write(p, n)
            changed += 1
    return changed


def build():
    return c14.build()


def run(args):
    os.makedirs(STATE, exist_ok=True)
    files = source_files()
    if not args.no_retype:
        changed, report = retype(files)
        kept = sorted({(k, e) for _, k, e, ok in report if not ok})
        print('step 1: %d files retyped, %d uses, %d kept (%d element types)' % (
            changed, sum(1 for r in report if r[3]), sum(1 for r in report if not r[3]), len(kept)), flush=True)
    manual = []
    for it in range(args.iterations):
        alias_pointers(files)
        rc, out = build()
        with open(os.path.join(STATE, 'build.log'), 'w', encoding='utf-8') as f:
            f.write(out)
        diags = [d for d in c14.parse_diagnostics(out) if d['kind'] != 'note']
        sources = {p: c14.Source(p, read(p)) for p in files}
        fixer = Fixer(sources)
        for d in diags:
            if d['kind'] == 'error' or d['code'].startswith('C4'):
                fixer.fix(d)
        count = 0
        for p, edits in fixer.edits.items():
            text, n = c14.apply_edits(read(p), edits)
            count += n
            write(p, ensure_includes(text))
        manual = fixer.manual
        with open(os.path.join(STATE, 'manual.txt'), 'w', encoding='utf-8') as f:
            f.write(''.join(x + '\n' for x in manual))
        print('  build %d: exit %d, %d diagnostics, %d edits, %d for the hand' % (
            it, rc, len(diags), count, len(manual)), flush=True)
        if rc == 0 and not diags:
            break
        if count == 0:
            break
    print('done: %d diagnostics for the hand (build/retype_containers/manual.txt)' % len(manual))
    return 0 if not manual else 1


def list_cmd(args):
    files = source_files()
    rows = {}
    for p in files:
        _, seen = retype_text(read(p))
        for kind, element, ok in seen:
            key = (kind, element, ok)
            rows[key] = rows.get(key, 0) + 1
    for (kind, element, ok), n in sorted(rows.items(), key=lambda r: (r[0][2], r[0][0], r[0][1])):
        print('%-6s %-5s %4d  %s' % ('retype' if ok else 'keep', kind, n, element))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--iterations', type=int, default=12)
    r.add_argument('--no-retype', action='store_true', help='step 2 only (on the files as they are)')
    sub.add_parser('retype')
    sub.add_parser('list')
    args = ap.parse_args()
    if args.cmd == 'run':
        return run(args)
    if args.cmd == 'retype':
        changed, report = retype(source_files())
        print('%d files retyped' % changed)
        return 0
    return list_cmd(args)


if __name__ == '__main__':
    sys.exit(main())
