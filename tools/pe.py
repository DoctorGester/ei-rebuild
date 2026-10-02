import struct, sys, datetime

PRODIDS = {  # partial table of known @comp.id product ids (VS6 era and around)
    0x00: 'Unmarked', 0x01: 'Import0', 0x02: 'Linker510', 0x03: 'Cvtomf510', 0x04: 'Linker600',
    0x05: 'Cvtomf600', 0x06: 'Cvtres500', 0x07: 'Utc11_Basic', 0x08: 'Utc11_C', 0x09: 'Utc12_Basic',
    0x0a: 'Utc12_C', 0x0b: 'Utc12_CPP', 0x0c: 'AliasObj60', 0x0d: 'VisualBasic60', 0x0e: 'Masm613',
    0x0f: 'Masm710', 0x10: 'Linker511', 0x11: 'Cvtomf511', 0x12: 'Masm614', 0x13: 'Linker512',
    0x14: 'Cvtomf512', 0x15: 'Utc12_C_Std', 0x16: 'Utc12_CPP_Std', 0x17: 'Utc12_C_Book',
    0x18: 'Utc12_CPP_Book', 0x19: 'Implib700', 0x1a: 'Cvtomf700', 0x1b: 'Utc13_Basic', 0x1c: 'Utc13_C',
    0x1d: 'Utc13_CPP', 0x1e: 'Linker610', 0x1f: 'Cvtomf610', 0x20: 'Linker601', 0x21: 'Cvtomf601',
    0x22: 'Utc12_1_Basic', 0x23: 'Utc12_1_C', 0x24: 'Utc12_1_CPP', 0x25: 'Linker620', 0x26: 'Cvtomf620',
    0x27: 'AliasObj70', 0x28: 'Linker621', 0x29: 'Cvtomf621', 0x2a: 'Masm615', 0x2b: 'Utc13_LTCG_C',
    0x2c: 'Utc13_LTCG_CPP', 0x2d: 'Masm620', 0x2e: 'ILAsm100', 0x2f: 'Utc12_2_Basic', 0x30: 'Utc12_2_C',
    0x31: 'Utc12_2_CPP', 0x32: 'Utc12_2_C_Std', 0x33: 'Utc12_2_CPP_Std', 0x34: 'Utc12_2_C_Book',
    0x35: 'Utc12_2_CPP_Book', 0x36: 'Implib622', 0x37: 'Cvtomf622', 0x38: 'Cvtres501',
    0x5a: 'Linker700', 0x5d: 'Utc13_POGO', 0x5f: 'Utc13_C', 0x60: 'Utc13_CPP', 0x5e: 'Cvtres700',
    0x93: 'Utc1310', 0x94: 'Utc1310_CPP',
}

def main(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    print(path, 'size', len(d), 'e_lfanew', hex(pe))
    # Rich header
    end = d.find(b'Rich', 0x80, pe)
    if end > 0:
        key = struct.unpack_from('<I', d, end + 4)[0]
        start = d.find(struct.pack('<I', 0x536e6144 ^ key), 0x80)
        print('Rich key', hex(key))
        for off in range(start + 16, end, 8):
            a, c = struct.unpack_from('<II', d, off)
            a ^= key; c ^= key
            pid, build = a >> 16, a & 0xffff
            print(f'  prodid {pid:#04x} {PRODIDS.get(pid, "?"):16} build {build:5d} count {c}')
    else:
        print('No Rich header')
    machine, nsec, ts, _, _, optsz, chars = struct.unpack_from('<HHIIIHH', d, pe + 4)
    print('timestamp', ts, datetime.datetime.fromtimestamp(ts, datetime.UTC), 'chars', hex(chars))
    o = pe + 24
    magic, lmaj, lmin = struct.unpack_from('<HBB', d, o)
    entry, = struct.unpack_from('<I', d, o + 16)
    base, salign, falign = struct.unpack_from('<III', d, o + 28)
    osmaj, osmin, imaj, imin, ssmaj, ssmin = struct.unpack_from('<6H', d, o + 40)
    sizeimg, sizehdr, csum, subsys, dllch = struct.unpack_from('<IIIHH', d, o + 56)
    print(f'linker {lmaj}.{lmin} entry {entry:#x} base {base:#x} align {salign:#x}/{falign:#x} os {osmaj}.{osmin} subsys {subsys} ver {ssmaj}.{ssmin} sizeimg {sizeimg:#x} checksum {csum:#x}')
    ndd, = struct.unpack_from('<I', d, o + 92)
    dds = [struct.unpack_from('<II', d, o + 96 + 8 * i) for i in range(ndd)]
    names = ['export', 'import', 'resource', 'exception', 'security', 'reloc', 'debug', 'arch', 'gp', 'tls', 'loadcfg', 'bound', 'iat', 'delay', 'clr', 'res']
    for n, (va, sz) in zip(names, dds):
        if va or sz: print(f'  dd {n:9} {va:#x} {sz:#x}')
    secs = []
    so = o + optsz
    for i in range(nsec):
        name = d[so:so + 8].rstrip(b'\0').decode('latin1')
        vsz, va, rsz, rptr, _, _, _, _, ch = struct.unpack_from('<IIIIIIHHI', d, so + 8)
        secs.append((name, va, vsz, rptr, rsz))
        print(f'  sec {name:8} va {va:#08x} vsz {vsz:#08x} raw {rptr:#08x} rsz {rsz:#08x} ch {ch:#x}')
        so += 40
    last = max(s[3] + s[4] for s in secs)
    print('end of last section', hex(last), 'overlay bytes', len(d) - last)

    def r2o(rva):
        for _, va, vsz, rp, rs in secs:
            if va <= rva < va + max(vsz, rs): return rva - va + rp
    ia = dds[1][0]
    if ia:
        p = r2o(ia)
        while True:
            oft, _, _, nm, ft = struct.unpack_from('<IIIII', d, p)
            if not nm: break
            dll = d[r2o(nm):d.index(b'\0', r2o(nm))].decode()
            t = r2o(oft or ft); funcs = []
            while True:
                v, = struct.unpack_from('<I', d, t)
                if not v: break
                if v & 0x80000000: funcs.append(f'#{v & 0xffff}')
                else:
                    q = r2o(v) + 2; funcs.append(d[q:d.index(b'\0', q)].decode())
                t += 4
            print(f'IMPORT {dll} ({len(funcs)}): ' + ', '.join(funcs))
            p += 20
    dbg = dds[6]
    if dbg[0]:
        p = r2o(dbg[0])
        for i in range(dbg[1] // 28):
            _, ts2, _, _, typ, sz, rva, ptr = struct.unpack_from('<IIHHIIII', d, p + 28 * i)
            print('debug type', typ, 'size', sz, d[ptr:ptr + min(sz, 200)])

for a in sys.argv[1:]:
    main(a); print('=' * 60)
