"""Ratchet on the clean-code patterns (docs/rewrite-guide.md, "Clean code").

New code is written the clean way: members instead of raw offsets, virtual calls instead of VCall,
named globals and plain C++ floats. This counts, per file under src/, the patterns the cleanup
removes and fails when a file has more than config/cleanup_baseline.csv allows (a new file: none).
Counts may only go down; run with --update after a cleanup to lower the baseline.

usage: python tools/check_cleanup.py [--update]
"""
import collections, csv, glob, os, re, sys

# Hand-written vector maths (docs/notes/float-maths.md, "Vector helpers"; the helpers are in src/lib/vecmath.h).
# 1. A two-product sum `P * Q + R * S` whose operand pairs are the same expression on another axis:
#    a.x * b.x + a.y * b.y, dx * dx + dy * dy, v[0] * w[0] + v[1] * w[1], m[8] * nx + m[4] * ny,
#    D(dz) * dz + D(dy) * dy (one wrapper call or static_cast<T>(...) is looked through). "Another
#    axis" = equal after every x/y/z/w letter and every digit run is blanked, but not identical, for
#    both pairs (P~R and Q~S, or P~S and Q~R). A 3-term sum counts once (the scan resumes after the
#    first two products); a 4-term matrix row counts twice. Not counted: differences (cross
#    products), scalar weights (`a * s + b * t`), squares of unrelated names (`a * a + b * b`).
# 2. A non-const `float name[3]`, `[16]` or `[4][4]` (a vector or matrix that should be Vector3 /
#    Matrix4), and `float name[4]` only when the name says quaternion or homogeneous point; names
#    that say colour, stats, sound or unknown bytes are skipped, as are const tables. Also skipped:
#    `block`, the SoA vertex packets of render/mesh_records.h (VertexPacket: rows x[4], y[4], z[4], w[4] of four
#    vertices, indexed by row and lane; not a matrix) and plurals ending in `distances` (one scalar per
#    triangle corner or per point, not the components of a vector).
_VEC_OPERAND = r'(?:\w+\s*(?:<\s*\w+\s*>\s*)?\(\s*[\w.\[\]>-]+\s*\)|\w+(?:\s*(?:\.|->)\s*\w+|\s*\[\s*\w+\s*\])*)'
_VEC_SUM = re.compile(r'(?<![\w.>\]])(' + _VEC_OPERAND + r')\s*\*\s*(' + _VEC_OPERAND + r')\s*\+\s*(' +
                      _VEC_OPERAND + r')\s*\*\s*(' + _VEC_OPERAND + r')(?![\w(\[])')
_VEC_WRAPPER = re.compile(r'^\w+(?:<\w+>)?\((.*)\)$')
_VEC_ARRAY = re.compile(r'(?<!const )(?<!constexpr )\bfloat\s+(\w+)\s*\[\s*(3|4|16)\s*\](\s*\[\s*\d+\s*\])?')
_VEC_NOT_VECTOR = re.compile(r'(?i)colou?r|rgb|tint|fog|^red$|stat|skill|attack|defen|absorp|reverb|unknown_|weight|^block$|distances$')
_VEC_QUATERNION = re.compile(r'(?i)quat|orient|rotat|turn|xyzw|^q$|projected')


def _other_axis(a, b):
    a, b = (re.sub(r'\s+', '', s) for s in (a, b))
    a, b = ((m.group(1) if (m := _VEC_WRAPPER.match(s)) else s) for s in (a, b))
    blank = lambda s: re.sub(r'[xyzwXYZW]', '#', re.sub(r'[0-9]+', '#', s))
    return a != b and blank(a) == blank(b)


def count_vector_maths(text):
    n = 0
    for m in _VEC_SUM.finditer(text):
        p, q, r, s = m.groups()
        if (_other_axis(p, r) and _other_axis(q, s)) or (_other_axis(p, s) and _other_axis(q, r)):
            n += 1
    for m in _VEC_ARRAY.finditer(text):
        name, size, second = m.groups()
        if second and not (size == '4' and re.sub(r'\D', '', second) == '4'):
            continue
        if _VEC_NOT_VECTOR.search(name) or (size == '4' and not second and not _VEC_QUATERNION.search(name)):
            continue
        n += 1
    return n


