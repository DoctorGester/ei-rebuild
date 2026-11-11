"""Ratchet on the clean-code patterns (docs/rewrite-guide.md, "Clean code").

New code is written the clean way: members instead of raw offsets, virtual calls instead of VCall,
named globals and plain C++ floats. This counts, per file under src/, the patterns the cleanup
removes and fails when a file has more than config/cleanup_baseline.csv allows (a new file: none).
Counts may only go down; run with --update after a cleanup to lower the baseline.

usage: python tools/check_cleanup.py [--update]
"""
import collections, csv, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout   # src/game's files by their flat names, wherever they are now (tools/layout_map.txt)

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


def _class_statements(text, lo, hi, keep=False):
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
                buf += text[i:j + 1] if keep else '{}'
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


# ---- Style round 4 and round 1b (docs/rewrite-guide.md, "Style rules"): counters that read the whole tree. A counter
# with `wants_path` also receives the file's path; the tree is read once (comments and string contents left out).
_code_tree = None


def _tree():
    global _code_tree
    if _code_tree is None:
        _code_tree = {}
        for path in sorted(glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True)):
            if path.endswith(('.cpp', '.h', '.inc')):
                with open(path, encoding='utf-8', errors='replace') as f:
                    text = re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\'', '""', strip_comments(f.read()))
                _code_tree[os.path.relpath(path, ROOT).replace(os.sep, '/')] = text
    return _code_tree


def wants_path(function):
    function.wants_path = True
    return function


# A plain `enum` (C160): every enum is an `enum class`, a flag set with lib/bit_flags.h's operators. The one plain enum
# kept is an array-index enum local to a .cpp (its last enumerator a count: ...Count, ...Kinds, ...Values), which indexes
# a file's own table and would need a cast at every use.
_PLAIN_ENUM = re.compile(r'^[ \t]*(?:typedef[ \t]+)?enum\b(?![ \t]+(?:class|struct)\b)[^{};]*\{([^{}]*)\}', re.M)
_COUNT_LAST = re.compile(r'(?:Count|Kinds|Values)\w*\s*(?:=[^,]*)?,?\s*$')


@wants_path
def count_plain_enums(text, rel):
    n = 0
    for m in _PLAIN_ENUM.finditer(text):
        if rel.endswith('.cpp') and _COUNT_LAST.search(m.group(1).strip()):
            continue
        n += 1
    return n


def _value(text):
    """A constant's value: a number when it is a literal (suffixes dropped, hex and decimal alike), else its text."""
    t = re.sub(r'\s+', '', text)
    u = re.sub(r'(?<=[0-9A-Fa-f.])[fFuUlL]+$', '', t) if not t.lower().startswith('0x') else re.sub(r'[uUlL]+$', '', t)
    for convert in (lambda v: float(int(v, 0)), float, float.fromhex):
        try:
            return convert(u)
        except ValueError:
            pass
    return t


# A k* constant defined at namespace scope (column 0) in two files with different values (rule Z3): kNoNaturalWeapon was
# 0x10, 7 and 8, kNoRange 1e38 and 1e10. A shared code lives once, in its owner's header.
_K_DEFINITION = re.compile(r'^(?:static\s+|inline\s+)*(?:constexpr|const)\s+(?:[\w:<>]+\s+)+?(k[A-Z]\w*)\s*'
                           r'(?:=\s*([^;]+?)|\{([^;{}]*)\})\s*;', re.M)
_constants = None
# Narrowed to the names of sentinels and codes (kNone, kNoRange, kFar, kKind*, kMode*, kFlag*, kMask*): a plain tuning
# number (kLineHeight, kMargin, kThreshold) is meant to differ from file to file.
_CODE_NAME = re.compile(r'k(?:No|None|Invalid|Unset|Any|Far|Near|Kind|Mode|State|Type|Flag|Mask|Bit)(?![a-z])')


@wants_path
def count_conflicting_constants(text, rel):
    global _constants
    if _constants is None:
        _constants = collections.defaultdict(dict)       # name -> {file: value}
        for path, code in _tree().items():
            for m in _K_DEFINITION.finditer(code):
                _constants[m.group(1)].setdefault(path, _value(m.group(2) if m.group(2) is not None else m.group(3)))
    return sum(1 for name, where in _constants.items()
               if _CODE_NAME.match(name) and rel in where and any(path != rel and value != where[rel] for path, value in where.items()))


