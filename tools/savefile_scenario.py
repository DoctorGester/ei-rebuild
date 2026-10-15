"""scenario.sav decoded (tools/savefile.py): GameFlow::SaveGame (0057B3B0, world_game.cpp) writes, into one memory
stream,
  PartyManager::Save (004A8560, party_manager.cpp)   the party manager as a chunk archive, as a sized block
  CInterface3D::SaveState (005F85C0)                  the interface's state
  the save stream (007AFDF8) as a sized block         what the bottom window wrote into it (005F86D0, slot 40):
                                                      CBattleScreenSetup::SaveState (00635080)
  CCamera::SaveState (00410C10)                       the camera
The party readers (read_party and what it calls) serve the players of mission.sav too (savefile_players.py).

The chunk archive (chunk_archive.cpp, ChunkArchive::Close 0040A000) is three top-level chunks: 1 the root chunk's
data, 0 the index (class id, key per saved object), 2 the object records (per object a chunk 1 with field 0
the key and chunk 1 its Serialize). A chunk's children are an id byte, a length (one byte len*2, or a dword
len*2|1) and the bytes. Objects (the class templates, the party records) are referred to by their key
(SaveObjectRef 0040A440): the original wrote the object's address, ours and the harnessed original its number by
first save (SV7b2a); the loader maps any key to the object it creates from the index.
"""
import struct

import savefile_items as items

# ---- the chunk archive ---------------------------------------------------------------------------

def _length(r):
    first = r.u8(None)
    if first & 1:
        rest = r.take(3)
        return (first | rest[0] << 8 | rest[1] << 16 | rest[2] << 24) >> 1
    return first >> 1


def chunk(r, expect):
    """A chunk header (ChunkArchive::AppendChild 00409110): the id (must be `expect`) and the length; returns
    where the chunk's content ends."""
    at = r.p
    cid = r.u8(None)
    if cid != expect:
        r.p = at
        r.fail(f'chunk {cid} where chunk {expect} was expected')
    n = _length(r)
    if r.p + n > len(r.d):
        r.fail(f'chunk {cid} of {n} bytes runs past the end')
    return r.p + n


def leave(r, end, what='chunk'):
    if r.p != end:
        r.fail(f'{what} ends at {end:#x}, the decode at {r.p:#x}')


def field(r, fid, name, kind, *args):
    """A field (ChunkArchive::Field 004092B0 / CStringField 004093A0): chunk `fid` holding one value read with
    the Reader method `kind`."""
    end = chunk(r, fid)
    v = getattr(r, kind)(name, *args)
    leave(r, end, f'field {fid}')
    return v


def sized(r):
    """CStream::WriteSized (004A5250): size * 2 in two bytes, or size * 2 + 1 in four; returns where the block
    ends."""
    word = r.u16(None)
    if word & 1:
        word |= r.u16(None) << 16
    n = word >> 1
    if r.p + n > len(r.d):
        r.fail(f'a sized block of {n} bytes runs past the end')
    return r.p + n


def stream_field(r, fid):
    """ChunkArchive::StreamField (00409640): chunk `fid` {field 1 the size, field 2 the bytes}; returns where
    the bytes end (the caller reads them)."""
    end = chunk(r, fid)
    size = field(r, 1, None, 'u32')
    bytes_end = chunk(r, 2)
    if bytes_end - r.p != size:
        r.fail(f'stream field of {bytes_end - r.p} bytes, its size field says {size}')
    return bytes_end, end


# ---- the party (CParty, party_save.cpp) ------------------------------------------------------------

def is_text_key(uid):
    """IsTextKey (004613A0): ids 1000000000..1999999999 (the heroes and named units)."""
    return uid is not None and 1000000000 <= uid < 2000000000


def read_stats(r, name, text_key):
    """UnitStats::SaveTo (00528800): the same block a unit writes, so savefile_unit's reader and rules."""
    import savefile_unit
    savefile_unit.read_stats(r, name, text_key)


def _item_ids(r, name):
    """WriteItemIds (party_save.cpp): a count, then each item's id (-1 for an empty entry)."""
    with r.scope(name):
        n = r.i32('count')
        if n < 0 or n > 10000:
            r.fail(f'item list of {n}')
        for i in range(n):
            r.id(str(i))


def _peek_unit_id(r):
    """The unitId of the PartyUnitRecord at r.p (after its five strings), for the entity key."""
    p = r.p
    for _ in range(5):
        p = r.d.find(b'\0', p)
        if p < 0:
            return None
        p += 1
    return struct.unpack_from('<i', r.d, p)[0] if p + 4 <= len(r.d) else None


