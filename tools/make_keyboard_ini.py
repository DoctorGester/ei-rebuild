"""Writes a remapped config\\keyboard.ini for tests/scenarios/ui_options_remap_load.txt (SV8).

usage: python tools/make_keyboard_ini.py <saves dir>
  <saves dir>  the run's harness_saves folder. The harness's copy-on-write overlay keeps the install's other
               files there by their relative path, and a read takes the overlay copy when there is one, so
               <saves dir>\\config\\keyboard.ini is what the game reads as its config\\keyboard.ini (00462C10).
               Nothing under the install is written.

The file is the one ui_options_remap.txt makes the game save (00462DF0, on the interface's shutdown) after
it remaps "obj" to Y, "crawl" to J and "run" to Q (taken from weapon1): the same bytes from the original
and the image, on both installs (whose own keyboard.ini files are the same). The restart half of a remap.
"""
import os, sys

BINDINGS = [
    ('1', 'spell1'), ('2', 'spell2'), ('3', 'spell3'), ('4', 'spell4'), ('5', 'spell5'), ('6', 'spell6'),
    ('7', 'spell7'), ('8', 'spell8'), ('Q', 'run'), ('W', 'weapon2'), ('E', 'weapon3'), ('R', 'weapon4'),
    ('Y', 'obj'), ('U', 'item4'), ('I', 'item3'), ('O', 'item2'), ('P', 'item1'), ('A', 'swarm'),
    ('S', 'use_science'), ('F', 'follow'), ('H', 'tutorial_script'), ('J', 'crawl'), ('K', 'w_text2'),
    ('L', 'w_text1'), ("'", 'w_info4'), ('X', 'walk'), ('C', 'sneak'), ('N', 'camera_norm'), ('M', 'w_minimap'),
    (',', 'w_info1'), ('.', 'w_info2'), ('/', 'w_info3'), ('SPACE', 'pause'), ('F1', 'select1'),
    ('F2', 'select2'), ('F3', 'select3'), ('F4', 'select_all'), ('F5', 'quicksave'), ('F8', 'quickload'),
    ('F9', 'camera1'), ('F10', 'camera2'), ('KP_UP', 'cs_head'), ('KP_MINUS', 'decel'), ('KP_LEFT', 'cs_rhand'),
    ('KP_CENTER', 'cs_body'), ('KP_RIGHT', 'cs_lhand'), ('KP_PLUS', 'accel'), ('KP_END', 'cs_rleg'),
    ('KP_PGDN', 'cs_lleg'), ('F11', 'camera3'), ('F12', 'camera4'), ('HOME', 'camera_track'), ('UP', 'camera_up'),
    ('PGUP', 'camera_zoom_in'), ('LEFT', 'camera_left'), ('RIGHT', 'camera_right'), ('DOWN', 'camera_down'),
    ('PGDN', 'camera_zoom_out'),
]


def text():
    return ''.join(f'{key} {action}\r\n' for key, action in BINDINGS).encode('ascii')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = os.path.join(sys.argv[1], 'config')
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'keyboard.ini'), 'wb') as f:
        f.write(text())


if __name__ == '__main__':
    main()
