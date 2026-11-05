r"""Writes a network item database with one item more into a run's overlay (a '# prepare:' tool; C154c).

usage: python tools/make_extra_item_db.py <saves dir> [--source <databaselmp.res>]
  <saves dir>  the run's harness_saves folder: the harness's copy-on-write overlay reads <saves dir>\Res\databaselmp.res
               in place of the install's (tests/verify/harness.cpp, ShadowOf), so this game alone has the extra item
  --source     default the vanilla install's Res\databaselmp.res (EI_GAME_VANILLA; read only; Rebirth's is the same file)

The archive is the source's with its items.idb grown by one quest item, EXTRA_ITEM: a copy of the quest table's last
record (goldnuggets) under that name, so its index (the table's old count) is one the other game's database lacks. A
network game loads databaseLMP.res (GameFlow::UseResources). net_pair_database's host gives it to the peer, whose
client refuses the server's item (HasEveryRecord) and leaves the server.

items.idb is a chunk archive (src/game/chunk_archive.h): top-level chunks 0 (the index), 1 (the root: one chunk per
table, 1 materials .. 6 loot, each record a child 1 of fields) and 2. A chunk is an id byte and its length: one byte
(length << 1) below 0x80 bytes, else a dword (length << 1 | 1); then the bytes.
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path, make_test_mod, stub_data

EXTRA_ITEM = 'c154cnuggets'
QUEST_TABLE = 5
RECORD = 1
NAME_FIELD = 0


def children(buf, off, size):
    """The chunks in buf[off:off + size]: (id, content offset, content length)."""
    out, cur = [], 0
    while cur + 2 <= size:
        cid, first = buf[off + cur], buf[off + cur + 1]
        cur += 2
        length = first
        if first & 1:
            length = struct.unpack_from('<I', buf, off + cur - 1)[0]
            cur += 3
        length >>= 1
        out.append((cid, off + cur, length))
        cur += length
    return out


def chunk(cid, content):
    size = len(content) << 1
    head = bytes([cid, size]) if size < 0x100 else bytes([cid]) + struct.pack('<I', size | 1)
    return head + content


def rebuild(buf, parts, change):
    """The chunks `parts` of buf again, each content through change(id, offset, length) (bytes, or None: as it was)."""
    out = b''
    for cid, off, size in parts:
        new = change(cid, off, size)
        out += chunk(cid, bytes(buf[off:off + size]) if new is None else new)
    return out


def with_extra_item(idb):
    def record_copy(off, size):
        fields = children(idb, off, size)
        name = EXTRA_ITEM.encode('ascii') + b'\0'
        return rebuild(idb, fields, lambda cid, o, s: name if cid == NAME_FIELD else None)

    def table(cid, off, size):
        if cid != QUEST_TABLE:
            return None
        records = children(idb, off, size)
        last = [r for r in records if r[0] == RECORD][-1]
        return bytes(idb[off:off + size]) + chunk(RECORD, record_copy(last[1], last[2]))

    def top(cid, off, size):
        return rebuild(idb, children(idb, off, size), table) if cid == 1 else None

    return rebuild(idb, children(idb, 0, len(idb)), top)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves')
    parser.add_argument('--source')
    args = parser.parse_args()
    source = args.source or exe_path.install_file('vanilla', 'Res', 'databaselmp.res')
    buf = bytearray(open(source, 'rb').read())
    files = {name: bytes(buf[start:start + size]) for name, start, size in stub_data.res_entries(buf, 0, len(buf))}
    key = next(name for name in files if name.lower() == 'items.idb')
    files[key] = with_extra_item(files[key])
    folder = os.path.join(args.saves, 'Res')
    os.makedirs(folder, exist_ok=True)
    out = os.path.join(folder, 'databaselmp.res')
    open(out, 'wb').write(make_test_mod.archive(files))
    print(f'{out}: items.idb {len(files[key])} bytes, quest item {EXTRA_ITEM} added')


if __name__ == '__main__':
    main()
