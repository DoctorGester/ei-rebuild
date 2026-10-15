r"""Game-state digests and golden runs (docs/notes/standalone-verification.md, tickets SV2 and SV3).

A harness run with "digest <N>" in its scenario, or EI_DIGEST=<N> in the environment, writes digest.txt
in its run folder: a record of the world server's state every N world updates (harness_digest.h), and a
"dump <name>" step writes dump_<name>.txt, the whole state at that point. This tool compares two runs'
records and names the first field that differs, and keeps golden runs.

A golden (tests\golden\<scenario>\<install>\) is a run of the original code (--no-hooks) that repeated:
  golden.json   when, from which commit, the digest period, the fields left out, the reason
  console.txt   what the game printed
  digest.txt    the digest in compact form: the world, the generators and the players field by field, every
                object as hashes of its fields (units: one hash each for place, vitals, AI and the rest), so
                a golden stays small; a difference names the record, the object and the group
  dump_*.txt    the scenario's checkpoints ("dump" steps, e.g. after loading a save), compact too
  saves.txt     every file the run left in harness_saves (the saves written at checkpoints): size and hash
To see the fields behind a differing hash, run the original again (its runs repeat) and `diff` the two
full digests.

Digest 2 (SV2a) adds entity groups (KEY_GROUPS: timed, missile, aievents, script, client, cunit) and names them in
its title line. A golden keeps the groups its digest had: a run is compacted down to them before the comparison, and
`diff` of two digests of different versions compares the groups both have. So adding a group needs no re-recording.

usage:
  python tools/golden.py diff A B [--ulps N] [--records K] [--fields K] [--ignore REGEX] [--all-fields]
      A, B: run folders (a name under build\verify\runs, or a path) or digest files (also compact ones, but
      not one of each). Walks both digests record by record; at the first record whose state differs it
      prints the record (world update, frame, tick) and the fields that differ, in record order: the world,
      the generators, the players, the objects by id. --ulps N lets floats differ by up to N units in the
      last place (the rewrite's float maths, rewrite guide rule 7); --records K shows K differing records
      (default 1), --fields K up to K fields each (default 12); --ignore drops fields whose "<key> <field>"
      matches the expression. Fields that follow heap addresses (ADDRESS_DEPENDENT; none since
      SV1b) are left out unless --all-fields.
  python tools/golden.py state A [--record N] [--key REGEX]
      The whole state at record N (default: the last) of one digest, or the entities whose key matches.
  python tools/golden.py snapshot DEST [SCENARIO...] [--install vanilla|rebirth]
      Copies the run folders build\verify\runs\<scenario>-<install> (default: every one with a digest) to
      DEST\<scenario>-<install>: the report, the digest, the dumps and harness_saves. For a second run of the
      same scenarios, to check that they repeat (record --runs).
  python tools/golden.py record [SCENARIO...] --reason TEXT [--install vanilla|rebirth] [--runs DIR...]
      Keeps runs of the original code as goldens. DIR (default build\verify\runs) holds run folders
      <scenario>-<install>; the first DIR's must have passed with --no-hooks and a digest, and the run
      folders of every other DIR (a second run of the original, runs with our hooks) must agree with it:
      the console, every digest record and dump (but the known differences, KNOWN). A scenario that does
      not is not recorded; its first difference is printed. The saves are kept (saves.txt) only when every
      run wrote the same bytes. No SCENARIO: every run folder of the install in the first DIR.
      Run the games through the scheduler, e.g.:
        set EI_DIGEST=20 & python tools\work.py test <ID> --no-hooks --fresh   (--fresh: not the reference cache,
                                                                                whose runs may hold an older digest)
        python tools\golden.py snapshot <scratch>\orig1   (then the same again, and with hooks)
  python tools/golden.py compare SCENARIO... [--install vanilla|rebirth] [--run FOLDER] [--strict] [--ulps N] ...
      A run (by default build\verify\runs\<scenario>-<install>, made with the golden's EI_DIGEST) against
      the scenario's golden: the console, the digest, the dumps and the saves. The known differences (KNOWN, each
      with its ticket) are left out unless --strict. `run_all.py --golden` (also
      through `work.py test`) runs the games with the goldens' digest period and does this for every run.
exit code: 0 the same, 1 a difference, 2 something missing
"""
import argparse, datetime, glob, hashlib, json, os, re, shutil, struct, subprocess, sys, zlib

