"""Ratchet on duplicate EI_ORIGINAL forwarders (cleanup plan, ticket C3).

Fails when a function is forwarded from more than one file and was not already on the list in
config/forwarder_duplicates.txt: new code must use the shared declaration in
src/game/originals_<area>.h (add one there when the function has none) instead of declaring its own
forwarder. Addresses that are no longer duplicated drop off the list with --update.

usage: python tools/check_forwarders.py [--update]
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_originals
import srcscan

BASELINE = os.path.join(gen_originals.ROOT, 'config', 'forwarder_duplicates.txt')


def duplicated():
    sites = gen_originals.collect_sites(srcscan.load_sources())
    return {a: gen_originals.files_of(s) for a, s in sites.items() if len(gen_originals.files_of(s)) > 1}


def main(argv):
    dups = duplicated()
    known = set()
    if os.path.exists(BASELINE):
        known = {int(l.split()[0], 16) for l in open(BASELINE) if l.strip() and not l.startswith('#')}
    if '--update' in argv:
        keep = sorted(a for a in dups if a in known or not known)
        with open(BASELINE, 'w', newline='\n') as f:
            f.write('# Functions still forwarded from more than one file (tools/check_forwarders.py).\n'
                    '# Only shrinks: new code uses src/game/originals_<area>.h.\n')
            f.writelines(f'{a:08X}\n' for a in keep)
        print(f'{len(keep)} duplicated functions listed in {os.path.relpath(BASELINE, gen_originals.ROOT)}')
        return 0
    new = sorted(a for a in dups if a not in known)
    for a in new:
        print(f'{a:08X} is now forwarded from several files: {", ".join(dups[a])}; '
              f'declare it once in src/game/originals_<area>.h and use that')
    gone = len(known - set(dups))
    print(f'{len(dups)} duplicated functions, {len(new)} new, {gone} fixed '
          f'(run with --update to drop fixed ones from the list)')
    return 1 if new else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
