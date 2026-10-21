r"""Saved games: their plain bytes, and which code wrote the bytes that differ between two runs (ticket SV7a).

A save file (scenario.sav, mission.sav, saves\current\<zone>.sav) is written by SaveFileWrite 00579B80: magic and
version, above version 0x6B a checksum of the plain data, above 0x71 a flag byte, then the data LZ-compressed (the coder
in src/game/net_coder.cpp) or, with flag 0, as it is. A harness run with EI_SAVETRACE=1 (tests/verify/harness_savetrace.h)
writes savetrace.txt in its run folder: every memory stream write (position, size, callers) up to each .sav file the
game opens for writing.

usage:
  python tools/save_trace.py plain FILE...
      Writes FILE.plain, the save's plain bytes, and checks them against the file's checksum.
  python tools/save_trace.py diff RUN_A RUN_B [--top N]
      For every <slot>\*.sav of RUN_A's harness_saves (but shot.sav and info.sav) that RUN_B has too: how many plain
      bytes differ, and by which writes (the first three callers, named from config/functions.csv and the rewrites'
      EI_REPLACE registrations), with the dwords that differ (offset in the write: A / B). RUN_A needs savetrace.txt.
      The trace's own stack use changes stack leftovers, so compare two traced runs.
"""
import argparse, bisect, collections, csv, glob, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def decode(data):
    """The LZ coder's output (CoderState::Decompress 00430960) back to the plain bytes."""
    p, acc, bits = 4, 0, 0

    def bit():
        nonlocal p, acc, bits
        if bits < 1:
            acc, bits, p = data[p], 8, p + 1
        b = acc & 1
        acc >>= 1
        bits -= 1
        return b

    def field(n):
        nonlocal p, acc, bits
        while bits < n:
            acc += data[p] << bits
            bits += 8
            p += 1
        v = acc & ((1 << n) - 1)
        acc >>= n
        bits -= n
        return v

    def length():
        if not bit(): return 4 if bit() else 2
        if not bit():
            if bit(): return field(2) + 7
            if bit(): return field(3) + 0xB
            return 3
        if not bit(): return field(6) + 0x23 if bit() else 5
        if not bit(): return 6
        if not bit(): return field(4) + 0x13
        return field(7) + 0x63

    def offset():
        if bit(): return field(10) + 0x162 if bit() else field(12) + 0x562
        if bit(): return field(8) + 0x62 if bit() else field(6) + 0x22
        if bit():
            if bit(): return field(1) if bit() else field(4) + 0x12
            return field(2) + 6
        return field(2) + 2 if bit() else field(3) + 0xA

    remaining = struct.unpack_from('<i', data, 0)[0]
    window, pos, out = bytearray(0x400), 0, bytearray()
    while remaining > 0:
        if not bit():
            b = data[p]
            p += 1
            out.append(b)
            window[pos] = b
            pos = (pos + 1) & 0x3FF
            remaining -= 1
            continue
        n = min(length(), remaining)
        back = offset()
        for _ in range(n):
            b = window[(pos - back - 1) & 0x3FF]
            out.append(b)
            window[pos] = b
            pos = (pos + 1) & 0x3FF
        remaining -= n
    return bytes(out)