# A data member of a class or struct without a default initialiser (docs/rewrite-guide.md, "Lifetimes"; MSVC's
# C26495 by source scan): a member of a fundamental type, a pointer, an enum or an array of them, declared with no
# `= ...` or `{...}`; a function pointer too. Class-type members construct themselves; union members (and the
# structs inside a union), static and const ones, references and members under an #if inside the class are not counted.
_FIELD_CLASS = re.compile(r'\b(struct|class|union)\s+(?:alignas\s*\([^)]*\)\s*)?(\w+)?\s*(?:final\s*)?(?::[^{;()]*)?\{')
_FIELD_SKIP = re.compile(r'^(?:using|typedef|friend|static|enum|struct|class|union|template|return|operator|~|'
                         r'virtual|explicit|inline|constexpr|#|public|private|protected)\b')
_FIELD_SCALAR = re.compile(r'^(?:(?:volatile|mutable|const)\s+)*(?:(?:unsigned|signed)\s+)?(?:(?:std::)?(?:u?int\d+_t|'
                           r'size_t|ptrdiff_t|uintptr_t|intptr_t)|bool|char|short|int|long|float|double|wchar_t|'
                           r'char8_t|char16_t|char32_t|unsigned|BOOL|DWORD|WORD|BYTE|UINT|LONG|HRESULT)\b')
_FIELD_DECL = re.compile(r'^(.*?)([\s*]+)(\w+)\s*((?:\[[^\]]*\]\s*)*)(?::\s*\w+\s*)?$', re.S)
_FIELD_FNPTR = re.compile(r'^[\w:<>,\s*&]+\(\s*(?:\w+::)?\*\s*\w+\s*\)\s*\(.*\)\s*(?:const\s*)?$')
_scalar_names = None   # names of enums, function pointers and aliases of scalars


def _scalar_names_all():
    global _scalar_names
    if _scalar_names is None:
        _scalar_names = set()
        for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
            if path.endswith(('.cpp', '.h', '.inc')):
                text = open(path, encoding='utf-8', errors='replace').read()
                _scalar_names.update(re.findall(r'\benum\s+(?:class\s+|struct\s+)?(\w+)\s*[:{]', text))
                # `using Handler = bool (*)(Call*);`: a function pointer type is as indeterminate as a pointer.
                _scalar_names.update(re.findall(r'\busing\s+(\w+)\s*=\s*[^;{]*\(\s*(?:\w+::)?\*\s*\)', text))
                # `using Id = std::uint32_t;`: a scalar under another name.
                _scalar_names.update(re.findall(
                    r'\busing\s+(\w+)\s*=\s*(?:unsigned\s+|signed\s+)?(?:std::)?(?:u?int\d+_t|size_t|bool|char|short|int|long|float|double)\s*;', text))
    return _scalar_names


def _closing_brace(text, i):
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return j
    return len(text) - 1


_FN_QUALIFIER = re.compile(r'\b(?:const|noexcept|override|final|volatile)\s*$')


def _opens_function_body(head):
    """The text before a `{` in a class body (nested braces already collapsed to `{}`) is a function's declarator:
    a `(` before any `=`, and it does not end in a member's name (`Type name{...}`, `: member{...}`)."""
    head = re.sub(r'\boperator\s*[^\w\s(]+|\boperator\s*\(\s*\)', 'operator', head).strip()
    if '(' not in head or ('=' in head and head.index('=') < head.index('(')):
        return False
    if re.search(r'\b(?:struct|class|union|enum)\b[^()]*$', head):
        return False
    return not re.search(r'\w$', head) or bool(_FN_QUALIFIER.search(head))


def _class_statements(text, lo, hi):
    """The class body's statements that end in ';' (a function body ends its statement; nested types and brace
    initialisers stay inside the one they belong to)."""
    out, buf, i = [], '', lo
    while i < hi:
        c = text[i]
        if c == '{':
            j = _closing_brace(text, i)
            if _opens_function_body(buf):
                buf = ''
            else:
                buf += '{}'
            i = j + 1
            continue
        if c == ';':
            out.append(buf)
            buf = ''
        elif c == ':' and text[i + 1:i + 2] != ':' and text[i - 1:i] != ':' and                 re.search(r'\b(public|private|protected)\s*$', buf):
            buf = ''
        else:
            buf += c
        i += 1
    return out


