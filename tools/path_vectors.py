"""The "path" family's saved maps (tests/vectors/path_vectors.h): after `vectors.py merge`, keep only
the @AIMap records the kept searches run on.

usage:
  python tools/path_vectors.py prune [--data tests/vectors/data/path] [--drop <map id>...] [--keep <map id>...]
      Drops the searches on the maps named by --drop (with --keep: on every other map), then every
      map no search runs on; prints each map kept with its size and how many records use it.

The maps are large (a zone's is 0.6 MB): re-recording merges with --cap 100000 into a scratch folder,
prunes that to a few maps with --keep, then merges the folder into the data with --cap 24.
"""
import argparse, collections, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vectors   # noqa: E402

DATA = os.path.join(vectors.DATA, 'path')
MAPS = '@AIMap.vec'
SEARCHES = {'CAIMap__FindPath.vec': 'find', 'CAIMap__PlanLongPath.vec': 'plan', 'PathSmoother__Smooth.vec': 'smooth'}


def map_of(kind, record):
    """The map id a record runs on (None: a FindPath that returned before taking its map)."""
    payload = record[8:]
    if kind == 'find':
        return struct.unpack_from('<Q', payload, 1)[0] if payload[0] else None
    return struct.unpack_from('<Q', payload, 0)[0]


def prune(args):
    drop = {int(x, 16) for x in args.drop}
    if args.keep:
        keep = {int(x, 16) for x in args.keep}
        _, _, _, maps = vectors.read(os.path.join(args.data, MAPS))
        drop |= {struct.unpack_from('<Q', r, 8)[0] for r in maps} - keep
    uses = collections.Counter()
    for name, kind in SEARCHES.items():
        path = os.path.join(args.data, name)
        if not os.path.exists(path):
            continue
        function, size, cap, records = vectors.read(path)
        kept = [r for r in records if map_of(kind, r) not in drop]
        for r in kept:
            uses[map_of(kind, r)] += 1
        if len(kept) != len(records):
            vectors.write(path, function, size, cap, kept)
        print(f'{name}: {len(kept)} records ({len(records) - len(kept)} dropped)')
    path = os.path.join(args.data, MAPS)
    function, size, cap, records = vectors.read(path)
    kept = [r for r in records if struct.unpack_from('<Q', r, 8)[0] in uses]
    vectors.write(path, function, size, cap, kept)
    for r in kept:
        map_id = struct.unpack_from('<Q', r, 8)[0]
        width, height = struct.unpack_from('<ii', r, 16)
        print(f'  map {map_id:016X}: {width}x{height}, {len(r)} bytes, {uses[map_id]} records')
    missing = set(uses) - {struct.unpack_from('<Q', r, 8)[0] for r in kept} - {None}
    print(f'{MAPS}: {len(kept)} maps ({len(records) - len(kept)} dropped), {sum(map(len, kept))} bytes')
    if missing:
        raise SystemExit('records run on maps not in @AIMap: ' + ', '.join(f'{m:016X}' for m in sorted(missing)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('prune')
    p.add_argument('--data', default=DATA)
    p.add_argument('--drop', nargs='*', default=[])
    p.add_argument('--keep', nargs='*', default=[])
    args = ap.parse_args()
    prune(args)


if __name__ == '__main__':
    main()
