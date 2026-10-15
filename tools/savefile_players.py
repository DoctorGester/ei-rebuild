"""The players in a decoded mission save (tools/savefile.py): SavePlayers (world_save.cpp), part of
CWorldServer::SaveWorld (00597BD0).

The save has no player count: SavePlayers writes one record per player of the world's player list and LoadPlayers
reads one per player of the list the zone already has when it loads. The reader stops at the first place where
the rest of the data is SaveWorld's tail (paused, the item records, the .mob files, the shared objects) to the end.
"""
import struct

import savefile_scenario as scenario


def _tail_follows(d, p):
    """Whether d[p:] is exactly SaveWorld's tail after the players: paused (a byte), {id, value} item records
    until -1, the .mob file count and names, the shared objects (id, player, money, count, ids)."""
    n = len(d)

    def i32(at):
        return struct.unpack_from('<i', d, at)[0] if at + 4 <= n else None

    if p + 1 > n or d[p] > 1:
        return False
    p += 1
    while True:
        oid = i32(p)
        if oid is None:
            return False
        p += 4
        if oid == -1:
            break
        if p + 4 > n:
            return False
        p += 4
    count = i32(p)
    if count is None or not 0 <= count <= 10000:
        return False
    p += 4
    for _ in range(count):
        end = d.find(b'\0', p)
        if end < 0:
            return False
        p = end + 1
    count = i32(p)
    if count is None or not 0 <= count <= 100000:
        return False
    p += 4
    for _ in range(count):
        if p + 16 > n:
            return False
        k = i32(p + 12)
        if not 0 <= k <= 100000:
            return False
        p += 16 + 4 * k
        if p > n:
            return False
    return p == n


def read_player(r):
    """One player of SavePlayers: partyId, the party (CParty::SaveParty 00661C10 with variables: the player's
    party records), sessionKind, the player's item ids until -1, godMode (a byte), the id list (count, ids)."""
    party_id = r.peek_i32()
    key = f'player{party_id}'
    r.entity(key, 'a player (SavePlayers, world_save.cpp)')
    r.i32('partyId')
    scenario.read_party(r, f'{key}.party', 'the player\'s party records (CParty::SaveParty 00661C10)', True)
    scenario._resume(r, key)
    r.i32('sessionKind')
    with r.scope('items'):
        n = 0
        while True:
            oid = r.i32(None)
            if oid == -1:
                break
            r.put(str(n), str(oid))
            n += 1
        r.put('count', str(n))
    r.u8('godMode')
    with r.scope('ids'):
        n = r.i32('count')
        if n < 0 or n > 100000:
            r.fail(f'{n} ids')
        for i in range(n):
            r.u32(str(i))


def read_players(r):
    """SavePlayers (world_save.cpp): a record per player in the world's player list, until SaveWorld's tail."""
    while not _tail_follows(r.d, r.p):
        if r.at_end():
            r.fail('the data ends inside the players')
        read_player(r)
