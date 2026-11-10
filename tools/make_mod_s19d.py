"""Writes a Rebirth mod for the S19d scenarios (the mod-folder patches the S19b scenario leaves unchecked).

usage: python tools/make_mod_s19d.py <saves dir> stream|archives|maps|loose|toml
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
  loose     loose files over archives (tests/scenarios/mod_loose_files.txt; docs/notes/mods.md, "Loose files"):
              Res\\s19d_texts_a.res   the mod's texts.res ([RES]): s19d.txt, s19d_c.txt, s19d_d.txt
              Res\\texts\\            s19d.txt over the mod's archive, Sub\\S19D_Sub.TXT in a sub-folder
              Res\\s19d_texts_a\\     s19d_d.txt: the mod's archive's own folder over its entry
              Res\\outro\\outro.rtf   over the game's outro.res (no [RES] line for it)
              Maps\\s19d_loose\\      a folder that stands for s19d_loose.mpr
  toml      records as TOML (tests/scenarios/mod_toml_records.txt; docs/notes/mods.md, "Records as TOML"):
              config.reg.toml                 the mod's config.reg as TOML (no config.reg)
              Res\\texts\\texts.toml          one string of the game's texts.res changed ("string quit")
              Res\\database\\acks.db.toml     one record of the game's acks.db changed (Haburu's speeches)
Rebirth.txt and a config.reg with [MOD] Title are written in every kind (as the shipped mods have them; toml: its
TOML).
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


def copy_archive(source, target):
    """The install's archive at source copied to target; from an unpacked install (tools/unpack_install.py) the folder
    of its name packed into one."""
    if os.path.exists(source):
        return copy(source, target)
    folder = os.path.splitext(source)[0]
    files = {}
    for path, _, names in os.walk(folder):
        for name in sorted(names):
            full = os.path.join(path, name)
            with open(full, 'rb') as f:
                files[os.path.relpath(full, folder).replace(os.sep, '\\')] = f.read()
    write(target, mod.archive(files))


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
    copy_archive(vanilla('Maps', 'zone2.mpr'), os.path.join(root, 'Maps', 'zone2.mpr'))
    copy_archive(vanilla('Maps', 'z9q2.mq'), os.path.join(root, 'Maps', 'z9q2.mq'))
    write(os.path.join(root, 'Maps', 's19d', 'note.txt'), b'a file of a subfolder\r\n')


def loose(root):
    config(root, {'texts.res': 'Res\\s19d_texts_a.res'})
    write(os.path.join(root, 'Res', 's19d_texts_a.res'), mod.archive({
        's19d.txt': b'first\r\n',                      # 7 bytes
        's19d_c.txt': b'only in the archive\r\n',      # 21
        's19d_d.txt': b'archive copy\r\n'}))           # 14
    write(os.path.join(root, 'Res', 'texts', 's19d.txt'), b'the mod folder\r\n')             # 16
    write(os.path.join(root, 'Res', 'texts', 'Sub', 'S19D_Sub.TXT'), b'sub file\n')          # 9
    write(os.path.join(root, 'Res', 's19d_texts_a', 's19d_d.txt'), b'own folder\r\n')        # 12
    write(os.path.join(root, 'Res', 'outro', 'outro.rtf'), b'loose credits\r\n')             # 15
    write(os.path.join(root, 'Maps', 's19d_loose', 's19d_loose.mp'), b'a map folder\r\n')


def toml(root):
    write(os.path.join(root, 'config.reg.toml'), b'[MOD]\nTitle = "S19d test mod"\n')
    write(os.path.join(root, 'Rebirth.txt'), b'')
    write(os.path.join(root, 'Res', 'texts', 'texts.toml'), b'"string quit" = "MF3 mod text"\n')
    # The record replaces the game's whole Haburu record: s24 gets another sound, s25 is gone.
    write(os.path.join(root, 'Res', 'database', 'acks.db.toml'),
          b'[[unit]]\nname = "Haburu"\nscenario = [\n'
          b'    { sound = "Acks\\\\Haburu\\\\Scenario\\\\MF3.wav", chance = 100, text = "MF3", name = "s24" },\n]\n')


KINDS = {'stream': stream, 'archives': archives, 'maps': maps, 'loose': loose, 'toml': toml}


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
