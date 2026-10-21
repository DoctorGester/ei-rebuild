"""Inlines one-statement `__fastcall Fn(T* self, void*, ...)` shims at their calls (C11).

A shim is a free function whose body is a single call or cast-and-call, taking the `this` pointer in
ECX and an unused EDX (`void*`). When every use is a direct call `Fn(x, nullptr, ...)`, the script
substitutes the arguments into the body at each call and removes the definition (and the `// 00xxxxxx`
address comment above it). Left alone: shims registered or passed as pointers, defined in a header and
elsewhere, overloaded, with control flow or more than one statement, returning float / double, or whose
arguments cannot be substituted without changing the order of effects. Spelling only.

usage: python tools/inline_shims.py [NAME_OR_FILE ...] [--apply] [--skip A,B]
  NAME_OR_FILE   only these shims (by name or by defining file); default all
  --apply        write the files (default: a dry run that prints the removed definitions)
  --skip A,B     leave these shims alone (a name, a file, or file:name)
Review `git diff` and build both configurations afterwards.
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srcscan

ROOT = srcscan.ROOT
_ap = argparse.ArgumentParser(description='Inline one-statement __fastcall (T* self, void*, ...) shims.')
_ap.add_argument('only', nargs='*', help='shim names or defining files (default: all)')
_ap.add_argument('--apply', action='store_true', help='write the changes')
_ap.add_argument('--skip', default='', help='comma-separated names, files or file:name to leave alone')
_args = _ap.parse_args()
APPLY = _args.apply
ONLY = _args.only
SKIP = set(x for x in _args.skip.split(',') if x)
BAD_BODY = re.compile(r'bit_cast|__asm|\[\s*[&=]|\bif\b|\bfor\b|\bwhile\b|\bswitch\b|\bdo\b|\{')


def load():
    files = {}
    for d in ('src', 'tests', 'include'):
        for dp, _, names in os.walk(os.path.join(ROOT, d)):
            for n in names:
                if n.endswith(('.cpp', '.h')):
                    p = os.path.join(dp, n)
                    with open(p, encoding='utf-8', errors='surrogateescape', newline='') as f:
                        files[os.path.relpath(p, ROOT).replace(os.sep, '/')] = f.read()
    return files


SHIM = re.compile(r'\b__fastcall\s+(\w+)\s*\(\s*([^(),]*?)\s*,\s*void\s*\*\s*\w*\s*((?:,[^()]*)?)\)\s*(?:const\s*)?\{')


def match_close(code, i, open_c, close_c):
    d = 1
    while i < len(code):
        c = code[i]
        if c == open_c:
            d += 1
        elif c == close_c:
            d -= 1
            if d == 0:
                return i
        i += 1
    return -1


def split_args(s):
    out = []
    depth = 0
    ang = 0
    cur = ''
    i = 0
    instr = None
    while i < len(s):
        c = s[i]
        if instr:
            cur += c
            if c == '\\':
                cur += s[i + 1]
                i += 1
            elif c == instr:
                instr = None
        elif c in '"\'':
            instr = c
            cur += c
        elif c in '([{':
            depth += 1
            cur += c
        elif c in ')]}':
            depth -= 1
            cur += c
        elif c == '<' and i > 0 and (s[i - 1].isalnum() or s[i - 1] == '_'):
            ang += 1
            cur += c
        elif c == '>' and ang > 0 and s[i - 1] != '-':
            ang -= 1
            cur += c
        elif c == ',' and depth == 0 and ang == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += c
        i += 1
    if cur.strip() or out:
        out.append(cur.strip())
    return out


def is_atom(a):
    t = a.strip()
    prev = None
    while prev != t:
        prev = t
        t = re.sub(r'\([^()]*\)|\[[^\[\]]*\]|<[^<>()]*>', '_', t)
    return re.fullmatch(r'[\w:.]+(?:(?:->|\.)[\w:]+|_)*', t) is not None and not t.startswith('(')


def is_unary(a):
    t = a.strip()
    while True:
        m = re.match(r'(?:[&*\-!~]\s*|\([\w:\s*<>,]+\)\s*)', t)
        if not m:
            break
        t = t[m.end():]
    return bool(t) and is_atom(t)


def pure(a):
    if '++' in a or '--' in a.replace('->', ''):
        return False
    return re.fullmatch(r'[\w:.\->&*\s]+|"(?:\\.|[^"\\])*"', a) is not None


def find_defs(files):
    defs = []
    for p, raw in files.items():
        code = srcscan.blank_comments(raw)
        for m in SHIM.finditer(code):
            b = max(code.rfind(';', 0, m.start()), code.rfind('}', 0, m.start()), code.rfind('{', 0, m.start()))
            prefix = code[b + 1:m.start()]
            if 'naked' in prefix or 'template' in prefix or '(' in prefix or '#' in prefix or 'EI_' in prefix:
                continue
            bo = m.end() - 1
            bc = match_close(code, bo + 1, '{', '}')
            if bc < 0:
                continue
            body = code[bo + 1:bc].strip()
            first = b + 1 + (len(prefix) - len(prefix.lstrip()))
            line_start = code.rfind('\n', 0, first) + 1
            if code[line_start:first].strip():
                continue
            after = code[bc + 1:]
            eol = after.find('\n')
            if eol < 0 or after[:eol].strip():
                continue
            end = bc + 1 + eol + 1
            rettype = re.sub(r'\b(inline|static|extern|constexpr|__forceinline)\b', '', prefix).strip()
            params = split_args(m.group(3).strip()[1:]) if m.group(3).strip() else []
            p0 = m.group(2).strip()
            defs.append(dict(path=p, name=m.group(1), rettype=rettype, p0=p0, rest=params,
                             body=body, start=line_start, end=end, sigstart=m.start(), bodystart=bo, bodyend=bc, first=first))
    return defs


def param_name(decl):
    t = decl.strip()
    if re.search(r'[*&]\s*$', t):
        return None
    m = re.search(r'(\w+)\s*(?:\[\d*\])?$', t)
    if not m:
        return None
    if re.fullmatch(r'(?:const\s+)?\w+(?:::\w+)*', t):
        return None  # a bare type
    if re.fullmatch(r'(?:unsigned|const)\s+\w+', t) and m.group(1) in ('int', 'char', 'short', 'long'):
        return None
    return m.group(1)


def analyse(files, defs):
    names = collections.Counter()
    for d in defs:
        names[d['name']] += 1
    byname = collections.defaultdict(list)
    DEFPATHS.clear()
    for d in defs:
        byname[d['name']].append(d)
        DEFPATHS[d['name']].add(d['path'])
    elig = []
    reasons = {}
    for d in defs:
        why = None
        b = d['body']
        if names[d['name']] > 1 and any(x['path'].endswith('.h') for x in byname[d['name']]):
            why = 'header dup'
        elif sum(1 for x in byname[d['name']] if x['path'] == d['path']) > 1:
            why = 'overload'
        elif d['name'] in SKIP or d['path'] in SKIP or (d['path'] + ':' + d['name']) in SKIP:
            why = 'skip'
        elif not b.endswith(';') or b.count(';') != 1 or BAD_BODY.search(b):
            why = 'body'
        if why is None:
            d['hasret'] = bool(re.match(r'return\b', b))
            d['expr'] = re.sub(r'^return\b\s*', '', b[:-1].strip()) if d['hasret'] else b[:-1].strip()
            if not d['hasret'] and d['rettype'] != 'void':
                why = 'nonvoid noreturn'
            if re.search(r'\b(float|double)\b', d['rettype']):
                why = 'float ret'
        if why is None:
            d['pnames'] = [param_name(d['p0']), None] + [param_name(x) for x in d['rest']]
            d['ptypes'] = [d['p0'], 'void*'] + d['rest']
        if why:
            reasons[(d['path'], d['name'])] = why
        else:
            elig.append(d)
    return elig, reasons


DEFPATHS = collections.defaultdict(set)


def scope_paths(files, d):
    other = DEFPATHS[d['name']] - {d['path']}
    return [p for p in files if p not in other]


def find_uses(files, codes, d):
    calls = []
    others = []
    pat = re.compile(r'\b' + re.escape(d['name']) + r'\b')
    for p in scope_paths(files, d):
        code = codes[p]
        if d['name'] not in code:
            continue
        for m in pat.finditer(code):
            s = m.start()
            if p == d['path'] and d['sigstart'] <= s < d['bodystart']:
                continue
            if code[max(0, s - 2):s] == '::' or code[max(0, s - 1):s] == '.' or code[max(0, s - 2):s] == '->':
                others.append((p, s))
                continue
            j = m.end()
            while j < len(code) and code[j] in ' \t':
                j += 1
            if j >= len(code) or code[j] != '(':
                others.append((p, s))
                continue
            c = match_close(code, j + 1, '(', ')')
            if c < 0:
                others.append((p, s))
                continue
            args = split_args(code[j + 1:c])
            if len(args) != len(d['ptypes']) or args[1] not in ('nullptr', '0'):
                others.append((p, s))
                continue
            pre = code[max(0, s - 40):s]
            if re.search(r'__fastcall\s*$', pre):
                others.append((p, s))
                continue
            calls.append((p, s, c + 1, args))
    return calls, others


def build_replacement(d, args, code, s, e):
    expr = d['expr']
    pn = d['pnames']
    uses = {}
    for idx, n in enumerate(pn):
        if n is None:
            continue
        pat = re.compile(r'(?<![\w.:])(?<!->)\b' + re.escape(n) + r'\b')
        uses[idx] = list(pat.finditer(expr))
    impure_order = []
    for idx, a in enumerate(args):
        if idx == 1:
            continue
        ms = uses.get(idx, [])
        if not pure(a):
            if len(ms) != 1:
                return None
            impure_order.append(ms[0].start())
    if impure_order != sorted(impure_order):
        return None
    edits = []
    for idx, ms in uses.items():
        a = args[idx]
        for m in ms:
            before = expr[:m.start()].rstrip()
            rest = expr[m.end():]
            after = rest.lstrip()
            if a == 'this' and after.startswith('->'):
                edits.append((m.start(), m.end() + (len(rest) - len(after)) + 2, ''))
                continue
            star = before.endswith('*') and not re.search(r'[\w)\]]\s*\*$', before) and re.fullmatch(r'&\s*([\w:.]+(?:(?:->|\.)[\w:]+)*)', a)
            if star:
                edits.append((m.start() - (len(expr[:m.start()]) - len(before)) - 1, m.end(), re.fullmatch(r'&\s*(.*)', a).group(1)))
                continue
            amp = re.fullmatch(r'&\s*([\w:.]+(?:(?:->|\.)[\w:]+)*)', a)
            if after.startswith('->') and amp:
                edits.append((m.start(), m.end() + (len(rest) - len(after)) + 2, amp.group(1) + '.'))
                continue
            plain = before[-1:] in ('(', ',', '') and after[:1] in (',', ')', '')
            if before.endswith(')') and not after.startswith(('->', '.', '[')) and is_unary(a):
                plain = True
            repl = a
            if not is_atom(a) and not plain:
                repl = '(' + a + ')'
            edits.append((m.start(), m.end(), repl))
    out = expr
    for st, en, r in sorted(edits, reverse=True):
        out = out[:st] + r + out[en:]
    pb = code[:s].rstrip()
    pc = pb[-1:] if pb else ''
    prevword = re.search(r'(\w+)$', pb)
    na = code[e:].lstrip()[:1]
    isolated = (pc in ('(', ',', ';', '{', '}', '=', '') or (prevword and prevword.group(1) == 'return')) and na in (')', ',', ';')
    stmt = na == ';' and (pc in (';', '{', '}', ')', '') ) and not (prevword and prevword.group(1) == 'return')
    if stmt and d['hasret']:
        out = re.sub(r'^\((?:int|BOOL|unsigned|void\*|CEngineObject\*|char\*)\)\s*', '', out)
    if not is_atom(out) and not isolated:
        out = '(' + out + ')'
    return out


def run():
    files = load()
    total_removed = []
    for it in range(8):
        codes = {p: srcscan.blank_comments(t) for p, t in files.items()}
        defs = find_defs(files)
        elig, reasons = analyse(files, defs)
        if ONLY:
            elig = [d for d in elig if d['name'] in ONLY or d['path'] in ONLY]
        edits = collections.defaultdict(list)
        chosen = collections.defaultdict(list)
        for d in elig:
            calls, others = find_uses(files, codes, d)
            if others:
                continue
            for (p, s, e, args) in calls:
                r = build_replacement(d, args, codes[p], s, e)
                if r is None:
                    continue
                if any(not (e <= a or s >= b) for a, b in chosen[p]):
                    continue
                chosen[p].append((s, e))
                edits[p].append((s, e, r))
        nedits = 0
        for p, es in edits.items():
            t = files[p]
            for s, e, r in sorted(es, reverse=True):
                t = t[:s] + r + t[e:]
                nedits += 1
            files[p] = t
        codes = {p: srcscan.blank_comments(t) for p, t in files.items()}
        defs = find_defs(files)
        elig2, _ = analyse(files, defs)
        if ONLY:
            elig2 = [d for d in elig2 if d['name'] in ONLY or d['path'] in ONLY]
        rem = collections.defaultdict(list)
        nrem = 0
        for d in elig2:
            calls, others = find_uses(files, codes, d)
            if calls or others:
                continue
            t = files[d['path']]
            st = d['start']
            above = []
            q = st
            while q > 0:
                ls = t.rfind('\n', 0, q - 1) + 1
                line = t[ls:q]
                if line.strip().startswith('//'):
                    above.append(ls)
                    q = ls
                else:
                    break
            if above and len(above) <= 2:
                first_line = t[above[-1]:t.find('\n', above[-1])]
                if re.match(r'\s*//\s*0[0-9A-Fa-f]{7}\b', first_line):
                    st = above[-1]
            en = d['end']
            if st > 1 and t[st - 2:st] == '\n\n' and t[en:en + 1] == '\n':
                en += 1
            rem[d['path']].append((st, en, d['name']))
        for p, rs in rem.items():
            t = files[p]
            for s, e, n in sorted(rs, reverse=True):
                t = t[:s] + t[e:]
                nrem += 1
                total_removed.append((p, n))
            files[p] = t
        print(f'pass {it}: {nedits} calls rewritten, {nrem} definitions removed', file=sys.stderr)
        if not nedits and not nrem:
            break
    print('removed', len(total_removed))
    if APPLY:
        for p, t in files.items():
            full = os.path.join(ROOT, p)
            with open(full, encoding='utf-8', errors='surrogateescape', newline='') as f:
                old = f.read()
            if old != t:
                with open(full, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
                    f.write(t)
    for p, n in total_removed:
        print(p, n)


if __name__ == '__main__':
    run()