# A flag constant that gives an owner enum's bit another name (round 1b: the AI's kFlagNearbyListed = 0x80000 was
# MapObjectFlag::RelationsChanged): a k...Flag/Bit/Mask constant with one bit of 0x100 or more, in a file other than the
# one that defines an enumerator of an enum named ...Flag(s)/Bit(s) with that bit.
_FLAG_ENUM = re.compile(r'\benum\s+(?:class\s+|struct\s+)?(\w*(?:Flags?|Bits?))\s*(?::\s*[\w:\s]+?)?\s*\{([^{}]*)\}')
_FLAG_ITEM = re.compile(r'\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|1u?\s*<<\s*\d+)\s*$')
_FLAG_CONSTANT = re.compile(r'\bconstexpr\s+[\w:\s]+?\bk\w*(?:Flag|Bit|Mask)\w*\s*=\s*(0x[0-9A-Fa-f]+u?|1u?\s*<<\s*\d+)\s*;')
_flag_owners = None


def _bit(text):
    t = text.strip().rstrip('uU')
    m = re.match(r'1[uU]?\s*<<\s*(\d+)$', t)
    value = (1 << int(m.group(1))) if m else int(t, 16)
    return value if value >= 0x100 and value & (value - 1) == 0 else None


@wants_path
def count_renamed_flag_bits(text, rel):
    global _flag_owners
    if _flag_owners is None:
        _flag_owners = collections.defaultdict(set)      # bit -> files with an enumerator of that bit
        for path, code in _tree().items():
            for m in _FLAG_ENUM.finditer(code):
                for item in m.group(2).split(','):
                    e = _FLAG_ITEM.match(item)
                    if e and _bit(e.group(2)):
                        _flag_owners[_bit(e.group(2))].add(path)
    return sum(1 for m in _FLAG_CONSTANT.finditer(text)
               if _bit(m.group(1)) in _flag_owners and rel not in _flag_owners[_bit(m.group(1))])


# Virtuals (rule: a virtual needs two real overrides; round 1b I3, round 4 Z4). Indexed once: every `Name(...)` in the
# tree is a call, a declaration or a definition (by what comes before it), and a definition has a body or none.
_VIRTUAL = re.compile(r'\bvirtual\s+[\w:<>\s*&,]+?\b([A-Za-z]\w*)\s*\([^;{}]*?\)\s*(?:const\s*)?(?:noexcept\s*)?'
                      r'(?:override\s*|final\s*)*(\{\s*return\s+(?:false|nullptr|0|NULL)\s*;\s*\}|\{\s*\}|\{|;|=\s*0\s*;)')
_NAME_HEAD = re.compile(r'(?<![\w~])((?:\w+(?:<[^<>;]*>)?::)*)([A-Za-z_]\w*)\s*\(')
_NAME_TAIL = re.compile(r'\s*(const\b\s*)?(?:noexcept\s*)?((?:override|final)\b\s*)*(\{\s*\}|\{)?')
_NOT_A_TYPE = {'return', 'else', 'case', 'do', 'throw', 'co_return', 'new', 'delete', 'and', 'or', 'not', 'co_yield', 'co_await'}
_virtual_index = None


def _closing_paren(text, i):
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '(':
            depth += 1
        elif text[j] == ')':
            depth -= 1
            if depth == 0:
                return j
    return len(text) - 1


def _index_virtuals(tree):
    """name -> {'declared': files, 'default': bodies, 'overrides': n, 'definitions': n, 'empty': n, 'calls': n} of every
    virtual declared in `tree` (path -> code)."""
    info = {}
    for path, code in tree.items():
        for m in _VIRTUAL.finditer(code):
            v = info.setdefault(m.group(1), {'declared': [], 'default': [], 'overrides': 0, 'definitions': 0, 'empty': 0,
                                             'calls': 0})
            v['declared'].append(path)
            v['default'].append(m.group(2))
    for path, code in tree.items():
        for m in _NAME_HEAD.finditer(code):
            v = info.get(m.group(2))
            if v is None:
                continue
            before = code[max(0, m.start() - 80):m.start()].rstrip()
            word = re.search(r'(\w+)$', before)
            declaration = (word and word.group(1) not in _NOT_A_TYPE) or (
                before[-1:] in ('>', '*', '&', '~') and not before.endswith(('->', '&&')))
            tail = _NAME_TAIL.match(code, _closing_paren(code, m.end() - 1) + 1)
            if tail.group(2):
                v['overrides'] += 1
            if tail.group(3):
                v['definitions'] += 1
                v['empty'] += tail.group(3) != '{'
            if before.endswith(('.', '->')) or not declaration and not tail.group(3):
                v['calls'] += 1
    return info