import run_all

ROOT = run_all.ROOT
GOLDEN = os.path.join(ROOT, 'tests', 'golden')
# Fields that follow heap addresses, which would differ between runs and between the original code alone and
# ours (the verify wrappers allocate too); left out unless --all-fields. None now: the one there was, a unit's
# update-map countdown (0x80 + bits 5..8 of its address in the game), harness runs on the virtual clock take
# from the object's id on both sides (tests/verify/harness_heap.h, SV1b). Runs with `--clock virtual,lfheap`
# keep the game's: pass --ignore ' updateCountdown$' for those.
ADDRESS_DEPENDENT = None
# Differences between our code and the original that are known, each with the ticket that fixes it: "<key> <field>"
# patterns. A compact digest keeps these fields in clear next to the hashes, and the comparison with a golden
# leaves them out (unless --strict), so the golden needs no re-recording when the ticket lands; drop the entry then.
KNOWN = {
    r'^obj2 hp$': "SV3h: the main menu unit unmoco2's hit points are the NaN its prototype-less stats' stack "
                  "leftovers give; on SV2b's heap they differ by scenario and install in the original "
                  "(0xFFFFFDxx vanilla, 0xFFFFFFxx Rebirth), ours keeps SV3a's 0xFFC00000",
}   # SV2b (zone1's script variable i between our runs under Rebirth) fixed 2026-10-15
HEADER = re.compile(r'@ (\d+) update (\d+) frame (\d+) tick (-?\d+) zone (\S+) hash ([0-9A-F]{8})')
COMPACT = '# compact'
SAVES = 'harness_saves'
# A unit's fields by group in a compact digest (the first prefix that matches; the rest are "unit").
GROUPS = (('place', ('pos.', 'rot.', 'scale.', 'cell.', 'motion.', 'heightOffset')),
          ('vitals', ('hp', 'maxHp', 'mana', 'maxMana', 'regenTicks', 'effect', 'dead')),
          ('ai', ('ai.',)))
# The entity groups by key prefix, in record order (harness_digest.cpp's kGroupNames). Digest 1 has the first four;
# digest 2 (SV2a) names the others it has in its title ("groups timed,missile,..."). A golden that lacks a group is
# compared without it, so new groups need no re-recording of older goldens.
KEY_GROUPS = ('world', 'random', 'player', 'obj', 'timed', 'missile', 'aievents', 'script', 'client', 'cunit')
BASE_GROUPS = frozenset(KEY_GROUPS[:4])
# Entities kept field by field in a compact digest (the rest become hashes).
CLEAR_GROUPS = ('world', 'random', 'player', 'aievents', 'client')


def digest_path(spec):
    """A digest file from a run folder's name or path, or a file."""
    for path in (spec, os.path.join(run_all.BUILD, 'runs', spec)):
        if os.path.isfile(path):
            return path
        if os.path.isdir(path) and os.path.exists(os.path.join(path, 'digest.txt')):
            return os.path.join(path, 'digest.txt')
    return None


def key_group(key):
    """The group of an entity key (KEY_GROUPS), or None."""
    for prefix in KEY_GROUPS:
        if key.startswith(prefix) and (len(key) == len(prefix) or key[len(prefix)] in '-0123456789'):
            return prefix
    return None


def key_order(key):
    """Record order of an entity key: world, random, players by id, objects by id, then digest 2's groups."""
    group = key_group(key)
    if group is None:
        return len(KEY_GROUPS), 0
    rest = key[len(group):]
    return KEY_GROUPS.index(group), int(rest) if rest.lstrip('-').isdigit() else 0


