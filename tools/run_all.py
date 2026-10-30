"""Runs every harness scenario on the vanilla install's data and on Rebirth's and prints a summary.

usage: python tools/run_all.py [scenario names...] [--rebirth | --rebirth-only | --all-rebirth]
                               [--jobs N] [--retries N] [--clock virtual|real[,...]] [--golden] [--x64]
       python tools/run_all.py --widescreen [scenario names...]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
  default          the standalone image (H40: game.exe is no longer run), as
                   --standalone: ei_launch --standalone, build\\verify\\ei-rebuild.exe lays out game.exe's data
                   without its code (it reads no game.exe, S18k1), the game and the harness linked in (S23), on the vanilla install's data
                   (install "standalone", vanilla goldens), and on the Rebirth install's data
                   with the patches of config/rebirth_profile.ini as EI_REBIRTH (no dgVoodoo, addon.dll or
                   EI_Plugin.asi either way; install "standalone-rebirth", Rebirth goldens). Every scenario runs
                   on the vanilla data; Rebirth's data only on demand (SV9; before releases, not after every merge):
                   --rebirth adds the smoke set (rebirth_scenarios(): config/rebirth_scenarios.csv's marks and every
                   scenario with a step for one install, rebirthflags, addonpath or widescreen) or the named
                   scenarios, --rebirth-only runs those alone, --all-rebirth every scenario on both. An address
                   with no rewrite ends the run: "standalone: no code at ...".
  drop-in          a scenario with a line "# drop-in: <why>" (tests/scenarios/dropin_*.txt) checks the exe dropped
                   into a game folder (S23a; docs/notes/standalone.md, "The drop-in exe"): ei_launch sees the line and
                   (as with --drop-in) copies the build's ei-rebuild.exe into <run folder>\\dropin, a folder made
                   like the install (its folders junctions, its files copies, no game.exe; nothing is written to the
                   install), and starts it from the run folder with no EI_GAME_ROOT or EI_REBIRTH.
                   On both of the image's installs (standalone, standalone-rebirth) by default (--vanilla-only /
                   --rebirth-only: one); never with --widescreen. Their goldens are their own runs' (H40; no
                   EI_REBIRTH: the Rebirth install's run plays in the vanilla runs' 4:3 window, harness_starter.h).
  renderers        The image draws with our renderer (no DirectDraw device, its own swap chain, no display mode
                   change), so its runs never need the machine to themselves. EI_RENDER reaches no run: a scenario's
                   "render" step sets the image's renderer options (EI_RENDER_EXTRA adds words to them).
  --widescreen     Rebirth's WideScreen against our rewrite of it (docs/notes/rebirth.md, "WideScreen"): the
                   scenarios (default ui_widescreen, ui_widescreen_more, ui_widescreen_hud,
                   ui_widescreen_screens and ui_widescreen_dialogue; they set the desktop's mode and WideScreen with the harness's
                   "resolution" / "widescreen" steps) on the image on the Rebirth install
                   (<scenario>-standalone-rebirth), then each frame against addon's recorded frames
                   (compare_widescreen: tools/compare_frames.py with WIDESCREEN_COMPARE). The references are
                   addon's frames, recorded from game.exe until H40 in <main checkout>\\build\\widescreen; a
                   scenario whose steps changed since has none (the comparison names it)
  --jobs N         games running at once (default 16); all runs on the machine together never
                   exceed EI_HARNESS_SLOTS (default 16) games, whatever --jobs says. Each run writes to its own folder,
                   build\\verify\\runs\\<scenario>-<install> (ei-rebuild.log, harness_report.txt and
                   harness_saves, emptied first, so every run starts without saves or texture cache).
  --retries N      re-run a failed scenario up to N times (default 1); fights against spawned
                   enemies are random, and the hero occasionally loses one on the original too.
                   A pass after a retry is reported as "PASS*".
  --clock virtual|real[,clocktrace][,randtrace]
                   run every scenario on the virtual clock or on real time, whatever its own lines say
                   (EI_HARNESS_CLOCK; tests/verify/harness_clock.h), with the clock or random trace
  --golden         runs the games with a state digest (EI_DIGEST, the goldens' period) and afterwards compares
                   every run that has a golden (tests/golden/<scenario>/<install>, tools/golden.py) with it:
                   the console, the digest records, the dumps and the saves. The image's runs on the vanilla data
                   compare with the vanilla golden, on Rebirth's with the Rebirth golden
  --x64            the x64 image's runs (build.cmd verify-x64: build\\verify-x64, its ei-rebuild.exe started by its own
                   x64 ei_launch; runs in build\\verify-x64\\runs), against the same goldens. EI_BUILD_DIR still wins.
exit code: 0 when every run passed

A run that outlives the scenario's time limit ("timeout <seconds>", default 60 s) is reported as TIMEOUT: the
game's watchdog ends it at the limit, ei_launch 20 s later if the watchdog could not (tools/launcher/launch.cpp).
A scenario with a line "# expect: TIMEOUT" passes when it times out (tests/scenarios/watchdog_*.txt check this).
"""
import argparse, functools, collections, concurrent.futures, contextlib, glob, hashlib, json, msvcrt, os, re, shutil, struct, subprocess, sys, tempfile, threading, time

import frame_store, prepare_scenario

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDESCREEN_SCENARIOS = ['ui_widescreen', 'ui_widescreen_more', 'ui_widescreen_hud', 'ui_widescreen_screens',
                        'ui_widescreen_dialogue']   # --widescreen without scenario names
STANDALONE_INSTALLS = ('standalone', 'standalone-rebirth')   # the image on the vanilla and the Rebirth data
BUILD = os.environ.get('EI_BUILD_DIR') or os.path.join(ROOT, 'build', 'verify')   # EI_BUILD_DIR: another verify build (timing runs)
BUILD_X64 = os.environ.get('EI_BUILD_DIR') or os.path.join(ROOT, 'build', 'verify-x64')   # --x64: build.cmd verify-x64
GOLDEN_PERIOD = 20   # world updates between the digest records of the suite's goldens (--golden; tools/golden.py)
# Runs go to <BUILD>/runs. <BUILD>/run_times.json keeps each run's seconds from the last suite: the
# longest start first, so the last games to finish are short ones (alphabetical order left a 50 s run
# for the end). run(), plan() and summary() take root/build so tools/work_sched.py can point them at
# another checkout.