def _virtuals():
    global _virtual_index
    if _virtual_index is None:
        _virtual_index = _index_virtuals(_tree())
    return _virtual_index


# A downcast virtual with one override (round 1b I3): `virtual X* AsFoo() { return nullptr; }` or `virtual bool IsFoo()
# const { return false; }` in a base, one `override` in the tree. The kind field says it already: a non-virtual member
# tests the kind (UI tokens first, now items' AsWeapon ...).
@wants_path
def count_single_override_downcasts(text, rel):
    # Narrowed to the cast shapes: a pointer that answers nullptr, or an Is.../Has... that answers false (a virtual that
    # answers 0 for a count or a stage is a policy hook, not a cast).
    return sum(1 for name, v in _virtuals().items()
               if v['overrides'] == 1 and rel == v['declared'][0]
               and any(re.search(r'nullptr|NULL', d) or (re.match(r'(?:Is|Has)[A-Z]', name) and 'false' in d) for d in v['default']))


# A virtual that nothing calls, and an empty virtual that is still called (round 4 Z4; `Unused14` through
# `NotifyCompanion`). The declaring file (the first) holds the count.
# Unreached gameplay code that stays (user, "keep gameplay code"; C161, C166): the overkill test (MapObject::IsOverkilled,
# hit points below -5 times the maximum) and the weapon model removal (Unit::RemoveWeaponModel, the counterpart of
# WeaponAdded) have both overrides written and no caller; they are kept to be called when that gameplay is switched on.
UNREACHED_GAMEPLAY_VIRTUALS = {'IsOverkilled', 'RemoveWeaponModel'}


@wants_path
def count_virtuals_never_called(text, rel):
    return sum(1 for name, v in _virtuals().items()
               if v['calls'] == 0 and rel == v['declared'][0] and name not in UNREACHED_GAMEPLAY_VIRTUALS)


@wants_path
def count_empty_virtuals_called(text, rel):
    return sum(1 for v in _virtuals().values()
               if v['calls'] and v['definitions'] and v['empty'] == v['definitions'] and rel == v['declared'][0])


# Constructors and Reset against the default initialisers (round 1b I1, T1; extends the field check above): a field
# that has `= value` or `{value}` in its class and is assigned another value (or the same, in a constructor) in the
# constructor's body or initialiser list, or another value in Reset(): two start states for one field. Only a constant
# is a start value (_constant): what a constructor computes from its parameters, a call or another member (a pointer it
# fills with a built object, a size read from a file, a state its branches decide) is not one the initialiser could hold.
_INITIALISED = re.compile(r'^(?:[\w:<>,\s*&]*?[\s*&])?(\w+)\s*(?:\[[^\]]*\]\s*)?(?:=\s*([^;=][^;]*)|\{([^;{}]*)\})$')
_FUNCTION_HEAD = re.compile(r'\b(?:(\w+)::)?(\w+)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*(?:const\s*)?(?:noexcept\s*)?(:[^{;]*?)?\{')
_ASSIGN = re.compile(r'(?:^|(?<=[;{}]))\s*(?:this->)?(\w+)\s*=\s*([^;=][^;]*);')
_class_inits = None


def _initialisers():
    global _class_inits
    if _class_inits is None:
        _class_inits = collections.defaultdict(dict)     # class name -> {field: value}
        for path, code in _tree().items():
            code = _DIRECTIVE.sub('', code)
            for m in _FIELD_CLASS.finditer(code):
                if m.group(1) == 'union' or not m.group(2):
                    continue
                start = m.end() - 1
                for statement in _class_statements(code, start + 1, _closing_brace(code, start), keep=True):
                    body = ' '.join(statement.split())
                    if _FIELD_SKIP.match(body) or re.search(r'\(.*[={]', body.split('=')[0]) and '(' in body.split('=')[0]:
                        continue
                    f = _INITIALISED.match(body)
                    if f and '(' not in body.split('=')[0].split('{')[0]:
                        _class_inits[m.group(2)][f.group(1)] = _value(f.group(2) if f.group(2) is not None else f.group(3))
    return _class_inits


