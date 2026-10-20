r"""Saved games decoded field by field (docs/notes/standalone-verification.md, SV7d).

The game writes a save (GameFlow::SaveGame 0057B3B0) as files in saves\<slot>\: scenario.sav (the party, the
input state, the scripts, the globals; magic 0xDEAD), mission.sav (the world, CWorldServer::SaveWorld 00597BD0;
magic 0x102), info.sav (the slot's title, raw) and shot.sav (the thumbnail). Visited zones are kept as
saves\current\<zone>.sav (SaveWorld too). Every .sav but info.sav and shot.sav is SaveFileWrite's (00579B80) frame:
magic, version, checksum of the plain data, a compressed flag and the LZ coder's output (net_coder.cpp).

decode(path) gives the save as entities keyed by object id rather than by memory: [(key, description, [(field,
value)])]. Values that follow memory layout can be written as a rule token (`~addr`, `~junk`, ...; RULES gives each
one's reason), so two runs of the same scenario decode to the same text whatever their heap layout (no reader uses
one since SV7b2a: the harness makes the original write canonical values instead):
  ~addr   a heap, stack or module address the writer copies with a raw block; nothing reads it back as a value
  ~junk   bytes the game never sets before writing them (struct padding, stack leftovers, unused tails)
  p<N>    a pointer the save uses as an identity (records referring to each other by address): numbered by first
          appearance, so the links compare and the addresses do not
Every reader below follows the game's writer named in its comment and must consume the stream exactly: a
decode that does not end where the data ends raises DecodeError (the file's offset and the field path).

usage:
  python tools/savefile.py show FILE.sav [--key REGEX]    the decoded fields
  python tools/savefile.py diff A.sav B.sav [--fields N]  the first fields that differ
  python tools/savefile.py raw FILE.sav OUT               the plain data (decompressed)
  python tools/savefile.py rules FILE.sav...              the fields left out while decoding these, with the reason
"""
import argparse, re, struct, sys

# ---- the file frame and the LZ coder (net_coder.cpp: CoderState::Decompress 00430960) -----------------

MAGIC_SCENARIO = 0xDEAD
MAGIC_MISSION = 0x102
MAGIC_INFO = 0x111
MAGIC_PARTY = 0x114   # a multiplayer hero, mp\<n>.mp (MpHeroRecord::WriteFile 00662AC0)


class DecodeError(Exception):
    pass


class _Bits:
    """BufferCursor's reader: bits LSB first; literal bytes are read from the byte cursor between them."""

    def __init__(self, data, p):
        self.d, self.p, self.acc, self.bits = data, p, 0, 0

    def bit(self):
        if self.bits < 1:
            self.acc = self.d[self.p]
            self.p += 1
            self.bits = 8
        b = self.acc & 1
        self.acc >>= 1
        self.bits -= 1
        return b

    def get(self, n):
        while self.bits < n:
            self.acc += self.d[self.p] << self.bits
            self.p += 1
            self.bits += 8
        v = self.acc & ((1 << n) - 1)
        self.acc >>= n
        self.bits -= n
        return v


def _length(r):
    if not r.bit():
        return 4 if r.bit() else 2
    if not r.bit():
        if r.bit():
            return r.get(2) + 7
        if r.bit():
            return r.get(3) + 0xB
        return 3
    if not r.bit():
        return r.get(6) + 0x23 if r.bit() else 5
    if not r.bit():
        return 6
    if not r.bit():
        return r.get(4) + 0x13
    return r.get(7) + 0x63


def _offset(r):
    if r.bit():
        return r.get(10) + 0x162 if r.bit() else r.get(12) + 0x562
    if r.bit():
        return r.get(8) + 0x62 if r.bit() else r.get(6) + 0x22
    if r.bit():
        if r.bit():
            return r.get(1) if r.bit() else r.get(4) + 0x12
        return r.get(2) + 6
    return r.get(2) + 2 if r.bit() else r.get(3) + 0xA


def decompress(data, p=0):
    """The LZ coder's output at data[p:] (int32 size, then a 0 bit and a literal byte or a 1 bit, a length
    and an offset - 1 into a 1 KB window) as bytes."""
    remaining = struct.unpack_from('<i', data, p)[0]
    r = _Bits(data, p + 4)
    out = bytearray()
    window = bytearray(0x400)
    pos = 0
    while remaining > 0:
        if not r.bit():
            b = r.d[r.p]
            r.p += 1
            out.append(b)
            window[pos] = b
            pos = (pos + 1) & 0x3FF
            remaining -= 1
            continue
        n = min(_length(r), remaining)
        o = _offset(r)
        for _ in range(n):
            b = window[(pos - o - 1) & 0x3FF]
            out.append(b)
            window[pos] = b
            pos = (pos + 1) & 0x3FF
        remaining -= n
    return bytes(out)


