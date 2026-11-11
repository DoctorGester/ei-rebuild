"""The engine's layering rule (docs/notes/file-layout.md, "The engine library"): the files of ei_engine (CMakeLists.txt)
include only the engine's own headers, src/render's, src/platform's, third_party's and the system's, never a game header
(units, world, ai, net, spells, script, ui, app, players, combat, objects, terrain, items, map, saves, sound, console
and the game-side effects).

The engine is src/lib and src/engine (core, data, models, scene, mods and the particle core, particles); the game is
src/game (with src/audio and src/video). tools/engine_layers.txt lists the includes still crossing the line (each a back-reference
MF15k2 and later cut); the check fails on a crossing include the list does not have and on a listed one that is gone
(take its line out: the list only shrinks).

usage: python tools/engine_layers.py [--write]   (--write rewrites the list from the sources, keeping its header and
                                                 each kept line's comment)
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
LIST = os.path.join(ROOT, 'tools', 'engine_layers.txt')
# The engine's folders under src/ (CMakeLists.txt's EI_ENGINE_SOURCES globs the same).
ENGINE_FOLDERS = ('lib/', 'engine/')
# What an engine file may include besides the engine's own files.
BELOW = ('render/', 'platform/')
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def in_engine(rel):
    return rel.startswith(ENGINE_FOLDERS)


def source_files():
    out = set()
    for dirpath, _, names in os.walk(SRC):
        for name in names:
            if name.endswith(('.cpp', '.h', '.inc')):
                out.add(os.path.relpath(os.path.join(dirpath, name), SRC).replace(os.sep, '/'))
    return out


def crossings():
    """(engine file, game header) for every include of a game header by an engine file, both relative to src/."""
    files = source_files()
    found = set()
    for rel in sorted(files):
        if not in_engine(rel):
            continue
        with open(os.path.join(SRC, rel), encoding='utf-8', errors='replace') as f:
            code = COMMENT.sub('', f.read())
        for target in INCLUDE.findall(code):
            if target not in files:
                beside = os.path.normpath(os.path.join(os.path.dirname(rel), target)).replace(os.sep, '/')
                if beside not in files:
                    continue   # a generated header (effect_files/builtin.h) or a third-party one
                target = beside
            if not in_engine(target) and not target.startswith(BELOW):
                found.add((rel, target))
    return found


HEADER = """# The engine's includes of game headers still left (tools/engine_layers.py; docs/notes/file-layout.md, "The engine
# library"): ENGINE_FILE GAME_HEADER [# what it needs], relative to src/. The list only shrinks: a line goes when its
# include does (python tools/engine_layers.py --write).
"""


def read_list():
    """The listed includes, each with its line's comment."""
    listed = {}
    if not os.path.exists(LIST):
        return listed
    with open(LIST, encoding='utf-8') as f:
        for line in f:
            text, _, why = line.partition('#')
            words = text.split()
            if words:
                listed[(words[0], words[1])] = why.strip()
    return listed


def write_list(found, listed):
    width = max((len(rel) for rel, _ in found), default=0)
    lines = [HEADER]
    for rel, target in sorted(found):
        why = listed.get((rel, target), '')
        line = f'{rel:<{width}} {target}'
        lines.append(f'{line:<{width + 32}} # {why}\n' if why else f'{line}\n')
    with open(LIST, 'w', encoding='utf-8', newline='\n') as f:
        f.write(''.join(lines))


def main(argv):
    found = crossings()
    listed = read_list()
    if '--write' in argv:
        write_list(found, listed)
        return 0
    listed = set(listed)
    problems = [f'engine_layers: {rel} includes the game header {target} (the engine includes no game header: '
                f'docs/notes/file-layout.md, "The engine library")' for rel, target in sorted(found - listed)]
    problems += [f'engine_layers: {rel} no longer includes {target}: take its line out of tools/engine_layers.txt'
                 for rel, target in sorted(listed - found)]
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