def _bare(value):
    """A brace-initialised value without its braces: `first{10000, 10000}` and `first = {10000, 10000}` are one value."""
    return value[1:-1] if isinstance(value, str) and value.startswith('{') and value.endswith('}') else value


def _constant(expression):
    """A constant expression: literals, k* constants, true / false / nullptr, enumerators (`State::Closed`), casts of these.
    A value that calls something or reads a parameter, a local or a member is computed: no default initialiser can hold it."""
    t = re.sub(r'static_cast<[^<>]*>', '', expression)
    t = re.sub(r'"[^"]*"', '', t)
    if re.search(r'[A-Za-z_]\w*\s*\(', t):
        return False
    return all(re.fullmatch(r'k[A-Z]\w*|true|false|nullptr|(?:\w+::)+(?:[A-Z]|k[A-Z])\w*', name)
               for name in re.findall(r'(?<![\w.])[A-Za-z_]\w*(?:::\w+)*', t))


def count_double_start_states(text, inits=None):
    inits = _initialisers() if inits is None else inits
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)    # as the initialisers' tree: a Reset's string equals its default's
    classes = [(m.group(2), m.end() - 1) for m in _FIELD_CLASS.finditer(text) if m.group(2)]
    n = 0
    for m in _FUNCTION_HEAD.finditer(text):
        owner, name = m.group(1), m.group(2)
        if owner is None:                                # inside a class body: the nearest class before it
            enclosing = [c for c, at in classes if at < m.start() and _closing_brace(text, at) > m.start()]
            owner = enclosing[-1] if enclosing else None
        constructor = owner is not None and name == owner
        if owner not in inits or not (constructor or name == 'Reset'):
            continue
        fields = inits[owner]
        if constructor and m.group(4):
            n += sum(1 for f, v in re.findall(r'[:,]\s*(\w+)\s*[({]([^(){}]*)[)}]', m.group(4))
                     if f in fields and _constant(v))
        body = text[m.end():_closing_brace(text, m.end() - 1)]
        for a in _ASSIGN.finditer(body):
            if a.group(1) in fields and _constant(a.group(2)) and (
                    constructor or _bare(_value(a.group(2))) != _bare(fields[a.group(1)])):
                n += 1
    return n


# A member that reaches its own object through the global (round 4 Z9): `WorldServer::Loot` reading g_serverWorld. Inside
# a member the object is `this`; a free function that needs a world takes it.
_GLOBAL_OF = re.compile(r'\bextern\s+(\w+)\s+(g_\w+)\s*;')
_MEMBER_HEAD = re.compile(r'\b(\w+)::~?\w+\s*\([^;{}]*?\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?::[^{;]*?)?\{')
_singletons = None


def _singleton_globals():
    global _singletons
    if _singletons is None:
        _singletons = collections.defaultdict(set)       # class name -> its globals
        for code in _tree().values():
            for m in _GLOBAL_OF.finditer(code):
                _singletons[m.group(1)].add(m.group(2))
    return _singletons


def count_members_through_global(text, singletons=None):
    singletons = _singleton_globals() if singletons is None else singletons
    n = 0
    for m in _MEMBER_HEAD.finditer(text):
        if m.group(1) in singletons:
            body = text[m.end():_closing_brace(text, m.end() - 1)]
            n += sum(len(re.findall(r'\b' + g + r'\b', body)) for g in singletons[m.group(1)])
    return n


# A member body written out again in a sibling file (round 4 Z2): three significant lines in a row that appear again in
# a file of the same family (the same directory, the same first two words of the name: spell_effect*.cpp; in src/game
# the name it had in the flat tree, tools/layout_map.txt, so a move or a meaning rename keeps a file's family), in the
# same or another place. Counted per copied run, in the later place. Lines that are only braces, `break;`, `return;` and the
# like, and windows shorter than 90 characters, are not significant.
_TRIVIAL = re.compile(r'^(?:[{}]+|\}\s*else\s*\{?|else\s*\{?|break;|continue;|return;|return (?:true|false|0|1|nullptr);|default:|'
                      r'#.*|using .*|public:|private:|protected:|case [\w:]+:|\)?;|\};?)$')