def checksum(data):
    """SaveFileWrite's checksum of the plain data (versions above 0x6B)."""
    h = 0
    for (w,) in struct.iter_unpack('<I', data[:len(data) & ~3]):
        h = ((h ^ w) * (((h ^ w) & 0xFC) | 3)) & 0xFFFFFFFF
    return h


def read_frame(data):
    """(magic, version, plain data) of a SaveFileWrite file; info.sav (magic 0x111) is not framed."""
    magic, version = struct.unpack_from('<II', data)
    if magic == MAGIC_INFO:
        return magic, version, data[8:]
    if version <= 0x6B:
        raise DecodeError(f'version {version:#x}: older saves are not decoded')
    stored = struct.unpack_from('<I', data, 8)[0]
    flag = data[12] if version > 0x71 else 1
    start = 13 if version > 0x71 else 12
    plain = decompress(data, start) if flag else data[start:]
    if checksum(plain) != stored:
        raise DecodeError('checksum of the plain data does not match')
    return magic, version, plain


# ---- the reader ---------------------------------------------------------------------------------------

EXCLUDED = {}   # (field with numbers as N, rule) -> why, filled while decoding
RULES = {
    'addr': 'a heap, stack or module address copied with a raw block; the loader does not use it as a value',
    'junk': 'bytes the game never sets before writing them (padding, stack leftovers, unused tails)',
    'identity': 'a pointer used as a record identity: numbered by first appearance (p1, p2, ...)',
}


def fmt_float(bits):
    if (bits & 0x7F800000) == 0x7F800000 and bits & 0x7FFFFF:
        return f'nan:{bits:08X}'
    v = struct.unpack('<f', struct.pack('<I', bits))[0]
    text = f'{v:.9g}'
    return text if any(c in text for c in '.eni') else text + '.0'


