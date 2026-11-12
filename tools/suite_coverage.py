"""The suite by coverage per scenario (H74): which runs reach code no other run reaches, which checks only one scenario
makes, the runs that could go or merge into longer ones, and a smoke set with the most coverage per run.

usage:
  python tools/suite_coverage.py [--coverage FILE] [--times FILE] [--out FILE] [--smoke N] [--budget SECONDS]
                                 [--members N] [--smoke-file FILE]
      reads build/coverage/scenarios.json (tools/coverage.py scenarios: the functions and flags each run of the
      coverage build reached), the scenarios (tests/scenarios, a pair's peer in tests/scenarios/peer), the goldens
      (tests/golden) and the coverage runs' times (build/coverage/run_times.json), and writes build/h74/report.md
      and build/h74/suite.json (the lists). Nothing in the suite changes.

What counts (docs/notes/harness.md, "Source coverage"):
- A run is <scenario>-standalone (the vanilla data) or <scenario>-standalone-rebirth; a pair's peer counts with its
  run. Coverage is src/'s functions (exact) and the coverage build's edge flags (finer; a timing-dependent branch
  can set one in one run and not in its repeat, so edges only rank, they never decide).
- A run is redundant when every function it reaches stays reached by the runs that remain: runs are taken off one at
  a time, the least valuable first (no check of its own, fewest edges of its own, a Rebirth twin before its vanilla
  run, longest), so two runs that alone reach a function never both go.
- A check of its own: a step that can fail the run (check, expect, waitfor, the renderer's and the interface's checks,
  samecapture / diffcapture, ...: everything but the input, wait and set-up steps, ACTION_VERBS; a pair's peer's
  too) whose text no other scenario has, a dump, a save (decoded hash) no other golden of the install has, a digest
  feature (zone, timed object kind, missile spell, object kind) no other golden of the install has, or an
  "# expect:" result. A Rebirth run makes its vanilla twin's checks on other data: its own are only the checks of its
  "rebirth" steps and digest features no golden of either install has.
- A drop candidate is a redundant run with no check of its own; the Rebirth runs config/rebirth_scenarios.csv marks
  are listed apart and stay in the counts after (the mark says Rebirth's data decides).
- A family is the scenarios with the same start (the set-up steps up to the new game, the load or the menu, the
  arguments and the peer) that are no drop candidates: their bodies could follow each other in one game, packed into
  runs of at most --budget seconds and --members scenarios. A member whose body leaves that state (a zone change, a
  load, a new game, a window or mode change, a mod, a peer) can only go last; ALONE_VERBS and "# key:" lines keep a
  scenario out.
- The smoke set: from the runs that stay, greedily the run that adds the most functions not yet reached (new edges
  break ties), --smoke runs; --smoke-file writes it as run_all's smoke tier (config/smoke_scenarios.csv).
"""
import argparse
import base64
import collections
import glob
import json
import os
import re
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import golden   # noqa: E402  (key_group)

SCENARIOS = os.path.join(ROOT, 'tests', 'scenarios')
GOLDENS = os.path.join(ROOT, 'tests', 'golden')
INSTALLS = {'standalone': 'vanilla', 'standalone-rebirth': 'rebirth'}

# Steps that cannot fail a run by themselves: input, waits for time, the game's start and its settings. Every other
# verb counts as a check (an unknown one too).
ACTION_VERBS = {
    'console', 'frames', 'wait', 'key', 'keydown', 'keyup', 'click', 'clickregion', 'clickunit', 'clickground', 'move',
    'moveregion', 'mousedrag', 'wheel', 'waitmenu', 'waitwindow', 'freeze', 'unfreeze', 'quit', 'report', 'newgame',
    'timeout', 'peer', 'peersignal', 'peerwait', 'zone', 'load', 'skin', 'mod', 'resolution', 'windowsize',
    'fullscreen', 'render', 'camera', 'lookat', 'screen', 'focus', 'speed', 'shadows', 'digest', 'capture', 'read',
    'droplist', 'walkto', 'msaa', 'anisotropy', 'clock', 'event', 'hit', 'save', 'plugin', 'widescreen', 'waitpresent',
    'losedevice', 'gputiming', 'frametime', 'camtrace', 'objtrace', 'reject', 'talk', 'rdown', 'rup', 'hold', 'release',
    'rclick', 'rdblclick', 'dblclick', 'dblclickregion', 'ldown', 'lup', 'typeunicode', 'logmark', 'allow', 'giveitem',
    'chat', 'askcompanion', 'campnexthero', 'record'}
