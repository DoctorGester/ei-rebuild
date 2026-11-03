r"""Readable summaries of how two digests (and two decoded saves) differ (ticket H45; used by tools/golden.py).

A difference between a run and its golden explains itself: the record where the two first differ comes first, then
what the change did (outcomes: zone, deaths, spawns, money, quest variables, party and items, game result, scripts),
then the digest's sections (world, random, players, units, objects, effects, missiles, ai events, script, client,
water) with each kind of field's size of change: the largest numeric delta with where it is, the number of entities
and records it touches. Grouped and capped, so it fits in a report.

What the numbers need: a compact digest (a golden) holds a unit's place and AI as hashes; only the fields in
OUTCOME_CLEAR (units' position, hit points, mana, death) are kept in clear next to them, as goldens recorded since
H45 do, and the players', world's and client's fields always are. Where a side holds only a hash the line says
"hash only"; `golden.py explain RUN_A RUN_B` (two runs' full digests) and `golden.py record --before DIR` give every
number.
"""
import math
import re

import golden
import savefile

# The fields a golden keeps in clear next to its hashes (golden.py record): a unit's place, hit points and death, the
# few numbers that tell whether a change moved the game. Anchored: compact_entity matches "<key> <field>".
OUTCOME_CLEAR = r'^obj\d+ (?:pos\.[xyz]|hp|mana|dead)$'
MAX_LINES = 30          # lines of the detail under the headline (the first-difference block not counted)
FIRST_FIELDS = 3        # fields shown of the first differing record
KINDS_PER_SECTION = 4   # kinds of field listed per section
SECTION_ORDER = ('world', 'random', 'players', 'units', 'objects', 'effects', 'missiles', 'ai events', 'script',
                 'client', 'client units', 'water')
NOUN = {'world': 'world', 'random': 'generators', 'players': 'players', 'units': 'units', 'objects': 'objects',
        'effects': 'effects', 'missiles': 'missiles', 'ai events': 'sides', 'script': 'scripts', 'client': 'client',
        'client units': 'client units', 'water': 'water'}
GROUP_SECTION = {'world': 'world', 'random': 'random', 'player': 'players', 'timed': 'effects', 'missile': 'missiles',
                 'aievents': 'ai events', 'script': 'script', 'client': 'client', 'cunit': 'client units',
                 'water': 'water'}
OUTCOME_ORDER = ('zone', 'deaths', 'spawns', 'money', 'quest variables', 'party and items', 'game result', 'script')
PRIORITY = ('dead', 'hp', 'mana', 'pos', 'rot', 'motion', 'state', 'ai')
IDS = re.compile(r'(?:^|\.)(?:target|seen|caster|unit|attachedTo|fromObject|toObject|commandTarget|nextTarget|prototype|class)$')
PARTY_FIELDS = ('items', 'partyItems', 'ids', 'units', 'objects', 'members', 'entrance')


def number(text):
    """(value, is_float) for a plain number as the digest writes it, else None (hashes, bit patterns, text)."""
    if text is None or text.startswith(('0x', 'nan', 'inf', '-inf', '"', '(')):
        return None
    try:
        value = float(text)
    except ValueError:
        return None
    if math.isnan(value) or math.isinf(value):
        return None
    return value, any(c in text for c in '.eE')


def delta(a, b):
    """(|b - a|, ULPs or None) when both are numbers, else None."""
    na, nb = number(a), number(b)
    if na is None or nb is None:
        return None
    ulps = None
    if na[1] and nb[1]:
        fa, fb = golden.float_bits(a), golden.float_bits(b)
        if fa is not None and fb is not None:
            ulps = abs(golden.ordered(fa) - golden.ordered(fb))
    return abs(nb[0] - na[0]), ulps


def short(x):
    return f'{x:.3g}'


def clip(text, n=40):
    text = str(text)
    return text if len(text) <= n else text[:n - 1] + '~'


def is_unit(desc, fields):
    return desc.split(' ', 1)[0] == '80' or any(f.startswith(('ai.', '#ai')) for f in fields)


def section_of(key, desc, fields):
    group = golden.key_group(key)
    if group == 'obj':
        return 'units' if is_unit(desc, fields) else 'objects'
    return GROUP_SECTION.get(group, 'other')