# Games running at once on this machine, over every checkout and every run_all (2026-10-14: three
# workers' suites, 24 games of ~640 MB video memory each rendering without a frame limit, hung the
# NVIDIA driver until Windows crashed twice). A slot is a byte lock on a file in the temp folder;
# Windows drops it when the process ends, so a killed run never leaves a slot taken.
MACHINE_SLOTS = int(os.environ.get('EI_HARNESS_SLOTS', '16'))
SLOT_DIR = os.path.join(tempfile.gettempdir(), 'ei_harness_slots')


def _lock(path):
    """Takes the one-byte lock on path without waiting; the open file, or None if another holds it."""
    f = open(path, 'a+b')
    try:
        msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        return f
    except OSError:
        f.close()
        return None


def _unlock(f):
    f.seek(0)
    msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
    f.close()


@contextlib.contextmanager
def machine_slot(alone=False, count=1):
    """count of the machine's MACHINE_SLOTS game slots (shared by every checkout's run_all; 2 for a pair's
    run, games_of()), or all of them (alone): for a game that sets the display mode, which takes the surfaces
    of every other game running then (they fail with DDERR_SURFACELOST: "Can't begin scene", untextured models).
    Games that want all the slots queue on one more lock, so two of them never split the slots; while
    one holds it, no other game takes a slot (a suite's next game would take each slot as it frees,
    and the one that wants them all waited for the end of every other suite on the machine)."""
    os.makedirs(SLOT_DIR, exist_ok=True)
    held = []
    gate = None
    try:
        if alone:
            while not (gate := _lock(os.path.join(SLOT_DIR, 'alone'))):
                time.sleep(0.5)
        while True:
            if not alone:
                # A game waiting for all the slots holds the gate; others take no new slot meanwhile,
                # or freed slots go to them first and it starves (35 minutes once with four suites).
                waiting = _lock(os.path.join(SLOT_DIR, 'alone'))
                if not waiting:
                    time.sleep(0.5)
                    continue
                _unlock(waiting)
            for i in range(MACHINE_SLOTS):
                if alone and i in [h[0] for h in held]:
                    continue
                if not alone and len(held) == count:
                    break
                f = _lock(os.path.join(SLOT_DIR, f'slot{i:02}'))
                if f:
                    held.append((i, f))
            if len(held) == (MACHINE_SLOTS if alone else count):
                break
            if not alone:
                # A pair's run takes its slots together: none held while waiting, or two pairs could each hold one.
                for _, f in held:
                    _unlock(f)
                held = []
            time.sleep(0.5)
        yield
    finally:
        for _, f in held:
            _unlock(f)
        if gate:
            _unlock(gate)


@functools.lru_cache(maxsize=None)
def run_dir(root):
    """root's run dir (the installs' game.exe copies). EI_RUN_DIR names it for a checkout that is not a worktree (the
    self-hosted runner's, .github/workflows/suites.yml), with no junction for a clean to follow. A worktree without
    its own (a worker that forgot the junction, so every game ended NO REPORT) uses the main checkout's: its .git file
    names <main>/.git/worktrees/<name>."""
    if os.environ.get('EI_RUN_DIR'):
        return os.environ['EI_RUN_DIR']
    own = os.path.join(root, 'run')
    if os.path.isdir(own):
        return own
    with contextlib.suppress(OSError):
        with open(os.path.join(root, '.git'), encoding='utf8') as f:
            gitdir = f.read().strip().removeprefix('gitdir:').strip()
        main = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(gitdir))), 'run')
        if os.path.isdir(main):
            return main
    return own


# EI_* variables that are not settings of a game: where the build and the references are, the machine's slots, and
# what ei_launch sets itself. Every other EI_* variable (EI_PROFILE, EI_DIGEST, EI_HEAP_JITTER, ...) is read by a game.
NOT_GAME_ENV = ('EI_BUILD_DIR', 'EI_HARNESS_SLOTS', 'EI_OUT_DIR', 'EI_HARNESS', 'EI_WIDESCREEN_DIR', 'EI_PAIR',
                'EI_PAIR_SLOT_HELD')


