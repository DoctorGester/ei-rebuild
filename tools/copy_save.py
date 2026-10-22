"""Copies a save kept in the repository into a run's saves for a scenario ("# prepare:" line).

usage: python tools/copy_save.py <saves dir> <source> <slot>
  <saves dir>  the run's harness_saves folder (what the game sees as <game root>saves\\)
  <source>     a folder of .sav files, relative to the checkout (tests/saves/<name>)
  <slot>       the save folder to write (save1 .. save9, savea): the scenario's "load <slot>" reads it

Used for a save the user made, replayed on our code (render_shadows_mound.txt: the Rebirth save "Shadow", R22).
"""
import os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    saves, source, slot = sys.argv[1:]
    source = os.path.join(ROOT, source)
    target = os.path.join(saves, slot)
    os.makedirs(target, exist_ok=True)
    for name in os.listdir(source):
        if name.lower().endswith('.sav'):
            shutil.copyfile(os.path.join(source, name), os.path.join(target, name))