# Steps that leave the state a scenario's start made: such a body can only end a merged run.
STATE_VERBS = {'zone', 'load', 'newgame', 'windowsize', 'resolution', 'fullscreen', 'mod', 'modfile', 'modarchive',
               'skin', 'render', 'msaa', 'anisotropy', 'losedevice', 'peer', 'peersignal', 'peerwait', 'screen',
               'plugin', 'widescreen', 'hit'}
# Steps that keep a scenario out of any merged run: the run ends in a crash, a hang or a full disk on purpose, or it
# replays recorded input by frame number.
ALONE_VERBS = {'expectcrash', 'hang', 'diskfull', 'recorded', 'deterministic'}
# The steps a family's start is made of: everything up to the first of these (inclusive).
START_VERBS = {'newgame', 'load'}
# Lines left out of a start's key: they change nothing a later body sees.
START_IGNORED = {'reject', 'timeout', 'digest', 'report'}


# ---- The scenarios ----

def read_steps(path, meta=None):
    """A scenario file's steps: (install or None, verb, normalised text); meta gets its "# key:" lines."""
    steps = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for text in f:
            line = text.strip()
            if not line:
                continue
            if line.startswith('#'):
                for key in ('expect', 'arguments', 'settings-file'):
                    if meta is not None and line.startswith(f'# {key}:'):
                        meta[key] = line.split(':', 1)[1].strip()
                continue
            words = line.split()
            install = None
            if words[0] in ('rebirth', 'vanilla') and len(words) > 1:
                install, words = words[0], words[1:]
            if words[0] == 'at' and len(words) > 2:
                words = words[2:]
            steps.append((install, words[0], ' '.join(words)))
    return steps


class Scenario:
    def __init__(self, name):
        self.name = name
        self.meta = {}           # '# expect:', '# arguments:', '# settings-file:'
        self.steps = read_steps(os.path.join(SCENARIOS, name + '.txt'), self.meta)
        self.peer = next((t.split()[1] for _, v, t in self.steps if v == 'peer' and len(t.split()) > 1), None)
        # A pair's peer game checks too (tests/scenarios/peer/<peer>.txt): its checks count as the run's.
        peer_path = os.path.join(SCENARIOS, 'peer', f'{self.peer}.txt') if self.peer else None
        self.peer_steps = read_steps(peer_path) if peer_path and os.path.exists(peer_path) else []
        self.install_steps = any(i for i, _, _ in self.steps + self.peer_steps)

    def checks(self, install):
        """The normalised check steps the run on install makes (a pair's peer's prefixed "peer: ")."""
        found = {t for i, v, t in self.steps if (i is None or i == install) and v not in ACTION_VERBS}
        found |= {f'peer: {t}' for i, v, t in self.peer_steps if (i is None or i == install) and v not in ACTION_VERBS}
        if 'expect' in self.meta:
            found.add(f'# expect: {self.meta["expect"]}')
        return found

    def start(self):
        """The family key: the set-up steps up to the game's start, the arguments, the peer."""
        key = []
        at_start = False
        for install, verb, text in self.steps:
            if verb in START_IGNORED or text == 'console thingamabob':
                continue
            key.append(text if install is None else f'{install} {text}')
            if verb in START_VERBS:
                at_start = True
                break
        if not at_start:   # a menu scenario: its start is the menu
            key = [t for t in key[:1]]
        return (self.meta.get('arguments', ''), self.peer or '', tuple(key))

    def body_leaves_state(self):
        seen_start = False
        for _, verb, _ in self.steps:
            if verb in START_VERBS and not seen_start:
                seen_start = True
                continue
            if verb in STATE_VERBS:
                return True
        return False

    def mergeable(self):
        """Whether the scenario's run could share a game at all: not one whose result, arguments or settings file
        run_all checks on its own (the watchdog ones, the drop-in ones), nor one with an ALONE_VERBS step."""
        return not self.meta and not any(v in ALONE_VERBS for _, v, _ in self.steps)