_families = None


def _family(rel):
    if rel.startswith(('src/game/', 'src/engine/')):
        stem = os.path.splitext(layout.flat_name(rel))[0]
        return 'src/game/' + '_'.join(stem.split('_')[:2])
    stem = os.path.splitext(os.path.basename(rel))[0]
    return os.path.dirname(rel) + '/' + '_'.join(stem.split('_')[:2])


def _copied_runs(tree):
    """path -> the number of runs of lines in that .cpp file that an earlier file of its family has too."""
    windows = collections.defaultdict(set)               # (family, three lines) -> the files that have them
    runs = collections.Counter()
    for path in sorted(p for p in tree if p.endswith('.cpp')):     # a header's lists of overrides repeat by design
        lines = [(i, ' '.join(l.split())) for i, l in enumerate(tree[path].split('\n'))]
        lines = [(i, l) for i, l in lines if l and not _TRIVIAL.match(l)]
        seen = set()
        for k in range(len(lines) - 2):
            key = ' | '.join(l for _, l in lines[k:k + 3])
            if len(key) < 90:
                continue
            spot = (_family(path), key)
            if windows[spot] - {path}:
                seen.add(k)
            windows[spot].add(path)
        runs[path] = sum(1 for k in seen if k - 1 not in seen)          # a run of copied windows is one copy
    return runs


@wants_path
def count_copied_member_bodies(text, rel):
    global _families
    if _families is None:
        _families = _copied_runs(_tree())
    return _families[rel]


# The platform layer's contract and the game do not speak Windows (round 4 Z11): Win32 type and message names outside
# the Windows backend (src/platform/win32), in code. Comments may name them.
_WINDOWS_NAME = re.compile(r'\b(?:HWND|HINSTANCE|HMODULE|HANDLE|WM_[A-Z0-9_]+|GetTickCount\w*|TranslateMessage|DispatchMessage[AW]?|'
                           r'LPARAM|WPARAM|LRESULT|WNDPROC|WinMain|VK_[A-Z0-9_]+)\b|#\s*include\s*<windows\.h>')

# A printf-like function (`const char* format, ...` or a va_list) with no format annotation on any declaration: the
# compiler cannot check its arguments (`%lu` with a `uint32_t` got in that way; round 4 Z11).
_PRINTF_LIKE = re.compile(r'\b(\w+)\s*\([^()]*const\s+char\s*\*\s*\w*\s*,\s*(?:\.\.\.|va_list\b)[^()]*\)')
_printf_state = None


@wants_path
def count_unannotated_printf(text, rel):
    global _printf_state
    if _printf_state is None:
        _printf_state = {}
        for path, code in _tree().items():
            for m in _PRINTF_LIKE.finditer(code):
                head = code[max(0, m.start() - 160):m.start()]
                declaration = head[head.rfind(';') + 1:] + m.group(0) + re.split(r'[;{]', code[m.end():m.end() + 80])[0]
                annotated = bool(re.search(r'EI_PRINTF_FORMAT|SDL_PRINTF_VARARG_FUNC|gnu::format|__attribute__', declaration))
                state = _printf_state.setdefault(m.group(1), [path, False])
                state[1] = state[1] or annotated
    return sum(1 for path, annotated in _printf_state.values() if path == rel and not annotated)


# A header's function that only returns a number (round 4 Z8: world_data.h was the exe's .rdata as 33 functions): a
# constant in its owner's header. A virtual (its base answers a default) and `override` are not counted.
_NUMBER_FUNCTION = re.compile(r'^[^\n;{}()]*\b\w+\s*\(\s*\)\s*(?:const\s*)?(?:noexcept\s*)?\{\s*return\s+-?(?:0x[0-9A-Fa-f]+|\d[\d.]*(?:e-?\d+)?)'
                              r'[fFuUlL]*\s*;\s*\}', re.M)