def _uninitialised(statement):
    body = re.sub(r'^\[\[[^\]]*\]\]\s*', '', ' '.join(statement.split()))
    if not body or _FIELD_SKIP.match(body):
        return 0
    if '=' not in body and '{' not in body and _FIELD_FNPTR.match(body):
        return 1
    if any(c in body for c in '(={&'):
        return 0
    parts, depth, cur = [], 0, ''
    for c in body:
        if c in '<[':
            depth += 1
        elif c in '>]':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += c
    parts.append(cur)
    m = _FIELD_DECL.match(parts[0])
    if not m or not m.group(1).strip():
        return 0
    base = m.group(1).strip()
    words = base.split('::')[-1].split()
    scalar = _FIELD_SCALAR.match(base) or (words and words[-1] in _scalar_names_all())
    array = re.match(r'(?:std::)?array\s*<\s*([^<>]*?)\s*,[^<>]*>$', base)     # a std::array of them leaves them too
    if array:
        element = array.group(1)
        scalar = _FIELD_SCALAR.match(element) or '*' in element or element.split('::')[-1] in _scalar_names_all()
    n = 0
    for i, part in enumerate(parts):
        pointer = '*' in m.group(2) if i == 0 else part.strip().startswith('*')
        if i == 0 and (re.search(r'\*\s*const\b', part) or (base.startswith('const ') and not pointer)):
            continue
        n += bool(pointer or scalar)
    return n


_DIRECTIVE = re.compile(r'^[ \t]*#(?:[^\n]*\\\n)*[^\n]*', re.M)


def count_uninitialised_fields(text):
    text = re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\'', '""', text)
    text = _DIRECTIVE.sub('', text)     # the members under an #if count (both branches)
    n = 0
    unions = [(m.end() - 1, _closing_brace(text, m.end() - 1)) for m in _FIELD_CLASS.finditer(text)
              if m.group(1) == 'union']
    for m in _FIELD_CLASS.finditer(text):
        if m.group(1) == 'union' or re.search(r'\benum\s*$', text[max(0, m.start() - 12):m.start()]):
            continue
        if any(lo < m.start() < hi for lo, hi in unions):     # a view of the union's bytes
            continue
        start = m.end() - 1
        for statement in _class_statements(text, start + 1, _closing_brace(text, start)):
            n += _uninitialised(statement)
    return n


# An enum never used as a type (style round 3, rule Y3): its name appears once in all of src/, in its definition. A field
# or parameter that holds a kind takes the enum, so a literal does not compile; an enum that no declaration names is
# dead or the code still passes ints and writes the codes as literals (its enumerators may be used one by one).
_ENUM = re.compile(r'\benum\s+(class\s+|struct\s+)?(\w+)\s*(?::\s*[\w:\s]+?)?\s*\{([^{}]*)\}')
_IDENTIFIER = re.compile(r'[A-Za-z_]\w*')
_words = None   # every identifier of src/ (comments and strings left out) with how often it occurs


def _word_counts():
    global _words
    if _words is None:
        _words = collections.Counter()
        for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
            if path.endswith(('.cpp', '.h', '.inc')):
                with open(path, encoding='utf-8', errors='replace') as f:
                    text = strip_comments(f.read())
                _words.update(_IDENTIFIER.findall(re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)))
    return _words


def count_unused_enums(text):
    words = _word_counts()
    return sum(1 for m in _ENUM.finditer(_DIRECTIVE.sub('', text)) if words[m.group(2)] <= 1)


