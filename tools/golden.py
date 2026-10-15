r"""Game-state digests and golden runs (docs/notes/standalone-verification.md, ticket SV2).

A harness run with "digest <N>" in its scenario, or EI_DIGEST=<N> in the environment, writes digest.txt
in its run folder: a record of the world server's state every N world updates (harness_digest.h). This
tool compares two runs' records and names the first field that differs, and keeps golden runs.

usage:
  python tools/golden.py diff A B [--ulps N] [--records K] [--fields K] [--ignore REGEX]
      A, B: run folders (a name under build\verify\runs, or a path) or digest files. Walks both
      digests record by record; at the first record whose state differs it prints the record (world
      update, frame, tick) and the fields that differ, in record order: the world, the generators,
      the players, the objects by id. --ulps N lets floats differ by up to N units in the last place
      (the rewrite's float maths, rewrite guide rule 7); --records K shows K differing records (default
      1), --fields K up to K fields each (default 12); --ignore drops fields whose "<key> <field>"
      matches the expression. Fields that follow heap addresses (ADDRESS_DEPENDENT; none since
      SV1b) are left out unless --all-fields.
  python tools/golden.py state A [--record N] [--key REGEX]
      The whole state at record N (default: the last) of one digest, or the entities whose key matches.
  python tools/golden.py record SCENARIO... --reason TEXT [--install vanilla|rebirth] [--run FOLDER]
      Keeps a run of the original code as the scenario's golden: build\verify\runs\<scenario>-<install>
      (or FOLDER) must have passed, run with --no-hooks and a digest. Writes
      tests\golden\<scenario>\<install>\ console.txt (what the game printed), digest.txt and golden.json
      (when, from which commit, the digest period, the reason). Run the games first through the
      scheduler, e.g.:  set EI_DIGEST=10 & python tools\work.py test <ID> <scenario> --vanilla-only --no-hooks
  python tools/golden.py compare SCENARIO... [--install vanilla|rebirth] [--run FOLDER] [--ulps N] ...
      A run (by default build\verify\runs\<scenario>-<install>, made with the golden's EI_DIGEST)
      against the scenario's golden: the console output, then the digests as "diff" does.
exit code: 0 the same, 1 a difference, 2 something missing
"""
import argparse, datetime, json, os, re, shutil, struct, subprocess, sys

import run_all

ROOT = run_all.ROOT
GOLDEN = os.path.join(ROOT, 'tests', 'golden')
# Fields that follow heap addresses, which would differ between runs and between the original code alone and
# ours (the verify wrappers allocate too); left out unless --all-fields. None now: the one there was, a unit's
# update-map countdown (0x80 + bits 5..8 of its address in the game), harness runs on the virtual clock take
# from the object's id on both sides (tests/verify/harness_heap.h, SV1b). Runs with `--clock virtual,lfheap`
# keep the game's: pass --ignore ' updateCountdown$' for those.
ADDRESS_DEPENDENT = None
HEADER = re.compile(r'@ (\d+) update (\d+) frame (\d+) tick (-?\d+) zone (\S+) hash ([0-9A-F]{8})')


def digest_path(spec):
    """A digest file from a run folder's name or path, or a file."""
    for path in (spec, os.path.join(run_all.BUILD, 'runs', spec)):
        if os.path.isfile(path):
            return path
        if os.path.isdir(path) and os.path.exists(os.path.join(path, 'digest.txt')):
            return os.path.join(path, 'digest.txt')
    return None


def key_order(key):
    """Record order of an entity key: world, random, players by id, objects by id."""
    for rank, prefix in enumerate(('world', 'random', 'player', 'obj')):
        if key.startswith(prefix):
            rest = key[len(prefix):]
            return rank, int(rest) if rest.lstrip('-').isdigit() else 0
    return 9, 0