def read_unit_record(r, owner, index):
    """PartyUnitRecord::Save (0065FDE0): texts 0, 2-5, the unit id, the stats, the figure's scale, the four item
    lists, text 1, moveMode2, weaponIndex, selected (a byte), leaderId, fightAtWill, text 6."""
    uid = _peek_unit_id(r)
    r.entity(f'{owner}.unit{uid}', f'party unit record {index} (PartyUnitRecord)')
    r.put('index', str(index))
    for i in (0, 2, 3, 4, 5):
        r.string(f'text{i}')
    r.id('unitId')
    read_stats(r, 'stats', is_text_key(uid))
    r.f32('scaleX')
    r.f32('scaleY')
    r.f32('height')
    for name in ('weapons', 'quickItems', 'armor', 'spells'):
        _item_ids(r, name)
    r.string('text1')
    r.i32('moveMode2')
    r.u32('weaponIndex')
    r.u8('selected')
    r.id('leaderId')
    r.i32('fightAtWill')
    r.string('text6')


def _resume(r, key):
    r.current = r.keys[key]


def _manager_list(r, name):
    """CBaseManager::Save (00404760): the count, then {id, type, prototype} per object."""
    out = []
    with r.scope(name):
        n = r.i32('count')
        if n < 0 or n > 100000:
            r.fail(f'manager list of {n}')
        for _ in range(n):
            oid, kind, proto = r.i32(None), r.i32(None), r.i32(None)
            r.put(f'object.{oid}', f'type {kind:#x} prototype {proto}')
            out.append((oid, kind))
    return out


def _manager_objects(r, owner, manager, kinds):
    """CBaseManager::SaveObjects (00404860): {id, the object's Save (slot 10)} per object, then the end mark and
    a 0 byte (a 1 byte after an id equal to the mark)."""
    kinds = dict(kinds)
    while True:
        oid = r.i32(None)
        if oid == items_end():
            if r.u8(None) == 0:
                break
        r.entity(f'{owner}.{manager}{oid}', f'{manager} type {kinds.get(oid, 0):#x}')
        items.read_managed(r, manager, kinds.get(oid))


def items_end():
    import savefile_objects
    return savefile_objects.END_OF_OBJECTS


def read_variables(r, name):
    """NamedFloatTable::Save (00461260): {name, value} in the table's (string hash) order, then ""."""
    with r.scope(name):
        while True:
            key = r.string(None)
            if key == '':
                break
            r.f32(key)


def read_party(r, owner, description, variables=True):
    """CParty::SaveParty (00661C10): the zone, the entrance, the item and spell managers (lists, then objects),
    the party's item ids, the unit records, the variables (when `variables`), the money XOR a random key and
    the key."""
    r.entity(owner, description)
    r.string('zone')
    r.i32('entrance')
    item_list = _manager_list(r, 'itemManager')
    spell_list = _manager_list(r, 'spellManager')
    _manager_objects(r, owner, 'item', item_list)
    _manager_objects(r, owner, 'spell', spell_list)
    _resume(r, owner)
    _item_ids(r, 'items')
    n = r.i32('members')
    if n < 0 or n > 1000:
        r.fail(f'{n} party unit records')
    for i in range(n):
        read_unit_record(r, owner, i)
    _resume(r, owner)
    if variables:
        read_variables(r, 'variables')
    masked = r.u32(None)
    key = r.u32(None)
    r.put('money', str(masked ^ key))
    r.put('moneyKey', f'0x{key:08X}')


# ---- the party manager's archive (PartyManager::Save 004A8560) -----------------------------------------

def _party_bytes(r, owner, description, variables):
    bytes_end, end = stream_field(r, 2)
    read_party(r, owner, description, variables)
    leave(r, bytes_end, f'{owner} party stream')
    leave(r, end, f'{owner} stream field')


def read_template(r, key):
    """ClassTemplate::Serialize (004A6C40): 1 the class, 2 / 3 the flags, 4 the weights and the dword after
    them, stream field 5 (the items and spells parties without variables, then both stocks' lists), 6 flag38."""
    field(r, 1, 'type', 'i32')
    field(r, 2, 'flag0C', 'u8')
    field(r, 3, 'flag0D', 'u8')
    end = chunk(r, 4)
    with r.scope('weights'):
        for i in range(9):
            r.f32(str(i))
    r.x32('weightsTail')   # SV7b2: zeroed before the constructor in both
    leave(r, end, 'field 4')
    bytes_end, end = stream_field(r, 5)
    read_party(r, f'{key}.items', 'the template\'s items party (CParty::SaveParty without variables)', False)
    read_party(r, f'{key}.spells', 'the template\'s spells party (CParty::SaveParty without variables)', False)
    for stock in ('allowed', 'magic'):
        r.entity(f'{key}.{stock}', 'ItemStock::SaveLists (0069BAC0)')
        read_stock(r)
    leave(r, bytes_end, f'{key} stream')
    leave(r, end, 'stream field 5')
    _resume(r, key)
    field(r, 6, 'flag38', 'u8')