# A scope block that copies the original's temporaries (rule Y9): `{` on a line of its own after a statement, a local
# of a value type (std::string, a class, auto) initialised from an expression, then at most three statements, no
# nested block and no return. { std::string path = folder + "x.res"; opened = resource.Open(path); } is
# `opened = resource.Open(folder + "x.res");`. Where the local is an RAII object (a lock, a stream: declared without
# `=`) or the block ends a lifetime on purpose, it is not counted.
_TEMPORARY_BLOCK = re.compile(
    r'^[ \t]*\{[ \t]*\n'
    r'[ \t]*(?:const[ \t]+)?(?:std::string(?:_view)?|[A-Z]\w*(?:::\w+)*|auto)[ \t]+\w+[ \t]*=[^;{}\n]*;[ \t]*\n'
    r'(?:[ \t]*[^{}\n]*;[ \t]*\n){0,3}'
    r'[ \t]*\}', re.M)


def count_temporary_blocks(text):
    return sum(1 for m in _TEMPORARY_BLOCK.finditer(text)
               if text[:m.start()].rstrip()[-1:] in (';', '{', '}') and 'return' not in m.group(0))


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'config', 'cleanup_baseline.csv')
PATTERNS = {
    # Also a file's own offset helper (a Get<T>(base, offset) once read Spell at the original's offsets after Spell
    # had become a normal class): a typed call with a hex offset, `Get<float>(spell, 0x40)`, and a byte pointer plus
    # an offset parameter, `static_cast<std::uint8_t*>(object) + offset`.
    'offset access (At<>/Field<>)': re.compile(
        r'\b(?:At|Field)\s*<'
        r'|\b\w+\s*<\s*[\w:]+\s*\*?\s*>\s*\(\s*[^,();]+,\s*0x[0-9A-Fa-f]+\s*\)'
        r'|(?:char|uint8_t|byte)\s*\*\s*>\s*\(\s*\w+\s*\)\s*\+\s*offset\b'),
    'VCall slot call': re.compile(r'\bVCall\b'),
    '__asm block': re.compile(r'\b__asm\b'),
    'float_control pragma': re.compile(r'#\s*pragma\s+float_control'),
    'raw game address (0x004..0x007 literal)': re.compile(r'\b0x00[4-7][0-9A-Fa-f]{5}\b(?!\s*\))'),
    '*At view struct': re.compile(r'\bstruct\s+\w+At\b'),
    # Ways around the rules above: a virtual called through a placeholder name (SlotNN()) instead of a
    # named slot, and byte arithmetic on a pointer with a literal offset instead of a member.
    'placeholder virtual (SlotNN)': re.compile(r'\bSlot\d+\s*\('),
    'byte offset arithmetic': re.compile(r'(?:char|uint8_t|byte)\s*\*\s*>\s*\([^()]*(?:\([^()]*\))?[^()]*\)\s*\+\s*0x'),
    # A container layout declared in a game file (struct FooArray { ... }, struct BarList : ...)
    # instead of std containers (std::vector / std::list, lib::HashMap).
    # Definitions only; src/lib itself is not counted.
    'per-file container struct': re.compile(r'(?<!template )\bstruct\s+\w*(?:Array|List)\b(?=\s*[:{])'),
    # The same for vector / map / set / hash map layouts declared per
    # file. Some matches are grids or managers named *Map; those go when they are renamed.
    'per-file vector/map/set struct': re.compile(r'(?<!template )\bstruct\s+\w*(?:Vector|Map|Set)\b(?=\s*[:{])'),
    # Vector maths written out instead of the vecmath.h helpers and types (count_vector_maths above).
    'hand-written vector maths': count_vector_maths,
    # A member (or local) still named after its offset: unknownXX / unknown_XXX. Each becomes a name
    # for what it holds, or unused_XXX when no code reads or writes it (tickets F1-F5).
    'unknown field': re.compile(r'^[ \t]*(?!return\b)(?:const\s+|volatile\s+|mutable\s+)?[\w:]+(?:\s*<[^;()\n]*>)?'
                                r'[\s*&]+unknown\w*\s*(?:\[[^\]\n]*\]\s*)*(?:\{[^}\n]*\}|=[^;\n]*)?;', re.M),
    # A float, a Vector3 or a Quaternion copied or cleared through memcpy / memset (C43): a float is
    # `a = b` or `f = 0.0f` (an SSE2 copy keeps a signalling NaN's bits), a vector `a = b`, a float and its bits
    # `std::bit_cast`. The byte reads of streams and buffers (a size that is a variable) are not this.
    'raw float / vector copy (memcpy, memset)': re.compile(
        r'\b(?:std::)?mem(?:cpy|set)\s*\([^;]*?,\s*(?:4u?|sizeof\s*\(\s*(?:float|Vector3|Quaternion)\s*\))\s*\)'),
    # An unused_XXX member that some code does touch after all: it needs a real name.
    'unused field accessed': re.compile(r'(?:\.|->)\s*unused\w*\b(?!\s*\()'),
    # The system's own sin, cos, pow ... round differently on each system; game code calls src/lib/maths.h's
    # (docs/notes/float-maths.md, "Maths functions"). The renderer's, the sound's and the scripts' doubles are counted
    # where they were.
    'system maths function': re.compile(
        r'\bstd::(?:sin|cos|tan|asin|acos|atan|atan2|exp|exp2|expm1|log|log2|log10|log1p|pow|hypot|cbrt|sinh|cosh|tanh)'
        r'\s*\('),
    # A field that starts out indeterminate (count_uninitialised_fields above).
    'field without a default initialiser': count_uninitialised_fields,
    # A number or a string behind a `#define EI_...` (style round 2, rule X1): a meaning-named constexpr in the owning
    # header instead. Function-like macros, include guards and the x-macro lists are not numbers and not counted.
    '#define EI_ number': re.compile(r'^[ \t]*#[ \t]*define[ \t]+EI_\w+[ \t]+(?:\(?[ \t]*-?[0-9]|")', re.M),
    # A constant named after its value (kOne, kHalf, kTwo, kZero, kMinusOne ...) says nothing about what it is for
    # (round 2, rule X1): name the meaning (kRateBase, kStandingHeight). Definitions only.
    'value-named constant': re.compile(
        r'\bk(?:Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine|Ten|Half|Quarter|MinusOne|Hundred|Thousand)\s*[={]'),
    # Style round 3 (docs/rewrite-guide.md, "Style rules"):
    # An enum never used as a type (count_unused_enums above).
    'enum never used as a type': count_unused_enums,
    # A method named after a message's hex id (SendState02, SendObject0A, OnMessage37): name what it means.
    'hex-named message method': re.compile(r'\b(?:Send|On)\w*[0-9A-F]{2}\b'),
    # The same small helper written again in a file: IsZero(float), the 1/255 colour scale (0.003921569f), a function
    # that asks whether the game server runs. Each lives once, in its owner's header.
    'copied small helper (IsZero, 1/255, server running)': re.compile(
        r'\bbool\s+IsZero\s*\('
        r'|\b1(?:\.0f?)?\s*/\s*255(?:\.0f?)?\b|\b0\.003921569f?\b'
        r'|\bbool\s+\w+\s*\(\s*\)\s*(?:const\s*)?\{[^{}]{0,200}?g_gameServer[^{}]{0,200}?\}'),
    # A function whose whole body is `return "text";` (a table of names as code): a constant or a lookup. A virtual that
    # answers its class's name is the one place it reads as the shortest form.
    'function returning only a string literal': re.compile(
        r'\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{\s*return\s+"[^"\n]*"\s*;\s*\}'),
    # A scope block around a temporary (count_temporary_blocks above).
    'scope block around a temporary': count_temporary_blocks,
    # A buffer read or written by hand: memcpy of N bytes then the cursor moved on (`p += N`). The stream and buffer
    # classes own their cursors; everything else calls their Put/Get/Read members.
    'cursor moved by hand after memcpy': re.compile(r'\bmemcpy\s*\([^;]*\)\s*;\s*\w+\s*\+=\s*[^;]+;'),
    # A file's own little-endian reader (U32, Get32, ReadWord, Load16 ...): the shared one is lib::Load<T> / the streams'.
    'own integer reader': re.compile(
        r'^[ \t]*(?:(?:static|inline|constexpr)\s+)*(?:std::)?u?int(?:8|16|32|64)_t\s+'
        r'(?:U|Get|Read|Load)(?:16|32|64|Word|Dword|LE\d*)\s*\(', re.M),
    # A pointer to a member function, `bool (Connection::*)(Message*)`: handlers and callbacks are virtuals, a switch
    # or a std::function.
    'pointer to member function': re.compile(r'\(\s*[A-Za-z_][\w:]*::\*\s*\w*\s*\)\s*\('),
}
# Patterns not counted under these directories (or files).
# Structs the vector/map/set pattern matches by name that are not container layouts. Each was checked
# (2026-10-14) to hold the shared templates rather than repeat one: FootprintCellSet is a bucket hash
# over std::vector; EffectIdMap and UnitSet
# (and MapObjectIdMap, its shape with MapObject* values) are records whose members are hash maps; RegionMap is a
# block grid; PlatformSet is the engine's CBridgeManager around a list.
NOT_CONTAINERS = {'FootprintCellSet', 'EffectIdMap', 'MapObjectIdMap', 'UnitSet', 'RegionMap', 'PlatformSet'}
EXEMPT = {'per-file container struct': 'src/lib/', 'per-file vector/map/set struct': 'src/lib/',
          'hand-written vector maths': ('src/lib/vecmath.h', 'src/lib/maths.h'),   # maths.h: Hypot's doubles
          'system maths function': 'src/lib/maths.h',
          'copied small helper (IsZero, 1/255, server running)': 'src/lib/', 'own integer reader': 'src/lib/',
          # The streams and buffers own their cursors; src/lib's readers are the shared ones.
          'cursor moved by hand after memcpy': ('src/lib/', 'src/game/core_streams.cpp', 'src/game/game_buffer',
                                                'src/game/net_coder.cpp')}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def counts():
    out = {}
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
        if not path.endswith(('.cpp', '.h', '.inc')):
            continue
        text = strip_comments(open(path, encoding='utf-8', errors='replace').read())
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        row = {}
        for name, rx in PATTERNS.items():
            n = 0
            if name in EXEMPT and rel.startswith(EXEMPT[name]):
                row[name] = 0
                continue
            if callable(rx):
                row[name] = rx(text)
                continue
            for m in rx.finditer(text):
                if name == 'per-file vector/map/set struct' and m.group(0).split()[-1] in NOT_CONTAINERS:
                    continue
                n += 1
            row[name] = n
        out[rel] = row
    return out