class Digest:
    """Reads a digest file record by record, keeping the whole state: {key: [description, {field: value}]}."""

    def __init__(self, path):
        self.path = path
        with open(path, encoding='cp1251', errors='replace') as f:
            first = f.readline().rstrip('\n')
        self.title = first if first.startswith('#') else ''
        self.file = open(path, encoding='cp1251', errors='replace')
        self.state = {}
        self.pending = None   # the next record's header line

    def __iter__(self):
        for line in self.file:
            line = line.rstrip('\n')
            if line.startswith('#'):
                self.title = self.title or line
                continue
            if line.startswith('@ '):
                if self.pending is not None:
                    yield self.header(self.pending)
                self.pending = line
                continue
            self.apply(line)
        if self.pending is not None:
            yield self.header(self.pending)
            self.pending = None

    @staticmethod
    def header(line):
        m = HEADER.match(line)
        fields = line.split()
        info = dict(zip(fields[1::2], fields[2::2]))
        return {'record': int(m.group(1)), 'update': int(m.group(2)), 'frame': int(m.group(3)), 'tick': int(m.group(4)),
                'zone': m.group(5), 'hash': m.group(6), 'n': info.get('n'), 'line': line}

    def apply(self, line):
        if line.startswith('+ '):
            _, key, description = (line.split(' ', 2) + [''])[:3]
            self.state[key] = [description, {}]
        elif line.startswith('- '):
            self.state.pop(line[2:], None)
        elif line:
            key, field, value = (line.split(' ', 2) + ['', ''])[:3]
            fields = self.state.setdefault(key, ['?', {}])[1]
            if value == '-':
                fields.pop(field, None)
            else:
                fields[field] = value

    def period(self):
        m = re.search(r'every (\d+) world updates', self.title)
        return int(m.group(1)) if m else None


def float_bits(text):
    """The bits of a float value as the digest writes it, or None for an integer or bit pattern."""
    if text.startswith('nan:'):
        return int(text[4:], 16)
    if text.startswith('0x') or not any(c in text for c in '.eni'):
        return None
    try:
        return struct.unpack('<I', struct.pack('<f', float(text)))[0]
    except (ValueError, OverflowError):
        return None


def ordered(bits):
    """A float's bits as an integer that counts ULPs across zero."""
    return bits if bits < 0x80000000 else 0x80000000 - bits


def same(a, b, ulps):
    if a == b:
        return True
    if not ulps or a is None or b is None:
        return False
    fa, fb = float_bits(a), float_bits(b)
    if fa is None or fb is None or (fa & 0x7F800000) == 0x7F800000 or (fb & 0x7F800000) == 0x7F800000:
        return False
    return abs(ordered(fa) - ordered(fb)) <= ulps


def differences(sa, sb, ulps, ignore):
    """The fields that differ between two states, in record order: (key, description, field, a, b)."""
    out = []
    for key in sorted(set(sa) | set(sb), key=key_order):
        ea, eb = sa.get(key), sb.get(key)
        if ea is None or eb is None:
            out.append((key, (ea or eb)[0], '(entity)', 'present' if ea else 'missing', 'present' if eb else 'missing'))
            continue
        if ea[0] != eb[0]:
            out.append((key, ea[0], '(description)', ea[0], eb[0]))
        fa, fb = ea[1], eb[1]
        for field in list(fa) + [f for f in fb if f not in fa]:
            if ignore and ignore.search(f'{key} {field}'):
                continue
            if not same(fa.get(field), fb.get(field), ulps):
                out.append((key, ea[0], field, fa.get(field, '(none)'), fb.get(field, '(none)')))
    return out


def where(h):
    return f'record {h["record"]} (world update {h["update"]}, frame {h["frame"]}, tick {h["tick"]}, zone {h["zone"]})'