def peer_scenario(scenario):
    """The peer's scenario of a pair (SV5d): a scenario's "peer <name>" line makes ei_launch start a second game on
    tests/scenarios/peer/<name>.txt beside it (tests/verify/harness_pair.h); None without one."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                words = text.split()
                if words[:1] == ['peer'] and len(words) > 1:
                    return os.path.join(os.path.dirname(scenario), 'peer', words[1] + '.txt')
    return None


def games_of(scenario):
    """The games a run of the scenario plays at once, and so the machine slots it takes: 2 for a pair, else 1."""
    return 2 if peer_scenario(scenario) else 1


def game_environment(environ=None):
    """The EI_* variables of environ (default: this process's) that a game reads: {name: value}."""
    return {k: v for k, v in (os.environ if environ is None else environ).items()
            if k.startswith('EI_') and k not in NOT_GAME_ENV}


# A run past its time limit. ei_launch exits with TIMEOUT_EXIT when it had to end the game itself and
# UNKILLABLE_EXIT when the game stayed alive after TerminateProcess (a thread stuck in a driver call); the
# game's own watchdog writes this failure into the report.
TIMEOUT_EXIT = 4
UNKILLABLE_EXIT = 5
LIMIT_MARGIN_S = 20          # ei_launch's margin past the scenario's limit (launch.cpp kTimeoutMarginMs)
IN_GAME_TIMEOUT = 'scenario exceeded its'
EXPECT_PREFIX = '# expect:'


def scenario_limit(scenario):
    """The scenario's time limit in seconds: its last "timeout <seconds>" line, default 60 (harness.cpp)."""
    limit = 60
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                m = re.match(r'\s*timeout\s+(\d+)', text)
                if m:
                    limit = int(m.group(1))
    return limit


def expected_result(scenario):
    """The result a scenario's line "# expect: <RESULT>" says it ends with (watchdog_*.txt: TIMEOUT), else None."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(EXPECT_PREFIX):
                    return text[len(EXPECT_PREFIX):].strip()
    return None


DROP_IN_PREFIX = '# drop-in:'


def drop_in(scenario):
    """Whether the scenario has a line "# drop-in: <why>": it checks the drop-in exe (S23a). ei_launch makes its runs
    drop-in runs (--drop-in); plan() runs it on both of the image's installs by default."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            return any(text.startswith(DROP_IN_PREFIX) for text in f)
    return False


def remove_drop_in_folder(out):
    """Removes a drop-in run's <out>/dropin (ei_launch's, left when it was killed) without following its junctions:
    each junction is removed itself, never the install folder it names."""
    def remove(path):
        for entry in os.scandir(path):
            if entry.is_junction():
                os.rmdir(entry.path)   # the junction, not the folder it names
            elif entry.is_symlink():
                (os.rmdir if entry.is_dir() else os.unlink)(entry.path)
            elif entry.is_dir():
                remove(entry.path)
            else:
                os.chmod(entry.path, 0o666)
                os.unlink(entry.path)
        os.rmdir(path)
    folder = os.path.join(out, 'dropin')
    with contextlib.suppress(OSError):
        if os.path.isjunction(folder) or os.path.islink(folder):
            os.rmdir(folder)
        elif os.path.isdir(folder):
            remove(folder)


def run(name, install, folder=None, root=ROOT, build=BUILD, take_slot=True, clock=None, game_env=None, held=1,
        slots=None):
    """One game of a scenario on an install (a pair's two games for a scenario with a "peer" line, the peer's in
    the run folder's peer\\): the image on standalone (the vanilla data) or standalone-rebirth. root: the checkout
    whose tests/scenarios it uses; build: the verify build folder (ei_launch.exe; the run folder is
    <build>/runs/<folder>).
    take_slot False: the caller already holds held machine slots for it (tools/work_sched.py); a pair whose caller
    holds one has ei_launch take the second. slots (with take_slot False): those slots as an object with release()
    (called once the game has ended: the frame packing runs without them, SV10h) and acquire() (before the game
    starts: waits until they are held again).
    clock overrides the scenario's clock (EI_HARNESS_CLOCK: 'virtual' or 'real', ',clocktrace',
    ',randtrace').
    game_env: None, the game inherits this process's environment; a dict (also {}), the game's EI_* variables
    are exactly these (this process's other game variables are dropped), for a scheduler running other
    submitters' batches. It goes to the subprocess only; os.environ stays as it is."""
    folder = folder or f'{name}-{install}'
    results = run_once(name, install, folder, root, build, take_slot, clock, game_env, held, slots)
    frame_store.pack_run(os.path.join(build, 'runs', folder))         # WB11: 4K frames stored compressed
    return results


# A game writes its run folder (harness_saves' texture cache, saves, frames): on a full disk a write fails and the game
# throws (CFileException diskFull: ticket X40, E: down to 20 MB with 16 games running). Below LOW_DISK_MB free a game
# waits up to LOW_DISK_WAIT_S for space, then the run fails without starting it.
LOW_DISK_MB = 1024
LOW_DISK_WAIT_S = 120


def free_mb(path):
    """Free megabytes on path's disk (None when unknown)."""
    try:
        return shutil.disk_usage(path).free // (1024 * 1024)
    except OSError:
        return None


def wait_for_disk(path, low=None, wait=None, poll=5.0):
    """None once path's disk has low MB free (default LOW_DISK_MB), else after wait seconds the failure line."""
    low = LOW_DISK_MB if low is None else low
    deadline = time.time() + (LOW_DISK_WAIT_S if wait is None else wait)
    while True:
        free = free_mb(path)
        if free is None or free >= low:
            return None
        if time.time() >= deadline:
            drive = os.path.splitdrive(os.path.abspath(path))[0] or path
            return (f'only {free} MB free on {drive} (a game needs {low} MB: its file writes fail on a full disk); '
                    'the game was not started')
        time.sleep(poll)


def crash_stack_note(out, build, root=ROOT):
    """The note under a crashed run's line: its stack by function names (tools/resolve_crash.py, X40a), the
    last frames before the crash; never fails the run."""
    try:
        import resolve_crash
        text = resolve_crash.named_stack(out, build=build, root=root, max_frames=10)
        return f'stack: {text}' if text else f'stack: not resolved (python tools/resolve_crash.py {os.path.relpath(out, root)})'
    except Exception as e:           # a tool problem must not hide the crash itself
        return f'stack: not resolved ({type(e).__name__}: {e})'


ASAN_FRAME = re.compile(r'^\s*#\d+ 0x[0-9a-f]+ in (\S+)')


def shown_path(path):
    """path relative to the checkout when it is inside it, else as it is."""
    with contextlib.suppress(ValueError):
        rel = os.path.relpath(path, ROOT)
        if not rel.startswith('..'):
            return rel
    return path


def asan_failure(out):
    """The failure line of a run of the AddressSanitizer build (build.cmd asan) that wrote a report (asan.<pid> in its
    folder or its peer's: tests/verify/image/asan_report.cpp): the error and the first frames; None without one."""
    for path in sorted(glob.glob(os.path.join(out, 'asan.*')) + glob.glob(os.path.join(out, 'peer', 'asan.*'))):
        with contextlib.suppress(OSError), open(path, errors='replace') as f:
            text = f.read()
        error = next((l.split('AddressSanitizer: ', 1)[1] for l in text.splitlines() if 'ERROR: AddressSanitizer' in l), None)
        if error is None:
            if text.strip():
                return f'AddressSanitizer: wrote {shown_path(path)}'
            continue
        frames = [m.group(1) for l in text.splitlines() if (m := ASAN_FRAME.match(l))]
        frames = [f for f in frames if not f.startswith(('__asan', '_asan', 'asan_', '__sanitizer'))][:5]
        return f'AddressSanitizer: {error.split(" on address")[0]} in {" <- ".join(frames)} [{shown_path(path)}]'
    return None


def run_once(name, install, folder, root, build, take_slot, clock, game_env, held=1, slots=None):
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(build, 'runs', folder or f'{name}-{install}')
    if install not in STANDALONE_INSTALLS:
        return 'FAIL', 0.0, [f'unknown install {install!r} (the image\'s: {", ".join(STANDALONE_INSTALLS)}; game.exe '
                             'is no longer run since H40)'], {}, []
    if low_disk := wait_for_disk(build):
        return 'FAIL', 0.0, [low_disk], {}, []
    remove_drop_in_folder(out)   # its junctions first: nothing may reach into the install
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'))   # "# prepare:" lines
    cmd = [os.path.join(build, 'ei_launch.exe'), '--harness', scenario, '--out', out]
    env = None
    if game_env is not None:
        env = {k: v for k, v in os.environ.items() if k not in game_environment()}
        env.update(game_env)
    if install == 'standalone':
        # Never the display mode: our renderer whatever the environment says. The image reads no game.exe (S18k1):
        # --game is the install whose data it plays (ei_launch sets EI_GAME_ROOT from it).
        cmd += ['--game', r'F:\games\EvilIslands', '--standalone']
    else:
        # ei_launch's default game folder is the Rebirth install's.
        cmd += ['--standalone']
        env = dict(os.environ if env is None else env, EI_REBIRTH=standalone_rebirth_flags(root))
    environ = os.environ if env is None else env
    if 'EI_RENDER' in environ:
        # The image's renderer options are its scenario's "render" step, never the environment's.
        env = {k: v for k, v in environ.items() if k != 'EI_RENDER'}
    if clock:
        env = dict(os.environ if env is None else env, EI_HARNESS_CLOCK=clock)
    games = games_of(scenario)
    if games > 1 and (take_slot or held >= games):
        env = dict(os.environ if env is None else env, EI_PAIR_SLOT_HELD='1')   # ei_launch takes no slot itself
    with machine_slot(count=games) if take_slot else contextlib.nullcontext():
        if slots:
            slots.acquire()
        start = time.time()
        # ei_launch ends a game at the limit plus its margin; this is for ei_launch itself (its job object
        # takes the game down with it).
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, encoding='cp1251', errors='replace', env=env,
                                  timeout=scenario_limit(scenario) + LIMIT_MARGIN_S + 90)
        except subprocess.TimeoutExpired as e:
            proc = subprocess.CompletedProcess(cmd, TIMEOUT_EXIT, '', f'ei_launch did not end in time; terminating it ({e})')
        finally:
            if slots:
                slots.release()       # the game has ended: the rest of the run needs no slot
        seconds = time.time() - start
    remove_drop_in_folder(out)   # a drop-in run's: ei_launch removes it, not when it was killed
    path = os.path.join(out, 'harness_report.txt')
    report = open(path, encoding='cp1251', errors='replace').read() if os.path.exists(path) else ''
    result = next((l[8:] for l in report.splitlines() if l.startswith('RESULT: ')), 'NO REPORT')
    failures = [l[8:] for l in report.splitlines() if l.startswith('FAILED: ')]
    # ei_launch's own output beside the run's log (C36b: a game that died with no line in ei-rebuild.log left a report
    # saying RUNNING and nothing else to tell why).
    with contextlib.suppress(OSError):
        with open(os.path.join(out, 'ei_launch.txt'), 'w', encoding='utf8', errors='replace') as f:
            f.write(f'exit code {proc.returncode}\n{proc.stdout}{proc.stderr}')
    if result == 'RUNNING' and not failures and proc.returncode not in (TIMEOUT_EXIT, UNKILLABLE_EXIT):
        failures = [f'the game ended before the harness wrote its result (ei_launch exit code {proc.returncode}; '
                    'its output in ei_launch.txt)']
    # The per-function totals (verify wrappers, until S21d); the slot stays in the result (the work database's run
    # rows keep its shape), always empty.
    counts = {}
    notes = [l for l in (proc.stdout + proc.stderr).splitlines()
             if 'terminating' in l or 'not found' in l or 'open window' in l or 'still alive' in l
             or l.startswith('ei-rebuild: ') and 'imports resolved, 0 missing' not in l]
    if proc.returncode in (TIMEOUT_EXIT, UNKILLABLE_EXIT) or any(f.startswith(IN_GAME_TIMEOUT) for f in failures):
        result = 'TIMEOUT'
        if not failures:
            failures = [f'scenario exceeded its {scenario_limit(scenario)} s time limit; ei_launch ended the game'
                        + ('' if proc.returncode != UNKILLABLE_EXIT else ' (it did not die: a thread is stuck in the kernel)')]
        if proc.returncode == UNKILLABLE_EXIT:
            failures.append('the game process is still alive after TerminateProcess')
    if any('the game crashed' in f or 'no code at' in f for f in failures):
        notes.append(crash_stack_note(out, build, root))
    if asan := asan_failure(out):
        failures.append(asan)
        if result == 'PASS':
            result = 'FAIL'
    expected = expected_result(scenario)
    if expected and result == expected and proc.returncode != UNKILLABLE_EXIT:
        result = 'PASS'
        failures = []
        notes.append(f'ended as the scenario expects ({expected})')
    notes += [l for l in report.splitlines() if l.startswith('EXIT: ')]   # a message box after quit
    notes += [l for l in report.splitlines() if l.startswith('WINDOW: in sight')]   # WB12: a window the user saw
    notes += [l for l in report.splitlines() if l.startswith('NET: outside')]   # WB14: a bind the firewall asks about
    return result, seconds, failures, counts, notes


