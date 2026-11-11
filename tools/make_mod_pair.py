"""The mods of a pair of games (MF16, docs/notes/network.md "Mods"): the showcase mod for the scenario's game and its
peer, each as it is, changed, or not at all.

usage: python tools/make_mod_pair.py <saves dir> <this game's> <the peer's>

A scenario's prepare line: <saves dir> is the run's harness_saves; the peer's is the run folder's peer\\harness_saves
(tools/run_all.py launch). Each mod is:
  showcase   examples/showcase_mod copied as harness_saves\\showcase_mod (make_showcase_mod.py)
  changed    the same, its Maps\\zone1.lua one comment line longer: the same title, other files (a counted file:
             src/engine/mods/mod_hash.h), so the content hash differs
  spell      the same with MF17d's scripted spell added (tools/make_spell_mod.py's add_spell), its blow 20 so
             that the target's hit points drop (net_pair_mod_spell's clienthurt)
  none       nothing
The scenarios name the mod with their own "mod harness_saves\\showcase_mod" line.
"""
import os, shutil, sys

import make_showcase_mod
import make_spell_mod

CHANGED_LINE = '-- make_mod_pair.py: this copy is another version of the mod\n'


def place(saves, kind):
    if kind == 'none':
        return
    if kind not in ('showcase', 'changed', 'spell'):
        sys.exit(f'make_mod_pair: unknown mod {kind!r} (showcase, changed, spell or none)')
    target = os.path.join(saves, make_showcase_mod.NAME)
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(make_showcase_mod.MOD, target)
    if kind == 'changed':
        with open(os.path.join(target, 'Maps', 'zone1.lua'), 'a', encoding='utf-8', newline='\n') as f:
            f.write(CHANGED_LINE)
    if kind == 'spell':
        make_spell_mod.add_spell(target, damage=20)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    saves, ours, peers = sys.argv[1:]
    place(saves, ours)
    place(os.path.join(os.path.dirname(os.path.abspath(saves)), 'peer', 'harness_saves'), peers)


if __name__ == '__main__':
    main()
