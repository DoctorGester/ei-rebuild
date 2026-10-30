"""docs/settings.md from the settings catalog (src/game/settings_catalog.inc): every setting of ei_settings.ini with
its default, its values and what it does (C69).

usage: python tools/gen_settings_doc.py            writes docs/settings.md
       python tools/gen_settings_doc.py --check    fails when docs/settings.md is out of date, or when the catalog and
                                                   the code disagree: a setting the code reads without a row, or a
                                                   row nothing reads (build_checks.py runs it)

The settings the code reads are found by their spelling: options.cpp's tables (section and name, the camera's
motion values, the shortcuts, the lights), every Get*(section, "name") under src/, the renderer's QualitySetting
names, Rebirth's patches (include/ei/rebirth_patches.inc) and the [patches] names startup.cpp understands.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG = os.path.join(ROOT, 'src', 'game', 'settings_catalog.inc')
SETTINGS_H = os.path.join(ROOT, 'src', 'game', 'settings.h')
DOC = os.path.join(ROOT, 'docs', 'settings.md')
# The settings_defaults scenario's expected file: the defaults' description comments come from the catalog too.
EXPECTED_DEFAULTS = os.path.join(ROOT, 'tests', 'scenarios', 'settings', 'settings_defaults.ini')
COMMENT_WIDTH = 108   # settings_document.cpp kCommentWidth
# The original's settings this build does not read (dropped in C69: nothing used them). A file that holds them keeps
# the lines; the game ignores them.
DROPPED = ['[video] VideoZBufferDepthInBits', 'VideoRefreshRate', 'Antialiasing', 'Dithering', 'PerspectiveCorrection',
           'ShowParticles', 'AutomaticQuantization', 'MovieCapturingFPS', '[shadows] HeroShadow', 'OtherShadow',
           '[terrain] EnableDynamicLights', 'LightingFreq', 'ShadowingFreq', '[game] Joystick', 'UseSWCursors',
           'CornerSize', 'CD1 Label', 'CD3 Label', 'Font CharSet', '[statistics] CreateOutputInfo', 'NumberOfFrames']

STRING = r'"((?:[^"\\]|\\.)*)"'


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def unescape(text):
    return re.sub(r'\\(.)', r'\1', text)


def sections():
    """{constant: section name} from settings.h (kVideo = "video")."""
    return dict(re.findall(r'inline constexpr const char\* (k\w+) = "([^"]*)";', read(SETTINGS_H)))


def catalog():
    """The rows in order: (constant, name, default, values, description). Each EI_SETTING's arguments: a section
    constant, then four texts, each one or more adjacent string literals."""
    text = re.sub(r'^[ \t]*//.*$', '', read(CATALOG), flags=re.M)   # whole-line comments (the header's example row)
    token = re.compile(r'\s+|//[^\n]*|' + STRING + r'|(k\w+)|(,)|(\))')
    rows = []
    for m in re.finditer(r'EI_SETTING\(', text):
        at, args, current, constant = m.end(), [], [], None
        while True:
            t = token.match(text, at)
            if not t:
                raise SystemExit(f'settings_catalog.inc: cannot read the row at {text[m.start():m.start() + 60]!r}')
            at = t.end()
            literal, ident, comma, close = t.groups()
            if literal is not None:
                current.append(unescape(literal))
            elif ident:
                constant = ident
            elif comma:
                if current:
                    args.append(''.join(current))
                current = []
            elif close:
                args.append(''.join(current))
                break
        if not constant or len(args) != 4:
            raise SystemExit(f'settings_catalog.inc: {constant} {args[:1]}: a section constant and 4 texts expected')
        rows.append((constant, *args))
    return rows


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
            if f == 'device_mesh.cpp':
                for name in re.findall(r'QualitySetting\("(\w+)"', text):
                    found.add(('kRenderer', name))
            if f == 'startup.cpp':
                for name in re.findall(r'(?:_stricmp|CompareIgnoringCase)\(name, "(\w+)"\)', text):
                    found.add(('kPatches', name))
            if 'GetValues(settings::kAutoExec)' in text:
                found.add(('kAutoExec', '<name>'))
    for name in re.findall(r'^EI_REBIRTH_PATCH\((\w+)\)', read(os.path.join(ROOT, 'include', 'ei', 'rebirth_patches.inc')), re.M):
        found.add(('kPatches', name))
    return found


def doc(rows, names):
    out = ['# ei_settings.ini',
           '',
           'Generated by tools/gen_settings_doc.py from src/game/settings_catalog.inc. Do not edit.',
           '',
           'The game keeps its settings in `ei_settings.ini` next to the exe (or in `%LOCALAPPDATA%\\EvilIslands` when that',
           'folder cannot be written). Each `[section]` holds `name = value` lines; a line starting with `;` or `#` is a',
           'comment. When the file is missing or empty the game writes the defaults, each line with its description above',
           'it; it never adds comments to a file that exists. A value the file lacks takes the default below.',
           '',
           'Defaults: the value a new file holds, else the one the game takes when the line is absent. `(profile)`: the',
           "game folder's profile decides (Rebirth's patches in a Rebirth install, none in a plain one).",
           '',
           "The original game's settings this build does not read (a file that holds them keeps the lines; the game",
           'ignores them): ' + ', '.join(DROPPED) + '.']
    current = None
    for constant, name, default, values, description in rows:
        section = names[constant]
        if section != current:
            out += ['', f'## [{section}]', '', '| Name | Default | Values | What it does |', '|---|---|---|---|']
            current = section
        cell = lambda t: t.replace('|', '\\|') if t else ''
        out.append(f'| `{name}` | {cell(default) and "`" + cell(default) + "`"} | {cell(values)} | {cell(description)} |')
    return '\n'.join(out) + '\n'


def description_lines(row):
    """A row's description as the defaults file's comment lines (settings_document.cpp DescriptionLines)."""
    _, _, _, values, description = row
    text = description + ('' if values == 'true / false' else f' ({values})')
    lines, line = [], ''
    for word in text.split(' '):
        if line and len(line) + 1 + len(word) > COMMENT_WIDTH:
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
