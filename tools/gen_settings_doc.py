"""docs/settings.md from the settings catalog (src/game/app/settings_catalog.inc): every setting of ei_settings.ini with
its default (the text of the row's typed default), its values and what it does (C69, C171).

usage: python tools/gen_settings_doc.py            writes docs/settings.md
       python tools/gen_settings_doc.py --check    fails when docs/settings.md is out of date, or when the catalog and
                                                   the code disagree: a setting the code reads without a row, a row
                                                   nothing reads, or a read that passes a default of its own (the
                                                   row's is the only one; build_checks.py runs it)

The settings the code reads are found by their spelling: options.cpp's tables (section and name, the camera's
motion values, the shortcuts, the lights), every Get*(section, "name") under src/, main_loop.cpp's QualitySetting
names.
"""
import decimal, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout   # src/game's and src/engine's files wherever they are now (tools/layout_map.txt)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG_REL = layout.src_path(layout.now('settings_catalog.inc'))
CATALOG = os.path.join(ROOT, *CATALOG_REL.split('/'))
SETTINGS_H = os.path.join(ROOT, *layout.src_path(layout.now('settings.h')).split('/'))
DOC = os.path.join(ROOT, 'docs', 'settings.md')
# The settings_defaults scenario's expected file: the defaults' description comments come from the catalog too.
EXPECTED_DEFAULTS = os.path.join(ROOT, 'tests', 'scenarios', 'settings', 'settings_defaults.ini')
COMMENT_WIDTH = 108   # settings_document.cpp kCommentWidth
# The original's settings this build does not read (dropped in C69: nothing used them; in C81: the modern renderer
# always draws one way, or the value is a constant; in P12: the far clip, as everything in view is drawn). A file that holds them keeps the lines; the game ignores them.
DROPPED = ['[video] VideoZBufferDepthInBits', 'VideoRefreshRate', 'Antialiasing', 'Dithering', 'PerspectiveCorrection',
           'ShowParticles', 'FPSIndependentCursor', 'AutomaticQuantization', 'MovieCapturingFPS', 'DrawingTarget',
           'VideoColorDepthInBits', 'Specular', 'TextureQuality', 'Mipmapping', 'Filtering', 'Contrast',
           '[shadows] ShadowQuality', 'HeroShadow', 'UnitShadow', 'BuildingShadow', 'FloraShadow', 'OtherShadow',
           'LightingFreq', 'FloraSpecialLight',
           '[terrain] EnableDynamicLights', 'ShadowingFreq', 'EnableWaterWaves', 'EnableSelfShadowing', 'EnableFootprints',
           'EnableBloodprints', 'EnableFireprints', 'EnableBloodprintsRelight', 'LightingFreq', '[game] Joystick',
           'UseSWCursors', 'CornerSize', 'CD1 Label', 'CD3 Label', 'Font CharSet', 'DrawVisibleOnly', 'Keyboard Layout',
           'ObjectsLightingCoeff', 'Hero Light R/G/B/Radius', 'Quest Light R/G/B/Radius', 'DefaultWindForce',
           'DefaultTimeScale', 'DefaultMapName', 'DefaultStreamingSound', 'FogNightStartDistance', 'FogDayStartDistance',
           '[camera] FarClipDistance', '[statistics] CreateOutputInfo', 'NumberOfFrames']

STRING = r'"((?:[^"\\]|\\.)*)"'


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def unescape(text):
    return re.sub(r'\\(.)', r'\1', text)


def sections():
    """{constant: section name} from settings.h (kVideo = "video")."""
    return dict(re.findall(r'inline constexpr const char\* (k\w+) = "([^"]*)";', read(SETTINGS_H)))


def call_arguments(text, at):
    """The top-level arguments of the call whose '(' is just before `at`, as source text, and the index past its ')'."""
    args, depth, start, i = [], 0, at, at
    while i < len(text):
        c = text[i]
        if c == '"':
            i = re.compile(STRING).match(text, i).end()
            continue
        if c == '(':
            depth += 1
        elif c == ')' and depth:
            depth -= 1
        elif c in ',)' and not depth:
            args.append(text[start:i].strip())
            start = i + 1
            if c == ')':
                return args, i + 1
        i += 1
    raise SystemExit(f'cannot read the call at {text[at - 20:at + 40]!r}')


def literals(arg):
    """One or more adjacent string literals, joined; None when `arg` is not that."""
    if not re.fullmatch(r'(?:\s*' + STRING + r'\s*)+', arg):
        return None
    return ''.join(unescape(m) for m in re.findall(STRING, arg))