def main_build_dir():
    """<main checkout>/build (shared by every worktree), or None outside a repository."""
    try:
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        return None
    return os.path.join(os.path.dirname(os.path.normpath(common)), 'build') if common else None


def console_of(folder, build=BUILD):
    """The console output a run's report holds (its lines after "console output:")."""
    path = os.path.join(build, 'runs', folder, 'harness_report.txt')
    lines = open(path, encoding='cp1251', errors='replace').read().splitlines() if os.path.exists(path) else []
    lines = lines[lines.index('console output:') + 1:] if 'console output:' in lines else []
    return [l for l in lines if l.startswith('  ')]


ATTEMPT_FILES = ('ei-rebuild.log', 'harness_report.txt')


def keep_attempt(folder, attempt, build=BUILD):
    """Keep a failed attempt before the retry empties its run folder: the whole folder is renamed to
    <build>/runs/<folder>-attempt<attempt> (replaced; no copy, so it also works on a full disk), or, when the rename
    fails (a file still open), its log and report are copied there. Returns that folder's name, or None when nothing
    was kept."""
    source = os.path.join(build, 'runs', folder)
    kept = f'{folder}-attempt{attempt}'
    target = os.path.join(build, 'runs', kept)
    shutil.rmtree(target, ignore_errors=True)
    for _ in range(3):
        try:
            os.rename(source, target)
            return kept
        except FileNotFoundError:
            return None
        except OSError:
            time.sleep(1)
    copied = False
    for name in ATTEMPT_FILES:
        with contextlib.suppress(OSError):
            os.makedirs(target, exist_ok=True)
            shutil.copy2(os.path.join(source, name), os.path.join(target, name))
            copied = True
    return kept if copied else None


