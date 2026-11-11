"""src/game's and src/engine's folder layout (C168, MF15k1c; docs/notes/file-layout.md) from tools/layout_map.txt.
The map's destinations are folder/name; the engine's folders (ENGINE_FOLDERS) are under src/engine, the rest under
src/game.

  python tools/layout.py check      the map against the two trees: every file has one line, every destination one owner,
                                    every rename says why, every line's file is where the map's state allows (flat,
                                    moved, not made yet, or folded early into a flat owner); exit 1 on a problem
  python tools/layout.py listing    rewrites the generated part of docs/notes/file-layout.md (the tree's counts and
                                    every file's destination); --check fails when it is not current
  python tools/layout.py where NAME...  where a file of the flat src/game goes and whether it moved (an old name in a
                                    ticket or a note: `where ai_map_construct.cpp` -> ai/ai_map.cpp, fold)
  python tools/layout.py sizes      per move batch: files, folds, the #include lines and the other references
                                    (comments, notes, tools, tests, config) a batch rewrites
  python tools/layout.py apply BATCH [--dry-run]
                                    a move batch (C168d-h): `git mv` of each of its folders' files, each fold
                                    concatenated onto its owner (tidied by hand after), then `rewrite`. --dry-run
                                    prints what it would do and changes nothing
  python tools/layout.py rewrite [--dry-run]
                                    every game include rooted at src (`#include "game/units/unit.h"`), every include
                                    and reference of a moved or folded file to its path now, the baselines' rows
                                    (summed for folds), CMake lists without repeats. Idempotent: run it after merging
                                    main into a branch that was open across a batch
"""
import collections
import csv
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'src', 'game')
ENGINE = os.path.join(ROOT, 'src', 'engine')
# The map's folders that are src/engine's (the engine library: docs/notes/file-layout.md, "The engine library").
ENGINE_FOLDERS = ('core', 'data', 'models', 'scene', 'mods', 'particles')
MAP = os.path.join(ROOT, 'tools', 'layout_map.txt')
DOC = os.path.join(ROOT, 'docs', 'notes', 'file-layout.md')
BEGIN = '<!-- layout.py listing: generated from tools/layout_map.txt, do not edit by hand -->'
END = '<!-- layout.py listing end -->'
OPS = ('', 'fold', 'split', 'new')

Line = collections.namedtuple('Line', 'old new op why number')


def read_map():
    lines, batches = [], {}
    with open(MAP, encoding='utf-8') as f:
        text = f.read()
    for number, raw in enumerate(text.splitlines(), 1):
        text, _, why = raw.partition('#')
        words = text.split()
        if not words:
            continue
        if words[0] == 'batch':
            batch = words[1].rstrip(':')
            for folder in words[2:]:
                batches[folder] = batch
            continue
        op = words[2] if len(words) > 2 else ''
        lines.append(Line(words[0], words[1], op, why.strip(), number))
    return lines, batches


def present():
    """Paths of src/game's and src/engine's files, each relative to its tree (forward slashes): the map's form."""
    found = set()
    for tree in (GAME, ENGINE):
        for dirpath, _, names in os.walk(tree):
            for name in names:
                found.add(os.path.relpath(os.path.join(dirpath, name), tree).replace(os.sep, '/'))
    return found


def top_of(new):
    """The src/ folder a map path is in: 'engine' for the engine's folders, else 'game' (a flat name too)."""
    return 'engine' if '/' in new and new.split('/')[0] in ENGINE_FOLDERS else 'game'


def src_path(new):
    """A map path (core/ref.h, units/unit.cpp, a flat name) as the repo's path (src/engine/core/ref.h)."""
    return f'src/{top_of(new)}/{new}'


def owners_of(lines):
    """Each destination's owner: the line whose file becomes it (a move or a new file), else the split that keeps
    its main part there."""
    owners = {}
    for l in lines:
        if l.op in ('', 'new'):
            owners.setdefault(l.new, l)
    for l in lines:
        if l.op == 'split':
            owners.setdefault(l.new, l)
    return owners


def moves_itself(l, owners):
    """A line whose file goes to NEW whole (a move, a new file, a split's main part with no other owner); the others
    (a fold, a split's main part into another's file) join NEW."""
    return l.op in ('', 'new') or (l.op == 'split' and owners.get(l.new) is l)


