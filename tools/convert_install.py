"""Makes an unpacked install's converted copy: its record files as TOML, the originals gone (docs/notes/mods.md,
"Records as TOML").

usage: python tools/convert_install.py UNPACKED TARGET [--convert EXE] [--force]
  UNPACKED   an install's unpacked copy (tools/unpack_install.py); only read
  TARGET     the converted copy: every file of UNPACKED hard-linked (copied where a link cannot be made), then
             ei-convert's TOML written into it (each record file's <name>.toml beside it, each texts archive's
             texts.toml) and every original it converted (its converted.txt) removed. The copy plays as the install
             does with the TOML standing in for the originals (run_all.py --converted runs the suites on it).
  --convert  ei-convert.exe (default: build\\verify's)
  --force    an existing TARGET is removed first (refused when it holds a link); without it a TARGET made from the
             same UNPACKED by the same ei-convert (its converted-from.txt) is kept as it is, one made by another
             ei-convert is made again, and any other TARGET is refused
Nothing is ever written into UNPACKED: no file of TARGET that links to one of UNPACKED is opened for writing (the
converter only adds .toml files and converted.txt, which the copy does not hold), and removing a link leaves the
other name.
"""
import argparse, hashlib, os, shutil, subprocess, sys

import unpack_install

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = 'ei-convert.exe' if os.name == 'nt' else 'ei-convert'
STAMP = 'converted-from.txt'      # in TARGET: the unpacked copy and the converter it was made with
LIST = 'converted.txt'            # in TARGET: ei-convert's list of the originals it converted


def file_hash(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def link_tree(source, target):
    """Every file of source hard-linked into target (copied when the link fails: another volume). Links in source
    are refused."""
    for folder, dirs, names in os.walk(source):
        for d in dirs:
            p = os.path.join(folder, d)
            if os.path.islink(p) or (hasattr(os.path, 'isjunction') and os.path.isjunction(p)):
                sys.exit(f'convert_install: {p} is a link')
        out = os.path.join(target, os.path.relpath(folder, source))
        os.makedirs(out, exist_ok=True)
        for name in names:
            if folder == source and name in (unpack_install.STAMP, STAMP, LIST):
                continue
            try:
                os.link(os.path.join(folder, name), os.path.join(out, name))
            except OSError:
                shutil.copy2(os.path.join(folder, name), os.path.join(out, name))


def make(unpacked, target, convert=None, force=False, say=print):
    """TARGET made from UNPACKED (see the module's text); True when it was made, False when it was there already."""
    unpacked, target = os.path.abspath(unpacked), os.path.abspath(target)
    convert = os.path.abspath(convert or os.path.join(ROOT, 'build', 'verify', EXE))
    if unpack_install.inside(unpacked, target) or unpack_install.inside(target, unpacked):
        sys.exit('convert_install: the target and the unpacked copy must not hold each other')
    if not os.path.exists(os.path.join(unpacked, unpack_install.STAMP)):
        sys.exit(f'convert_install: {unpacked} is not an unpacked copy (no {unpack_install.STAMP})')
    if not os.path.exists(convert):
        sys.exit(f'convert_install: no {convert}; build.cmd verify first')
    stamp = os.path.join(target, STAMP)
    made_from = f'{unpacked}\n{file_hash(convert)}\n'
    if os.path.exists(target):
        ours = os.path.exists(stamp)
        if ours and open(stamp, encoding='utf-8').read() == made_from and not force:
            return False
        if not ours and not force:
            sys.exit(f'convert_install: {target} is there and not a converted copy (--force replaces it)')
        unpack_install.remove_tree(target)
    say(f'convert_install: {unpacked} -> {target}')
    link_tree(unpacked, target)
    done = subprocess.run([convert, target, target], capture_output=True, text=True, encoding='utf-8',
                          errors='replace')
    counts = [line for line in done.stdout.splitlines() if line.startswith('ei-convert: ')]
    if done.returncode != 0:
        sys.exit('convert_install: ei-convert failed:\n' + done.stdout[-4000:] + done.stderr[-2000:])
    removed = 0
    with open(os.path.join(target, LIST), encoding='utf-8') as f:
        for line in f:
            relative = line.rstrip('\r\n')
            if not relative:
                continue
            path = os.path.join(target, *relative.split('\\'))
            if not unpack_install.inside(path, target):
                sys.exit(f'convert_install: {relative} is outside the copy')
            os.remove(path)
            removed += 1
    with open(stamp, 'w', encoding='utf-8') as f:
        f.write(made_from)
    for line in counts:
        say(f'convert_install: {line[len("ei-convert: "):]}')
    say(f'convert_install: {removed} originals removed')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('unpacked')
    parser.add_argument('target')
    parser.add_argument('--convert')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    if not make(args.unpacked, args.target, args.convert, args.force):
        print(f'convert_install: {args.target} is made from {args.unpacked} by this ei-convert already')


if __name__ == '__main__':
    main()