class Reader:
    """Reads the plain data of a save the way the game's loader does and records each field under the
    current entity. A field's name is the scope path (`scope`) and its own name."""

    def __init__(self, data, version, name=''):
        self.d = data
        self.p = 0
        self.version = version
        self.name = name
        self.limit = None           # where the block being read ends (a read that crosses it fails at the field)
        self.entities = []          # [key, description, [(field, value)]]
        self.current = None
        self.path = []
        self.pointers = {}          # identity pointers -> their first-appearance number
        self.keys = {}

    # entities and scopes
    def entity(self, key, description=''):
        if key in self.keys:
            n = 2
            while f'{key}#{n}' in self.keys:
                n += 1
            key = f'{key}#{n}'
        self.current = [key, description, []]
        self.keys[key] = self.current
        self.entities.append(self.current)
        return key

    class _Scope:
        def __init__(self, reader, name):
            self.r, self.name = reader, name

        def __enter__(self):
            self.r.path.append(self.name)

        def __exit__(self, *exc):
            self.r.path.pop()

    def scope(self, name):
        return Reader._Scope(self, str(name))

    def put(self, name, value):
        if self.current is None:
            self.entity('header')
        self.current[2].append(('.'.join(self.path + [str(name)]) if name is not None else '.'.join(self.path), value))

    def fail(self, what):
        where = '.'.join(self.path)
        key = self.current[0] if self.current else '-'
        error = DecodeError(f'{self.name}: {what} at offset {self.p:#x} ({key} {where})')
        error.entities = [(k, desc, fields) for k, desc, fields in self.entities]   # what was decoded before
        error.offset = self.p
        raise error

    def take(self, n):
        if n < 0 or self.p + n > len(self.d):
            self.fail(f'{n} bytes past the end ({len(self.d):#x})')
        if self.limit is not None and self.p + n > self.limit:
            self.fail(f'{n} bytes past the end of its block ({self.limit:#x})')
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def peek_i32(self, at=0):
        if self.p + at + 4 > len(self.d):
            return None
        return struct.unpack_from('<i', self.d, self.p + at)[0]

    # plain values: each returns what it read and records it under `name`
    def _num(self, fmt, size, name):
        v = struct.unpack('<' + fmt, self.take(size))[0]
        if name is not None:
            self.put(name, str(v))
        return v

    def u8(self, name):
        return self._num('B', 1, name)

    def i8(self, name):
        return self._num('b', 1, name)

    def u16(self, name):
        return self._num('H', 2, name)

    def i16(self, name):
        return self._num('h', 2, name)

    def i32(self, name):
        return self._num('i', 4, name)

    def u32(self, name):
        return self._num('I', 4, name)

    def x32(self, name):
        """A dword of flags or bits, in hex."""
        v = struct.unpack('<I', self.take(4))[0]
        if name is not None:
            self.put(name, f'0x{v:08X}')
        return v

    def f32(self, name):
        bits = struct.unpack('<I', self.take(4))[0]
        if name is not None:
            self.put(name, fmt_float(bits))
        return struct.unpack('<f', struct.pack('<I', bits))[0]

    def vec(self, name, n=3):
        with self.scope(name):
            return [self.f32('xyzw'[i] if n <= 4 else i) for i in range(n)]

    def id(self, name):
        """An object id (-1: none)."""
        return self.i32(name)

    def string(self, name):
        """A zero-terminated string (CStream::WriteString / Write(text, strlen + 1))."""
        end = self.d.find(b'\0', self.p)
        if end < 0:
            self.fail('a string without its terminator')
        if self.limit is not None and end >= self.limit:
            self.fail(f'a string past the end of its block ({self.limit:#x})')
        text = self.d[self.p:end].decode('cp1251', 'replace')
        self.p = end + 1
        if name is not None:
            self.put(name, '"' + text.replace('\\', '\\\\').replace('\n', '\\n').replace('"', '\\"') + '"')
        return text

    def raw(self, name, n):
        """Bytes kept as they are (hex)."""
        b = self.take(n)
        if name is not None:
            self.put(name, b.hex() if b else '-')
        return b

    # values that follow memory layout: each takes `why`, the reason the field is left out (EXCLUDED collects them
    # by field name with numbers as N, for `savefile.py rules` and the notes)
    def excluded(self, name, rule, why):
        field = re.sub(r'\d+', 'N', '.'.join(self.path + [str(name)]))
        EXCLUDED.setdefault((field, rule), why)

    def addr(self, name, why, null=True):
        """A dword holding an address (rule addr). null: zero is kept as 0 (whether there was one)."""
        v = struct.unpack('<I', self.take(4))[0]
        self.excluded(name, 'addr', why)
        self.put(name, '0' if null and v == 0 else '~addr')
        return v

    def junk(self, name, n, why):
        """n bytes the game never set (rule junk)."""
        self.take(n)
        self.excluded(name, 'junk', why)
        self.put(name, '~junk')

    def identity(self, name, why):
        """A pointer the save uses as a record's identity (0: none): numbered by first appearance."""
        v = struct.unpack('<I', self.take(4))[0]
        self.excluded(name, 'identity', why)
        if v == 0:
            self.put(name, '0')
        else:
            self.put(name, 'p%d' % self.pointers.setdefault(v, len(self.pointers) + 1))
        return v

    def dwords(self, name, n, names=None):
        """n dwords of a raw block by offset (+XX), named where `names` ({offset: name or (kind, name[, why])})
        knows them; kinds: 'i' int, 'u', 'x' hex, 'f' float, and with a reason 'addr', 'junk', 'ident'. The rest
        are hex."""
        names = names or {}
        with self.scope(name):
            for i in range(n):
                spec = names.get(4 * i, ('x', f'+{4 * i:X}'))
                spec = spec if isinstance(spec, tuple) else ('x', spec)
                kind, field = spec[0], spec[1]
                if kind == 'junk':
                    self.junk(field, 4, spec[2])
                elif kind in ('addr', 'ident'):
                    (self.addr if kind == 'addr' else self.identity)(field, spec[2])
                else:
                    {'i': self.i32, 'u': self.u32, 'x': self.x32, 'f': self.f32}[kind](field)

    def at_end(self):
        return self.p == len(self.d)


# ---- the world (CWorldServer::SaveWorld 00597BD0, world_save.cpp) --------------------------------------

def read_diff(r, name):
    """CStream::WriteDiff (004A4DA0, core_stream_diff.cpp): runs of equal and of changed bytes against the
    zone's own AI graph state; kept as the changed bytes by offset in the graph state."""
    offset = 0
    equal = True
    first = True
    last = None
    changes = []
    while True:
        n = r.u8(None)
        if n == 0 and not first and last is not None and last != 255:
            break   # the end mark: an empty run after a run that was not full
        first = False
        if not equal and n:
            changes.append((offset, r.take(n)))
        offset += n
        last = n
        equal = not equal
    with r.scope(name):
        r.put('size', str(offset))
        for at, data in changes:
            r.put(f'+{at:X}', data.hex())