def problems(lines, batches, files):
    out = []
    olds = collections.Counter(l.old for l in lines)
    out += [f'{name}: {n} lines' for name, n in olds.items() if n > 1]
    owners = collections.defaultdict(list)
    for l in lines:
        if l.op not in OPS:
            out.append(f'line {l.number}: unknown op {l.op!r}')
        folder, _, base = l.new.partition('/')
        if not base or '/' in base:
            out.append(f'line {l.number}: {l.new} is not folder/name')
        elif folder not in batches:
            out.append(f'line {l.number}: folder {folder} has no batch')
        if l.op in ('', 'new'):
            owners[l.new].append(l)
        if l.op in ('fold', 'split') and not l.why:
            out.append(f'line {l.number}: {l.old} {l.op} without a reason')
        if l.op == '' and base and base != l.old and not l.why:
            out.append(f'line {l.number}: {l.old} -> {l.new} renamed without a reason')
    out += [f'{new}: owned by {", ".join(l.old for l in ls)}' for new, ls in owners.items() if len(ls) > 1]
    # A split keeps its main part under NEW: it owns NEW when no move does (model.h -> scene/scene_renderer.h).
    for l in lines:
        if l.op == 'split' and l.new not in owners:
            owners[l.new].append(l)
    for l in lines:
        if l.op in ('fold', 'split') and l.new not in owners:
            out.append(f'line {l.number}: {l.old} goes into {l.new}, which no line owns')
    news = {l.new for l in lines}
    for name in sorted(files):
        if '/' in name:
            if name not in news:
                out.append(f'src/game/{name}: in a folder, but no line names it')
        elif name not in olds:
            out.append(f'src/game/{name}: no line in tools/layout_map.txt')
    # Every line's file where the map's state allows: a moving file flat or at NEW (not both, not neither; a split's
    # new file may not be made yet), a folded file flat or gone with NEW there.
    first = owners_of(lines)
    for l in lines:
        old, new = l.old in files, l.new in files
        if moves_itself(l, first):
            if old and new:
                out.append(f'line {l.number}: {l.old} is both flat and at {l.new}')
            elif not old and not new and l.op != 'new':
                out.append(f'line {l.number}: {l.old} is neither in src/game nor at {l.new}')
        elif l.op == 'fold' and not old and not new and not folded_early(l, first, files):
            out.append(f'line {l.number}: {l.old} is gone but {l.new}, which it folds into, is not there')
    return out


def folded_early(l, owners, files):
    """A fold a split batch made while its owner was still flat (misc_400.cpp into server_specific_manager.cpp): its
    file gone, NEW not there yet, NEW's owner flat. Its move batch counts it done."""
    owner = owners.get(l.new)
    return l.op == 'fold' and l.old not in files and l.new not in files and owner is not None and owner.old in files