def float_text(literal):
    """A float literal as settings::FloatText writes the float (std::to_chars): the fewest digits that read back to the
    same float, fixed or scientific, whichever is shorter (fixed on a tie)."""
    single = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
    value = single(float(literal.rstrip('fF')))
    digits = next(d for d in range(1, 10) if single(float(f'{value:.{d - 1}e}')) == value)
    scientific = f'{value:.{digits - 1}e}'
    fixed = format(decimal.Decimal(scientific), 'f')
    return fixed if len(fixed) <= len(scientific) else scientific


def default_text(arg):
    """A row's typed default as the docs show it (settings::DefaultText; the desktop's size in words)."""
    m = re.fullmatch(r'(Int|Bool|Float|Text|DesktopSize)\((.*)\)', arg, flags=re.S)
    if not m:
        return None
    kind, value = m.groups()
    value = value.strip()
    if kind == 'Int' and re.fullmatch(r'-?\d+', value):
        return value
    if kind == 'Bool' and value in ('true', 'false'):
        return value
    if kind == 'Float' and re.fullmatch(r'-?\d+(?:\.\d*)?[fF]?', value):
        return float_text(value)
    if kind == 'Text' and literals(value) is not None:
        return literals(value)
    if kind == 'DesktopSize' and not value:
        return '(desktop size)'
    return None


def catalog():
    """The rows in order: (constant, name, default, values, description, written). Each EI_SETTING's arguments: a
    section constant, the name, the typed default, kWritten or kLeftOut, then the values and the description, each
    one or more adjacent string literals."""
    text = re.sub(r'^[ \t]*//.*$', '', read(CATALOG), flags=re.M)   # whole-line comments (the header's example row)
    rows = []
    for m in re.finditer(r'EI_SETTING\(', text):
        args, _ = call_arguments(text, m.end())
        where = text[m.start():m.start() + 70]
        if len(args) != 6:
            raise SystemExit(f'settings_catalog.inc: {where!r}: 6 arguments expected')
        constant, name, default, written, values, description = args
        name, values, description = literals(name), literals(values), literals(description)
        default = default_text(default)
        if (not re.fullmatch(r'k\w+', constant) or None in (name, values, description) or default is None or
                written not in ('kWritten', 'kLeftOut')):
            raise SystemExit(f'settings_catalog.inc: {where!r}: a section constant, a name, Int(n) / Bool(b) / '
                             'Float(f) / Text("...") / DesktopSize(), kWritten or kLeftOut, values and a description '
                             'expected')
        rows.append((constant, name, default, values, description, written == 'kWritten'))
    return rows


def own_defaults():
    """Every settings read under src/ that passes a default of its own: (file, line, call)."""
    found = []
    call = re.compile(r'\bGet(?:Int|Bool|Float|String)\(\s*(?:(?:::)?settings::)?k[A-Z]\w*\s*,')
    src = os.path.join(ROOT, 'src')
    for folder, _, files in os.walk(src):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(folder, f)
            text = read(path)
            for m in call.finditer(text):
                args, end = call_arguments(text, text.index('(', m.start()) + 1)
                if len(args) > 2:
                    rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
                    found.append((rel, text.count('\n', 0, m.start()) + 1, ' '.join(text[m.start():end].split())))
    return found


def read_names(names_of):
    """Every (section constant, name) the code reads."""
    found = set()
    src = os.path.join(ROOT, 'src')
    for folder, _, files in os.walk(src):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(folder, f)
            text = read(path)
            for section, name in re.findall(r'Get(?:Int|Bool|Float|String)\((?:settings::|::settings::)?(k\w+),\s*' + STRING, text):
                found.add((section, unescape(name)))
            if f == 'options.cpp':
                for section, name in re.findall(r'\{(k\w+),\s*' + STRING, text):
                    found.add((section, name))
                for name in re.findall(r'\{"(CameraDefault\w+)"', text):
                    found.add(('kCamera', name))
                for name in re.findall(r'"CameraShortcut %d (\w+)"', text):
                    found.add(('kCamera', f'CameraShortcut <n> {name}'))
                for name in re.findall(r'"((?:Hero|Quest) Light [^"]+)"', text):
                    found.add(('kGame', name))
                for name in re.findall(r'ReadCameraFlip\(\w+, "(\w+)"\)', text):
                    found.add(('kCamera', name))
            if f == 'main_loop.cpp':
                for name in re.findall(r'QualitySetting\("(\w+)"', text):
                    found.add(('kRenderer', name))
            if 'GetValues(settings::kAutoExec)' in text:
                found.add(('kAutoExec', '<name>'))
    return found