# ---- The goldens ----

def golden_features(name, install):
    """(dumps, save hashes, digest features) of a golden, or None without one."""
    folder = os.path.join(GOLDENS, name, install)
    if not os.path.exists(os.path.join(folder, 'golden.json')):
        return None
    dumps = sorted(os.path.basename(p)[5:-4] for p in glob.glob(os.path.join(folder, 'dump_*.txt')))
    saves = set()
    if os.path.exists(os.path.join(folder, 'saves.txt')):
        for line in open(os.path.join(folder, 'saves.txt'), encoding='utf-8', errors='replace'):
            parts = line.split()
            if len(parts) == 2:
                saves.add(parts[1])
    features = set()
    if os.path.exists(os.path.join(folder, 'digest.txt')):
        for line in open(os.path.join(folder, 'digest.txt'), encoding='utf-8', errors='replace'):
            if line.startswith('@ '):
                words = line.split()
                if 'zone' in words:
                    zone = words[words.index('zone') + 1]
                    if zone != '-':
                        features.add(f'zone {zone}')
            elif line.startswith('+ '):
                words = line.split()
                group = golden.key_group(words[1]) if len(words) > 1 else None
                if group == 'timed' and len(words) > 2:
                    features.add(f'timed object kind {words[2]}')
                elif group == 'missile' and len(words) > 3:
                    features.add(f'missile of spell {words[3]}')
                elif group == 'obj' and len(words) > 2:
                    features.add(f'object kind {words[2]}')
                elif group == 'water':
                    features.add('water')
    return {'dumps': dumps, 'saves': saves, 'features': features}


# ---- Coverage ----

def load_coverage(path):
    data = json.load(open(path, encoding='utf-8'))
    runs = {}
    for name, run in data['runs'].items():
        m = re.match(r'^(.*)-(standalone|standalone-rebirth)$', name)
        if not m:
            continue
        bits = zlib.decompress(base64.b64decode(run['flags']))
        functions = 0
        for i in run['functions']:
            functions |= 1 << i
        runs[(m.group(1), INSTALLS[m.group(2)])] = {'functions': functions, 'flags': int.from_bytes(bits, 'little')}
    return data['functions'], runs


def popcount(x):
    return bin(x).count('1')


def once_masks(masks):
    """(bits set in exactly one mask, bits set in any)."""
    once = more = 0
    for m in masks:
        more |= once & m
        once = (once | m) & ~more
    return once, once | more


def run_label(key):
    return key[0] if key[1] == 'vanilla' else f'{key[0]} (Rebirth)'


# ---- The analysis ----