def diff(path_a, path_b, name_a='A', name_b='B', ulps=0, records=1, fields=12, ignore=None, out=print):
    """Compares two digests; prints the first differing records. 0 the same, 1 a difference."""
    da, db = Digest(path_a), Digest(path_b)
    ia, ib = iter(da), iter(db)
    shown, compared, tolerated = 0, 0, 0
    ignore = re.compile(ignore) if ignore else None
    while True:
        ha, hb = next(ia, None), next(ib, None)
        if ha is None or hb is None:
            if ha is not hb:
                longer, h = (name_a, ha) if ha else (name_b, hb)
                out(f'digest: {longer} has more records; the other ends before its {where(h)}')
                return 1
            break
        compared += 1
        if (ha['update'], ha['tick']) != (hb['update'], hb['tick']) and shown == 0:
            out(f'digest: the records no longer line up at record {ha["record"]}: {name_a} world update {ha["update"]} '
                f'tick {ha["tick"]}, {name_b} world update {hb["update"]} tick {hb["tick"]}')
        if ha['hash'] == hb['hash'] and ha['zone'] == hb['zone']:
            continue
        found = differences(da.state, db.state, ulps, ignore)
        if not found:
            tolerated += 1
            continue
        if ha['zone'] != hb['zone']:
            out(f'digest: {where(ha)}: zone {ha["zone"]} in {name_a}, {hb["zone"]} in {name_b}')
        out(f'digest: {"first " if shown == 0 else ""}difference at {where(ha)}' +
            (f' ({name_b}: frame {hb["frame"]})' if hb['frame'] != ha['frame'] else '') + f': {len(found)} fields')
        for key, description, field, a, b in found[:fields]:
            out(f'  {key} ({description}) {field}: {a} in {name_a}, {b} in {name_b}')
        if len(found) > fields:
            out(f'  ... {len(found) - fields} more')
        shown += 1
        if shown >= records:
            return 1
    if shown:
        return 1
    note = f'; {tolerated} records differ only in fields left out' + (f' or within {ulps} ULPs' if ulps else '') if tolerated else ''
    out(f'digest: {compared} records, the same{note}')
    return 0


def run_folder(name, install, run):
    return run or os.path.join(run_all.BUILD, 'runs', f'{name}-{install}')


def report_result(folder):
    path = os.path.join(folder, 'harness_report.txt')
    if not os.path.exists(path):
        return None
    text = open(path, encoding='cp1251', errors='replace').read().splitlines()
    return next((l[8:] for l in text if l.startswith('RESULT: ')), None)


def console(folder):
    """The console lines of a run folder's report (as run_all.console_of reads them)."""
    path = os.path.join(folder, 'harness_report.txt')
    lines = open(path, encoding='cp1251', errors='replace').read().splitlines() if os.path.exists(path) else []
    lines = lines[lines.index('console output:') + 1:] if 'console output:' in lines else []
    return [l for l in lines if l.startswith('  ')]


def commit():
    try:
        return subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
    except OSError:
        return ''


def record(args):
    code = 0
    for name in args.scenarios:
        folder = run_folder(name, args.install, args.run)
        digest = os.path.join(folder, 'digest.txt')
        problem = None
        if report_result(folder) != 'PASS':
            problem = f'{folder} did not pass ({report_result(folder) or "no report"})'
        elif not os.path.exists(digest):
            problem = f'{folder} has no digest.txt (run it with EI_DIGEST=<N>)'
        elif 'hooks off' not in Digest(digest).title:
            problem = f'{folder} ran with our hooks; a golden is the original code (--no-hooks)'
        if problem:
            print(f'{name}: not recorded: {problem}')
            code = 2
            continue
        target = os.path.join(GOLDEN, name, args.install)
        os.makedirs(target, exist_ok=True)
        shutil.copyfile(digest, os.path.join(target, 'digest.txt'))
        with open(os.path.join(target, 'console.txt'), 'w', encoding='cp1251', errors='replace') as f:
            f.writelines(line + '\n' for line in console(folder))
        meta = {'scenario': name, 'install': args.install, 'recorded': datetime.date.today().isoformat(),
                'commit': commit(), 'digest_period': Digest(digest).period(), 'reason': args.reason}
        with open(os.path.join(target, 'golden.json'), 'w', encoding='utf-8') as f:
            json.dump(meta, f, indent=2)
            f.write('\n')
        print(f'{name}: recorded in {os.path.relpath(target, ROOT)}')
    return code