def read_world(r):
    """SaveWorld: the world's own fields, the AI graph's changes, the objects by manager, the players."""
    import savefile_objects as objects
    import savefile_items as items
    import savefile_players as players
    r.entity('world', 'CWorldServer')
    r.string('mobName')
    r.string('mapName')
    r.string('zoneName')
    r.vec('windDirection')
    r.f32('windStrength')
    r.f32('nightLevel')
    for i in range(5):
        with r.scope(f'alarm{i}'):
            r.u8('active')
            r.raw('pad', 3)   # LoadWorld (world_save.cpp) zeroes all five alarms before the map loads
            r.i32('time')
            r.f32('x')
            r.f32('y')
    r.u32('tick')
    r.i32('weather')
    r.f32('weatherStrength')
    r.f32('time')
    r.f32('timeSpeed')
    r.u32('lastRainBoltTick')
    r.i32('sessionKind')
    r.entity('aigraph', 'the AI graph state against the zone file (00596140 / 005B1380)')
    read_diff(r, 'diff')

    # The map objects' identities (mapObjects_ in its hash order), then the managers' object lists.
    identities = []
    r.entity('mapObjects', 'map object identities (id, type, prototype) in mapObjects_ order')
    with r.scope('object'):
        while True:
            oid = r.i32(None)
            if oid == -1:
                break
            kind = r.i32(None)
            proto = r.i32(None)
            r.put(str(oid), f'type {kind:#x} prototype {proto}')
            identities.append((oid, kind, proto))
    lists = {}
    for manager in ('timed', 'spell', 'item'):
        r.entity(f'{manager}Objects', f'CBaseManager::Save (00404760): {manager} objects (id, type, prototype)')
        count = r.i32('count')
        lists[manager] = []
        for _ in range(count):
            oid, kind, proto = r.i32(None), r.i32(None), r.i32(None)
            r.put(f'object.{oid}', f'type {kind:#x} prototype {proto}')
            lists[manager].append((oid, kind))

    # SaveMapObjects (00596730, world_zone.cpp): per map object its id, prototype name, Save (slot 10), the
    # changed / sent parts; then -1.
    types = {oid: kind for oid, kind, _ in identities}
    while True:
        oid = r.i32(None)
        if oid == -1:
            break
        kind = types.get(oid)
        r.entity(f'obj{oid}', f'map object type {kind:#x}' if kind is not None else 'map object (no identity)')
        r.string('prototype')
        objects.read_map_object(r, kind)
        r.x32('parts')
    # CBaseManager::SaveObjects (00404860) of the timed objects, the spells and the items.
    for manager in ('timed', 'spell', 'item'):
        kinds = dict(lists[manager])
        while True:
            oid = r.i32(None)
            if oid == objects.END_OF_OBJECTS:
                if r.u8(None) == 0:
                    break
            r.entity(f'{manager}{oid}', f'{manager} type {kinds.get(oid, 0):#x}')
            items.read_managed(r, manager, kinds.get(oid))

    r.entity('world.lists', 'the timed ids, missiles, time wheel, commands, diplomacy, countdowns')
    with r.scope('timedIds'):
        n = 0
        while True:
            v = r.i32(None)
            if v == -1:
                break
            r.put(str(n), str(v))
            n += 1
    n = 0
    while r.u8(None):
        r.entity(f'missile{n}', 'Projectile (0067E510)')
        items.read_projectile(r)
        n += 1
    r.entity('timeWheel', 'TimeWheelSave')
    items.read_time_wheel(r)
    r.entity('commands', 'the first command queue (CommandQueueSave)')
    items.read_command_queue(r)
    r.entity('diplomacy', 'DiplomacyWrite')
    items.read_diplomacy(r)
    r.entity('world.tail', 'climate, cave, countdowns, counted objects, players, items, .mob files')
    r.i32('climate')
    r.i32('cave')
    with r.scope('countdowns'):
        items.read_countdowns(r)
    with r.scope('counted'):
        n = 0
        while True:
            v = r.i32(None)
            if v == -1:
                break
            r.put(str(n), str(v))
            n += 1
    players.read_players(r)
    r.entity('world.end', 'paused, item records, .mob files, shared objects')
    r.u8('paused')
    with r.scope('itemRecord'):
        while True:
            oid = r.i32(None)
            if oid == -1:
                break
            r.i32(str(oid))
    count = r.i32('mobFiles')
    for i in range(count):
        r.string(f'mobFile{i}')
    count = r.i32('shared')
    for i in range(count):
        with r.scope(f'shared{i}'):
            r.i32('objectId')
            r.i32('player')
            r.u32('money')
            n = r.i32('count')
            for j in range(n):
                r.i32(f'object{j}')