def run_with_retries(name, install, retries, folder=None, runner=run, keep=None):
    """run() again after a failure, up to retries times. runner: run() or a stand-in with its arguments.
    keep(folder, attempt) -> the name of a folder that now holds the failed attempt's log and report (keep_attempt),
    called before each retry (a retry empties the run folder); a retry that passes names it in its failures."""
    result, seconds, failures, counts, notes = runner(name, install, folder)
    first_failures = failures
    first_log = None
    for attempt in range(1, retries + 1):
        if result == 'PASS':
            break
        kept = keep(folder or f'{name}-{install}', attempt) if keep else None
        if attempt == 1:
            first_log = kept
        result, seconds, failures, counts, notes = runner(name, install, folder)
        if result == 'PASS':
            result = 'PASS*'
            failures = ['first run: ' + '; '.join(first_failures[:2]) + (f' (log kept in runs/{first_log})' if first_log else '')]
    return result, seconds, failures, counts, notes


COMPARE_JOBS = 8     # compare_frames.py processes at once (each one CPU core, ~1 GB for three 4K frames)


# ---- --widescreen: the image's frames against addon's WideScreen (S21b)
#
# Rebirth's WideScreen is addon.dll's code, on the Rebirth install only. Its frames were recorded with the original
# game's harness until H40 (game.exe with addon, drawn by dgVoodoo at the desktop's mode) and are kept as references;
# `--widescreen` runs the image on the Rebirth install ("<scenario>-standalone-rebirth", our renderer) and compares
# its frames with them. The references live in <main checkout>/build/widescreen/<scenario>/<steps key>/: frames/<frame>.bmp.z (the frames, packed as
# tools/frame_store.py packs a run's), skip/<frame>.bmp.z (what moved in addon's run between <frame> and its
# _later / _moving twins, left out of the comparison) and recorded.json. The key is the scenario's steps (its lines
# without comments): a changed step has no references any more, a comment keeps them.

WIDESCREEN_SIZE = (3840, 2160)      # the frame size WIDESCREEN_AREAS' rectangles are given at (scaled to others)
# compare_frames.py's settings for our renderer against dgVoodoo (docs/notes/standalone-verification.md,
# "WideScreen references", S21b: measured over the four scenarios' 19 frames). A pixel differs when a channel lies more than 8 outside the range of the other
# frame's pixels around it (--shift 1: the renderers sample magnified textures a fraction of a texel apart and
# rasterise lines, model and alpha-tested edges a pixel apart); a frame fails when more than 0.6% of its pixels
# differ (the scene's edges: at most 0.39%, the menu's trees) or more than a quarter of a 64-pixel block does
# (a moved element; at most 11% seen, the options window's slider thumbs).
WIDESCREEN_COMPARE = ['--tolerance', '8', '--shift', '1', '--max-fraction', '0.006', '--block', '64',
                      '--max-block', '0.25']
# Areas that differ between the renderers whatever WideScreen does, left out of the comparison:
# {(scenario or '*', frame or '*'): [((x0, y0, x1, y1) in a WIDESCREEN_SIZE frame, x1 and y1 excluded, why), ...]}.
WIDESCREEN_AREAS = {
    ('ui_widescreen_more', 'save'): [((1954, 360, 3002, 1154), 'the Save window\'s picture: a copy of the screen, '
                                                               'scaled down by each renderer')],
}


def widescreen_dir():
    """<main checkout>/build/widescreen (shared by every worktree), or None outside a repository. EI_WIDESCREEN_DIR:
    another folder (tests)."""
    if os.environ.get('EI_WIDESCREEN_DIR'):
        return os.environ['EI_WIDESCREEN_DIR']
    base = main_build_dir()
    return os.path.join(base, 'widescreen') if base else None


def steps_key(scenario):
    """The key of a scenario's widescreen references: its steps (every line but comments and blank ones)."""
    h = hashlib.sha1()
    with open(scenario, encoding='utf-8', errors='replace') as f:
        for text in f:
            text = text.strip()
            if text and not text.startswith('#'):
                h.update(text.encode() + b'\n')
    return h.hexdigest()[:12]


def widescreen_reference(name, root=ROOT):
    """The folder of the scenario's recorded references for its current steps, or None when there is none."""
    base = widescreen_dir()
    if not base:
        return None
    folder = os.path.join(base, name, steps_key(os.path.join(root, 'tests', 'scenarios', name + '.txt')))
    return folder if os.path.exists(os.path.join(folder, 'recorded.json')) else None


def primary_frames(folder):
    """The frames of a frames folder that are compared: not the _later / _moving twins."""
    names = [f[:-4] for f in frame_store.frame_names(folder)] if os.path.isdir(folder) else []
    return [f for f in names if not f.endswith(('_later', '_moving'))]


def twin_excludes(frames, frame):
    """compare_frames.py's --exclude pairs for <frame> of a frames folder: what moved between <frame> and
    <frame>_later (captured after the clocks ran on: the hero, his portrait, the menu's creatures), and between
    <frame>_moving and <frame>_moving_later (captured before a window that pauses the world opened)."""
    args = []
    for x, y in ((frame, frame + '_later'), (frame + '_moving', frame + '_moving_later')):
        a, b = os.path.join(frames, x + '.bmp'), os.path.join(frames, y + '.bmp')
        if frame_store.exists(a) and frame_store.exists(b):
            args += ['--exclude', a, b]
    return args