def compare(args):
    code = 0
    for name in args.scenarios:
        golden = os.path.join(GOLDEN, name, args.install)
        folder = run_folder(name, args.install, args.run)
        if not os.path.exists(os.path.join(golden, 'golden.json')):
            print(f'{name}: no golden in {os.path.relpath(golden, ROOT)}')
            code = 2
            continue
        if report_result(folder) is None:
            print(f'{name}: no run in {folder}')
            code = 2
            continue
        lines, differs = [], False
        expected = open(os.path.join(golden, 'console.txt'), encoding='cp1251', errors='replace').read().splitlines()
        actual = console(folder)
        if expected != actual:
            differs = True
            first = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), min(len(expected), len(actual)))
            lines.append(f'console: differs from line {first + 1}: golden '
                         f'{expected[first].strip() if first < len(expected) else "(ends)"!r}, run '
                         f'{actual[first].strip() if first < len(actual) else "(ends)"!r}')
        golden_digest, run_digest = os.path.join(golden, 'digest.txt'), os.path.join(folder, 'digest.txt')
        if not os.path.exists(run_digest):
            lines.append('digest: the run has none (run it with the golden\'s EI_DIGEST)')
            differs = True
        else:
            period_golden, period_run = Digest(golden_digest).period(), Digest(run_digest).period()
            if period_golden != period_run:
                lines.append(f'digest: the golden takes a record every {period_golden} world updates, the run every {period_run}')
                differs = True
            elif diff(golden_digest, run_digest, 'the golden', 'the run', args.ulps, args.records, args.fields, args.ignore,
                      out=lines.append):
                differs = True
        print(f'{name} {args.install}: {"DIFFERS" if differs else "SAME"}')
        for line in lines:
            print('  ' + line)
        if differs:
            code = max(code, 1)
    return code


def state(args):
    path = digest_path(args.digest)
    if not path:
        print(f'no digest at {args.digest}')
        return 2
    d = Digest(path)
    header = None
    for h in d:
        header = h
        if args.record is not None and h['record'] == args.record:
            break
    if header is None:
        print('no records')
        return 2
    print(header['line'])
    pattern = re.compile(args.key) if args.key else None
    for key in sorted(d.state, key=key_order):
        if pattern and not pattern.search(key):
            continue
        description, fields = d.state[key]
        print(f'{key} {description}')
        for field, value in fields.items():
            print(f'  {field} {value}')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)

    def comparing(p):
        p.add_argument('--ulps', type=int, default=0)
        p.add_argument('--records', type=int, default=1)
        p.add_argument('--fields', type=int, default=12)
        p.add_argument('--ignore')
        p.add_argument('--all-fields', action='store_true')

    p = sub.add_parser('diff')
    p.add_argument('a')
    p.add_argument('b')
    comparing(p)
    p = sub.add_parser('state')
    p.add_argument('digest')
    p.add_argument('--record', type=int)
    p.add_argument('--key')
    for command in ('record', 'compare'):
        p = sub.add_parser(command)
        p.add_argument('scenarios', nargs='+')
        p.add_argument('--install', default='vanilla', choices=['vanilla', 'rebirth'])
        p.add_argument('--run')
        if command == 'record':
            p.add_argument('--reason', required=True)
        else:
            comparing(p)
    args = ap.parse_args()
    if args.command in ('diff', 'compare') and not args.all_fields and ADDRESS_DEPENDENT:
        args.ignore = f'{args.ignore}|{ADDRESS_DEPENDENT}' if args.ignore else ADDRESS_DEPENDENT

    if args.command == 'diff':
        a, b = digest_path(args.a), digest_path(args.b)
        if not a or not b:
            print(f'no digest at {args.a if not a else args.b}')
            return 2
        label = lambda spec: os.path.basename(os.path.normpath(spec))
        return diff(a, b, label(args.a), label(args.b), args.ulps, args.records, args.fields, args.ignore)
    return {'state': state, 'record': record, 'compare': compare}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