def analyse(args):
    functions, cov = load_coverage(args.coverage)
    times = {}
    for path in (args.times, os.path.join(ROOT, 'build', 'verify', 'run_times.json')):
        if os.path.exists(path):
            for k, v in json.load(open(path)).items():
                m = re.match(r'^(.*)-(standalone|standalone-rebirth)$', k)
                if m:
                    times.setdefault((m.group(1), INSTALLS[m.group(2)]), v)
    scenarios = {n: Scenario(n) for n in (os.path.splitext(os.path.basename(p))[0]
                                          for p in glob.glob(os.path.join(SCENARIOS, '*.txt')))}
    keys = sorted(k for k in cov if k[0] in scenarios)
    missing = sorted(k for k in cov if k[0] not in scenarios)
    goldens = {k: golden_features(*k) for k in keys}

    # Checks only one run makes: assertions across scenarios (a Rebirth twin of the same scenario does not count
    # against it), saves and digest features across the install's goldens.
    check_owners = collections.defaultdict(set)
    for name, s in scenarios.items():
        for install in ('vanilla', 'rebirth'):
            for c in s.checks(install):
                check_owners[c].add(name)
    save_owners = collections.defaultdict(set)
    feature_owners = collections.defaultdict(set)
    for k, g in goldens.items():
        if g:
            for h in g['saves']:
                save_owners[(k[1], h)].add(k[0])
            for feat in g['features']:
                feature_owners[(k[1], feat)].add(k[0])

    once_f, all_f = once_masks(cov[k]['functions'] for k in keys)
    once_e, all_e = once_masks(cov[k]['flags'] for k in keys)
    rows = {}
    for k in keys:
        s = scenarios[k[0]]
        g = goldens[k]
        unique = []
        if k[1] == 'vanilla':
            for c in sorted(s.checks(k[1])):
                if check_owners[c] == {k[0]}:
                    unique.append(('check', c))
            if g:
                unique += [('dump', d) for d in g['dumps']]
                unique += [('save', h) for h in sorted(g['saves']) if save_owners[(k[1], h)] == {k[0]}]
                unique += [('digest', f) for f in sorted(g['features']) if feature_owners[(k[1], f)] == {k[0]}]
        else:
            # A Rebirth run makes its vanilla twin's checks, dumps and saves on other data: its own are the checks of
            # its "rebirth" steps (when the vanilla run does not make the same), and the digest features no golden of
            # either install has.
            twin = s.checks('vanilla')
            for c in sorted(s.checks('rebirth') - twin):
                if check_owners[c] == {k[0]}:
                    unique.append(('check', c))
            if g:
                unique += [('digest', f) for f in sorted(g['features'])
                           if feature_owners[('rebirth', f)] == {k[0]} and not feature_owners[('vanilla', f)]]
        rows[k] = {
            'unique_functions': popcount(cov[k]['functions'] & once_f),
            'unique_edges': popcount(cov[k]['flags'] & once_e),
            'functions': popcount(cov[k]['functions']), 'edges': popcount(cov[k]['flags']),
            'time': times.get(k, 0.0), 'golden': g is not None, 'unique_checks': unique,
        }

    # Redundant runs: taken off one at a time while every function stays reached.
    count = collections.Counter()
    for k in keys:
        x = cov[k]['functions']
        while x:
            low = x & -x
            count[low.bit_length() - 1] += 1
            x ^= low
    order = sorted(keys, key=lambda k: (bool(rows[k]['unique_checks']), rows[k]['unique_edges'], k[1] == 'vanilla',
                                        -rows[k]['time']))
    redundant = []
    for k in order:
        bits = [i for i in iter_bits(cov[k]['functions'])]
        if all(count[i] >= 2 for i in bits):
            for i in bits:
                count[i] -= 1
            redundant.append(k)
    kept = [k for k in keys if k not in set(redundant)]
    kept_e = 0
    for k in kept:
        kept_e |= cov[k]['flags']
    dropped_set = set(redundant)
    drop = [k for k in redundant if not rows[k]['unique_checks']]
    # Rebirth runs: why each is in the Rebirth set; the csv-marked ones stay in the counts after (the user's call).
    csv = {}
    for line in open(os.path.join(ROOT, 'config', 'rebirth_scenarios.csv'), encoding='utf-8'):
        parts = line.strip().split(',', 2)
        if len(parts) == 3 and parts[1] == '1':
            csv[parts[0]] = parts[2]
    final_drop = [k for k in drop if k[1] == 'vanilla' or k[0] not in csv]
    after_f = after_e = 0
    for k in set(keys) - set(final_drop):
        after_f |= cov[k]['functions']
        after_e |= cov[k]['flags']

    # Families and merged runs.
    families = collections.defaultdict(list)
    final_set = set(final_drop)
    for k in keys:
        if scenarios[k[0]].mergeable() and k not in final_set:   # a drop candidate merges into nothing
            families[(k[1],) + scenarios[k[0]].start()].append(k)
    merges = []
    for fam_key, members in families.items():
        if len(members) < 2:
            continue
        timed = sorted(members, key=lambda k: rows[k]['time'])
        start_cost = min(rows[k]['time'] for k in members) * 0.8
        enders = [k for k in timed if scenarios[k[0]].body_leaves_state()]
        bodies = [k for k in timed if k not in enders]
        groups = []
        current, used = [], start_cost
        for k in bodies:
            cost = max(rows[k]['time'] - start_cost, 0.5)
            if current and (used + cost > args.budget or len(current) >= args.members):
                groups.append(current)
                current, used = [], start_cost
            current.append(k)
            used += cost
        if current:
            groups.append(current)
        # each group may end with one member that leaves the state, when it fits
        for k in enders:
            cost = max(rows[k]['time'] - start_cost, 0.5)
            for grp in groups:
                if not any(scenarios[m[0]].body_leaves_state() for m in grp) and len(grp) < args.members and \
                        start_cost + sum(max(rows[m]['time'] - start_cost, 0.5) for m in grp) + cost <= args.budget:
                    grp.append(k)
                    break
        for grp in groups:
            if len(grp) >= 2:
                merges.append({'family': fam_key, 'members': grp, 'start_cost': start_cost,
                               'time': start_cost + sum(max(rows[m]['time'] - start_cost, 0.5) for m in grp)})

    # The smoke set: greedy, the most new functions per run (new edges break ties).
    smoke, covered_f, covered_e = [], 0, 0
    curve = []
    pool = set(keys) - final_set   # from the runs that stay
    while pool and len(smoke) < args.smoke:
        best = max(pool, key=lambda k: (popcount(cov[k]['functions'] & ~covered_f),
                                        popcount(cov[k]['flags'] & ~covered_e), -rows[k]['time'], k))
        pool.discard(best)
        covered_f |= cov[best]['functions']
        covered_e |= cov[best]['flags']
        smoke.append(best)
        curve.append((len(smoke), popcount(covered_f), popcount(covered_e)))

    return {'functions': functions, 'cov': cov, 'keys': keys, 'missing': missing, 'rows': rows,
            'scenarios': scenarios, 'all_f': all_f, 'all_e': all_e, 'redundant': redundant, 'drop': drop,
            'kept_e': kept_e, 'dropped_set': dropped_set, 'csv': csv, 'final_drop': final_drop,
            'after_f': after_f, 'after_e': after_e, 'merges': merges, 'families': families,
            'smoke': smoke, 'curve': curve}


