"""Fails the build when one game address has two registrations that InstallHooks would both install.

Every registration macro (include/ei/hook.h: EI_REPLACE, EI_REPLACE_METHOD, EI_REPLACE_DISPATCH,
EI_REPLACE_STANDALONE, EI_REPLACE_SHORT, EI_RECORD) leaves a record of the
address, its kind and the file and line of the expansion in the section .eireg of the module. This
check reads those records from the built ei_hook.dll, so it sees what this build registers (macro
generated registrations included, and registrations left out by #if are not there), and groups them
per address into the two sets InstallHooks keeps one of:

  - plain replacements: EI_REPLACE, _METHOD, _DISPATCH, _STANDALONE and _SHORT together (the standalone
    image installs all of them; the second one at an address is skipped, or its jump wins, and the
    rewrite never runs: SV6 saw this at run time),
  - vector recorders: EI_RECORD (installed only in game.exe, with EI_VECTORS=1).

A replacement together with a recorder at one address is the normal case, not a problem. Each problem names the address and every file:line registering it.

usage: python tools/check_registrations.py [--dll build/release/ei_hook.dll]
exit code 1 on any problem
"""
import argparse, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KINDS = ['EI_REPLACE', 'EI_REPLACE_STANDALONE', 'EI_REPLACE_SHORT', 'EI_REPLACE_DISPATCH',
         'EI_RECORD']   # include/ei/hook.h RegKind
SET_OF = ['plain replacement', 'plain replacement', 'plain replacement', 'plain replacement',
          'recorder']
RECORD = struct.Struct('<IIII')   # address, file (VA of the string), line, kind


class Image:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec, = struct.unpack_from('<H', d, pe + 6)
        optsz, = struct.unpack_from('<H', d, pe + 20)
        opt = pe + 24
        self.base, = struct.unpack_from('<I', d, opt + 28)
        self.sections = []
        so = opt + optsz
        for i in range(nsec):
            name = d[so:so + 8].rstrip(b'\0').decode('latin1')
            vsz, va, rsz, rptr = struct.unpack_from('<IIII', d, so + 8)
            self.sections.append((name, va, vsz, rptr, rsz))
            so += 40

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        return None

    def string_at(self, va):
        rva = va - self.base
        for _, sva, vsz, rptr, rsz in self.sections:
            if sva <= rva < sva + max(vsz, rsz):
                at = rptr + rva - sva
                return self.data[at:self.data.index(b'\0', at)].decode('latin1')
        return '?'


def records(img):
    sec = img.section('.eireg')
    if sec is None:
        return None
    _, _, vsz, rptr, rsz = sec
    raw = img.data[rptr:rptr + min(vsz, rsz)]
    out = []
    off = 0
    while off + RECORD.size <= len(raw):   # the linker pads between the modules' contributions with zeros
        address, file_va, line, kind = RECORD.unpack_from(raw, off)
        if address == 0:
            off += 4
            continue
        out.append((address, img.string_at(file_va), line, kind))
        off += RECORD.size
    return out


def short(path):
    path = path.replace('\\', '/')
    root = ROOT.replace('\\', '/') + '/'
    return path[len(root):] if path.lower().startswith(root.lower()) else path


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build', 'release', 'ei_hook.dll'))
    args = ap.parse_args(argv)
    recs = records(Image(args.dll))
    if not recs:
        print(f'{args.dll}: no registration records (section .eireg missing or empty)')
        return 1
    groups = {}
    for address, file, line, kind in recs:
        if not 0 <= kind < len(KINDS):
            print(f'{short(file)}({line}): {address:08X} has the unknown registration kind {kind}')
            return 1
        groups.setdefault((address, SET_OF[kind]), []).append((short(file), line, KINDS[kind]))
    problems = 0
    for (address, which), sites in sorted(groups.items()):
        if len(sites) > 1:
            problems += 1
            where = '; '.join(f'{k} at {f}:{l}' for f, l, k in sorted(sites))
            print(f'{address:08X} has {len(sites)} {which} registrations: {where}')
    print(f'{len(recs)} registrations at {len({a for a, _, _, _ in recs})} addresses, {problems} address(es) registered twice')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