class Digest:
    """Reads a digest file record by record, keeping the whole state: {key: [description, {field: value}]}.
    touched: the keys the records read so far changed (the reader empties it when it has looked)."""

    def __init__(self, path):
        self.path = path
        with open(path, encoding='cp1251', errors='replace') as f:
            first = f.readline().rstrip('\n')
            second = f.readline()
        self.title = first if first.startswith('#') else ''
        self.compact = second.startswith(COMPACT)
        self.state = {}
        self.touched = set()
        self.pending = None   # the next record's header line

    def __iter__(self):
        with open(self.path, encoding='cp1251', errors='replace') as f:
            for line in f:
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
            self.touched.add(key)
        elif line.startswith('- '):
            self.state.pop(line[2:], None)
            self.touched.add(line[2:])
        elif line:
            key, field, value = (line.split(' ', 2) + ['', ''])[:3]
            fields = self.state.setdefault(key, ['?', {}])[1]
            self.touched.add(key)
            if value == '-':
                fields.pop(field, None)
            else:
                fields[field] = value

    def period(self):
        m = re.search(r'every (\d+) world updates', self.title)
        return int(m.group(1)) if m else None

    def groups(self):
        """The entity groups the digest has: digest 1's (BASE_GROUPS) and those its title names."""
        m = re.search(r'; groups ([\w,]+)', self.title)
        return BASE_GROUPS | frozenset(m.group(1).split(',')) if m else BASE_GROUPS


# ---- the compact form

def crc(text):
    return f'{zlib.crc32(text.encode("cp1251", "replace")):08X}'


def group_of(field):
    for name, prefixes in GROUPS:
        if field.startswith(prefixes):
            return name
    return 'unit'


def known_pattern():
    """The "<key> <field>" expression of the known differences (KNOWN), or None."""
    return '|'.join(f'(?:{p})' for p in KNOWN) or None


def compact_entity(key, entry, ignore, clear=None):
    """An entity as the compact digest keeps it: [description, fields]. Fields that ignore matches are left
    out; an object's fields become hashes, '#' (one for the object) or '#<group>' for a unit, but for those
    that clear matches, which are kept as they are."""
    description, fields = entry
    kept = {f: v for f, v in fields.items() if not (ignore and ignore.search(f'{key} {f}'))}
    group = key_group(key)
    if group in CLEAR_GROUPS:
        return [description, kept]
    unit = group == 'cunit' or (group == 'obj' and any(f.startswith('ai.') or f == 'player' for f in kept))
    groups, plain = {}, {}
    for f in sorted(kept):
        if clear and clear.search(f'{key} {f}'):
            plain[f] = kept[f]
        elif group == 'script':
            # The script: one hash per kind (run, inst, global, fx, part, light, ...).
            groups.setdefault('#' + re.match(r'[a-zA-Z]*', f).group(0), []).append(f'{f} {kept[f]}')
        else:
            groups.setdefault('#' + group_of(f) if unit else '#', []).append(f'{f} {kept[f]}')
    plain.update((g, crc('\n'.join(lines))) for g, lines in sorted(groups.items()))
    return [description, plain]


def titled_groups(title, groups):
    """A digest's title line naming only `groups` (the ones beyond digest 1's)."""
    extra = [g for g in KEY_GROUPS if g in groups and g not in BASE_GROUPS]
    title = re.sub(r'; groups [\w,]+', '', title)
    if not extra:
        return title
    if title.startswith('# ei state digest') and ';' in title:
        head, rest = title.split(';', 1)
        return f'{head}; groups {",".join(extra)};{rest}'
    return f'{title}; groups {",".join(extra)}'


