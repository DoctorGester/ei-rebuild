"""game.exe's retired vtables (C19; docs/notes/standalone.md, "Compiler-made vtables"): config/vtables.csv
(tools/find_vtables.py) lists game.exe's vtables, and every row is retired: its class's vtable is the compiler's now,
or only game.exe's own code stored it (file '-'). Nothing installs them and the image holds their bytes zero;
tools/named_data.py --check proves nothing names or points into them.

usage: python tools/retired_vtables.py [--check] [--exe game.exe]
  (no option)  check the rows against the sources and against game.exe's code: each row's start is stored by the code
               (stored_vtable) or is the tail of the row before it
  --check      the rows against the sources only; no game.exe (build.cmd)
"""
import argparse, csv, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VTABLES = os.path.join(ROOT, 'config', 'vtables.csv')
BASE = 0x00400000
CODE = (0x00401000, 0x0073A670)   # .text's bytes (include/ei/hook.h's kGameTextBegin / kGameTextEnd)


def retired_vtables():
    """(vtable, slots, class, file) of config/vtables.csv: game.exe's vtables, every one retired (C19): its class's
    vtable is the compiler's now (docs/notes/standalone.md "Compiler-made vtables"), or only game.exe's own code
    stored it (file '-': base vtables its destructors stored, locals its functions made, dead code's; the note column
    names the storing functions). Nothing installs them and the image holds their bytes zero; tools/named_data.py
    --check proves nothing names or points into them."""
    with open(VTABLES, newline='', encoding='utf-8') as f:
        return [(int(r['vtable'], 16), int(r['slots']), r['class'], r['file']) for r in csv.DictReader(f)]


def stored_vtable(img, v):
    """Whether game.exe's code stores `v` as an immediate (mov r/m32, imm32: C7 /0 with any addressing) or loads it
    into a register (mov r32, imm32: B8+r, then stored from there): a vtable's start. find_vtables.py splits a vtable at every address the code holds, so config/vtables.csv also has tails of
    longer tables, split off at a devirtualised slot call (call [slot]) or a coincidental byte match; those are slots
    of the table before them, never installed."""
    text = bytes(img[CODE[0] - BASE:CODE[1] - BASE])
    pattern = struct.pack('<I', v)
    i = text.find(pattern)
    while i >= 0:
        if i >= 1 and 0xB8 <= text[i - 1] <= 0xBF:
            return True
        for back, mods, rms in ((2, (0,), (0, 1, 2, 3, 6, 7)), (3, (0,), (4,)), (3, (1,), (0, 1, 2, 3, 5, 6, 7)),
                                (4, (1,), (4,)), (6, (0,), (5,)), (6, (2,), (0, 1, 2, 3, 5, 6, 7)), (7, (2,), (4,))):
            if i >= back and text[i - back] == 0xC7:
                m = text[i - back + 1]
                if m >> 6 in mods and m & 7 in rms and (m >> 3) & 7 == 0:
                    return True
        i = text.find(pattern, i + 1)
    return False


def check_retired(problems, img):
    """Each vtable of config/vtables.csv is listed once, names its class (and the file declaring it, or '-'), and is
    the start of a table (stored_vtable) or the tail of the row before it (the row it ends). With no image (`img`
    None: --check) the stored-by-code proof is left out."""
    seen = set()
    # A tail goes with its table: a row's end, when that row is stored or a tail itself.
    ends = set()
    for v, n, _, _ in sorted(retired_vtables()):
        if img is not None and (stored_vtable(img, v) or v in ends):
            ends.add(v + 4 * n)
    for v, n, cls, path in retired_vtables():
        where = f'config/vtables.csv: {v:08X} ({cls})'
        if img is not None and not stored_vtable(img, v) and v not in ends:
            problems.append(f'retired_vtables: {where}: no code of game.exe stores it: the tail of the vtable '
                            'before it (a slot call or a byte match split it off), not a vtable of its own; '
                            'list it with that vtable')
        if v in seen:
            problems.append(f'retired_vtables: {where} is listed twice')
        seen.add(v)
        if not cls or not path:
            problems.append(f'retired_vtables: {where}: no class or file (the class whose vtable is the compiler\'s, '
                            'or file - with a note naming the functions that store it)')
            continue
        if path == '-':   # no class of ours: only game.exe's code stored it (the note says which)
            continue
        if not os.path.exists(os.path.join(ROOT, path)):
            problems.append(f'retired_vtables: {where}: no file {path}')
        elif not re.search(rf'\b(class|struct)\s+{re.escape(cls)}\b',
                           open(os.path.join(ROOT, path), encoding='utf-8', errors='replace').read()):
            problems.append(f'retired_vtables: {where}: {path} declares no class {cls}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--exe')   # default: tools/exe_path.py
    a = ap.parse_args()
    problems = []
    check_retired(problems, None if a.check else exe_path.image(exe_path.resolve(a.exe)))
    for p in problems:
        print(p)
    print(f'retired_vtables: {len(retired_vtables())} retired vtable(s), {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
