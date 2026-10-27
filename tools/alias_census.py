"""C31: census of `using Name = Type;` aliases in src (container aliases by default).

For each alias: name, target type, use count (identifier occurrences in src, tests and tools' sources outside the
alias's own definition), files that use it. Kinds: `once` (0-1 uses: inline it), `reexport` (the target is only another
alias of src, a pass-through), `used` (named in several places).

usage:  python tools/alias_census.py [--all] [--kind once|reexport|used] [--summary] [--names A B ...]
        --all     every using alias, not only those whose target spells a container type
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ['src', 'tests', 'include']
EXTS = ('.h', '.cpp', '.inl', '.hpp')

CONTAINER_RE = re.compile(
    r'\b(ArrayAbi|ListAbi|VectorAbi|CArray|CList|CMap|CTypedPtrList|CTypedPtrArray|CTypedPtrMap|AlignedArray|'
    r'list|vector|hash_map|hash_set|map|set|deque|array|MapAbi|CPtrList|CPtrArray|CStringArray|CByteArray|'
    r'ListTemporary|StringArrayAbi|CPlex)\b')
ALIAS_RE = re.compile(r'^[ \t]*using[ \t]+(\w+)[ \t]*=[ \t]*([^;]*);', re.M)


def files():
    for d in DIRS:
        for base, _, names in os.walk(os.path.join(ROOT, d)):
            for n in names:
                if n.endswith(EXTS):
                    yield os.path.join(base, n)


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--kind')
    ap.add_argument('--summary', action='store_true')
    ap.add_argument('--names', nargs='*')
    args = ap.parse_args()

    texts = {}
    for f in files():
        with open(f, encoding='utf-8', errors='replace') as fh:
            texts[os.path.relpath(f, ROOT).replace(os.sep, '/')] = strip_comments(fh.read())

    aliases = []   # (name, target, file)
    for f, t in texts.items():
        for m in ALIAS_RE.finditer(t):
            aliases.append((m.group(1), ' '.join(m.group(2).split()), f))
    names = collections.Counter(a[0] for a in aliases)

    tokens = collections.Counter()
    where = collections.defaultdict(collections.Counter)
    for f, t in texts.items():
        for m in re.finditer(r'\b\w+\b', t):
            tokens[m.group(0)] += 1
            where[m.group(0)][f] += 1

    # an alias is a container alias when its target spells a container or names another container alias
    tgt = {}
    for name, target, f in aliases:
        tgt.setdefault(name, []).append(target)

    def is_container(target, seen=()):
        if CONTAINER_RE.search(target):
            return True
        m = re.match(r'^(?:\w+::)*(\w+)', target)
        if m and m.group(1) in tgt and m.group(1) not in seen:
            return any(is_container(t, seen + (m.group(1),)) for t in tgt[m.group(1)])
        return False

    rows = []
    for name, target, f in aliases:
        if not args.all and not is_container(target):
            continue
        if args.names and name not in args.names:
            continue
        uses = tokens[name] - names[name]          # minus the definitions of every alias of that name
        head = re.match(r'[\w:]*?(\w+)\s*(<|$)', target)
        re_target = re.match(r'^(?:\w+::)*(\w+)$', target)
        reexport = bool(re_target and re_target.group(1) in names)
        kind = 'reexport' if reexport else ('once' if uses <= 1 else 'used')
        rows.append((name, target, f, uses, kind, [k for k in where[name] if k != f or where[name][k] > 1]))
    if args.kind:
        rows = [r for r in rows if r[4] == args.kind]
    if args.summary:
        c = collections.Counter(r[4] for r in rows)
        print(f'{len(rows)} aliases:', dict(c))
        return
    for name, target, f, uses, kind, fs in sorted(rows, key=lambda r: (r[4], r[3], r[0])):
        dup = f' (name defined {names[name]}x)' if names[name] > 1 else ''
        print(f'{kind:8} {uses:3} {name}{dup} = {target[:90]}  [{f}] used in {len(fs)} files')


if __name__ == '__main__':
    main()
