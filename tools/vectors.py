"""Function vectors (tests/vectors/vectors.h): merge recorded runs into tests/vectors/data, show files.

Record mode: run scenarios with EI_VECTORS=1 in the environment (the verify build; e.g.
`set EI_VECTORS=1` then `python tools/work.py test <ID> units sandbox_actions --vanilla-only`). Each run
writes <run folder>/vectors/<family>/<function>.vec.

usage:
  python tools/vectors.py merge <run folder>... [--out tests/vectors/data] [--add] [--cap N]
      Every function's records from the runs, repeats dropped, `cap` per bucket (the recorder's, or N),
      chosen by a hash of the record so the same runs give the same files. Replaces those functions'
      files under --out; --add also keeps the records already there.
  python tools/vectors.py show <file or folder>...
      Header, records per bucket and control words.

Replay: build\\<config>\\ei_vectors.exe (build.cmd runs it on tests/vectors/data).
"""
import argparse, collections, glob, hashlib, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'tests', 'vectors', 'data')
MAGIC, VERSION = 0x31564945, 1
HEADER = struct.Struct('<8I64s')   # magic, version, payloadSize, count, cap, reserved[3], function


def read(path):
    """(function, payload size, cap, [record bytes]) of a .vec file."""
    data = open(path, 'rb').read()
    magic, version, size, count, cap, _, _, _, name = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        raise SystemExit(f'{path}: not a version {VERSION} vector file')
    stride = 4 + size
    records = [data[HEADER.size + i * stride:HEADER.size + (i + 1) * stride] for i in range(count)]
    if any(len(r) != stride for r in records):
        raise SystemExit(f'{path}: shorter than its {count} records')
    return name.split(b'\0', 1)[0].decode(), size, cap, records


def write(path, function, size, cap, records):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(HEADER.pack(MAGIC, VERSION, size, len(records), cap, 0, 0, 0, function.encode()))
        for r in records:
            f.write(r)


def vec_files(paths):
    for p in paths:
        if os.path.isdir(p):
            yield from sorted(glob.glob(os.path.join(p, '**', '*.vec'), recursive=True))
        else:
            yield p


def merge(args):
    groups = {}   # relative path -> [function, size, cap, records]
    for run in args.runs:
        folder = os.path.join(run, 'vectors') if os.path.isdir(os.path.join(run, 'vectors')) else run
        for path in vec_files([folder]):
            rel = os.path.relpath(path, folder)
            function, size, cap, records = read(path)
            g = groups.setdefault(rel, [function, size, cap, []])
            if (g[0], g[1]) != (function, size):
                raise SystemExit(f'{path}: {function} ({size} bytes) differs from an earlier {g[0]} ({g[1]} bytes)')
            g[2] = max(g[2], cap)
            g[3].extend(records)
    if not groups:
        raise SystemExit('no .vec files in the runs given (were they run with EI_VECTORS=1?)')
    for rel, (function, size, cap, records) in sorted(groups.items()):
        out = os.path.join(args.out, rel)
        if args.add and os.path.exists(out):
            records = records + read(out)[3]
        cap = args.cap or cap
        by_bucket = collections.defaultdict(dict)
        for r in records:
            by_bucket[r[2]][hashlib.sha1(r).digest()] = r
        kept = []
        for bucket in sorted(by_bucket):
            unique = by_bucket[bucket]
            kept += [unique[h] for h in sorted(unique)[:cap]]
        write(out, function, size, cap, kept)
        print(f'{os.path.relpath(out, ROOT)}: {function}, {len(kept)} records '
              f'({len(records)} recorded, {sum(len(u) for u in by_bucket.values())} distinct)')


def show(args):
    for path in vec_files(args.paths):
        function, size, cap, records = read(path)
        buckets = collections.Counter(r[2] for r in records)
        words = collections.Counter(struct.unpack_from('<H', r)[0] for r in records)
        print(f'{path}: {function}, {len(records)} records of {size} bytes, cap {cap}; buckets '
              + ', '.join(f'{b}: {n}' for b, n in sorted(buckets.items()))
              + '; control words ' + ', '.join(f'{w:04X}: {n}' for w, n in sorted(words.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    m = sub.add_parser('merge')
    m.add_argument('runs', nargs='+')
    m.add_argument('--out', default=DATA)
    m.add_argument('--add', action='store_true')
    m.add_argument('--cap', type=int, default=0)
    s = sub.add_parser('show')
    s.add_argument('paths', nargs='+')
    args = ap.parse_args()
    {'merge': merge, 'show': show}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