def main(argv):
    now = counts()
    base = {}
    header = []
    if os.path.exists(BASELINE):
        reader = csv.DictReader(open(BASELINE, newline=''))
        header = reader.fieldnames or []
        for r in reader:
            base[r['file']] = {k: int(r[k]) for k in PATTERNS if r.get(k) not in (None, '')}
    # A pattern the baseline has no column for yet is seeded from the current counts of every file.
    seeded = [k for k in PATTERNS if k not in header]
    if '--update' in argv:
        with open(BASELINE, 'w', newline='') as f:
            w = csv.writer(f, lineterminator='\n')
            w.writerow(['file'] + list(PATTERNS))
            for rel in sorted(now):
                row = now[rel]
                if rel in base or not base:
                    old = base.get(rel, {})
                    row = {k: min(row[k], old.get(k, row[k])) for k in PATTERNS}
                else:
                    row = {k: row[k] if k in seeded else 0 for k in PATTERNS}
                if any(row.values()):
                    w.writerow([rel] + [row[k] for k in PATTERNS])
        print(f'wrote {os.path.relpath(BASELINE, ROOT)}')
        return 0
    problems = 0
    for rel, row in sorted(now.items()):
        allowed = base.get(rel, {})
        for k in PATTERNS:
            limit = allowed.get(k, 0 if rel not in base else row[k])
            if row[k] > limit:
                problems += 1
                print(f'{rel}: {row[k]} {k} (allowed {limit}); write it the clean way '
                      f'(docs/rewrite-guide.md, "Clean code")')
    totals = {k: sum(r[k] for r in now.values()) for k in PATTERNS}
    print('cleanup counts: ' + ', '.join(f'{v:,} {k}' for k, v in totals.items()) + f'; {problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