def compact(src, dst, ignore=ADDRESS_DEPENDENT, clear=None, groups=None):
    """Writes the compact form of the digest or dump src to dst (the same record format, so diff and state
    read it). ignore: the fields left out (default the address-dependent ones); hashed fields cannot be
    left out later. clear: the fields kept in clear (default the known differences, KNOWN). groups: the entity
    groups kept (default all; a golden's, to compare a newer digest with it)."""
    clear = known_pattern() if clear is None else clear
    clear = re.compile(clear) if clear else None
    d = Digest(src)
    if d.compact:
        shutil.copyfile(src, dst)
        return
    pattern = re.compile(ignore) if ignore else None
    keep = d.groups() if groups is None else d.groups() & groups
    state, hashes = {}, {}
    with open(dst, 'w', encoding='cp1251', errors='replace', newline='\n') as out:
        out.write(titled_groups(d.title or '# ei state digest', keep) + '\n')
        out.write(f'{COMPACT} (tools/golden.py): objects as hashes of their fields' +
                  (f'; left out: {ignore}' if ignore else '') + '\n')
        for h in d:
            lines = []
            for key in sorted(d.touched, key=key_order):
                if key_group(key) not in keep:
                    continue
                new = compact_entity(key, d.state[key], pattern, clear) if key in d.state else None
                old = state.get(key)
                if new is None:
                    if old is not None:
                        lines.append(f'- {key}')
                        del state[key], hashes[key]
                    continue
                old_fields = old[1] if old is not None and old[0] == new[0] else None
                if old_fields is None:
                    lines.append(f'+ {key} {new[0]}')
                    old_fields = {}
                lines += [f'{key} {f} {v}' for f, v in new[1].items() if old_fields.get(f) != v]
                lines += [f'{key} {f} -' for f in old_fields if f not in new[1]]
                state[key] = new
                hashes[key] = crc(new[0] + '\n' + '\n'.join(f'{f} {v}' for f, v in sorted(new[1].items())))
            d.touched.clear()
            total = crc(''.join(k + hashes[k] for k in sorted(hashes, key=key_order)))
            out.write(f'@ {h["record"]} update {h["update"]} frame {h["frame"]} tick {h["tick"]} zone {h["zone"]} '
                      f'hash {total} n {len(state)}\n')
            out.writelines(line + '\n' for line in lines)


# ---- comparing digests

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


def differences(sa, sb, ulps, ignore, keys=None):
    """The fields that differ between two states, in record order: (key, description, field, a, b).
    keys: only these entities (default all)."""
    out = []
    for key in sorted(keys if keys is not None else set(sa) | set(sb), key=key_order):
        ea, eb = sa.get(key), sb.get(key)
        if ea is None and eb is None:
            continue
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
    if da.compact != db.compact:
        out(f'digest: {name_a if da.compact else name_b} is compact and the other is not (golden.py compact the other)')
        return 1
    ia, ib = iter(da), iter(db)
    shown, compared, tolerated = 0, 0, 0
    ignore = re.compile(ignore) if ignore else None
    recheck = set()   # entities that differed at a record shown: looked at again even if untouched
    # Digests of different versions: only the groups both have are compared (their record hashes cover the rest too,
    # so every record is looked at).
    common = da.groups() & db.groups()
    partial = common != da.groups() or common != db.groups()
    if partial:
        out(f'digest: comparing the groups both have; left out: '
            f'{", ".join(sorted((da.groups() | db.groups()) - common))}')
    while True:
        ha, hb = next(ia, None), next(ib, None)
        if ha is None or hb is None:
            if ha is not hb:
                longer, h = (name_a, ha) if ha else (name_b, hb)
                out(f'digest: {longer} has more records; the other ends before its {where(h)}')
                return 1
            break
        compared += 1
        keys = da.touched | db.touched | recheck
        da.touched.clear()
        db.touched.clear()
        if partial:
            keys = {k for k in keys if key_group(k) in common}
        if (ha['update'], ha['tick']) != (hb['update'], hb['tick']) and shown == 0:
            out(f'digest: the records no longer line up at record {ha["record"]}: {name_a} world update {ha["update"]} '
                f'tick {ha["tick"]}, {name_b} world update {hb["update"]} tick {hb["tick"]}')
        if ha['hash'] == hb['hash'] and ha['zone'] == hb['zone']:
            recheck = set()
            continue
        found = differences(da.state, db.state, ulps, ignore, keys)
        if not found:
            tolerated += 0 if partial and ha['zone'] == hb['zone'] else 1
            recheck = set()
            continue
        if ha['zone'] != hb['zone']:
            out(f'digest: {where(ha)}: zone {ha["zone"]} in {name_a}, {hb["zone"]} in {name_b}')
        out(f'digest: {"first " if shown == 0 else ""}difference at {where(ha)}' +
            (f' ({name_b}: frame {hb["frame"]})' if hb['frame'] != ha['frame'] else '') + f': {len(found)} fields')
        for key, description, field, a, b in found[:fields]:
            out(f'  {key} ({description}) {field}: {a} in {name_a}, {b} in {name_b}')
        if len(found) > fields:
            out(f'  ... {len(found) - fields} more')
        recheck = {f[0] for f in found}
        shown += 1
        if shown >= records:
            return 1
    if shown:
        return 1
    note = f'; {tolerated} records differ only in fields left out' + (f' or within {ulps} ULPs' if ulps else '') if tolerated else ''
    out(f'digest: {compared} records, the same{note}')
    return 0


