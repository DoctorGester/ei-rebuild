"""src/game's folder layout (C168; docs/notes/file-layout.md) from tools/layout_map.txt.

  python tools/layout.py check      the map against src/game: every file has one line, every destination one owner,
                                    every name change beyond the folder rule says why (exit 1 on a problem)
  python tools/layout.py listing    rewrites the generated part of docs/notes/file-layout.md (the tree's counts and
                                    every file's destination); --check fails when it is not current
  python tools/layout.py where NAME...  where a file of today's flat src/game goes (an old name in a ticket or
                                    a note: `where ai_map_construct.cpp` -> ai/map.cpp, fold)
  python tools/layout.py sizes      per move batch: files, folds, the #include lines and the other references
                                    (comments, notes, tools, tests, config) a batch rewrites

The moves themselves (git mv, the include and reference rewrite, CMake) are the batches' tool (C168a).
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'src', 'game')
MAP = os.path.join(ROOT, 'tools', 'layout_map.txt')
DOC = os.path.join(ROOT, 'docs', 'notes', 'file-layout.md')
BEGIN = '<!-- layout.py listing: generated from tools/layout_map.txt, do not edit by hand -->'
END = '<!-- layout.py listing end -->'
OPS = ('', 'fold', 'split', 'new')

Line = collections.namedtuple('Line', 'old new op why number')


def read_map():
    lines, batches = [], {}
    for number, raw in enumerate(open(MAP, encoding='utf-8'), 1):
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
    """Paths of src/game's files, relative to src/game (forward slashes)."""
    found = set()
    for dirpath, _, names in os.walk(GAME):
        for name in names:
            found.add(os.path.relpath(os.path.join(dirpath, name), GAME).replace(os.sep, '/'))
    return found


def rule_name(old, folder):
    """The folder rule: a name starting with the folder's word (the folder, or without its plural s) and '_' drops
    it; a name that is only the word keeps it."""
    stem, ext = os.path.splitext(old)
    for word in {folder, folder[:-1] if folder.endswith('s') else folder}:
        if stem.startswith(word + '_'):
            return stem[len(word) + 1:] + ext
    return old


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
        if l.op == '' and base and os.path.basename(l.new) != rule_name(l.old, folder) and not l.why:
            out.append(f'line {l.number}: {l.old} -> {l.new} renamed beyond the folder rule without a reason')
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
                out.append(f'src/game/{name}: moved, but no line names it')
        elif name not in olds:
            out.append(f'src/game/{name}: no line in tools/layout_map.txt')
    for l in lines:
        if l.op in ('', 'split', 'fold') and l.old not in files and l.new not in files and l.op == '':
            out.append(f'line {l.number}: {l.old} is neither in src/game nor at {l.new}')
    return out


def lines_counted(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


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
        size = sum(lines_counted(os.path.join(GAME, l.old)) for l in moved)
        out.append(f'| `{folder}/` | {len(owned[folder])} | {size} | {batches[folder]} |')
        total_files += len(owned[folder])
        total_lines += size
    out += [f'| all | {total_files} | {total_lines} | |', '']
    folds = [l for l in lines if l.op == 'fold']
    splits = [l for l in lines if l.op == 'split']
    renames = [l for l in lines if l.op == '' and os.path.basename(l.new) != rule_name(l.old, l.new.split('/')[0])]
    news = [l for l in lines if l.op == 'new']
    out += [f'{len(lines) - len(news)} files today; {len(folds)} fold into an owner, {len(splits)} split by meaning, '
            f'{len(news)} new files come out of the splits, {len(renames)} moves rename beyond the folder rule.', '']
    out += ['### Every file', '',
            'Op: blank a move (a name change is the folder rule\'s unless a reason is given), **fold** the content '
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
    for top in ('src', 'tests', 'tools', 'docs', 'config', 'CMakeLists.txt'):
        path = os.path.join(ROOT, top)
        walk = [(ROOT, [], [top])] if os.path.isfile(path) else os.walk(path)
        for dirpath, dirs, names in walk:
            dirs[:] = [d for d in dirs if d not in ('golden', 'tickets', 'third_party')]
            for name in names:
                if not name.endswith(('.cpp', '.h', '.inc', '.md', '.py', '.txt', '.csv', '.cmake', '.mm', '.c')):
                    continue
                if os.path.join(dirpath, name) in (MAP, DOC):   # the plan itself
                    continue
                with open(os.path.join(dirpath, name), encoding='utf-8', errors='replace') as f:
                    for text in f:
                        include = text.lstrip().startswith('#include')
                        for m in REF.finditer(text):
                            batch = batch_of.get(m.group(1))
                            if batch:
                                (includes if include else refs)[batch] += 1
    print('batch   files  folds  #include lines  other references')
    for batch in sorted(set(batches.values())):
        print(f'{batch:7} {moved[batch]:5}  {folded[batch]:5}  {includes[batch]:14}  {refs[batch]:16}')


def main(argv):
    if len(argv) < 2 or argv[1] not in ('check', 'listing', 'sizes', 'where'):
        print(__doc__)
        return 2
    lines, batches = read_map()
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
        for name in argv[2:]:
            l = by_old.get(os.path.basename(name))
            print(f'{name}: ' + (f'{l.new}' + (f', {l.op}' if l.op else '') + (f' ({l.why})' if l.why else '')
                                 if l else 'not in tools/layout_map.txt'))
        return 0
    sizes(lines, batches)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