def lines_counted(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


def listing(lines, batches):
    owned = collections.defaultdict(list)
    for new, l in owners_of(lines).items():
        owned[new.split('/')[0]].append(l)
    folders = sorted(owned)
    out = [BEGIN, '', '### The tree after the moves', '',
           '| Folder | Files | Lines today | Move batch |', '|---|---|---|---|']
    total_files = total_lines = 0
    for folder in folders:
        moved = [l for l in lines if l.new.split('/')[0] == folder and l.op != 'new']
        size = sum(lines_counted(os.path.join(GAME, l.old)) or lines_counted(os.path.join(ROOT, src_path(l.new)))
                   for l in moved)
        out.append(f'| `{folder}/` | {len(owned[folder])} | {size} | {batches[folder]} |')
        total_files += len(owned[folder])
        total_lines += size
    out += [f'| all | {total_files} | {total_lines} | |', '']
    folds = [l for l in lines if l.op == 'fold']
    splits = [l for l in lines if l.op == 'split']
    renames = [l for l in lines if l.op == '' and os.path.basename(l.new) != l.old]
    news = [l for l in lines if l.op == 'new']
    out += [f'{len(lines) - len(news)} files today; {len(folds)} fold into an owner, {len(splits)} split by meaning, '
            f'{len(news)} new files come out of the splits, {len(renames)} moves rename for meaning.', '']
    out += ['### Every file', '',
            'Op: blank a move (under the same name unless a reason is given), **fold** the content '
            'joins the destination, **split** divided by meaning (the destination keeps the main part), **new** a '
            'file a split creates.', '']
    for folder in folders:
        out += [f'#### `{folder}/` ({batches[folder]})', '', '| Today | After | Op | Why |', '|---|---|---|---|']
        here = sorted((l for l in lines if l.new.split('/')[0] == folder),
                      key=lambda l: (l.new, OPS.index(l.op), l.old))
        for l in here:
            why = l.why.replace('|', '\\|')
            out.append(f'| {l.old} | {l.new} | {l.op} | {why} |')
        out.append('')
    out.append(END)
    return '\n'.join(out) + '\n'


def write_listing(lines, batches, check):
    doc = open(DOC, encoding='utf-8').read()
    start, end = doc.find(BEGIN), doc.find(END)
    if start < 0 or end < 0:
        sys.exit(f'{os.path.relpath(DOC, ROOT)}: no listing markers')
    new = doc[:start] + listing(lines, batches) + doc[end + len(END) + 1:]
    if check:
        if new != doc:
            sys.exit(f'{os.path.relpath(DOC, ROOT)}: the listing is not current (python tools/layout.py listing)')
        return
    with open(DOC, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new)


REF = re.compile(r'(?<![\w.])(?:src/game/)?([a-z0-9_]+\.(?:cpp|h|inc))\b')


def sizes(lines, batches):
    """Per move batch: the files it moves and folds, and the lines elsewhere that name them (#include lines, and
    other references: comments, notes, tools, tests, config; done tickets are history and not counted)."""
    batch_of = {l.old: batches[l.new.split('/')[0]] for l in lines}
    moved = collections.Counter(batch_of[l.old] for l in lines if l.op != 'new')
    folded = collections.Counter(batch_of[l.old] for l in lines if l.op == 'fold')
    includes, refs = collections.Counter(), collections.Counter()
    for path in text_files():
        for text in open(path, encoding='utf-8', errors='replace'):
            include = text.lstrip().startswith('#include')
            for m in REF.finditer(text):
                batch = batch_of.get(m.group(1))
                if batch:
                    (includes if include else refs)[batch] += 1
    print('batch   files  folds  #include lines  other references')
    for batch in sorted(set(batches.values())):
        print(f'{batch:7} {moved[batch]:5}  {folded[batch]:5}  {includes[batch]:14}  {refs[batch]:16}')


# ---- Where each file is now (the tools that name game files by their flat names ask here) ------------------------

_now = None


def moved_map(lines=None, files=None, planned=()):
    """OLD -> NEW for every line whose file has gone to NEW (OLD gone, NEW there), and for the lines in `planned`
    (a batch's dry run); a fold made early -> its owner's flat name. A split into another's file is left out: its
    batch rewrote its references by hand."""
    if lines is None:
        lines, _ = read_map()
    files = present() if files is None else files
    owners = owners_of(lines)
    out = {}
    for l in lines:
        if l.op == 'split' and owners.get(l.new) is not l:
            continue
        if (l.old not in files and l.new in files) or l in planned:
            out[l.old] = l.new
        elif folded_early(l, owners, files):
            out[l.old] = owners[l.new].old
    return out


def now(name):
    """A src/game file named by its flat name (or by its path now): its map path now (src_path gives the repo's)."""
    global _now
    if _now is None:
        _now = moved_map() if os.path.exists(MAP) else {}
    return _now.get(name, name)


def game_paths(*names):
    """src/game/... and src/engine/... paths for flat names or name prefixes ('ui_cursor', 'game_buffer', 'random.cpp'): each file of
    the flat tree whose name starts with one, where it is now (a fold: the file it joined), and each flat file the
    map does not know yet."""
    lines = read_map()[0] if os.path.exists(MAP) else []
    flat = sorted(n for n in os.listdir(GAME) if os.path.isfile(os.path.join(GAME, n)))
    out = []
    for name in names:
        out += [src_path(now(l.old)) for l in lines if l.old.startswith(name)]
        out += ['src/game/' + n for n in flat if n.startswith(name)]
    return tuple(dict.fromkeys(out))


def flat_name(rel):
    """The flat name of a src/game or src/engine path (src/engine/core/coder.cpp -> net_coder.cpp: the owner's line; a
    name the map does not know is its own)."""
    base = rel.split('/', 2)[2] if rel.startswith(('src/game/', 'src/engine/')) else rel
    if '/' not in base:
        return base
    global _flat
    if _flat is None:
        lines = read_map()[0] if os.path.exists(MAP) else []
        owners = owners_of(lines)
        _flat = {new: l.old for new, l in owners.items()}
    return _flat.get(base, os.path.basename(base))


_flat = None


# ---- The rewrite --------------------------------------------------------------------------------------------------

# What the rewrite reads: the sources, tests, tools, notes and config (not the tickets: done ones are history, open
# ones change through tools/tickets.py; not the goldens; not the plan itself).
TOPS = ('src', 'tests', 'tools', 'docs', 'config', '.github', 'CMakeLists.txt', 'README.md', 'CLAUDE.md',
        'build.cmd', 'harness.cmd')
SKIP_DIRS = {'tickets', 'golden', 'third_party', '__pycache__', 'node_modules', '.git'}
EXTS = ('.cpp', '.h', '.inc', '.c', '.mm', '.hlsl', '.asm', '.md', '.py', '.txt', '.csv', '.cmake', '.yml', '.yaml',
        '.cmd', '.sh', '.json', '.html', '.ini')
CPP = ('.cpp', '.h', '.inc', '.c', '.mm')
BASELINES = ('config/cleanup_baseline.csv', 'config/comment_baseline.csv')


def text_files():
    """The files the rewrite reads (absolute paths)."""
    keep = {os.path.normcase(p) for p in (MAP, DOC, os.path.abspath(__file__),
                                           os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_layout.py'))}
    for top in TOPS:
        path = os.path.join(ROOT, top)
        if os.path.isfile(path):
            yield path
            continue
        for dirpath, dirs, names in os.walk(path):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for name in names:
                full = os.path.join(dirpath, name)
                if name.endswith(EXTS) and os.path.normcase(full) not in keep:
                    yield full


GAME_PATHS_CALL = re.compile(r'\bgame_paths\([^()]*\)')
INCLUDE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*")([^"\n]+)(")', re.M)


def root_include(path, target, moved):
    """`#include "TARGET"` in PATH as src/game's rooted form ("game/units/unit.h"), or None when it names no game file.
    A bare name or a relative path is found as the compiler finds it (beside the includer), a flat name the map moved
    at its path now."""
    if target.startswith(('game/', 'engine/', 'lib/', 'platform/', 'render/', 'audio/', 'video/')):
        return None
    src = os.path.join(ROOT, 'src')
    in_game = os.path.normcase(path).startswith((os.path.normcase(GAME + os.sep), os.path.normcase(ENGINE + os.sep)))
    if in_game and '/' not in target and target in moved:
        return src_path(moved[target])[len('src/'):]
    found = os.path.normpath(os.path.join(os.path.dirname(path), target))
    if not os.path.isfile(found) and in_game and '/' not in target:
        found = os.path.join(GAME, target)
    if not os.path.isfile(found):
        return None
    rel = os.path.relpath(found, src).replace(os.sep, '/')
    if rel.startswith('../'):
        return None
    if rel.startswith(('game/', 'engine/')) or in_game:
        return rel
    return None


class Rewrite:
    def __init__(self, moved):
        self.moved = moved
        names = sorted(moved, key=len, reverse=True)
        alternatives = '|'.join(re.escape(n) for n in names) or r'(?!x)x'
        # A path into src/game: `src/game/OLD` (also `../../src/game/OLD`) or `game/OLD` (an include, the harness).
        self.paths = re.compile(r'(?<![\w.])((?:src/)?game/)(' + alternatives + r')(?!\w)')
        # A bare name in prose and comments, for a file whose name changed (a meaning rename, a fold); not a name
        # some other file in the repo has too (bink_video.cpp: src/video's decoder).
        others = other_names()
        self.renamed = {n: v for n, v in moved.items() if os.path.basename(v) != n and n not in others}
        self.ambiguous = sorted(n for n, v in moved.items() if os.path.basename(v) != n and n in others)
        bare = '|'.join(re.escape(n) for n in sorted(self.renamed, key=len, reverse=True)) or r'(?!x)x'
        self.bare = re.compile(r'(?<![\w./\\-])(' + bare + r')(?!\w)')
        self.counts = collections.Counter()

    def text(self, path, text):
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        if path.endswith('.py'):
            # The arguments of layout.game_paths(...) are flat names: the call asks the map where they are now, so
            # they stay as written.
            pieces, last = [], 0
            for m in GAME_PATHS_CALL.finditer(text):
                pieces.append(self.segment(path, rel, text[last:m.start()]))
                pieces.append(m.group(0))
                last = m.end()
            pieces.append(self.segment(path, rel, text[last:]))
            return ''.join(pieces)
        text = self.segment(path, rel, text)
        if rel in BASELINES:
            text = merge_rows(text, self)
        if rel.endswith('CMakeLists.txt'):
            text = no_repeats(text, self)
        return text

    def segment(self, path, rel, text):
        if path.endswith(CPP) and rel.startswith(('src/', 'tests/')):
            def include(m):
                rooted = root_include(path, m.group(2), self.moved)
                if rooted is None or rooted == m.group(2):
                    return m.group(0)
                self.counts['includes rooted'] += 1
                return m.group(1) + rooted + m.group(3)
            text = INCLUDE.sub(include, text)

        def path_ref(m):
            self.counts['paths'] += 1
            new = self.moved[m.group(2)]
            return m.group(1).replace('game/', top_of(new) + '/') + new
        text = self.paths.sub(path_ref, text)

        def bare_ref(m):
            self.counts['renamed names'] += 1
            return self.renamed[m.group(1)]
        out = []
        for line in text.split('\n'):
            out.append(line if line.lstrip().startswith('#include') else self.bare.sub(bare_ref, line))
        return '\n'.join(out)


def other_names():
    """Base names of the repo's files outside src/game (tests, tools, the rest of src)."""
    out = set()
    for top in ('src', 'tests', 'tools'):
        for dirpath, dirs, names in os.walk(os.path.join(ROOT, top)):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            if os.path.normcase(dirpath).startswith((os.path.normcase(GAME), os.path.normcase(ENGINE))):
                continue
            out.update(names)
    return out


def merge_rows(text, rewrite):
    """A baseline (file, counts...) with the rows of folded files summed into their owner's, sorted by file."""
    rows = list(csv.reader(io.StringIO(text)))
    if not rows or rows[0][:1] != ['file']:
        return text
    merged = collections.OrderedDict()
    for r in rows[1:]:
        if not r:
            continue
        if r[0] in merged:
            rewrite.counts['baseline rows summed'] += 1
            merged[r[0]] = [r[0]] + [str(int(a or 0) + int(b or 0)) for a, b in zip(merged[r[0]][1:], r[1:])]
        else:
            merged[r[0]] = r
    if len(merged) == len(rows) - 1 and [r[0] for r in rows[1:]] == sorted(merged):
        return text
    out = io.StringIO()
    w = csv.writer(out, lineterminator='\n')
    w.writerow(rows[0])
    for key in sorted(merged):
        w.writerow(merged[key])
    return out.getvalue()


def no_repeats(text, rewrite):
    """A CMake command's src/game sources named once (a fold gives two names of one file)."""
    def command(m):
        seen = set()

        def token(t):
            if t.group(1) in seen:
                rewrite.counts['CMake repeats'] += 1
                return ''
            seen.add(t.group(1))
            return t.group(0)
        return re.sub(r'[ \t]*\b(src/(?:game|engine)/[\w/]+\.cpp)\b', token, m.group(0))
    return re.sub(r'\b(?:add_executable|add_library|target_sources)\s*\([^()]*\)', command, text)


def rewrite(moved, dry_run):
    """The rewrite over the repo's text files; prints what it changes. Returns the changed files."""
    r = Rewrite(moved)
    changed = []
    for path in text_files():
        try:
            with open(path, encoding='utf-8', newline='') as f:
                text = f.read()
        except UnicodeDecodeError:
            continue
        new = r.text(path, text)
        if new != text:
            changed.append(path)
            if not dry_run:
                with open(path, 'w', encoding='utf-8', newline='') as f:
                    f.write(new)
    for n in r.ambiguous:
        print(f'layout: {n} also names a file outside src/game: its bare references are left for a reading')
    what = ', '.join(f'{v} {k}' for k, v in sorted(r.counts.items())) or 'nothing'
    print(f'layout: rewrite{" (dry run)" if dry_run else ""}: {what} in {len(changed)} files')
    return changed


# ---- A move batch -------------------------------------------------------------------------------------------------

def fold_text(owner, folded, old):
    """FOLDED's text joined to OWNER's: in place of OWNER's include of it when there is one (the owner uses it), else
    after it; a header's `#pragma once` dropped. The fold's tidying (one include list, one anonymous namespace, the
    file comment) is by hand."""
    body = re.sub(r'^[ \t]*#[ \t]*pragma[ \t]+once[ \t]*\n', '', folded, count=1, flags=re.M).strip('\n') + '\n'
    include = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"(?:game/)?' + re.escape(old) + r'"[^\n]*\n', re.M)
    if include.search(owner):
        return include.sub(lambda m: body, owner, count=1)
    return owner.rstrip('\n') + '\n\n' + body


def git(*args):
    subprocess.run(['git', *args], cwd=ROOT, check=True)


def apply(lines, batches, batch, dry_run):
    folders = [f for f, b in batches.items() if b == batch]
    if not folders:
        print(f'layout: no folder has batch {batch} (tools/layout_map.txt: {", ".join(sorted(set(batches.values())))})')
        return 1
    files = present()
    found = problems(lines, batches, files)
    for p in found:
        print(f'layout: {p}')
    if found:
        print('layout: the map does not match src/game; nothing moved')
        return 1
    owners = owners_of(lines)
    mine = [l for l in lines if l.new.split('/')[0] in folders]
    moves, folds, done, errors = [], [], 0, []
    for l in mine:
        old, new = l.old in files, l.new in files
        if moves_itself(l, owners):
            if old:
                moves.append(l)
            elif new:
                done += 1
            else:
                errors.append(f'line {l.number}: {l.old} not made yet (its split batch comes first)')
        elif old:
            folds.append(l)
        else:
            done += 1
    for l in folds:
        if not (owners.get(l.new) in moves or l.new in files):
            errors.append(f'line {l.number}: {l.old} folds into {l.new}, which neither exists nor moves now')
    for e in errors:
        print(f'layout: {e}')
    if errors:
        return 1
    for l in moves:
        print(f'move  {l.old} -> {l.new}')
    for l in folds:
        print(f'fold  {l.old} -> {l.new}')
    print(f'layout: {batch} ({" ".join(folders)}): {len(moves)} moves, {len(folds)} folds, {done} done before')
    planned = set(moves) | set(folds)
    if dry_run:
        rewrite(moved_map(lines, files, planned), True)
        return 0
    for l in moves:
        os.makedirs(os.path.dirname(os.path.join(ROOT, src_path(l.new))), exist_ok=True)
        git('mv', f'src/game/{l.old}', src_path(l.new))
    for l in folds:
        owner_path, folded_path = os.path.join(ROOT, src_path(l.new)), os.path.join(GAME, l.old)
        with open(owner_path, encoding='utf-8', newline='') as f:
            owner = f.read()
        with open(folded_path, encoding='utf-8', newline='') as f:
            folded = f.read()
        with open(owner_path, 'w', encoding='utf-8', newline='') as f:
            f.write(fold_text(owner, folded, l.old))
        git('rm', '-q', f'src/game/{l.old}')
    rewrite(moved_map(lines), False)
    if folds:
        print(f'layout: tidy the {len(folds)} folds by hand (docs/notes/file-layout.md, "The rules": one include '
              f'list, one anonymous namespace), then build')
    return 0


def main(argv):
    commands = ('check', 'listing', 'sizes', 'where', 'apply', 'rewrite')
    if len(argv) < 2 or argv[1] not in commands or (argv[1] == 'apply' and len(argv) < 3):
        print(__doc__)
        return 2
    lines, batches = read_map()
    dry_run = '--dry-run' in argv
    if argv[1] == 'check':
        found = problems(lines, batches, present())
        for p in found:
            print(f'layout: {p}')
        print(f'layout: {len(lines)} lines, {len(found)} problems')
        return 1 if found else 0
    if argv[1] == 'listing':
        write_listing(lines, batches, '--check' in argv)
        return 0
    if argv[1] == 'where':
        by_old = {l.old: l for l in lines}
        files = present()
        for name in argv[2:]:
            l = by_old.get(os.path.basename(name))
            if not l:
                print(f'{name}: not in tools/layout_map.txt')
                continue
            state = ('moved' if l.new in files and l.old not in files else
                     'flat' if l.old in files else 'not made yet' if l.op == 'new' else 'gone')
            print(f'{name}: {l.new}' + (f', {l.op}' if l.op else '') + f' ({state})' +
                  (f': {l.why}' if l.why else ''))
        return 0
    if argv[1] == 'apply':
        return apply(lines, batches, argv[2], dry_run)
    if argv[1] == 'rewrite':
        rewrite(moved_map(lines), dry_run)
        return 0
    sizes(lines, batches)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