# ---- run folders

def run_folder(name, install, run=None, build=None):
    return run or os.path.join(build or run_all.BUILD, 'runs', f'{name}-{install}')


def read_lines(path, encoding='cp1251'):
    with open(path, encoding=encoding, errors='replace') as f:
        return f.read().splitlines()


def report_result(folder):
    path = os.path.join(folder, 'harness_report.txt')
    if not os.path.exists(path):
        return None
    text = read_lines(path)
    return next((l[8:] for l in text if l.startswith('RESULT: ')), None)


def console(folder):
    """The console lines of a run folder's report (as run_all.console_of reads them)."""
    path = os.path.join(folder, 'harness_report.txt')
    lines = read_lines(path) if os.path.exists(path) else []
    lines = lines[lines.index('console output:') + 1:] if 'console output:' in lines else []
    return [l for l in lines if l.startswith('  ')]


def dumps(folder):
    """{name: path} of a run folder's (or a golden's) dump_<name>.txt files."""
    return {os.path.basename(p)[5:-4]: p for p in sorted(glob.glob(os.path.join(folder, 'dump_*.txt')))}


def saves(folder):
    """The saved games in a run folder's harness_saves: ['<path> <size> <sha1>'] of every <save>\\*.sav, paths
    relative to it. Left out: shot.sav (the save's thumbnail, a frame: the renderer's), and what else the game
    keeps there (the portrait texture cache, keyboard.ini)."""
    base = os.path.join(folder, SAVES)
    out = []
    for top, _, files in os.walk(base):
        for f in files:
            if not f.lower().endswith('.sav') or f.lower() == 'shot.sav':
                continue
            path = os.path.join(top, f)
            with open(path, 'rb') as f:
                data = f.read()
            out.append(f'{os.path.relpath(path, base).replace(os.sep, "/")} {len(data)} {hashlib.sha1(data).hexdigest()[:16]}')
    return sorted(out)


def golden_saves(folder):
    path = os.path.join(folder, 'saves.txt')
    return read_lines(path, 'utf-8') if os.path.exists(path) else None


def first_line_difference(what, expected, actual, name_a, name_b):
    first = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), min(len(expected), len(actual)))
    return (f'{what}: differs from line {first + 1}: {name_a} '
            f'{expected[first].strip() if first < len(expected) else "(ends)"!r}, {name_b} '
            f'{actual[first].strip() if first < len(actual) else "(ends)"!r}')