UNIT_KINDS = (('pos.', 'pos'), ('rot.', 'rot'), ('scale.', 'rot'), ('cell.', 'motion'), ('motion.', 'motion'),
              ('heightOffset', 'motion'), ('hp', 'hp'), ('maxHp', 'hp'), ('mana', 'mana'), ('maxMana', 'mana'),
              ('dead', 'dead'), ('regenTicks', 'vitals'), ('effect', 'vitals'), ('ai.', 'ai'))
HASH_KINDS = {'#place': ('pos', 'rot', 'motion'), '#vitals': ('hp', 'mana', 'dead', 'vitals'), '#ai': ('ai',),
              '#unit': ('state',), '#': ('hp', 'pos', 'state')}


def kind_of(section, field):
    """The kind of field a difference counts under (a word for a unit's groups; the field's name with its numbers
    left out for the rest)."""
    if section == 'units' or section == 'client units':
        for prefix, kind in UNIT_KINDS:
            if field.startswith(prefix):
                return kind
        return field if field.startswith('#') else 'state'
    parts = field.split('.')
    kind = re.sub(r'\d+', 'N', parts[0])
    if kind == 'instN' and len(parts) > 1:
        kind += '.' + parts[1]
    if section == 'players' and parts[0] == 'var':
        kind = 'var'
    return clip(kind, 28)


class Kind:
    """What differs in one kind of field of one section."""

    def __init__(self):
        self.first = None          # the first record
        self.entities = set()
        self.fields = 0
        self.records = set()
        self.biggest = None        # (size, ulps, key, field, record, a, b)
        self.example = None        # (key, field, record, a, b)
        self.hash_only = 0

    def add(self, record, key, field, a, b, size=None, ulps=None):
        self.first = record if self.first is None else min(self.first, record)
        self.entities.add(key)
        self.fields += 1
        self.records.add(record)
        if self.example is None:
            self.example = (key, field, record, a, b)
        if field.startswith('#'):
            self.hash_only += 1
            return
        if size is None and not IDS.search(field):
            d = delta(a, b)
            size, ulps = d if d else (None, None)
        if size is not None and (self.biggest is None or size > self.biggest[0]):
            self.biggest = (size, ulps, key, field, record, a, b)