def read_stock(r):
    """ItemStock::SaveLists (0069BAC0): the armour, weapon and quick item names, the materials (name, min, max,
    left), the spell codes, the modifier codes."""
    for name in ('armors', 'weapons', 'quickItems'):
        with r.scope(name):
            n = r.i32('count')
            for i in range(n):
                r.string(str(i))
    with r.scope('materials'):
        n = r.i32('count')
        for i in range(n):
            with r.scope(str(i)):
                r.string('name')
                r.i32('minCount')
                r.i32('maxCount')
                r.i32('left')
    with r.scope('spells'):
        n = r.i32('count')
        for i in range(n):
            r.string(str(i))
    with r.scope('modifiers'):
        n = r.i32('count')
        for i in range(n):
            # A CString of a stack copy of the record's four code bytes (SV7b2a: its fifth byte 0 in both).
            r.string(str(i))


def read_record(r, key):
    """PartyRecord::Serialize (004A5AF0): 1 the name, stream field 2 the party (SaveParty with variables)."""
    field(r, 1, 'name', 'string')
    _party_bytes(r, f'{key}.party', 'the record\'s party (CParty::SaveParty)', True)


CLASSES = {1: 'template', 2: 'record'}


def read_party_manager(r):
    """PartyManager::Save (004A8560): the archive (ArchiveManager: 1 the parties by key, 2 the entries, field 3,
    4 {1 the class templates}, 5 the named records {1 the records, 2 / 3 the two names}) as a sized block."""
    block_end = sized(r)
    r.entity('partyManager', 'PartyManager::Save (004A8560): the archive\'s root chunk')

    # Top-level chunk 1: the root chunk's data.
    root_end = chunk(r, 1)
    end = chunk(r, 1)                       # SerializeParties (004ACEC0)
    n = 0
    while r.p < end:
        c1 = chunk(r, 1)
        key = field(r, 1, f'party{n}.key', 'i32')
        leave(r, c1)
        c2 = chunk(r, 2)                    # CParty::ArchiveParty (006617F0): stream field 1 (with variables)
        bytes_end, sf_end = stream_field(r, 1)
        read_party(r, f'party{key}', f'the manager\'s party {key} (CParty::SaveParty)', True)
        leave(r, bytes_end)
        leave(r, sf_end)
        leave(r, c2)
        _resume(r, 'partyManager')
        n += 1
    leave(r, end, 'the parties chunk')
    r.put('parties', str(n))
    end = chunk(r, 2)                       # SerializePartyEntries (004AD0C0)
    n = 0
    while r.p < end:
        with r.scope(f'entry{n}'):
            e = chunk(r, 1)
            field(r, 1, 'name', 'string')
            field(r, 2, 'text', 'string')
            ids = chunk(r, 3)
            k = 0
            while r.p < ids:
                c = chunk(r, 1)
                field(r, 1, f'id{k}', 'i32')
                leave(r, c)
                k += 1
            leave(r, ids)
            leave(r, e)
        n += 1
    leave(r, end, 'the entries chunk')
    r.put('entries', str(n))
    field(r, 3, 'field3', 'i32')
    end = chunk(r, 4)
    templates = chunk(r, 1)                 # SerializeClassTemplates (004ACDC0)
    n = 0
    while r.p < templates:
        field(r, 1, f'template{n}', 'i32')
        n += 1
    leave(r, templates)
    leave(r, end)
    end = chunk(r, 5)                       # NamedPartyRecords::Serialize (004A64B0)
    records = chunk(r, 1)
    n = 0
    while r.p < records:
        field(r, 1, f'record{n}', 'i32')
        n += 1
    leave(r, records)
    field(r, 2, 'text0C', 'string')
    field(r, 3, 'text10', 'string')
    leave(r, end)
    leave(r, root_end, 'the root chunk')

    # Top-level chunk 0: the index, per saved object its class id and its key: its number by first save
    # (SV7b2a; the original wrote its address, ChunkArchive::SaveObjectRef 0040A440).
    r.entity('partyManager.index', 'the archive\'s index: per object its class and its key')
    end = chunk(r, 0)
    classes = []
    i = 0
    while r.p < end:
        cid = r.i32(f'{i}.class')
        r.i32(f'{i}.object')
        classes.append(cid)
        i += 1
    leave(r, end, 'the index')

    # Top-level chunk 2: the object records (in the index's order), then field 2 the version.
    end = chunk(r, 2)
    counts = {}
    for i, cid in enumerate(classes):
        rec = chunk(r, 1)
        f0 = chunk(r, 0)
        at = r.p
        r.take(4)
        leave(r, f0)
        body = chunk(r, 1)
        kind = CLASSES.get(cid)
        if kind is None:
            r.fail(f'object {i} of class {cid} is not decoded')
        n = counts.get(kind, 0)
        counts[kind] = n + 1
        key = f'{kind}{n}'
        if kind == 'template' and r.d[r.p:r.p + 2] == b'':   # keyed by its class (the type field)
            key = 'template.class%d' % struct.unpack_from('<i', r.d, r.p + 2)[0]
        r.entity(key, 'ClassTemplate (class 1)' if kind == 'template' else 'PartyRecord (class 2)')
        here = r.p
        r.p = at
        r.i32('identity')   # field 0: the key the index names it by
        r.p = here
        (read_template if kind == 'template' else read_record)(r, key)
        leave(r, body, f'{key} record')
        leave(r, rec)
    r.entity('partyManager.end', 'the archive\'s version field')
    field(r, 2, 'version', 'x32')
    leave(r, end, 'the records chunk')
    leave(r, block_end, 'the party manager block')