def compare_folders(a, b, name_a, name_b, ulps=0, records=1, fields=12, ignore=ADDRESS_DEPENDENT, golden=False,
                    with_saves=True, known=None, clear=None):
    """Two runs' console, digest, dumps and saves (a may be a golden: golden=True; b's digest and dumps are
    then compacted into b first, leaving out ignore and keeping clear in clear, as the golden was; with_saves
    False: not the saves). known: fields left out of the comparison too (the known differences). The lines that
    describe the differences; [] when the same."""
    lines = []
    left_out = known if golden else '|'.join(p for p in (ignore, known) if p) or None
    if golden:
        expected = read_lines(os.path.join(a, 'console.txt'))
    else:
        expected = console(a)
    actual = console(b)
    if expected != actual:
        lines.append(first_line_difference('console', expected, actual, name_a, name_b))
    digest_a, digest_b = os.path.join(a, 'digest.txt'), os.path.join(b, 'digest.txt')
    if not os.path.exists(digest_b):
        lines.append(f'digest: {name_b} has none (run it with EI_DIGEST={Digest(digest_a).period()})')
    else:
        period_a, period_b = Digest(digest_a).period(), Digest(digest_b).period()
        if period_a != period_b:
            lines.append(f'digest: {name_a} takes a record every {period_a} world updates, {name_b} every {period_b}')
        else:
            if golden:
                # Only the groups the golden has (an older golden lacks digest 2's).
                compact(digest_b, os.path.join(b, 'golden_digest.txt'), ignore, clear, Digest(digest_a).groups())
                digest_b = os.path.join(b, 'golden_digest.txt')
            found = []
            if diff(digest_a, digest_b, name_a, name_b, ulps, records, fields, left_out, out=found.append):
                lines += found
    dumps_a, dumps_b = dumps(a), dumps(b)
    if sorted(dumps_a) != sorted(dumps_b):
        lines.append(f'dumps: {name_a} has {sorted(dumps_a) or "none"}, {name_b} {sorted(dumps_b) or "none"}')
    for name in sorted(set(dumps_a) & set(dumps_b)):
        path_b = dumps_b[name]
        if golden:
            path_b = os.path.join(b, f'golden_dump_{name}.txt')
            compact(dumps_b[name], path_b, ignore, clear, Digest(dumps_a[name]).groups())
        found = []
        if diff(dumps_a[name], path_b, name_a, name_b, ulps, 1, fields, left_out, out=found.append):
            lines += [f'dump {name}: ' + found[0]] + found[1:]
    saves_a = golden_saves(a) if golden else saves(a)
    if saves_a is not None and with_saves:
        saves_b = saves(b)
        if saves_a != saves_b:
            lines.append(first_line_difference('saves', saves_a, saves_b, name_a, name_b))
    return lines


def shown_path(path):
    """path relative to the checkout when it is inside it."""
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path


def commit():
    try:
        return subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip()
    except OSError:
        return ''


# ---- commands

def snapshot(args):
    source = os.path.join(run_all.BUILD, 'runs')
    names = [f'{n}-{args.install}' for n in args.scenarios] if args.scenarios else \
        sorted(d for d in os.listdir(source) if os.path.exists(os.path.join(source, d, 'digest.txt')))
    for name in names:
        src, dst = os.path.join(source, name), os.path.join(args.dest, name)
        if not os.path.isdir(src):
            print(f'{name}: no run folder')
            continue
        shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(dst)
        for f in ['harness_report.txt', 'digest.txt'] + [os.path.basename(p) for p in dumps(src).values()]:
            if os.path.exists(os.path.join(src, f)):
                shutil.copyfile(os.path.join(src, f), os.path.join(dst, f))
        if os.path.isdir(os.path.join(src, SAVES)):
            shutil.copytree(os.path.join(src, SAVES), os.path.join(dst, SAVES))
    print(f'{len(names)} run folders copied to {args.dest}')
    return 0