class Explainer:
    def __init__(self, name_a, name_b):
        self.name_a, self.name_b = name_a, name_b
        self.kinds = {}            # (section, kind) -> Kind
        self.entities = {}         # section -> set of keys that differ
        self.outcomes = {}         # name -> [count, first record, text]
        self.descs = {}            # key -> description
        self.records = 0           # compared
        self.differing = 0
        self.first = None          # (header a, header b, found)
        self.moved = None          # (size, key, record): the largest position change of a unit
        self.records_with = set()
        self.unknown = False       # a unit's vitals differ where only a hash says so: deaths cannot be told
        self.seen = {}             # entity key -> its section
        self.var_names = set()     # the players' variables that differ

    def outcome(self, name, record, text):
        entry = self.outcomes.get(name)
        if entry is None:
            self.outcomes[name] = [1, record, text]
        else:
            entry[0] += 1

    def zone(self, ha, hb):
        self.outcome('zone', ha['record'], f'record {ha["record"]}: {ha["zone"]} in {self.name_a}, '
                                           f'{hb["zone"]} in {self.name_b}')

    def add(self, ha, hb, found, sa, sb):
        record = ha['record']
        self.differing += 1
        self.records_with.add(record)
        if self.first is None:
            self.first = (ha, hb, found)
        seen = set()
        by_key = {}
        for key, desc, field, a, b in found:
            by_key.setdefault(key, []).append((desc, field, a, b))
        for key, items in by_key.items():
            entry = sa.get(key) or sb.get(key)
            desc, fields = entry
            self.descs[key] = desc
            section = section_of(key, desc, fields)
            if items[0][1] == '(entity)':
                side = self.name_a if items[0][2] == 'present' else self.name_b
                kind = self.kind(section, 'only in ' + side)
                kind.add(record, key, '(entity)', items[0][2], items[0][3])
                self.entities.setdefault(section, set()).add(key)
                if section in ('units', 'objects'):
                    self.outcome('spawns', record, f'{key} ({desc}) only in {side}, from record {record}')
                continue
            names = {f for _, f, _, _ in items}
            kinds = {f: kind_of(section, f) for f in names}
            present = {k for f, k in kinds.items() if not f.startswith('#')}
            for _, field, a, b in items:
                kind = kinds[field]
                if field.startswith('#'):
                    # A hash covers fields that differ in clear too: counted once, by the clear ones.
                    if set(HASH_KINDS.get(field, ())) & present:
                        continue
                    if section in ('objects', 'script'):
                        kind = 'fields' if section == 'objects' else kind_of(section, field[1:])
                size = ulps = None
                if kind == 'pos' and section == 'units':
                    # One entry per unit and record: the distance between the two positions.
                    if (key, 'pos') in seen:
                        continue
                    seen.add((key, 'pos'))
                    size = self.distance(key, sa, sb)
                    if size is None:
                        continue
                    self.moved = max(self.moved or (0, '', 0), (size, key, record))
                    field, a, b = 'pos', self.place(key, sa), self.place(key, sb)
                self.kind(section, kind).add(record, key, field, a, b, size, ulps)
                self.entities.setdefault(section, set()).add(key)
                self.outcomes_of(section, key, desc, field, kind, record, a, b)

    @staticmethod
    def place(key, state):
        fields = state.get(key, ('', {}))[1]
        return '(' + ', '.join(short(float(fields.get('pos.' + c, 0))) for c in 'xyz') + ')'

    @staticmethod
    def distance(key, sa, sb):
        try:
            va = [float(sa[key][1].get('pos.' + c, 0)) for c in 'xyz']
            vb = [float(sb[key][1].get('pos.' + c, 0)) for c in 'xyz']
        except (KeyError, ValueError):
            return None
        return math.dist(va, vb)

    def kind(self, section, kind):
        return self.kinds.setdefault((section, kind), Kind())

    def outcomes_of(self, section, key, desc, field, kind, record, a, b):
        where = f'{key} {field}: {a} in {self.name_a}, {b} in {self.name_b}, from record {record}'
        if section == 'units' and kind == 'dead':
            self.outcome('deaths', record, where)
        elif section == 'units' and field == '#vitals':
            self.unknown = True
        elif section == 'players' and field == 'money':
            self.outcome('money', record, where)
        elif section == 'players' and field.startswith('var.'):
            self.var_names.add(field[4:])
            self.outcome('quest variables', record, f'{key} {field[4:]}: {a} in {self.name_a}, {b} in {self.name_b}')
        elif section == 'players' and re.match(r'(?:%s|member\d+)' % '|'.join(PARTY_FIELDS), field):
            self.outcome('party and items', record, where)
        elif section == 'world' and field == 'gameResult':
            self.outcome('game result', record, where)
        elif section == 'script':
            self.outcome('script', record, f'{key} {field} differs (hashes only), from record {record}')

    # ---- the text

    def describe(self, section, name, kind):
        a, b = self.name_a, self.name_b
        noun = NOUN[section] if len(kind.entities) != 1 else NOUN[section].rstrip('s')
        head = f'{name}: {len(kind.entities)} {noun}, {len(kind.records)} record{'s' if len(kind.records) != 1 else ''}, from {kind.first}'
        if name.startswith('only in'):
            keys = sorted(kind.entities, key=golden.key_order)
            return f'{name}: {len(keys)} ({", ".join(f"{k} {self.descs.get(k, "")}".strip() for k in keys[:3])}' + \
                   (', ...' if len(keys) > 3 else '') + f'), from record {kind.first}'
        if kind.biggest:
            size, ulps, key, field, record, va, vb = kind.biggest
            word = 'moved' if name == 'pos' and section == 'units' else 'max |d|'
            tail = f'{key} {self.descs.get(key, "")}'.strip() if name == 'pos' and section == 'units' else f'{key} {field}'
            text = f'{head}; {word} {short(size)}' + (f' ({ulps} ulps)' if ulps and ulps <= 1000 else '') + \
                   f' at {clip(tail, 34)}, record {record}'
            if not (name == 'pos' and section == 'units'):
                text += f': {clip(va)} -> {clip(vb)}'
            return text
        key, field, record, va, vb = kind.example
        if kind.hash_only:
            return f'{head}; hash only (no numbers kept)'
        return f'{head}; e.g. {clip(key)} {clip(field, 30)}: {clip(va)} in {a}, {clip(vb)} in {b}'

    def see(self, keys, sa, sb):
        """Counts the entities of each section as they appear in either digest."""
        for key in keys:
            if key not in self.seen and (key in sa or key in sb):
                desc, fields = sa.get(key) or sb.get(key)
                self.seen[key] = section_of(key, desc, fields)

    def totals(self):
        out = {}
        for section in self.seen.values():
            out[section] = out.get(section, 0) + 1
        return out

    def outcome_lines(self):
        changed = [n for n in OUTCOME_ORDER if n in self.outcomes]
        same = [n for n in OUTCOME_ORDER if n not in self.outcomes]
        text = 'outcomes: ' + ('CHANGED ' + ', '.join(changed) if changed else 'none changed')
        if self.unknown and 'deaths' not in self.outcomes:
            same.remove('deaths')
            text += ' (deaths unknown: vitals differ, only a hash is kept)'
        if same:
            text += f'; same: {", ".join(same)}'
        out = [text]
        for name in changed:
            count, record, detail = self.outcomes[name]
            if name == 'quest variables':
                names = sorted(self.var_names)
                out.append(f'  {name}: {len(names)} variables differ ({", ".join(names[:6])}'
                           f'{", ..." if len(names) > 6 else ""}); first from record {record}: {detail}')
            else:
                out.append(f'  {name}: {detail}' + (f' (+{count - 1} more)' if count > 1 else ''))
        return out

    def headline(self):
        ha, hb, found = self.first
        changed = [n for n in OUTCOME_ORDER if n in self.outcomes]
        text = (f'{self.differing} of {self.records} records differ, first at record {ha["record"]} '
                f'(world update {ha["update"]}, tick {ha["tick"]}, frame {ha["frame"]}, zone {ha["zone"]}); '
                f'outcomes {"CHANGED: " + ", ".join(changed) if changed else "the same"}'
                + (' (deaths unknown: hash only)' if self.unknown and 'deaths' not in self.outcomes else ''))
        if self.moved:
            text += f'; max unit move {short(self.moved[0])} ({self.moved[1]}, record {self.moved[2]})'
        elif ('units', '#place') in self.kinds:
            text += '; unit place differs (hash only: no numbers)'
        else:
            text += '; no unit position differs'
        return text

    def lines(self, sa, sb, max_lines=MAX_LINES, with_first=True):
        out = []
        ha, hb, found = self.first
        if with_first:
            out.append(f'first difference at record {ha["record"]} (world update {ha["update"]}, tick {ha["tick"]}, '
                       f'frame {ha["frame"]}' + (f'; {self.name_b}: frame {hb["frame"]}' if hb['frame'] != ha['frame'] else '')
                       + f', zone {ha["zone"]}): {len(found)} fields')
            for key, desc, field, a, b in found[:FIRST_FIELDS]:
                d = delta(a, b) if not field.startswith('#') else None
                out.append(f'  {key} ({clip(desc, 24)}) {field}: {clip(a)} in {self.name_a}, {clip(b)} in {self.name_b}'
                           + (f' (d {short(d[0])})' if d and d[0] else ''))
            if len(found) > FIRST_FIELDS:
                out.append(f'  ... {len(found) - FIRST_FIELDS} more')
        out += self.outcome_lines()
        totals = self.totals()
        sections = sorted({s for s, _ in self.kinds},
                          key=lambda s: (min(k.first for (sec, _), k in self.kinds.items() if sec == s),
                                         SECTION_ORDER.index(s) if s in SECTION_ORDER else 99))
        detail = []
        for section in sections:
            kinds = sorted(((n, k) for (s, n), k in self.kinds.items() if s == section),
                           key=lambda nk: (nk[1].first, PRIORITY.index(nk[0]) if nk[0] in PRIORITY else len(PRIORITY), nk[0]))
            detail.append(f'{section}: {len(self.entities.get(section, ()))} of {totals.get(section, "?")} differ')
            for name, kind in kinds[:KINDS_PER_SECTION]:
                detail.append('  ' + self.describe(section, name, kind))
            if len(kinds) > KINDS_PER_SECTION:
                detail.append(f'  ... {len(kinds) - KINDS_PER_SECTION} more (other kinds of field)')
        if len(detail) > max_lines:
            detail = detail[:max_lines] + [f'... {len(detail) - max_lines} more lines (golden.py diff for the fields)']
        return out + detail