def read_info(r):
    """info.sav (the slot list's title): version, a float, the allod and the zone, a dword."""
    r.entity('info', 'info.sav')
    r.f32('f8')
    r.string('allod')
    r.string('zone')
    r.raw('rest', len(r.d) - r.p)


# ---- whole files ----------------------------------------------------------------------------------------

def decode_data(data, name=''):
    """The decoded entities of a save file's bytes: [(key, description, [(field, value)])]."""
    magic, version, plain = read_frame(data)
    r = Reader(plain, version, name)
    r.entity('file', f'magic {magic:#x}')
    r.put('version', f'{version:#x}')
    if magic == MAGIC_MISSION:
        read_world(r)
    elif magic == MAGIC_SCENARIO:
        import savefile_scenario
        savefile_scenario.read_scenario(r)
    elif magic == MAGIC_INFO:
        read_info(r)
    elif magic == MAGIC_PARTY:
        import savefile_scenario
        savefile_scenario.read_party(r, 'party', "the hero's party (CParty::SaveParty with variables)")
    else:
        raise DecodeError(f'{name}: unknown magic {magic:#x}')
    if not r.at_end():
        r.fail(f'{len(r.d) - r.p} bytes left after the decode')
    return [(k, desc, fields) for k, desc, fields in r.entities]


def decode(path):
    with open(path, 'rb') as f:
        return decode_data(f.read(), path)


def lines(entities):
    """The decoded save as text lines: '+ key description', then 'key field value' per field."""
    out = []
    for key, description, fields in entities:
        out.append(f'+ {key} {description}'.rstrip())
        out += [f'{key} {field} {value}' for field, value in fields]
    return out


def parse_lines(text_lines):
    """Lines (as `lines` writes them) back into an ordered {(key, field): value} and the entity list."""
    fields, order = {}, []
    for line in text_lines:
        if line.startswith('+ '):
            continue
        key, field, value = (line.split(' ', 2) + ['', ''])[:3]
        fields[(key, field)] = value
        order.append((key, field))
    return fields, order


def differences(a_lines, b_lines):
    """The fields that differ between two decoded saves, in the first one's order (then the second's extra
    fields): [(key, field, a, b)]."""
    fa, oa = parse_lines(a_lines)
    fb, ob = parse_lines(b_lines)
    out = []
    for k in oa + [k for k in ob if k not in fa]:
        va, vb = fa.get(k, '(none)'), fb.get(k, '(none)')
        if va != vb:
            out.append((k[0], k[1], va, vb))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('show')
    p.add_argument('file')
    p.add_argument('--key')
    p = sub.add_parser('diff')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--fields', type=int, default=20)
    p = sub.add_parser('raw')
    p.add_argument('file')
    p.add_argument('out')
    p = sub.add_parser('rules')
    p.add_argument('files', nargs='+')
    args = ap.parse_args()
    if args.command == 'rules':
        for path in args.files:
            decode(path)
        for (field, rule), why in sorted(EXCLUDED.items()):
            print(f'{field} ~{rule}: {why}')
        return 0
    if args.command == 'raw':
        with open(args.file, 'rb') as f:
            magic, version, plain = read_frame(f.read())
        with open(args.out, 'wb') as f:
            f.write(plain)
        print(f'magic {magic:#x} version {version:#x}: {len(plain)} bytes')
        return 0
    if args.command == 'show':
        pattern = re.compile(args.key) if args.key else None
        try:
            entities, error = decode(args.file), None
        except DecodeError as e:   # what was decoded up to the error, then the error
            entities, error = getattr(e, 'entities', []), e
        for line in lines(entities):
            key = line[2:].split(' ', 1)[0] if line.startswith('+ ') else line.split(' ', 1)[0]
            if not pattern or pattern.search(key):
                print(line)
        if error:
            print(f'DecodeError: {error}')
            return 2
        return 0
    found = differences(lines(decode(args.a)), lines(decode(args.b)))
    for key, field, a, b in found[:args.fields]:
        print(f'{key} {field}: {a} in A, {b} in B')
    if len(found) > args.fields:
        print(f'... {len(found) - args.fields} more')
    print(f'{len(found)} fields differ' if found else 'the same')
    return 1 if found else 0


if __name__ == '__main__':
    sys.path.insert(0, __import__('os').path.dirname(__file__))
    sys.exit(main())