def iter_bits(x):
    while x:
        low = x & -x
        yield low.bit_length() - 1
        x ^= low


# ---- The report ----

def describe_unique(unique, limit=3):
    by_kind = collections.defaultdict(list)
    for kind, what in unique:
        by_kind[kind].append(what)
    parts = []
    for kind in ('dump', 'save', 'digest', 'check'):
        items = by_kind.get(kind)
        if not items:
            continue
        shown = ', '.join(f'`{x[:60]}`' for x in items[:limit])
        more = f' (+{len(items) - limit})' if len(items) > limit else ''
        parts.append(f'{len(items)} {kind}{"s" if len(items) > 1 else ""}: {shown}{more}')
    return '; '.join(parts) or 'none'


def write(result, args):
    rows, keys, cov, scen = result['rows'], result['keys'], result['cov'], result['scenarios']
    n_f, n_e = popcount(result['all_f']), popcount(result['all_e'])
    total_f = len(result['functions'])
    vanilla = [k for k in keys if k[1] == 'vanilla']
    rebirth = [k for k in keys if k[1] == 'rebirth']
    seconds = sum(rows[k]['time'] for k in keys)
    out = []
    w = out.append
    w('# H74: the suite by coverage per scenario\n')
    w('Made by `python tools/suite_coverage.py` from one pass of the image and Rebirth suites on the coverage build '
      '(`python tools/coverage.py run H74`), build/coverage/scenarios.json. How it counts: the tool\'s docstring. '
      'Nothing is deleted or merged by this report: the lists go to the user first.\n')
    w('## The suite now\n')
    w(f'- Runs with coverage: {len(keys)} ({len(vanilla)} vanilla, {len(rebirth)} Rebirth); their run time '
      f'{seconds / 60:.0f} game-minutes (the sum of the runs\' times on the coverage build).')
    w(f'- Reached by the suite: {n_f} of {total_f} src/ functions, {n_e} edge flags.')
    unmeasured = sorted(n for n in scen if (n, 'vanilla') not in rows and not n.startswith('screenshots_'))
    if unmeasured:
        w(f'- Scenarios with no coverage run (no coverage.dat: a game killed from outside writes none; they stay as '
          f'they are): {", ".join(unmeasured)}')
    if result['missing']:
        w(f'- Runs without a scenario file (left out): {", ".join(run_label(k) for k in result["missing"])}')
    lone_f = sum(1 for k in keys if rows[k]['unique_functions'])
    w(f'- Runs that alone reach some function: {lone_f}; runs that reach no function another run does not: '
      f'{len(keys) - lone_f}.')
    w(f'- Runs with no check of their own (no unique check step, dump, save or digest feature): '
      f'{sum(1 for k in keys if not rows[k]["unique_checks"])}.\n')

    redundant, drop = result['redundant'], result['drop']
    lost_e = n_e - popcount(result['kept_e'])
    w('## 1. Candidates to drop\n')
    w(f'Taken off one at a time (least valuable first) while every function stays reached: {len(redundant)} runs '
      f'could go without losing a function ({lost_e} edge flags of {n_e} would be lost with all of them). Of those, '
      f'**{len(drop)} check nothing of their own** (every check step another scenario also makes, no dump, no save or '
      'digest feature only their golden has): the drop candidates. The others carry unique checks: candidates to '
      'merge into a run of their family (section 2), keeping their checks.\n')
    w('A Rebirth run\'s own checks are only those of its `rebirth` steps and digest features no golden of either '
      'install has: its other checks, dumps and saves are its vanilla twin\'s on other data.\n')
    tables = [
        ('Vanilla runs', [k for k in drop if k[1] == 'vanilla'], False),
        ('Rebirth runs (in the Rebirth set for a Rebirth golden or a step for one install)',
         [k for k in drop if k[1] == 'rebirth' and k[0] not in result['csv']], True),
        ('Rebirth runs marked in config/rebirth_scenarios.csv (the mark says Rebirth\'s data decides: the user\'s '
         'call)', [k for k in drop if k[1] == 'rebirth' and k[0] in result['csv']], True),
    ]
    for title, runs, rebirth_run in tables:
        w(f'### {title}: {len(runs)}\n')
        if not runs:
            continue
        w('| run | time s | functions | edges of its own | golden | ' + ('why it is in the Rebirth set |' if rebirth_run
                                                                     else 'what it checks (all shared) |'))
        w('|---|---:|---:|---:|---|---|')
        for k in sorted(runs):
            r = rows[k]
            s = scen[k[0]]
            if rebirth_run:
                why = ('marked: ' + result['csv'][k[0]]) if k[0] in result['csv'] else \
                    ('a step for one install' if s.install_steps else 'its Rebirth golden (H51a)')
            else:
                checks = sorted(s.checks('vanilla'))
                why = f'{len(checks)} check steps' + (f', e.g. `{checks[0][:50]}`' if checks else '')
            w(f'| {run_label(k)} | {r["time"]:.1f} | {r["functions"]} | {r["unique_edges"]} | '
              f'{"yes" if r["golden"] else "no"} | {why} |')
        w('')
    keep_checks = [k for k in redundant if rows[k]['unique_checks']]
    w(f'### Redundant by coverage but with checks of their own ({len(keep_checks)})\n')
    w('Merge candidates rather than drops: their checks would move into another run with the same start.\n')
    w('| run | time s | edges of its own | its own checks |')
    w('|---|---:|---:|---|')
    for k in sorted(keep_checks, key=lambda k: (k[1], k[0])):
        r = rows[k]
        w(f'| {run_label(k)} | {r["time"]:.1f} | {r["unique_edges"]} | {describe_unique(r["unique_checks"])} |')
    w('')

    w('### Rebirth runs in short\n')
    reb_drop = [k for k in result['final_drop'] if k[1] == 'rebirth']
    golden_only = [k for k in reb_drop if not scen[k[0]].install_steps]
    w(f'- {len(rebirth)} Rebirth runs; {sum(1 for k in rebirth if k in result["dropped_set"])} reach no function the '
      f'remaining runs miss; {len(reb_drop)} of those check nothing their vanilla twin does not (the table above).')
    w(f'- Of the {len(reb_drop)}: {len(golden_only)} are in the Rebirth set only for their Rebirth golden (H51a), '
      f'{len(reb_drop) - len(golden_only)} for a step for one install (their steps differ by install, but the '
      'Rebirth run reaches no code and makes no check of its own). A narrower cut drops only the first kind: '
      f'{len(rebirth) - len(golden_only)} Rebirth runs would stay.')
    w('- Coverage cannot see data: a Rebirth run whose code is all reached elsewhere still compares Rebirth\'s '
      'records, maps and scripts with its golden. The csv marks name the runs where that is the point; the smoke '
      'set keeps the Rebirth runs that add coverage.\n')

    w('## 2. Families: small scenarios that could share one run\n')
    merges = result['merges']
    members = sum(len(m['members']) for m in merges)
    w(f'Scenarios with the same start (set-up steps up to the new game or load, arguments, peer; per install) packed '
      f'into runs of at most {args.budget:.0f} s (the start once, then each body: its run time less 0.8 of the '
      f'family\'s shortest run); a body that leaves the start\'s state (zone, load, new game, window, mod, peer) only '
      f'last. **{members} runs would become {len(merges)}** (saving {members - len(merges)} runs). Each merged run '
      'keeps every member\'s checks; goldens of merged runs are re-recorded by name (phase 2).\n')
    by_family = collections.defaultdict(list)
    for m in merges:
        by_family[m['family']].append(m)
    for fam, groups in sorted(by_family.items(), key=lambda kv: -sum(len(g['members']) for g in kv[1])):
        install, arguments, peer, start = fam
        title = ' / '.join(start[-3:]) if start else '(no steps)'
        extra = (f', arguments `{arguments}`' if arguments else '') + (f', peer `{peer}`' if peer else '')
        w(f'### {install}: start `{title[:110]}`{extra}\n')
        for g in groups:
            names = ', '.join(f'{k[0]} ({rows[k]["time"]:.0f}s)' for k in g['members'])
            w(f'- one run of ~{g["time"]:.0f} s: {names}')
        w('')

    w(f'## 3. A smoke set ({len(result["smoke"])} runs, the most coverage per run)\n')
    smoke = result['smoke']
    last = result['curve'][-1] if result['curve'] else (0, 0, 0)
    smoke_time = sum(rows[k]['time'] for k in smoke)
    w(f'Greedy: each next run is the one that adds the most functions not yet reached (new edge flags break ties). '
      f'The {len(smoke)} runs reach {last[1]} of the suite\'s {n_f} functions ({100.0 * last[1] / max(n_f, 1):.1f}%) '
      f'and {last[2]} of {n_e} edge flags ({100.0 * last[2] / max(n_e, 1):.1f}%), in {smoke_time / 60:.1f} '
      f'game-minutes ({sum(1 for k in smoke if k[1] == "vanilla")} vanilla, '
      f'{sum(1 for k in smoke if k[1] == "rebirth")} Rebirth).\n')
    w('| runs | functions | % of the suite\'s | edges | % |')
    w('|---:|---:|---:|---:|---:|')
    for n, f, e in result['curve']:
        if n in (1, 5, 10, 20, 30, 40, 50, 60, 80, 100) or n == len(result['curve']):
            w(f'| {n} | {f} | {100.0 * f / max(n_f, 1):.1f}% | {e} | {100.0 * e / max(n_e, 1):.1f}% |')
    w('')
    w('| # | run | time s | new functions | functions |')
    w('|---:|---|---:|---:|---:|')
    prev = 0
    for (n, f, e), k in zip(result['curve'], smoke):
        w(f'| {n} | {run_label(k)} | {rows[k]["time"]:.1f} | {f - prev} | {rows[k]["functions"]} |')
        prev = f
    w('')

    w('## 4. Counts before and after\n')
    drop_v = sum(1 for k in drop if k[1] == 'vanilla')
    drop = result['final_drop']
    drop_r = sum(1 for k in drop if k[1] == 'rebirth')
    dropped = set(drop)
    saved_v = sum(max(len([k for k in m['members'] if k not in dropped]) - 1, 0) for m in merges if m['family'][0] == 'vanilla')
    saved_r = sum(max(len([k for k in m['members'] if k not in dropped]) - 1, 0) for m in merges if m['family'][0] == 'rebirth')
    after_v = len(vanilla) - drop_v - saved_v
    after_r = len(rebirth) - drop_r - saved_r
    w('| | vanilla runs | Rebirth runs | converted pass |')
    w('|---|---:|---:|---:|')
    w(f'| now | {len(vanilla)} | {len(rebirth)} | {len(vanilla) + len(rebirth)} (every run on the converted copies) |')
    w(f'| drop candidates gone (the csv-marked Rebirth ones kept) | {len(vanilla) - drop_v} | {len(rebirth) - drop_r} | |')
    w(f'| and the families merged | {after_v} | {after_r} | {after_v + after_r} (only when data or conversion code '
      'changes) |')
    w(f'| smoke tier (while iterating) | {sum(1 for k in smoke if k[1] == "vanilla")} | '
      f'{sum(1 for k in smoke if k[1] == "rebirth")} | - |')
    w('')
    w(f'Coverage after the drops: functions {popcount(result["after_f"])} of {n_f}, edge flags '
      f'{popcount(result["after_e"])} of {n_e} (the drop rule keeps every function; the edges lost are those only '
      'dropped runs set). The merged runs keep their members\' steps, so their coverage is the union of the '
      'members\'.\n')

    w('## 5. Every run\n')
    w('| run | time s | functions | own functions | own edges | own checks |')
    w('|---|---:|---:|---:|---:|---|')
    for k in sorted(keys, key=lambda k: (k[1], k[0])):
        r = rows[k]
        w(f'| {run_label(k)} | {r["time"]:.1f} | {r["functions"]} | {r["unique_functions"]} | {r["unique_edges"]} | '
          f'{describe_unique(r["unique_checks"], 2)} |')
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    summary = {
        'drop': [list(k) for k in drop], 'redundant_with_checks': [list(k) for k in keep_checks],
        'merges': [{'install': m['family'][0], 'members': [k[0] for k in m['members']], 'time': round(m['time'], 1)}
                   for m in merges],
        'smoke': [list(k) for k in smoke],
        'runs': {f'{k[0]}|{k[1]}': {x: v for x, v in rows[k].items() if x != 'unique_checks'} for k in keys},
    }
    if args.smoke_file:
        last_f = result['curve'][-1][1] if result['curve'] else 0
        with open(args.smoke_file, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# The smoke tier (H74; run_all --smoke, work.py test\'s default without names): written by\n'
                    '# `python tools/suite_coverage.py --smoke-file config/smoke_scenarios.csv` from a coverage pass, '
                    'greedily the run that\n'
                    f'# adds the most src/ functions; {len(smoke)} runs reached {last_f} of the suite\'s {n_f} '
                    'functions.\n')
            f.write('scenario,install\n')
            for k in smoke:
                f.write(f'{k[0]},{k[1]}\n')
    with open(os.path.join(os.path.dirname(args.out), 'suite.json'), 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=1)
    print(f'suite_coverage: {len(keys)} runs; {len(redundant)} redundant by function, {len(drop)} drop candidates; '
          f'{members} runs in {len(merges)} merged runs; smoke {len(smoke)} runs reach {last[1]} / {n_f} functions '
          f'-> {args.out}')


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--coverage', default=os.path.join(ROOT, 'build', 'coverage', 'scenarios.json'))
    ap.add_argument('--times', default=os.path.join(ROOT, 'build', 'coverage', 'run_times.json'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'h74', 'report.md'))
    ap.add_argument('--smoke', type=int, default=60)
    ap.add_argument('--budget', type=float, default=40.0)
    ap.add_argument('--members', type=int, default=6)
    ap.add_argument('--smoke-file', help='also write the smoke set there (config/smoke_scenarios.csv)')
    args = ap.parse_args(argv)
    write(analyse(args), args)


if __name__ == '__main__':
    main(sys.argv[1:])