def explain(path_a, path_b, name_a='A', name_b='B', ulps=0, ignore=None, max_lines=MAX_LINES, with_first=True):
    """Walks two digests record by record and summarises every difference. (code, headline, lines): 0 the same, 1 a
    difference; headline: one line for a roll-up."""
    da, db = golden.Digest(path_a), golden.Digest(path_b)
    if da.compact != db.compact:
        line = f'digest: {name_a if da.compact else name_b} is compact and the other is not (golden.py compact the other)'
        return 1, line, [line]
    ex = Explainer(name_a, name_b)
    ia, ib = iter(da), iter(db)
    ignore = re.compile(ignore) if ignore else None
    common = da.groups() & db.groups()
    partial = common != da.groups() or common != db.groups()
    lines = []
    if partial:
        lines.append(f'digest: comparing the groups both have; left out: '
                     f'{", ".join(sorted((da.groups() | db.groups()) - common))}')
    diverged = set()
    ended = None
    misaligned = None
    while True:
        ha, hb = next(ia, None), next(ib, None)
        if ha is None or hb is None:
            if ha is not hb:
                longer, h = (name_a, ha) if ha else (name_b, hb)
                ended = f'digest: {longer} has more records; the other ends before its {golden.where(h)}'
            break
        ex.records += 1
        keys = da.touched | db.touched | diverged
        ex.see(da.touched | db.touched, da.state, db.state)
        da.touched.clear()
        db.touched.clear()
        if partial:
            keys = {k for k in keys if golden.key_group(k) in common}
        if (ha['update'], ha['tick']) != (hb['update'], hb['tick']) and misaligned is None:
            misaligned = (f'digest: the records no longer line up at record {ha["record"]}: {name_a} world update '
                          f'{ha["update"]} tick {ha["tick"]}, {name_b} world update {hb["update"]} tick {hb["tick"]}')
        zone_differs = ha['zone'] != hb['zone']
        if zone_differs:
            ex.zone(ha, hb)
        if ha['hash'] == hb['hash'] and not zone_differs:
            diverged = set()
            continue
        found = golden.differences(da.state, db.state, ulps, ignore, keys)
        if not found:
            diverged = set()
            if zone_differs:
                ex.differing += 1
                ex.records_with.add(ha['record'])
                if ex.first is None:
                    ex.first = (ha, hb, [('world', 'world', 'zone', ha['zone'], hb['zone'])])
            continue
        ex.add(ha, hb, found, da.state, db.state)
        diverged = {f[0] for f in found}
    if ex.first is None and not ended and not misaligned:
        note = f'digest: {ex.records} records, the same'
        return 0, note, [note]
    code_lines = [misaligned] if misaligned else []
    if ended:
        code_lines.append(ended)
    if ex.first is None:
        return 1, (misaligned or ended), code_lines
    headline = ex.headline() + ('; the digests have different numbers of records' if ended else '')
    return 1, headline, lines + code_lines + ex.lines(da.state, db.state, max_lines, with_first)