def checksum(data):
    h = 0
    for (word,) in struct.iter_unpack('<I', data[:len(data) // 4 * 4]):
        h ^= word
        h = (h * ((h & 0xFC) | 3)) & 0xFFFFFFFF
    return h


def plain(path):
    """(plain bytes, checksum or None)."""
    raw = open(path, 'rb').read()
    magic, version = struct.unpack_from('<II', raw, 0)
    at, sum_ = 8, None
    if version > 0x6B:
        sum_ = struct.unpack_from('<I', raw, at)[0]
        at += 4
    flag = 1
    if version > 0x71:
        flag = raw[at]
        at += 1
    return (decode(raw[at:]) if flag else raw[at:]), sum_


def names():
    """Function starts and names: config/functions.csv, then the rewrites' names."""
    found = {}
    for row in csv.DictReader(open(os.path.join(ROOT, 'config', 'functions.csv'))):
        found[int(row['address'], 16)] = row['name']
    pattern = re.compile(r'(?:EI_REPLACE\w*|EI_REWRITTEN)\(0x([0-9A-Fa-f]{8}),\s*([\w:]+)')
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        for m in pattern.finditer(open(path, encoding='utf-8', errors='replace').read()):
            found[int(m.group(1), 16)] = m.group(2)
    starts = sorted(found)
    return starts, [found[s] for s in starts]


def traces(folder):
    """[(save path, stream size, [(position, size, callers)])] from savetrace.txt."""
    out, cur = [], None
    for line in open(os.path.join(folder, 'savetrace.txt')):
        if line.startswith('# save'):
            parts = line.split()
            cur = (parts[2].lower(), int(parts[-1]), [])
            out.append(cur)
        elif cur:
            f = line.split()
            cur[2].append((int(f[0]), int(f[1]), tuple(int(x, 16) for x in f[2:])))
    return out


def diff(args):
    starts, labels = names()

    def name(address):
        if not address: return '-'
        if address >= 0x10000000: return f'ei_hook+{address - 0x10000000:X}'
        i = bisect.bisect_right(starts, address) - 1
        return f'{labels[i]}+{address - starts[i]:X}' if i >= 0 else f'{address:08X}'

    dumps = traces(args.a)
    code = 0
    for pa in sorted(glob.glob(os.path.join(args.a, 'harness_saves', '*', '*.sav'))):
        rel = os.path.relpath(pa, os.path.join(args.a, 'harness_saves'))
        pb = os.path.join(args.b, 'harness_saves', rel)
        if os.path.basename(pa).lower() in ('shot.sav', 'info.sav') or not os.path.exists(pb):
            continue
        (da, _), (db, _) = plain(pa), plain(pb)
        differ = [i for i in range(min(len(da), len(db))) if da[i] != db[i]]
        print(f'{rel}: {len(da)} / {len(db)} plain bytes, {len(differ)} differ')
        if not differ:
            continue
        code = 1
        slot = '\\' + os.path.dirname(rel).lower() + '\\'
        streams = [s for s in dumps if slot in s[0] and s[1] == len(da)]
        if not streams:
            print('  no traced stream of that size')
            continue
        entries = sorted(streams[-1][2])
        positions = [e[0] for e in entries]
        bytes_by = collections.Counter()
        size_of, dwords = {}, collections.defaultdict(dict)
        for i in differ:
            j = bisect.bisect_right(positions, i) - 1
            while j >= 0 and not entries[j][0] <= i < entries[j][0] + entries[j][1]:
                j -= 1
            if j < 0:
                bytes_by[('?',)] += 1
                continue
            e = entries[j]
            key = tuple(name(c) for c in e[2][:3])
            bytes_by[key] += 1
            size_of[key] = e[1]
            o = (i - e[0]) & ~3
            dwords[key].setdefault(o, e[0] + o)
        for key, n in bytes_by.most_common(args.top):
            print(f'  {n:6} bytes in writes of {size_of.get(key, "?")}: {" <- ".join(key)}')
            shown = [f'+{o:X} {da[at:at + 4].hex()}/{db[at:at + 4].hex()}' for o, at in sorted(dwords[key].items())[:12]]
            if shown:
                print('         ' + ', '.join(shown) + (' ...' if len(dwords[key]) > 12 else ''))
    return code


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plain')
    p.add_argument('files', nargs='+')
    p = sub.add_parser('diff')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--top', type=int, default=25)
    args = ap.parse_args()
    if args.command == 'diff':
        return diff(args)
    for path in args.files:
        data, sum_ = plain(path)
        open(path + '.plain', 'wb').write(data)
        ok = '' if sum_ is None else (', checksum ok' if checksum(data) == sum_ else ', CHECKSUM DIFFERS')
        print(f'{path}: {len(data)} plain bytes{ok}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
