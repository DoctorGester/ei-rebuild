"""Writes a Rebirth mod for the S19d scenarios (the mod-folder patches the S19b scenario leaves unchecked).

usage: python tools/make_mod_s19d.py <saves dir> stream|archives|maps
  <saves dir>   the run's harness_saves folder; the mod goes to <saves dir>\\s19d_mod, which the scenario selects with
                the harness step "mod harness_saves\\s19d_mod" (the settings' Mod)

Every file is a copy or a repack of the vanilla install's own data (EI_GAME_VANILLA; read only) or a few made-up bytes;
nothing is committed. Kinds (docs/notes/rebirth.md, "Resources and paths"):
  stream    StreamPatch and MoviesPatch (tests/scenarios/rebirth_mod_stream_movies.txt)
              Stream\\s19d.mp3      a copy of the install's murder.mp3: a track the game has no file of
              Stream\\bravery.mp3   a copy of rage.mp3: a track the game has, in the mod another one
              Movies\\crdtfin.bik   a copy of the install's progres.bik under the name of a movie the game has
  archives  ResOverride's lists (tests/scenarios/rebirth_mod_archives.txt), config.reg [RES]:
              texts.res    = [Res\\s19d_texts_a.res, Res\\s19d_texts_b.res]   two mod archives, the first one wins
              outro.res    = [Res\\s19d_outro.res, "-"]                       the game's archive switched off
              figures.res  = Res\\s19d_figures.res                            extra figure archive (before the game's)
              textures.res = Res\\s19d_textures.res                           also overrides the game's archive
              menus.res    = Res\\s19d_menus.res                              extra figure (before) and texture (after)
  maps      Maps (tests/scenarios/rebirth_mod_maps.txt, rebirth_mod_zone_table.txt)
              Maps\\zone2.mpr       a copy of the install's: zone2's map from the mod, its .mob from the game's folder
              Maps\\z9q2.mq         a copy of the install's: the zone table and the quest files read the mod's folder
              Maps\\s19d\\note.txt   a file of a subfolder, listed as "s19d\\note.txt"
Rebirth.txt and a config.reg with [MOD] Title are written in every kind (as the shipped mods have them).
"""
import argparse, os, shutil, sys

import exe_path
import make_test_mod as mod

MOD = 's19d_mod'


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def copy(source, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.copyfile(source, target)


def vanilla(*names):
    return exe_path.install_file('vanilla', *names)


def config(root, res=None):
    sections = {'MOD': {'Title': 'S19d test mod'}}
    if res:
        sections['RES'] = res
    write(os.path.join(root, 'config.reg'), mod.registry(sections))
    write(os.path.join(root, 'Rebirth.txt'), b'')


def stream(root):
    config(root)
    copy(vanilla('Stream', 'murder.mp3'), os.path.join(root, 'Stream', 's19d.mp3'))
    copy(vanilla('Stream', 'rage.mp3'), os.path.join(root, 'Stream', 'bravery.mp3'))
    copy(vanilla('Movies', 'progres.bik'), os.path.join(root, 'Movies', 'crdtfin.bik'))


def archives(root):
    res = {
        'texts.res': ['Res\\s19d_texts_a.res', 'Res\\s19d_texts_b.res'],
        'outro.res': ['Res\\s19d_outro.res', '-'],
        'figures.res': 'Res\\s19d_figures.res',
        'textures.res': 'Res\\s19d_textures.res',
        'menus.res': 'Res\\s19d_menus.res',
    }
    config(root, res)
    out = lambda name, files: write(os.path.join(root, 'Res', name), mod.archive(files))
    out('s19d_texts_a.res', {'s19d.txt': b'first\r\n'})                                    # 7 bytes
    out('s19d_texts_b.res', {'s19d.txt': b'second file\r\n', 's19d_b.txt': b'only in b\r\n'})   # 13 and 11
    out('s19d_outro.res', {'outro_mod.txt': b'outro of the mod\r\n'})                      # 18
    # unmodr.anm is the dragon's animation, in the game's figures.res (and Rebirth's), loaded only when a dragon appears.
    out('s19d_figures.res', {'s19d.fig': b'a figure of the mod', 'unmodr.anm': b'dragon, mod'})   # 19 and 11
    out('s19d_textures.res', {'s19d_t2.mmp': b'texture'})                                  # 7
    # armytent01.mmp is in the game's textures.res: the game's three archives come first for the texture manager.
    out('s19d_menus.res', {'s19d_tex.mmp': b'a texture of the mod, 30 bytes', 'armytent01.mmp': b'mod copy'})   # 30 and 8


def maps(root):
    config(root)
    copy(vanilla('Maps', 'zone2.mpr'), os.path.join(root, 'Maps', 'zone2.mpr'))
    copy(vanilla('Maps', 'z9q2.mq'), os.path.join(root, 'Maps', 'z9q2.mq'))
    write(os.path.join(root, 'Maps', 's19d', 'note.txt'), b'a file of a subfolder\r\n')


KINDS = {'stream': stream, 'archives': archives, 'maps': maps}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('saves')
    parser.add_argument('kind', choices=sorted(KINDS))
    args = parser.parse_args()
    root = os.path.join(args.saves, MOD)
    os.makedirs(root, exist_ok=True)
    KINDS[args.kind](root)
    print(f'{root}: {args.kind}')


if __name__ == '__main__':
    main()
