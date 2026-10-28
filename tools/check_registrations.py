"""Fails the build when one game address has two EI_REWRITTEN lines (the function-to-original map).

EI_REWRITTEN / EI_REWRITTEN_SPECIAL (include/ei/hook.h; a rewrite, C20d) leave {address, function, file, line} in
the section .eiorg of the module. This check reads those records from the built ei_hook.dll, so it sees what this
build maps (lines left out by #if are not there): one address has at most one, or the tools' map would name two
functions for one original.

usage: python tools/check_registrations.py [--dll build/release/ei_hook.dll]
exit code 1 on any problem
"""
import argparse, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIGIN = struct.Struct('<IIII')   # EI_REWRITTEN: address, VA of a constant holding &fn (or of a name), file (VA of the string), line


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
    """.eiorg: (address, file, line, function VA). None without the section."""
    sec = img.section('.eiorg')
    if sec is None:
        return None
    _, _, vsz, rptr, rsz = sec
    raw = img.data[rptr:rptr + min(vsz, rsz)]
    out = []
    off = 0
    while off + ORIGIN.size <= len(raw):   # the linker pads between the modules' contributions with zeros
        address, code, file_va, line = ORIGIN.unpack_from(raw, off)
        if address == 0:
            off += 4
            continue
        out.append((address, img.string_at(file_va), line, code))
        off += ORIGIN.size
    return out


def short(path):
    path = path.replace('\\', '/')
    root = ROOT.replace('\\', '/') + '/'
    return path[len(root):] if path.lower().startswith(root.lower()) else path


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build', 'release', 'ei_hook.dll'))
    args = ap.parse_args(argv)
    img = Image(args.dll)
    origins = records(img)
    if not origins:
        print(f'{args.dll}: no EI_REWRITTEN records (section .eiorg missing or empty)')
        return 1
    by_address = {}
    for address, file, line, code in origins:
        by_address.setdefault(address, []).append((short(file), line))
    twice = 0
    for address, sites in sorted(by_address.items()):
        if len(sites) > 1:
            twice += 1
            print(f'{address:08X} has {len(sites)} EI_REWRITTEN: ' + '; '.join(f'{f}:{l}' for f, l in sorted(sites)))
    print(f'{len(origins)} EI_REWRITTEN at {len(by_address)} addresses, {twice} address(es) mapped twice')
    return 1 if twice else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