@wants_path
def count_number_functions_in_headers(text, rel):
    return sum(1 for m in _NUMBER_FUNCTION.finditer(text) if rel.endswith('.h') and not re.search(r'\b(?:virtual|override)\b', m.group(0)))


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE =os.path.join(ROOT, 'config', 'cleanup_baseline.csv')
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
    # (docs/notes/float-maths.md, "Maths functions"). Two files keep it, by EXEMPT below (C161, C166):
    # script_addon_math.cpp's Sin / Cos / Pow (Rebirth's script functions answer the CRT's own results; lib:: would move
    # a script's value by an ulp) and bink_audio.cpp's double pow (the decoder is checked bit for bit against the oracle).
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
    # A plain `enum` instead of an `enum class` (count_plain_enums above).
    'plain enum (not enum class)': count_plain_enums,
    # A method named after a message's hex id (SendState02, SendObject0A, OnMessage37): name what it means.
    'hex-named message method': re.compile(r'\b(?:Send|On)\w*[0-9A-F]{2}\b'),
    # The same small helper written again in a file: IsZero(float), the 1/255 colour scale (0.003921569f), a function
    # that asks whether the game server runs. Each lives once, in its owner's header.
    'copied small helper (IsZero, 1/255, server running)': re.compile(
        r'\bbool\s+IsZero\s*\('
        r'|\b1(?:\.0f?)?\s*/\s*255(?:\.0f?)?\b|\b0\.003921569f?\b'
        r'|\bbool\s+\w+\s*\(\s*\)\s*(?:const\s*)?\{[^{}]{0,200}?g_gameServer[^{}]{0,200}?\}'),
    # A function whose whole body is `return "text";` (a table of names as code): a constant or a lookup. A virtual that
    # answers its class's name is the one place it reads as the shortest form: `ClassName()` is not counted.
    'function returning only a string literal': re.compile(
        r'(?<!ClassName\()\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{\s*return\s+"[^"\n]*"\s*;\s*\}'),
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
    # Style round 4 and round 1b (docs/rewrite-guide.md, "Style rules"):
    # A random word scaled by hand, `Next() * 0x1p-32f + low` or `* 2.3283064e-10f`: IsaacRandom::Range (or a member beside it)
    # does it, and a re-spelled copy drifts (7 in the unit AI, 19 in the particles). The scale as 0x1p-30 ... 0x1p-32, as
    # a 2^-32 / 2^-31 / 2^-30 decimal (`[1-9].dddddde-10`) or as kTwoToMinus32.
    'random word scaled by hand (Range copy)': re.compile(
        r'\b0x1p-3[0-2]f?\b|\b[1-9]\.\d{6,}e-10f?\b|\bkTwoToMinus32\w*'),
    # A flag constant under a second name for an owner enum's bit (count_renamed_flag_bits above).
    'flag bit under a second name': count_renamed_flag_bits,
    # A downcast virtual with one override (count_single_override_downcasts above).
    'downcast virtual with one override': count_single_override_downcasts,
    # A field with a default initialiser that a constructor or Reset gives another start value (count_double_start_states).
    'field with two start values (initialiser, constructor or Reset)': count_double_start_states,
    # A k* constant at namespace scope defined in two files with different values (count_conflicting_constants above).
    'constant defined twice with another value': count_conflicting_constants,
    # Virtuals nobody calls, and empty virtuals that are still called (count_virtuals_never_called above; the two
    # unreached gameplay virtuals it leaves out are named there).
    'virtual never called': count_virtuals_never_called,
    'empty virtual still called': count_empty_virtuals_called,
    # Maths helpers take and return values (round 4 Z5): a helper over the address of a constant, ConstPtr<>(2.0f), a
    # kFloatConstant reference, or `const float* a, ..., float* out` parameters.
    'float helper over addresses (ConstPtr, out pointer)': re.compile(
        r'\bConstPtr\b|\bkFloatConstant\b|\(\s*(?:const\s+float\s*\*\s*\w+\s*,\s*)+(?:\w+\s*,\s*)*float\s*\*\s*\w+\s*\)'),
    # x87 store residue on an x64-only build (round 4 Z6): `volatile float`, and a float copied into a local only to be
    # copied back, `float stored = x; x = stored;`. On SSE every float operation already rounds to float.
    'x87 store residue (volatile float, store and reload)': re.compile(
        r'\bvolatile\s+(?:float|double)\b|\bfloat\s+(\w+)\s*=\s*([\w.\[\]>-]+)\s*;\s*\2\s*=\s*\1\s*;'),
    # A function that returns only a number, in a header (count_number_functions_in_headers above).
    'function returning only a number (header)': count_number_functions_in_headers,
    # A member that reaches its own object through the global (count_members_through_global above).
    'member through its own global': count_members_through_global,
    # A member body written out again in a family file (count_copied_member_bodies above).
    'member body copied in a family file': count_copied_member_bodies,
    # Windows names in code outside the Windows backend (_WINDOWS_NAME above).
    'Windows name outside the Windows backend': _WINDOWS_NAME,
    # A printf-like function with no format annotation (count_unannotated_printf above).
    'printf-like function without format annotation': count_unannotated_printf,
    # std::lround outside the client-only files (CLIENT_ONLY_ROUNDING below): it rounds halves away from zero, the original's
    # FISTP to even (lib::RoundEven) or its _ftol truncates (docs/notes/float-maths.md, "Conversions and copies"). Client-only
    # values (a colour byte, a volume, a window size) may use it; a value that gates an order or reaches game state may not.
    'std::lround in game-affecting code': re.compile(r'\bstd::lround\s*\('),
    # An empty namespace left behind by deleted code.
    'empty namespace': re.compile(r'\bnamespace\s*\w*\s*\{\s*\}'),
    # An input by pointer where a reference or a view does it (round 4 Z10): `const std::string*`, `const std::vector<T>*`. A
    # non-const `std::vector<T>*` may be the caller's buffer and is not counted. A local declared in an `if (` / `for (`
    # head (`if (const std::string* old = Find(...); old && ...)`, a lookup that may find nothing) is not a parameter.
    'const string or vector pointer parameter': re.compile(
        r'(?:,|(?<!\bif )(?<!\bif)(?<!\bfor )(?<!\bfor)\()\s*const\s+std::(?:string|vector<[^<>()]*(?:<[^<>]*>)?[^<>()]*>)\s*\*\s*\w*\s*(?=[,)=])'),
}
# Patterns not counted under these directories (or files). src/game's files by their flat names (or name prefixes),
# wherever they are now: layout.game_paths.
# Structs the vector/map/set pattern matches by name that are not container layouts. Each was checked
# (2026-10-14) to hold the shared templates rather than repeat one: FootprintCellSet is a bucket hash
# over std::vector; EffectIdMap and UnitSet
# (and MapObjectIdMap, its shape with MapObject* values) are records whose members are hash maps; RegionMap is a
# block grid; PlatformSet is the engine's CBridgeManager around a list.
NOT_CONTAINERS = {'FootprintCellSet', 'EffectIdMap', 'MapObjectIdMap', 'UnitSet', 'RegionMap', 'PlatformSet'}
EXEMPT = {'per-file container struct': 'src/lib/', 'per-file vector/map/set struct': 'src/lib/',
          'hand-written vector maths': ('src/lib/vecmath.h', 'src/lib/maths.h'),   # maths.h: Hypot's doubles
          'system maths function': ('src/lib/maths.h', 'src/video/bink_audio.cpp') + layout.game_paths('script_addon_math.cpp'),
          'copied small helper (IsZero, 1/255, server running)': 'src/lib/', 'own integer reader': 'src/lib/',
          # The streams and buffers own their cursors; src/lib's readers are the shared ones.
          'cursor moved by hand after memcpy': ('src/lib/',) + layout.game_paths('core_streams.cpp', 'game_buffer',
                                                                                 'net_coder'),
          # The generator itself and its tests of the scale.
          'random word scaled by hand (Range copy)': ('src/lib/',) + layout.game_paths('random.cpp'),
          'Windows name outside the Windows backend': 'src/platform/win32/'}
# Files whose rounded values only the picture, the sound or the window see (2026-11-05, every site read): the effects,
# particles, lightning, weather and terrain drawing, the ground prints, the renderer, the platform layer's sizes and
# scales, and the interface's layout and displays (cursor scale, colour bytes, the ping and vision numbers shown).
CLIENT_ONLY_ROUNDING = ('src/platform/', 'src/render/', 'src/lib/') + layout.game_paths(
    'effects', 'lightning', 'particle', 'weather_particles', 'terrain', 'ground_print', 'renderer_scene',
    'console_window', 'ui_cursor', 'ui_mesh', 'ui_connect_screen', 'ui_hud_side')
EXEMPT['std::lround in game-affecting code'] = CLIENT_ONLY_ROUNDING


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
                row[name] = rx(text, rel) if getattr(rx, 'wants_path', False) else rx(text)
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