# ---- the interface, the bottom window, the camera -------------------------------------------------------

def read_interface(r):
    """CInterface3D::SaveState (005F85C0): the three HUD panel flags, the body part, the top page, fastGame, an
    empty obsolete list (its count), the message text, its flag, the saved text."""
    r.entity('input', 'CInterface3D::SaveState (005F85C0)')
    for i in range(3):
        r.u8(f'hudPanelIn{i}')
    r.i32('hudBodyPart')
    r.i32('hudTopPage')
    r.i32('fastGame')
    n = r.i32('obsoleteList')
    if n != 0:
        r.fail(f'the obsolete list has {n} entries (the writer writes 0)')
    r.string('messageText')
    r.i32('messageFlag')
    r.string('savedText')


def read_screen(r):
    """The save stream (007AFDF8) as a sized block (CStream::WriteSized 004A5250): what the bottom window's slot
    40 (CInterface3D::SaveWindowState 005F86D0) wrote, CBattleScreenSetup::SaveState (00635080): the selected
    units' ids, the map zoom, the server flag, savedWithScreen, the top panel's first page text, savedFlag,
    savedSelection. The briefing screen (CBriefingScreen::SaveState 00602490) and the global map (0062D330) are the
    bottom window of a save made over them, and write one zero dword instead (the battle screen's block is at least
    19 bytes, so the size tells the two apart)."""
    end = sized(r)
    r.entity('screen', 'the bottom window\'s state (CBattleScreenSetup::SaveState 00635080)')
    r.put('size', str(end - r.p))
    if r.p == end:
        return
    r.limit = end
    if end - r.p == 4:
        if r.i32('overOtherScreen') != 0:
            r.fail('a 4-byte screen block that is not a zero dword')
        r.limit = None
        return
    n = r.i32('selected')
    if n < 0 or n > 1000:
        r.fail(f'{n} selected units')
    for i in range(n):
        r.id(f'selected{i}')
    r.f32('mapZoom')
    r.u8('serverFlag')
    r.i32('savedWithScreen')
    r.string('topPanelText')
    r.u8('savedFlag')
    r.i32('savedSelection')
    r.limit = None
    leave(r, end, 'the screen block')


def read_camera(r):
    """CCamera::SaveState (00410C10): focus, focus height, view rotation, pitch, distance, the pan velocity and
    its target and response, the turn / tilt / zoom rate, target and response, flags, the target id, views 4-7."""
    r.entity('camera', 'CCamera::SaveState (00410C10)')
    r.vec('focus')
    r.f32('focusHeight')
    r.vec('viewRotation', 4)
    r.f32('pitch')
    r.f32('distance')
    r.vec('panVelocity')
    r.vec('panTarget')
    r.f32('panResponse')
    for name in ('turn', 'tilt', 'zoom'):
        r.f32(f'{name}Rate')
        r.f32(f'{name}Target')
        r.f32(f'{name}Response')
    r.x32('flags')
    r.id('targetId')
    for i in range(4, 8):
        with r.scope(f'view{i}'):
            r.f32('distance')
            r.vec('rotation', 4)
            r.f32('pitch')


def read_scenario(r):
    """GameFlow::SaveGame (0057B3B0): the party manager, the interface, the bottom window's block, the camera."""
    read_party_manager(r)
    read_interface(r)
    read_screen(r)
    read_camera(r)