def run_compares(commands, out):
    """compare_frames.py's commands, COMPARE_JOBS at once (4K frames take 20-40 s each in Python), their output
    printed in order. True when every one passed."""
    with concurrent.futures.ThreadPoolExecutor(max_workers=COMPARE_JOBS) as pool:
        done = list(pool.map(lambda cmd: subprocess.run(cmd, capture_output=True, text=True, errors='replace'), commands))
    ok = True
    for p in done:
        out.write(p.stdout + p.stderr)
        ok = p.returncode == 0 and ok
    out.flush()
    return ok


def run_passed(folder):
    """Whether the run folder's report says PASS."""
    with contextlib.suppress(OSError):
        with open(os.path.join(folder, 'harness_report.txt'), encoding='cp1251', errors='replace') as f:
            return 'RESULT: PASS' in f.read().splitlines()
    return False


def widescreen_areas(name, frame, size):
    """compare_frames.py's --exclude-rect arguments for WIDESCREEN_AREAS' rectangles of a frame, scaled to its size."""
    args = []
    width, height = size or WIDESCREEN_SIZE
    for key in ((name, frame), (name, '*'), ('*', '*')):
        for (x0, y0, x1, y1), _ in WIDESCREEN_AREAS.get(key, []):
            args += ['--exclude-rect', str(x0 * width // WIDESCREEN_SIZE[0]), str(y0 * height // WIDESCREEN_SIZE[1]),
                     str(-(-x1 * width // WIDESCREEN_SIZE[0])), str(-(-y1 * height // WIDESCREEN_SIZE[1]))]
    return args


def compare_widescreen(name, install='standalone-rebirth', out=None, root=ROOT, build=BUILD):
    """--widescreen: the image's run <name>-<install> against the scenario's references, frame by frame
    (tools/compare_frames.py with WIDESCREEN_COMPARE), without what moved in either run between a frame and its
    twins and without WIDESCREEN_AREAS. The differences go to the run's frames_diff (packed).
    out: where the comparisons print (default stdout)."""
    out = out or sys.stdout
    reference = widescreen_reference(name, root)
    if not reference:
        print(f'no references of {name} for its steps (recorded from game.exe until H40; a scenario whose steps changed '
              'since has none)', file=out)
        return False
    with open(os.path.join(reference, 'recorded.json')) as f:
        info = json.load(f)
    print(f'(references recorded {info.get("time", "?")}: {reference})', file=out)
    ours = os.path.join(build, 'runs', f'{name}-{install}', 'frames')
    with contextlib.suppress(OSError, struct.error):
        width, height = struct.unpack_from('<ii', frame_store.read_frame(os.path.join(ours, info['frames'][0] + '.bmp')), 18)
        if info.get('size') and [width, abs(height)] != info['size']:
            print(f'the references are {info["size"][0]}x{info["size"][1]}, the run\'s frames {width}x{abs(height)} (the '
                  'desktop\'s mode changed?)', file=out)
            return False
    diffs = os.path.join(build, 'runs', f'{name}-{install}', 'frames_diff')
    shutil.rmtree(diffs, ignore_errors=True)
    os.makedirs(diffs)
    tool = os.path.join(root, 'tools', 'compare_frames.py')
    commands = []
    for frame in info['frames']:
        a, b = os.path.join(ours, frame + '.bmp'), os.path.join(reference, 'frames', frame + '.bmp')
        cmd = [sys.executable, tool, a, b, *WIDESCREEN_COMPARE, '--worst', '4', '--diff', os.path.join(diffs, frame + '.bmp')]
        skip = os.path.join(reference, 'skip', frame + '.bmp')
        if frame_store.exists(skip):
            cmd += ['--exclude-mask', skip]
        excludes = twin_excludes(ours, frame)
        if excludes:
            cmd += excludes + ['--exclude-grow', '8']
        commands.append(cmd + widescreen_areas(name, frame, info.get('size')))
    ok = run_compares(commands, out)
    frame_store.pack_folder(diffs)
    return ok


def widescreen_check(jobs, out=None, root=ROOT, build=BUILD):
    """After --widescreen's runs: compares the image's runs with addon's frames. Prints each scenario's frames and a
    verdict; True when every one matched."""
    out = out or sys.stdout
    ok = True
    for name, install in jobs:
        print(f'\nframes of {name}: the image ({name}-{install}) against addon\'s', file=out)
        out.flush()
        ok = compare_widescreen(name, install, out, root, build) and ok
    print('WIDESCREEN FRAMES MATCH' if ok else 'WIDESCREEN FRAMES DIFFER', file=out)
    return ok


def standalone_rebirth_flags(root=ROOT):
    """EI_REBIRTH for the standalone image on the Rebirth install: the patch names config/rebirth_profile.ini sets
    true, comma-separated (S19). The scheduler runs main's run_all on every worker's checkout: one from before C66a has
    config/ei_rebirth_standalone.ini instead (its names set to 1; C66b drops this once no such branch is left)."""
    path, value = os.path.join(root, 'config', 'rebirth_profile.ini'), 'true'
    if not os.path.exists(path):
        path, value = os.path.join(root, 'config', 'ei_rebirth_standalone.ini'), '1'
    names = []
    with open(path, encoding='utf-8') as f:
        for text in f:
            m = re.match(r'\s*(\w+)\s*=\s*' + value + r'\s*$', text)
            if m and m.group(1) not in ('Default', 'NPlayers', 'Pause'):
                names.append(m.group(1))
    return ','.join(names)


# A scenario line that makes the run depend on the install (SV9): a step for one install only, Rebirth's flags, its
# mod folder or WideScreen switch.
REBIRTH_LINE = re.compile(r'^\s*(?:rebirth|vanilla|rebirthflags|addonpath|widescreen)\s', re.M)


def rebirth_scenarios(names, root=ROOT):
    """The Rebirth smoke set among names (SV9: Rebirth's data only where it decides something): the scenarios
    config/rebirth_scenarios.csv marks 1 (Rebirth's maps, scripts, resources) and those with a REBIRTH_LINE."""
    path = os.path.join(root, 'config', 'rebirth_scenarios.csv')
    marked = set()
    with contextlib.suppress(OSError), open(path, encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2 and parts[1] == '1':
                marked.add(parts[0])
    smoke = set()
    for n in names:
        if n in marked:
            smoke.add(n)
            continue
        with contextlib.suppress(OSError):
            with open(os.path.join(root, 'tests', 'scenarios', n + '.txt'), encoding='utf-8', errors='replace') as f:
                if REBIRTH_LINE.search(f.read()):
                    smoke.add(n)
    return smoke


def parse_args(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--vanilla-only', action='store_true')
    ap.add_argument('--rebirth', action='store_true')
    ap.add_argument('--rebirth-only', action='store_true')
    ap.add_argument('--all-rebirth', action='store_true')
    ap.add_argument('--standalone', action='store_true')
    ap.add_argument('--widescreen', action='store_true')
    ap.add_argument('--golden', action='store_true')
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--clock')
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--x64', action='store_true')
    return ap.parse_args(argv)


def build_of(args):
    """The verify build args' runs use: build\\verify-x64 with --x64 (BUILD_X64), else build\\verify (BUILD)."""
    return BUILD_X64 if getattr(args, 'x64', False) else BUILD


def has_golden(name, install, root=ROOT):
    """Whether tests/golden holds a golden of the scenario for install ('vanilla' or 'rebirth')."""
    return os.path.exists(os.path.join(root, 'tests', 'golden', name, install, 'golden.json'))


def plan(args, root=ROOT):
    """The runs args ask for: (scenario, install) pairs in the summary's order. install is standalone (the vanilla
    data) or standalone-rebirth; the run folder is <scenario>-<install>. Also used by tools/work_sched.py
    (`work.py test`), so a batch holds the runs run_all would make."""
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(root, 'tests', 'scenarios', '*.txt')))
    if args.widescreen and args.vanilla_only:
        sys.exit("--widescreen compares the image on the Rebirth install with addon's recorded frames: not with "
                 '--vanilla-only')
    installs = ['standalone'] if args.vanilla_only else ['standalone-rebirth'] if args.rebirth_only else list(STANDALONE_INSTALLS)
    # The Rebirth data (SV9): only on demand: --rebirth / --rebirth-only the named scenarios or the smoke set,
    # --all-rebirth every one.
    if args.all_rebirth or ((args.rebirth or args.rebirth_only) and args.scenarios):
        rebirth = set(names)
    elif args.rebirth or args.rebirth_only:
        rebirth = rebirth_scenarios(names, root)
    else:
        rebirth = set()
    path = lambda name: os.path.join(root, 'tests', 'scenarios', name + '.txt')
    # The drop-in exe's scenarios (S23a): the image only, on both installs unless one is asked for alone.
    drop_ins = {n for n in names if drop_in(path(n))}
    if args.widescreen:
        for n in sorted(drop_ins):
            print(f'{n}: left out, a drop-in scenario ("{DROP_IN_PREFIX}") runs the image\'s exe in a copy of the install',
                  file=sys.stderr)
        names = [n for n in names if n not in drop_ins]
        drop_ins = set()
    drop_in_installs = installs if args.vanilla_only or args.rebirth_only else list(STANDALONE_INSTALLS)
    if args.rebirth_only:
        names = [n for n in names if n in rebirth or n in drop_ins]
    jobs = [(name, install) for name in names for install in (drop_in_installs if name in drop_ins else installs)
            if install not in ('rebirth', 'standalone-rebirth') or name in rebirth or name in drop_ins]
    if args.widescreen:
        # addon's WideScreen exists on the Rebirth install only
        names = args.scenarios or WIDESCREEN_SCENARIOS
        jobs = [(name, 'standalone-rebirth') for name in names]
    return jobs


def load_times(build=BUILD):
    try:
        return json.load(open(os.path.join(build, 'run_times.json')))
    except (OSError, ValueError):
        return {}


def save_times(seconds, build=BUILD):
    """seconds: {'<scenario>-<install>': seconds}, merged into the build's run_times.json."""
    times = load_times(build)
    times.update({k: round(v, 1) for k, v in seconds.items()})
    with contextlib.suppress(OSError):
        json.dump(times, open(os.path.join(build, 'run_times.json'), 'w'), indent=0, sort_keys=True)


def timing_db():
    """The machine-wide work database (tools/work.py, table timing) for this process's expected times and
    their recording, or None: a run never fails because of it."""
    try:
        import work
        return work.connect()
    except (Exception, SystemExit):
        return None


def expected_times(jobs, root=ROOT, build=BUILD, db=None):
    """{(scenario, install): expected seconds or None}: the machine-wide timings (work.expected_times), then the
    build's run_times.json."""
    fallbacks = load_times(build)
    if db is not None:
        try:
            import work
            return work.expected_times(db, root, jobs, fallbacks)
        except Exception:
            pass
    return {(n, i): fallbacks.get(f'{n}-{i}') for n, i in jobs}


def record_time(db, name, install, seconds, result, root=ROOT, batch=None, notes=()):
    """Appends a finished game's time to the machine-wide table; never raises. Crashed and timed-out runs (ERROR, NO
    REPORT, TIMEOUT) are not times."""
    if db is None or result in ('ERROR', 'NO REPORT', 'TIMEOUT'):
        return
    try:
        import work
        work.record_time(db, name, install, root, seconds, batch)
    except Exception:
        pass


HEADER = f'{"scenario":<18} {"install":<8} {"result":<10} {"time":>6}  failures'


def line(name, install, result, seconds, failures, notes, root=ROOT, build=BUILD):
    """A run's line in the table (and under "not passed:")."""
    detail = '; '.join(failures[:3]) + (f' (+{len(failures) - 3} more)' if len(failures) > 3 else '')
    if result not in ('PASS', 'PASS*'):
        detail += f'  [{os.path.relpath(os.path.join(build, "runs", f"{name}-{install}"), root)}]'
    text = f'{name:<18} {install:<8} {result:<10} {seconds:5.0f}s  {detail}'
    for n in notes:
        text += f'\n{"":<38}{n}'
    return text


def summary(jobs, results, args, elapsed, out=None, root=ROOT, build=BUILD):
    """Prints what follows the table: runs not passed, message boxes after quit and the totals line
    (--widescreen's frames and --golden come after it). results: {(scenario, install): (result, seconds,
    failures, notes)} for every job. True when every run passed."""
    out = out or sys.stdout
    ok = all(r[0] in ('PASS', 'PASS*') for r in results.values())
    failed = [(n, i) for n, i in jobs if results[(n, i)][0] not in ('PASS', 'PASS*')]
    if failed and len(jobs) > 1:
        print('\nnot passed:', file=out)
        for n, i in failed:
            print(line(n, i, *results[(n, i)], root=root, build=build), file=out)
    exits = [(n, i) for n, i in jobs if any(x.startswith('EXIT: ') for x in results[(n, i)][3])]
    if exits:
        print(f'\n{len(exits)} run(s) showed a message box after the scenario quit (EXIT: lines; stack in ei-rebuild.log): '
              + ', '.join(f'{n}-{i}' for n, i in exits), file=out)
    window_check(jobs, out, build)
    net_check(jobs, out, build)
    print(f'\n{len(jobs)} runs in {elapsed:.0f}s with {args.jobs} at once', file=out)
    print('ALL PASS' if ok else 'SOME RUNS FAILED', file=out)
    return ok


def window_check(jobs, out, build=BUILD):
    """WB12, WB12a: harness runs (the image's and the original's) keep the game's windows out of sight (cloaked tool
    windows, never in front); each report's WINDOW: line says whether any check (harness.cpp, noforeground::Watch)
    saw one. Prints how many runs had the check and the runs that saw a window."""
    lines = {}
    for name, install in jobs:
        path = os.path.join(build, 'runs', f'{name}-{install}', 'harness_report.txt')
        if os.path.exists(path):
            text = open(path, encoding='cp1251', errors='replace').read()
            lines[(name, install)] = next((l for l in text.splitlines() if l.startswith('WINDOW: ')), None)
    checked = {j: l for j, l in lines.items() if l}
    if not checked:
        return
    seen = [(j, l) for j, l in checked.items() if not l.startswith('WINDOW: out of sight')]
    print(f'\ngame windows: {len(checked) - len(seen)} of {len(checked)} checked runs kept every window out of sight '
          'and out of the foreground', file=out)
    for (name, install), l in sorted(seen):
        print(f'  {name}-{install}: {l[8:]}', file=out)


def net_check(jobs, out, build=BUILD):
    """WB14: harness games bind on 127.0.0.1 only, so Windows Firewall never asks about them; each report's NET:
    line (tests/verify/harness_net.h) says whether the binds and the endpoint checks saw an address outside
    loopback. Prints how many runs had the check and the runs that saw one."""
    lines = {}
    for name, install in jobs:
        path = os.path.join(build, 'runs', f'{name}-{install}', 'harness_report.txt')
        if os.path.exists(path):
            text = open(path, encoding='cp1251', errors='replace').read()
            lines[(name, install)] = next((l for l in text.splitlines() if l.startswith('NET: ')), None)
    checked = {j: l for j, l in lines.items() if l}
    if not checked:
        return
    outside = [(j, l) for j, l in checked.items() if not l.startswith('NET: loopback only')]
    print(f'network: {len(checked) - len(outside)} of {len(checked)} checked runs bound nothing outside 127.0.0.1',
          file=out)
    for (name, install), l in sorted(outside):
        print(f'  {name}-{install}: {l[5:]}', file=out)


GOLDEN_INSTALL = {'standalone': 'vanilla', 'standalone-rebirth': 'rebirth'}   # a run's install -> its goldens'


def golden_check(jobs, out=None, root=ROOT, build=BUILD):
    """--golden: every run of jobs whose scenario has a golden for its install against it (tools/golden.py
    compare). Prints the runs that differ and a count; True when none differs."""
    import golden
    out = out or sys.stdout
    counts = collections.Counter()
    print('\ngoldens (tests/golden):', file=out)
    for name, install in jobs:
        folder = os.path.join(build, 'runs', f'{name}-{install}')
        result, lines = golden.compare_one(name, GOLDEN_INSTALL.get(install, install), folder, root=root)
        counts[result] += 1
        if result != 'NO GOLDEN' and (result != 'SAME' or len(jobs) == 1):
            print(f'  {name:<18} {install:<8} {result}', file=out)
            for text in lines:
                print(f'      {text}', file=out)
    print(f'  {counts["SAME"]} the same, {counts["DIFFERS"]} differ, {counts["NO RUN"]} without a run, '
          f'{counts["NO GOLDEN"]} without a golden', file=out)
    ok = not counts['DIFFERS'] and not counts['NO RUN']
    print('GOLDENS THE SAME' if ok else 'GOLDENS DIFFER', file=out)
    return ok


def main():
    args = parse_args()
    if args.golden:
        os.environ['EI_DIGEST'] = str(GOLDEN_PERIOD)
    jobs = plan(args)
    build = build_of(args)
    db = timing_db()
    times = expected_times(jobs, build=build, db=db)
    order = sorted(jobs, key=lambda j: -(times[j] if times[j] is not None else 1000))   # new scenarios first

    results = {}
    lock = threading.Lock()
    started = time.time()
    print(HEADER)
    sys.stdout.flush()

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(run_with_retries, n, i, args.retries, f'{n}-{i}',
                               functools.partial(run, build=build, clock=args.clock),
                               functools.partial(keep_attempt, build=build)): (n, i)
                   for n, i in order}
        for future in concurrent.futures.as_completed(futures):
            name, install = futures[future]
            result, seconds, failures, _, notes = future.result()
            with lock:
                results[(name, install)] = (result, seconds, failures, notes)
                record_time(db, name, install, seconds, result, notes=notes)
                print(line(name, install, result, seconds, failures, notes, build=build))
                sys.stdout.flush()

    save_times({f'{n}-{i}': r[1] for (n, i), r in results.items()}, build=build)
    ok = summary(jobs, results, args, time.time() - started, build=build)
    if args.widescreen:
        ok = widescreen_check(jobs, build=build) and ok
    if args.golden:
        ok = golden_check(jobs, build=build) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