def doc(rows, names):
    out = ['# ei_settings.ini',
           '',
           f'Generated by tools/gen_settings_doc.py from {CATALOG_REL}. Do not edit.',
           '',
           'The game keeps its settings in `ei_settings.ini` next to the exe (on macOS in',
           '`~/Library/Application Support/ei-rebuild`; or in `%LOCALAPPDATA%\\EvilIslands` when that folder cannot be',
           'written). Each `[section]` holds `name = value` lines; a line starting with `;` or `#` is a',
           'comment. When the file is missing or empty the game writes the defaults, each line with its description above',
           'it; it never adds comments to a file that exists. A value the file lacks takes the default below.',
           '',
           'Defaults: what the game takes when the line is absent; a new file holds the lines marked *(new file)*.',
           '',
           "The original game's settings this build does not read (a file that holds them keeps the lines; the game",
           'ignores them): ' + ', '.join(DROPPED) + '.']
    current = None
    for constant, name, default, values, description, written in rows:
        section = names[constant]
        if section != current:
            out += ['', f'## [{section}]', '', '| Name | Default | Values | What it does |', '|---|---|---|---|']
            current = section
        cell = lambda t: t.replace('|', '\\|') if t else ''
        shown = (cell(default) and '`' + cell(default) + '`') + (' *(new file)*' if written else '')
        out.append(f'| `{name}` | {shown} | {cell(values)} | {cell(description)} |')
    return '\n'.join(out) + '\n'


def description_lines(row):
    """A row's description as the defaults file's comment lines (settings_document.cpp DescriptionLines; its widths
    are UTF-8 bytes)."""
    _, _, _, values, description, _ = row
    text = description + ('' if values == 'true / false' else f' ({values})')
    width = lambda s: len(s.encode('utf-8'))
    lines, line = [], ''
    for word in text.split(' '):
        if line and width(line) + 1 + width(word) > COMMENT_WIDTH:
            lines.append(line)
            line = ''
        line = f'{line} {word}' if line else word
    if line:
        lines.append(line)
    return ['; ' + l for l in lines]


def described_defaults(rows, names, text):
    """The expected defaults file with each described line's comments (the ones right above its first line) as the
    catalog's."""
    by_key = {}
    for row in rows:
        by_key.setdefault(names[row[0]], []).append(row)

    def row_of(section, name):
        for row in by_key.get(section, []):
            pattern = '^' + re.escape(row[1].lower()).replace(re.escape('<n>'), r'\d+').replace(re.escape('<name>'), '.+') + '$'
            if re.match(pattern, name.lower()):
                return row
        return None

    out, section, seen = [], '', set()
    for line in text.split('\n'):
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].lower()
        elif '=' in line and not line.startswith(';'):
            row = row_of(section, line.split('=', 1)[0].strip())
            if row and id(row) not in seen:
                seen.add(id(row))
                start = len(out)
                while start > 0 and out[start - 1].startswith(';'):
                    start -= 1
                if start < len(out):   # the defaults' own description: replaced (a file's other comments are not)
                    out[start:] = description_lines(row)
        out.append(line)
    return '\n'.join(out)


def main(argv):
    check = '--check' in argv
    names = sections()
    rows = catalog()
    problems = []
    seen = set()
    for constant, name, *_ in rows:
        if constant not in names:
            problems.append(f'{name}: unknown section constant {constant}')
        if (constant, name.lower()) in seen:
            problems.append(f'[{names.get(constant)}] {name}: two rows')
        seen.add((constant, name.lower()))
    read_by_code = read_names(names)
    rows_lower = {(c, n.lower()) for c, n, *_ in rows}
    for constant, name in sorted(read_by_code):
        if (constant, name.lower()) not in rows_lower:
            problems.append(f'[{names.get(constant, constant)}] {name}: read by the code, no catalog row')
    read_lower = {(c, n.lower()) for c, n in read_by_code}
    for constant, name, *_ in rows:
        if (constant, name.lower()) not in read_lower:
            problems.append(f'[{names.get(constant, constant)}] {name}: a catalog row nothing reads')
    for path, line, call in own_defaults():
        problems.append(f'{path}:{line}: {call}: a default of its own (the catalog row\'s is the one)')
    text = doc(rows, names)
    expected = read(EXPECTED_DEFAULTS)
    described = described_defaults(rows, names, expected)
    if check:
        if not os.path.exists(DOC) or read(DOC) != text:
            problems.append('docs/settings.md is out of date: python tools/gen_settings_doc.py')
        if described != expected:
            problems.append('settings_defaults.ini\'s description comments are out of date: python tools/gen_settings_doc.py')
    else:
        with open(DOC, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        with open(EXPECTED_DEFAULTS, 'w', encoding='utf-8', newline='\n') as f:
            f.write(described)
    for p in problems:
        print(f'gen_settings_doc: {p}')
    print(f'settings catalog: {len(rows)} rows; {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