def record(args):
    dirs = args.runs or [os.path.join(run_all.BUILD, 'runs')]
    names = args.scenarios or sorted(d[:-len(args.install) - 1] for d in os.listdir(dirs[0])
                                     if d.endswith('-' + args.install) and os.path.exists(os.path.join(dirs[0], d, 'digest.txt')))
    code = 0
    recorded = 0
    for name in names:
        folders = [os.path.join(d, f'{name}-{args.install}') for d in dirs]
        digest = os.path.join(folders[0], 'digest.txt')
        problem = []
        if report_result(folders[0]) != 'PASS':
            problem = [f'{folders[0]} did not pass ({report_result(folders[0]) or "no report"})']
        elif not os.path.exists(digest):
            problem = [f'{folders[0]} has no digest.txt (run it with EI_DIGEST=<N>)']
        elif 'hooks off' not in Digest(digest).title:
            problem = [f'{folders[0]} ran with our hooks; a golden is the original code (--no-hooks)']
        for other in folders[1:]:
            if problem:
                break
            if report_result(other) != 'PASS':
                problem = [f'{other} did not pass ({report_result(other) or "no report"})']
            else:
                label_a, label_b = (os.path.basename(os.path.dirname(f)) for f in (folders[0], other))
                found = compare_folders(folders[0], other, label_a, label_b, ulps=0, records=1, fields=args.fields,
                                        with_saves=False, known=known_pattern())
                if found:
                    problem = [f'{other} differs from {folders[0]}:'] + found
        if problem:
            print(f'{name} {args.install}: not recorded: ' + problem[0])
            for line in problem[1:]:
                print('  ' + line)
            code = 2
            continue
        target = os.path.join(GOLDEN, name, args.install)
        shutil.rmtree(target, ignore_errors=True)
        os.makedirs(target)
        compact(digest, os.path.join(target, 'digest.txt'))
        for dump, path in dumps(folders[0]).items():
            compact(path, os.path.join(target, f'dump_{dump}.txt'))
        with open(os.path.join(target, 'console.txt'), 'w', encoding='cp1251', errors='replace', newline='\n') as f:
            f.writelines(line + '\n' for line in console(folders[0]))
        meta = {'scenario': name, 'install': args.install, 'recorded': datetime.date.today().isoformat(),
                'commit': commit(), 'digest_period': Digest(digest).period(), 'left_out': ADDRESS_DEPENDENT,
                'clear': known_pattern(), 'repeated_in': len(folders), 'reason': args.reason}
        # The saves' bytes only when every run wrote the same: a save also holds what the digest leaves out
        # (values that follow heap addresses). The dumps after loading them compare them field by field.
        files = saves(folders[0])
        if files:
            differ = next(((os.path.basename(os.path.dirname(f)), saves(f)) for f in folders[1:] if saves(f) != files), None)
            if differ is None:
                with open(os.path.join(target, 'saves.txt'), 'w', encoding='utf-8', newline='\n') as f:
                    f.writelines(line + '\n' for line in files)
            else:
                meta['saves'] = 'not kept: ' + first_line_difference('saves', files, differ[1], 'the first run', differ[0])
        with open(os.path.join(target, 'golden.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(meta, f, indent=2)
            f.write('\n')
        recorded += 1
        print(f'{name} {args.install}: recorded in {shown_path(target)}')
    print(f'{recorded} of {len(names)} recorded')
    return code


def golden_folder(name, install, root=ROOT):
    return os.path.join(root, 'tests', 'golden', name, install)


def compare_one(name, install, folder, ulps=0, records=1, fields=12, root=ROOT, strict=False):
    """A run folder against the scenario's golden: ('SAME' | 'DIFFERS' | 'NO GOLDEN' | 'NO RUN', lines).
    The known differences (KNOWN) are left out unless strict."""
    golden = golden_folder(name, install, root)
    if not os.path.exists(os.path.join(golden, 'golden.json')):
        return 'NO GOLDEN', []
    if report_result(folder) is None:
        return 'NO RUN', [f'no run in {folder}']
    with open(os.path.join(golden, 'golden.json'), encoding='utf-8') as f:
        meta = json.load(f)
    lines = compare_folders(golden, folder, 'the golden', 'the run', ulps, records, fields,
                            meta.get('left_out', ADDRESS_DEPENDENT), golden=True,
                            known=None if strict else known_pattern(), clear=meta.get('clear') or '')
    return ('DIFFERS' if lines else 'SAME'), lines


def compare(args):
    code = 0
    for name in args.scenarios:
        result, lines = compare_one(name, args.install, run_folder(name, args.install, args.run), args.ulps, args.records,
                                    args.fields, strict=args.strict)
        print(f'{name} {args.install}: {result}')
        for line in lines:
            print('  ' + line)
        code = max(code, {'SAME': 0, 'DIFFERS': 1}.get(result, 2))
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
    p = sub.add_parser('snapshot')
    p.add_argument('dest')
    p.add_argument('scenarios', nargs='*')
    p.add_argument('--install', default='vanilla', choices=['vanilla', 'rebirth'])
    p = sub.add_parser('record')
    p.add_argument('scenarios', nargs='*')
    p.add_argument('--install', default='vanilla', choices=['vanilla', 'rebirth'])
    p.add_argument('--runs', nargs='+')
    p.add_argument('--reason', required=True)
    p.add_argument('--fields', type=int, default=12)
    p = sub.add_parser('compare')
    p.add_argument('scenarios', nargs='+')
    p.add_argument('--install', default='vanilla', choices=['vanilla', 'rebirth'])
    p.add_argument('--run')
    p.add_argument('--strict', action='store_true')
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
    return {'state': state, 'snapshot': snapshot, 'record': record, 'compare': compare}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