# ---- saves

def save_lines(lines_a, lines_b, name_a, name_b, known=None, examples=2):
    """How two decoded saves differ, by kind of record (the key's letters: obj, item, spell, ...): fields and
    records that differ, the largest numeric change. [] when they do not."""
    known = re.compile(known) if known else None
    found = [f for f in savefile.differences(lines_a, lines_b) if not (known and known.search(f'{f[0]} {f[1]}'))]
    if not found:
        return []
    kinds = {}
    for key, field, va, vb in found:
        kind = re.match(r'[A-Za-z]+', key)
        kind = kind.group(0) if kind else key
        entry = kinds.setdefault(kind, {'fields': 0, 'keys': set(), 'example': (key, field, va, vb), 'biggest': None})
        entry['fields'] += 1
        entry['keys'].add(key)
        d = delta(va, vb)
        if d and (entry['biggest'] is None or d[0] > entry['biggest'][0]):
            entry['biggest'] = (d[0], key, field, va, vb)
    out = [f'{len(found)} fields in {len({f[0] for f in found})} records differ: ' +
           ', '.join(f'{k} {e["fields"]}' for k, e in sorted(kinds.items(), key=lambda ke: -ke[1]['fields'])[:8])]
    for kind, e in sorted(kinds.items(), key=lambda ke: -ke[1]['fields'])[:examples + 2]:
        key, field, va, vb = e['example']
        text = f'  {kind}: {e["fields"]} fields in {len(e["keys"])} records; first {key} {field}: {clip(va)} in {name_a}, ' \
               f'{clip(vb)} in {name_b}'
        if e['biggest'] and e['biggest'][1:3] != (key, field):
            size, key, field, va, vb = e['biggest']
            text += f'; largest numeric d {short(size)} at {key} {field}: {clip(va)} -> {clip(vb)}'
        elif e['biggest']:
            text += f' (d {short(e["biggest"][0])})'
        out.append(text)
    return out
