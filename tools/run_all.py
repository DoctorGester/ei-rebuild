"""Runs every harness scenario on the vanilla install and under Rebirth and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only | --all-rebirth | --native | --standalone]
                               [--no-hooks]
                               [--dgvoodoo] [--fresh] [--jobs N] [--retries N] [--clock virtual|real[,...]] [--golden]
                               [--verified-candidates N]
       python tools/run_all.py --widescreen [scenario names...]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
                   Every scenario runs on vanilla; under Rebirth only those config/rebirth_scenarios.csv
                   marks (tools/rebirth_coverage.py: the fewest that reach every function Rebirth patches
                   twice) and any it does not list yet. Named scenarios and --all-rebirth run under both.
  --native         only the install without dgVoodoo (run\\native, tools/setup_native_run.cmd), with
                   EI_RENDER=new,present: the game has no DirectDraw or Direct3D 7 device and the new
                   renderer presents through its own swap chain (docs/notes/renderer.md section 9);
                   scenarios that need the old device (below) run there too: their old-path steps
                   do nothing, and what is left checks the new frame alone
  --standalone     the standalone image instead of game.exe (ei_launch --standalone: build\\verify\\ei_standalone.exe
                   lays out run\\vanilla's game.exe without its code and loads ei_hook.dll; docs/notes/standalone.md)
                   on the vanilla install's data, with EI_RENDER=new,present like --native; install "standalone",
                   run folders <scenario>-standalone. Without scenario names only the boot scenario (main menu,
                   then quit). An address with no rewrite ends the run: "standalone: no code at ..." in failures
  renderers        Runs of our code (vanilla and Rebirth with hooks) draw with our renderer
                   (EI_RENDER=new,present: no DirectDraw device, its own swap chain, no display mode
                   change), so they never need the machine to themselves. dgVoodoo draws only the
                   original code (--no-hooks, --widescreen's rebirth-nohooks, compare.py's original
                   side) and scenarios whose "render" step leaves out "present" (they compare the old
                   path's frame with the new one in one run), and --widescreen's runs with hooks too
                   (their frames are compared with the original's pixel by pixel; across renderers
                   they differ, wants_dgvoodoo). A dgVoodoo run with a "resolution" step runs alone.
                   EI_RENDER in the environment is kept as it is (with hooks; the original code
                   never gets it).
  --dgvoodoo       runs of our code on dgVoodoo as before R10 (the old device path, and the verify
                   wrappers that compare with the original's DirectDraw calls)
  --fresh          play the original code's runs again instead of taking them from the reference
                   cache (build\\reference in the main checkout: a passed run of the original code,
                   kept by scenario commit, install files, clock and EI_* settings; reference_key)
  --widescreen     Rebirth's WideScreen against our rewrite of it (docs/notes/rebirth.md, "WideScreen"):
                   the scenarios (default ui_widescreen, ui_widescreen_more, ui_widescreen_hud and
                   ui_widescreen_screens; they
                   set the wide mode with the harness's "resolution" / "widescreen" steps) under Rebirth
                   twice, with our hooks
                   (<scenario>-rebirth) and without (<scenario>-rebirth-nohooks: addon's code only),
                   then tools/compare_frames.py on the two runs' frames
  --jobs N         games running at once (default 16); all runs on the machine together never
                   exceed EI_HARNESS_SLOTS (default 16) games, whatever --jobs says. Each run writes to its own folder,
                   build\\verify\\runs\\<scenario>-<install> (ei_hook.log, harness_report.txt and
                   harness_saves, emptied first, so every run starts without saves or texture cache).
  --retries N      re-run a failed scenario up to N times (default 1); fights against spawned
                   enemies are random, and the hero occasionally loses one on the original too.
                   A pass after a retry is reported as "PASS*".
  --clock virtual|real[,clocktrace][,randtrace]
                   run every scenario on the virtual clock or on real time, whatever its own lines say
                   (EI_HARNESS_CLOCK; tests/verify/harness_clock.h), with the clock or random trace
  --golden         runs the games with a state digest (EI_DIGEST, the goldens' period) and afterwards compares
                   every run that has a golden (tests/golden/<scenario>/<install>, tools/golden.py) with it:
                   the console, the digest records, the dumps and the saves. Native runs compare with the vanilla
                   golden; Rebirth runs with the Rebirth golden, also with the flags off (EI_REBIRTH=FollowAddon=0:
                   the rewrites of what addon.dll patches are not installed, so addon's code runs)
  --verified-candidates N
                   afterwards, list rewritten functions with at least N verify calls and no
                   mismatch in any run that are not yet in tests/verify/verified.txt
exit code: 0 when every run passed

A run that outlives the scenario's time limit ("timeout <seconds>", default 60 s) is reported as TIMEOUT: the
game's watchdog ends it at the limit, ei_launch 20 s later if the watchdog could not (tools/launcher/launch.cpp).
A scenario with a line "# expect: TIMEOUT" passes when it times out (tests/scenarios/watchdog_*.txt check this).
"""
import argparse, functools, collections, concurrent.futures, contextlib, glob, hashlib, json, msvcrt, os, re, shutil, subprocess, sys, tempfile, threading, time

import prepare_scenario

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDESCREEN_SCENARIOS = ['ui_widescreen', 'ui_widescreen_more', 'ui_widescreen_hud', 'ui_widescreen_screens']   # --widescreen without scenario names
STANDALONE_SCENARIOS = ['boot']   # --standalone without scenario names
BUILD = os.environ.get('EI_BUILD_DIR') or os.path.join(ROOT, 'build', 'verify')   # EI_BUILD_DIR: another verify build (timing runs)
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
def machine_slot(alone=False):
    """One of the machine's MACHINE_SLOTS game slots (shared by every checkout's run_all), or all of
    them (alone): for a game that sets the display mode, which takes the surfaces of every other
    game running then (they fail with DDERR_SURFACELOST: "Can't begin scene", untextured models).
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
                f = _lock(os.path.join(SLOT_DIR, f'slot{i:02}'))
                if f:
                    held.append((i, f))
                    if not alone:
                        break
            if len(held) == (MACHINE_SLOTS if alone else 1):
                break
            time.sleep(0.5)
        yield
    finally:
        for _, f in held:
            _unlock(f)
        if gate:
            _unlock(gate)


@functools.lru_cache(maxsize=None)
def run_dir(root):
    """root's run dir (the installs' game.exe copies). A worktree without its own (a worker that forgot the junction,
    so every game ended NO REPORT) uses the main checkout's: its .git file names <main>/.git/worktrees/<name>."""
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


def switches_display(scenario):
    """Whether a scenario asks for full screen at a mode other than the desktop's (`resolution WxH` with
    `fullscreen 1`). Only our renderer covers the monitor without changing the mode; the original's DirectDraw,
    under dgVoodoo or not, switches the real display, which upsets the user's desktop. So such a scenario runs
    only on our renderer (plan() leaves out its runs without our hooks, with --dgvoodoo or with an EI_RENDER
    without present)."""
    with open(scenario, encoding='cp1251', errors='replace') as f:
        lines = f.read().splitlines()
    mode = any(re.match(r'\s*resolution\s+(?!desktop\b)\S', l) for l in lines)
    return mode and any(re.match(r'\s*fullscreen\s+1\b', l) for l in lines)


def sets_display_mode(scenario):
    """Whether a scenario sets the display mode (a `resolution` step): on dgVoodoo it runs with no other game."""
    with open(scenario, encoding='cp1251', errors='replace') as f:
        return any(re.match(r'\s*resolution\b', line) for line in f)


# Our renderer's switch-over (docs/notes/renderer.md section 9): no DirectDraw or Direct3D 7 device, the frame
# presented through its own swap chain, full screen without a display mode change.
OUR_RENDERER = 'new,present'


def render_steps(scenario):
    """The word lists of a scenario's `render` steps."""
    steps = []
    with contextlib.suppress(OSError):
        with open(scenario, encoding='cp1251', errors='replace') as f:
            for text in f:
                m = re.match(r'\s*render\s+(\S+)', text)
                if m:
                    steps.append(m.group(1).split(','))
    return steps


def needs_old_device(scenario):
    """Whether a scenario's `render` step leaves out `present`: it compares the old path's frame (drawn through
    dgVoodoo) with the new renderer's in one run, so it runs on dgVoodoo."""
    return any('present' not in words for words in render_steps(scenario))


def our_renderer(scenario, install, hooks, dgvoodoo=False, environ=None):
    """Whether a run draws with our renderer (EI_RENDER=new,present) rather than dgVoodoo (ticket R10). native
    always; the original code (no hooks) never; otherwise EI_RENDER in environ (default: this process's) decides
    when it is set, else our renderer unless dgvoodoo (--dgvoodoo) or the scenario needs the old device. A
    scenario whose `render` step says `present` draws with ours whatever else is set (the harness adds EI_RENDER's
    words to the step's)."""
    if install in ('native', 'standalone'):
        return True
    if not hooks:
        return False
    if any('new' in words and 'present' in words for words in render_steps(scenario)):
        return True
    environ = os.environ if environ is None else environ
    if 'EI_RENDER' in environ:
        return 'present' in environ['EI_RENDER'].split(',')
    return not dgvoodoo and not needs_old_device(scenario)


def runs_alone(scenario, install, hooks, dgvoodoo=False, environ=None):
    """A run that keeps the machine to itself: a dgVoodoo run that sets the display mode. Our renderer changes
    no display mode (full screen is a window over the monitor), so its runs share the machine."""
    return sets_display_mode(scenario) and not our_renderer(scenario, install, hooks, dgvoodoo, environ)


# EI_* variables that are not settings of a game: where the build is, the machine's slots, and what ei_launch
# sets itself. Every other EI_* variable (EI_PROFILE, EI_PLAIN, EI_RENDER, EI_COVER, ...) is read by a game.
NOT_GAME_ENV = ('EI_BUILD_DIR', 'EI_HARNESS_SLOTS', 'EI_OUT_DIR', 'EI_HARNESS')


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


def run(name, install, hooks, folder=None, root=ROOT, build=BUILD, take_slot=True, clock=None, game_env=None,
        dgvoodoo=False, fresh=False):
    """One game of a scenario on an install. root: the checkout whose tests/scenarios and run/ it uses;
    build: the verify build folder (ei_launch.exe; the run folder is <build>/runs/<folder>).
    take_slot False: the caller already holds a machine slot for it (tools/work_sched.py).
    clock overrides the scenario's clock (EI_HARNESS_CLOCK: 'virtual' or 'real', ',clocktrace',
    ',randtrace').
    game_env: None, the game inherits this process's environment; a dict (also {}), the game's EI_* variables
    are exactly these (this process's other game variables are dropped), for a scheduler running other
    submitters' batches. It goes to the subprocess only; os.environ stays as it is.
    dgvoodoo: a run of our code draws through dgVoodoo instead of our renderer (our_renderer).
    A run of the original code (no hooks) is taken from the reference cache when it holds it (reference_key),
    unless fresh; a passed one is put there."""
    folder = folder or f'{name}-{install}'
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    environ = os.environ if game_env is None else game_env
    key = reference_key(name, install, root, clock, environ) if not hooks else None
    if key and not fresh:
        cached = reference_from_cache(name, install, key, os.path.join(build, 'runs', folder))
        if cached:
            return cached
    result, seconds, failures, counts, notes = run_once(name, install, hooks, folder, root, build, take_slot, clock,
                                                        game_env, dgvoodoo)
    if key and result == 'PASS':
        store_reference(name, install, key, os.path.join(build, 'runs', folder), (result, seconds, failures, counts, notes))
    return result, seconds, failures, counts, notes


def run_once(name, install, hooks, folder, root, build, take_slot, clock, game_env, dgvoodoo=False):
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(build, 'runs', folder or f'{name}-{install}')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'))   # "# prepare:" lines
    cmd = [os.path.join(build, 'ei_launch.exe'), '--harness', scenario, '--out', out]
    env = None
    if game_env is not None:
        env = {k: v for k, v in os.environ.items() if k not in game_environment()}
        env.update(game_env)
    if install == 'vanilla':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(run_dir(root), 'vanilla', 'game.exe')]
    elif install == 'native':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(run_dir(root), 'native', 'game.exe')]
        env = dict(os.environ if env is None else env, EI_RENDER=OUR_RENDERER)
    elif install == 'standalone':
        # Never the display mode: our renderer whatever the environment says.
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(run_dir(root), 'vanilla', 'game.exe'), '--standalone']
        env = dict(os.environ if env is None else env, EI_RENDER=OUR_RENDERER)
    elif os.path.exists(os.path.join(run_dir(root), 'rebirth', 'game.exe')):
        # Rebirth's engine copied with our own dgVoodoo.conf (tools/setup_rebirth_run.cmd).
        cmd += ['--exe', os.path.join(run_dir(root), 'rebirth', 'game.exe')]
    environ = os.environ if env is None else env
    if install not in ('native', 'standalone') and 'EI_RENDER' not in environ and our_renderer(scenario, install, hooks, dgvoodoo, environ):
        env = dict(environ, EI_RENDER=OUR_RENDERER)    # dgVoodoo's DLLs stay beside game.exe; nothing draws through them
    elif not hooks and 'EI_RENDER' in environ:
        # The original code draws only through dgVoodoo: with the harness's capture taking the new renderer's frame
        # (present), which nothing draws without our hooks, its frames came out empty.
        env = {k: v for k, v in environ.items() if k != 'EI_RENDER'}
        environ = env
    if not hooks:
        cmd.append('--no-hooks')
    if clock:
        env = dict(os.environ if env is None else env, EI_HARNESS_CLOCK=clock)
    with machine_slot(alone=runs_alone(scenario, install, hooks, dgvoodoo, environ)) if take_slot else contextlib.nullcontext():
        start = time.time()
        # ei_launch ends a game at the limit plus its margin; this is for ei_launch itself (its job object
        # takes the game down with it).
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, encoding='cp1251', errors='replace', env=env,
                                  timeout=scenario_limit(scenario) + LIMIT_MARGIN_S + 90)
        except subprocess.TimeoutExpired as e:
            proc = subprocess.CompletedProcess(cmd, TIMEOUT_EXIT, '', f'ei_launch did not end in time; terminating it ({e})')
        seconds = time.time() - start
    path = os.path.join(out, 'harness_report.txt')
    report = open(path, encoding='cp1251', errors='replace').read() if os.path.exists(path) else ''
    result = next((l[8:] for l in report.splitlines() if l.startswith('RESULT: ')), 'NO REPORT')
    failures = [l[8:] for l in report.splitlines() if l.startswith('FAILED: ')]
    counts = {}
    for line in report.splitlines():
        m = re.match(r'(.+?)\s+(\d+) calls\s+(\d+) mismatches$', line)
        if m:
            counts[m.group(1).strip()] = (int(m.group(2)), int(m.group(3)))
    notes = [l for l in (proc.stdout + proc.stderr).splitlines()
             if 'terminating' in l or 'not found' in l or 'open window' in l or 'still alive' in l
             or l.startswith('ei_standalone: ') and 'imports resolved, 0 missing' not in l]
    if proc.returncode in (TIMEOUT_EXIT, UNKILLABLE_EXIT) or any(f.startswith(IN_GAME_TIMEOUT) for f in failures):
        result = 'TIMEOUT'
        if not failures:
            failures = [f'scenario exceeded its {scenario_limit(scenario)} s time limit; ei_launch ended the game'
                        + ('' if proc.returncode != UNKILLABLE_EXIT else ' (it did not die: a thread is stuck in the kernel)')]
        if proc.returncode == UNKILLABLE_EXIT:
            failures.append('the game process is still alive after TerminateProcess')
    expected = expected_result(scenario)
    if expected and result == expected and proc.returncode != UNKILLABLE_EXIT:
        result = 'PASS'
        failures = []
        notes.append(f'ended as the scenario expects ({expected})')
    notes += [l for l in report.splitlines() if l.startswith('EXIT: ')]   # a message box after quit
    return result, seconds, failures, counts, notes


# ---- the reference cache: runs of the original code (on dgVoodoo) kept by what decides them (ticket R10)
#
# The original code does not change, so a run of it only needs playing again when its scenario, its install's
# files, the clock or the game's EI_* settings change. A passed run's folder (frames, report, log; not
# harness_saves) is kept in <main checkout>/build/reference/<scenario>-<install>/<key>/ with cached.json (run()'s
# results); run() copies it into the run folder instead of playing it. The harness's own sources
# (tests/verify/harness*) are in the key, so a digest or clock change plays the original again; other harness
# changes that alter what a reference run captures need --fresh.

CACHED_NOTE = 'reference cache: '     # the note of a run taken from the cache (no time recorded for it)
REFERENCE_KEEP = 3                     # keys kept per <scenario>-<install> (other worktrees' scenario revisions)


def reference_dir():
    """<main checkout>/build/reference (shared by every worktree), or None outside a repository. EI_REFERENCE_DIR:
    another folder (tests)."""
    if os.environ.get('EI_REFERENCE_DIR'):
        return os.environ['EI_REFERENCE_DIR']
    try:
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
    except OSError:
        return None
    return os.path.join(os.path.dirname(os.path.normpath(common)), 'build', 'reference') if common else None


def install_fingerprint(root, install):
    """What of an install decides a run of the original code: the files beside its game.exe in run/<install>
    (names, sizes, times; configuration files such as dgVoodoo.conf by content)."""
    h = hashlib.sha1(install.encode())
    with contextlib.suppress(OSError):
        for entry in sorted(os.scandir(os.path.join(run_dir(root), install)), key=lambda e: e.name.lower()):
            if not entry.is_file():
                continue
            st = entry.stat()
            h.update(f'{entry.name.lower()}\0{st.st_size}\0{st.st_mtime_ns}\0'.encode())
            if os.path.splitext(entry.name)[1].lower() in ('.conf', '.ini', '.cfg', '.txt') and st.st_size < 1 << 20:
                with open(entry.path, 'rb') as f:
                    h.update(f.read())
    return h.hexdigest()


def reference_key(name, install, root, clock, environ):
    """The cache key of a run of the original code, or None when it is not cached (the scenario has uncommitted
    edits or no commit). Made of: the scenario's last commit (work.scenario_revs, as WB6's timing table), the
    install's files, the harness's sources, the tools its "# prepare:" lines run, the clock override and the game's
    EI_* settings."""
    try:
        import work
        rev = work.scenario_revs(root).get(name, '')
    except (Exception, SystemExit):
        return None
    if not rev or rev.endswith('+dirty'):
        return None
    h = hashlib.sha1(f'{name}\0{install}\0{rev}\0{clock or ""}\0'.encode())
    settings = {k: v for k, v in game_environment(environ).items() if k != 'EI_RENDER'}    # run_once drops it
    h.update(json.dumps(settings, sort_keys=True).encode())
    h.update(install_fingerprint(root, install).encode())
    for source in sorted(glob.glob(os.path.join(root, 'tests', 'verify', 'harness*'))):
        with contextlib.suppress(OSError), open(source, 'rb') as f:
            h.update(f.read())
    with contextlib.suppress(OSError):
        with open(os.path.join(root, 'tests', 'scenarios', name + '.txt'), encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(prepare_scenario.PREFIX):
                    tool = text[len(prepare_scenario.PREFIX):].split()[:1]
                    with contextlib.suppress(OSError), open(os.path.join(root, 'tools', *tool), 'rb') as t:
                        h.update(t.read())
    return f'{rev[:10]}-{h.hexdigest()[:12]}'


def reference_from_cache(name, install, key, out):
    """run()'s results of a cached run, its files copied into the run folder out; None when not cached."""
    base = reference_dir()
    if not base:
        return None
    entry = os.path.join(base, f'{name}-{install}', key)
    try:
        with open(os.path.join(entry, 'cached.json')) as f:
            info = json.load(f)
        start = time.time()
        shutil.rmtree(out, ignore_errors=True)
        shutil.copytree(entry, out, ignore=shutil.ignore_patterns('cached.json'))
        os.utime(entry)                                  # recently used: kept by the pruning
        result, _, failures, counts, notes = info['run']
    except (OSError, ValueError, TypeError, KeyError):
        return None
    return (result, time.time() - start, failures, {fn: tuple(c) for fn, c in counts.items()},
            notes + [f'{CACHED_NOTE}{key} (played {info.get("time", "?")} in {info.get("seconds", 0):.0f}s)'])


def store_reference(name, install, key, out, results):
    """Keeps a passed run of the original code (its folder without harness_saves) under its key and drops the
    scenario's least recently used keys beyond REFERENCE_KEEP. Never raises: a run never fails because of it."""
    base = reference_dir()
    if not base:
        return
    home = os.path.join(base, f'{name}-{install}')
    entry = os.path.join(home, key)
    tmp = f'{entry}.{os.getpid()}.{threading.get_ident()}.tmp'
    try:
        os.makedirs(home, exist_ok=True)
        shutil.copytree(out, tmp, ignore=shutil.ignore_patterns('harness_saves'))
        result, seconds, failures, counts, notes = results
        with open(os.path.join(tmp, 'cached.json'), 'w') as f:
            json.dump({'run': [result, seconds, failures, {fn: list(c) for fn, c in counts.items()}, notes],
                       'seconds': seconds, 'time': time.strftime('%Y-%m-%d %H:%M'), 'folder': out}, f)
        if os.path.isdir(entry):
            shutil.rmtree(entry, ignore_errors=True)     # played again (--fresh): the newer run
        os.replace(tmp, entry)
        keys = sorted((e for e in os.scandir(home) if e.is_dir() and not e.name.endswith('.tmp')),
                      key=lambda e: e.stat().st_mtime, reverse=True)
        for old in keys[REFERENCE_KEEP:]:
            shutil.rmtree(old.path, ignore_errors=True)
    except OSError:
        pass
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def cached_reference(name, install, hooks, root=ROOT, clock=None, environ=None):
    """Whether run() would take this run from the reference cache (work.py test: then it is not alone)."""
    if hooks:
        return False
    key = reference_key(name, install, root, clock, os.environ if environ is None else environ)
    base = reference_dir()
    return bool(key and base and os.path.exists(os.path.join(base, f'{name}-{install}', key, 'cached.json')))


def is_cached(notes):
    """Whether run() took the run from the reference cache (no game was played)."""
    return any(n.startswith(CACHED_NOTE) for n in notes)


def console_of(folder, build=BUILD):
    """The console output a run's report holds (its lines after "console output:")."""
    path = os.path.join(build, 'runs', folder, 'harness_report.txt')
    lines = open(path, encoding='cp1251', errors='replace').read().splitlines() if os.path.exists(path) else []
    lines = lines[lines.index('console output:') + 1:] if 'console output:' in lines else []
    return [l for l in lines if l.startswith('  ')]


ATTEMPT_FILES = ('ei_hook.log', 'harness_report.txt')


def keep_attempt(folder, attempt, build=BUILD):
    """Keep a failed attempt's log and report before the retry empties its run folder: they go to
    <build>/runs/<folder>-attempt<attempt> (replaced). Returns that folder's name, or None when there was nothing."""
    source = os.path.join(build, 'runs', folder)
    kept = f'{folder}-attempt{attempt}'
    target = os.path.join(build, 'runs', kept)
    shutil.rmtree(target, ignore_errors=True)
    copied = False
    for name in ATTEMPT_FILES:
        with contextlib.suppress(OSError):
            os.makedirs(target, exist_ok=True)
            shutil.copy2(os.path.join(source, name), os.path.join(target, name))
            copied = True
    return kept if copied else None


def run_with_retries(name, install, hooks, retries, folder=None, runner=run, keep=None):
    """run() again after a failure, up to retries times. runner: run() or a stand-in with its arguments.
    keep(folder, attempt) -> the name of a folder that now holds the failed attempt's log and report (keep_attempt),
    called before each retry (a retry empties the run folder); a retry that passes names it in its failures."""
    result, seconds, failures, counts, notes = runner(name, install, hooks, folder)
    first_failures = failures
    first_log = None
    for attempt in range(1, retries + 1):
        if result == 'PASS' or any(bad for _, bad in counts.values()):   # never retry away a mismatch
            break
        kept = keep(folder or f'{name}-{install}', attempt) if keep else None
        if attempt == 1:
            first_log = kept
        result, seconds, failures, counts, notes = runner(name, install, hooks, folder)
        if result == 'PASS':
            result = 'PASS*'
            failures = ['first run: ' + '; '.join(first_failures[:2]) + (f' (log kept in runs/{first_log})' if first_log else '')]
    return result, seconds, failures, counts, notes


COMPARE_JOBS = 8     # compare_frames.py processes at once (each one CPU core, ~1 GB for three 4K frames)


def compare_widescreen(name, out=None, root=ROOT, build=BUILD):
    """--widescreen: each frame of <name>-rebirth against <name>-rebirth-nohooks (tools/compare_frames.py).
    A frame <f> with a <f>_later (captured after the clocks ran on) is compared without what moved
    between the two in either run (animations whose phase differs between runs: the hero, his
    portrait, the global map's clouds), so that what is compared is the interface.
    out: where the comparisons print (default stdout)."""
    out = out or sys.stdout
    runs = os.path.join(build, 'runs')
    ours, addons = (os.path.join(runs, f'{name}-{i}', 'frames') for i in ('rebirth', 'rebirth-nohooks'))
    diffs = os.path.join(runs, f'{name}-rebirth', 'frames_diff')
    os.makedirs(diffs, exist_ok=True)
    frames = sorted(f[:-4] for f in os.listdir(ours) if f.endswith('.bmp')) if os.path.isdir(ours) else []
    if not frames:
        print('no frames to compare', file=out)
        return False
    commands = []
    for frame in frames:
        if frame.endswith('_later'):
            continue
        a, b = os.path.join(ours, frame + '.bmp'), os.path.join(addons, frame + '.bmp')
        cmd = [sys.executable, os.path.join(root, 'tools', 'compare_frames.py'), a, b, '--worst', '4',
               '--diff', os.path.join(diffs, frame + '.bmp')]
        later_a, later_b = os.path.join(ours, frame + '_later.bmp'), os.path.join(addons, frame + '_later.bmp')
        excludes = []
        if os.path.exists(later_a) and os.path.exists(later_b):
            excludes += ['--exclude', a, later_a, '--exclude', b, later_b]
        # A window that pauses the world (the Esc menu's): what moved in <f>_moving / <f>_moving_later,
        # captured before it opened, is left out of <f> too.
        moving = [os.path.join(d, frame + s) for d in (ours, addons) for s in ('_moving.bmp', '_moving_later.bmp')]
        if all(os.path.exists(m) for m in moving):
            excludes += ['--exclude', moving[0], moving[1], '--exclude', moving[2], moving[3]]
        if excludes:
            cmd += excludes + ['--exclude-grow', '8']
        commands.append(cmd)
    # compare_frames.py reads 4K frames pixel by pixel in Python (20-40 s a frame): the frames at once, printed in
    # their order (R10: the comparisons took longer than the games)
    with concurrent.futures.ThreadPoolExecutor(max_workers=COMPARE_JOBS) as pool:
        done = list(pool.map(lambda cmd: subprocess.run(cmd, capture_output=True, text=True, errors='replace'), commands))
    ok = True
    for p in done:
        out.write(p.stdout + p.stderr)
        ok = p.returncode == 0 and ok
    out.flush()
    return ok


def rebirth_scenarios(names, root=ROOT):
    """The scenarios that also run under Rebirth: marked 1 in config/rebirth_scenarios.csv, or not in it."""
    path = os.path.join(root, 'config', 'rebirth_scenarios.csv')
    listed = {}
    if os.path.exists(path):
        for line in open(path):
            parts = line.strip().split(',')
            if len(parts) >= 2 and parts[1] in ('0', '1'):
                listed[parts[0]] = parts[1] == '1'
    return {n for n in names if listed.get(n, True)}


def parse_args(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--vanilla-only', action='store_true')
    ap.add_argument('--rebirth-only', action='store_true')
    ap.add_argument('--all-rebirth', action='store_true')
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--standalone', action='store_true')
    ap.add_argument('--no-hooks', action='store_true')
    ap.add_argument('--dgvoodoo', action='store_true')
    ap.add_argument('--fresh', action='store_true')
    ap.add_argument('--widescreen', action='store_true')
    ap.add_argument('--golden', action='store_true')
    ap.add_argument('--verified-candidates', type=int, default=0)
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--clock')
    ap.add_argument('--jobs', type=int, default=16)
    return ap.parse_args(argv)


def plan(args, root=ROOT):
    """The runs args ask for: (scenario, install) pairs in the summary's order. install is vanilla,
    rebirth, native or rebirth-nohooks (--widescreen); the run folder is <scenario>-<install>.
    Also used by tools/work_sched.py (`work.py test`), so a batch holds the runs run_all would make."""
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(root, 'tests', 'scenarios', '*.txt')))
    installs = ['vanilla', 'rebirth']
    if args.vanilla_only: installs = ['vanilla']
    if args.rebirth_only: installs = ['rebirth']
    if args.native:
        installs = ['native']
        native = os.path.join(run_dir(root), 'native', 'game.exe')
        if not os.path.exists(native):
            sys.exit(f'{native} is missing: run tools\\setup_native_run.cmd first')
    if args.standalone:
        if args.no_hooks or args.widescreen or args.dgvoodoo:
            sys.exit('--standalone runs our code on our renderer: not with --no-hooks, --dgvoodoo or --widescreen')
        names = args.scenarios or STANDALONE_SCENARIOS
        installs = ['standalone']
    rebirth = set(names) if args.scenarios or args.all_rebirth or args.rebirth_only else rebirth_scenarios(names, root)
    jobs = [(name, install) for name in names for install in installs if install != 'rebirth' or name in rebirth]
    if args.widescreen:
        names = args.scenarios or WIDESCREEN_SCENARIOS
        jobs = [(name, install) for name in names for install in ('rebirth', 'rebirth-nohooks')]
    path = lambda name: os.path.join(root, 'tests', 'scenarios', name + '.txt')
    kept = [(n, i) for n, i in jobs if not switches_display(path(n))
            or our_renderer(path(n), i.split('-')[0], uses_hooks(i, args), wants_dgvoodoo(args))]
    for n, i in jobs:
        if (n, i) not in kept:
            print(f'{n}-{i}: left out, full screen at its own mode without our renderer would switch the display',
                  file=sys.stderr)
    return kept


def wants_dgvoodoo(args):
    """Whether runs of our code draw through dgVoodoo: --dgvoodoo, and --widescreen, whose frames are compared with
    the original's (dgVoodoo's) pixel by pixel: across the renderers 0.2-1% of a 4K interface frame differ by more
    than 8 (texture filtering of the scene behind the panels, up to 150-230), the water's phase up to 20% (R10)."""
    return getattr(args, 'dgvoodoo', False) or getattr(args, 'widescreen', False)


def uses_hooks(install, args):
    """Whether a run of plan()'s install loads our hooks ("rebirth-nohooks": Rebirth without them)."""
    return not args.no_hooks and not install.endswith('-nohooks')


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
    REPORT, TIMEOUT) and runs taken from the reference cache are not times."""
    if db is None or result in ('ERROR', 'NO REPORT', 'TIMEOUT') or is_cached(notes):
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


def summary(jobs, results, totals, args, elapsed, out=None, root=ROOT, build=BUILD):
    """Prints what follows the table: runs not passed, message boxes after quit, verify mismatches,
    --widescreen's frame comparisons, --verified-candidates and the totals line. results:
    {(scenario, install): (result, seconds, failures, notes)} for every job; totals: {function:
    [calls, mismatches]}. True when every run passed (and the frames matched)."""
    out = out or sys.stdout
    ok = all(r[0] in ('PASS', 'PASS*') for r in results.values())
    failed = [(n, i) for n, i in jobs if results[(n, i)][0] not in ('PASS', 'PASS*')]
    if failed and len(jobs) > 1:
        print('\nnot passed:', file=out)
        for n, i in failed:
            print(line(n, i, *results[(n, i)], root=root, build=build), file=out)
    exits = [(n, i) for n, i in jobs if any(x.startswith('EXIT: ') for x in results[(n, i)][3])]
    if exits:
        print(f'\n{len(exits)} run(s) showed a message box after the scenario quit (EXIT: lines; stack in ei_hook.log): '
              + ', '.join(f'{n}-{i}' for n, i in exits), file=out)
    bad = sorted((fn, c) for fn, c in totals.items() if c[1])
    if bad:
        print('\nverify mismatches:', file=out)
        for fn, (calls, mismatches) in bad:
            print(f'  {fn}: {mismatches} of {calls}', file=out)
    if args.widescreen:
        for name in args.scenarios or WIDESCREEN_SCENARIOS:
            print(f'\nframes of {name}: ours (rebirth) against addon\'s alone (rebirth-nohooks)', file=out)
            out.flush()
            ok = compare_widescreen(name, out, root, build) and ok
    if args.verified_candidates:
        print(f'\nfunctions with >= {args.verified_candidates} calls and no mismatch (names; map them to addresses '
              'in tests/verify/*.cpp before adding to verified.txt):', file=out)
        for fn, (calls, mismatches) in sorted(totals.items(), key=lambda kv: -kv[1][0]):
            if calls >= args.verified_candidates and not mismatches:
                print(f'  {calls:10d}  {fn}', file=out)
    print(f'\n{len(jobs)} runs in {elapsed:.0f}s with {args.jobs} at once', file=out)
    print('ALL PASS' if ok else 'SOME RUNS FAILED', file=out)
    return ok


GOLDEN_INSTALL = {'vanilla': 'vanilla', 'native': 'vanilla', 'standalone': 'vanilla', 'rebirth': 'rebirth', 'rebirth-nohooks': 'rebirth'}


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
    db = timing_db()
    times = expected_times(jobs, db=db)
    order = sorted(jobs, key=lambda j: -(times[j] if times[j] is not None else 1000))   # new scenarios first

    totals = collections.defaultdict(lambda: [0, 0])
    results = {}
    lock = threading.Lock()
    started = time.time()
    print(HEADER)
    sys.stdout.flush()

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        # (--widescreen's "rebirth-nohooks": Rebirth without our hooks, in its own folder)
        futures = {pool.submit(run_with_retries, n, i.split('-')[0], uses_hooks(i, args), args.retries, f'{n}-{i}',
                               functools.partial(run, clock=args.clock, dgvoodoo=wants_dgvoodoo(args), fresh=args.fresh),
                               keep_attempt): (n, i)
                   for n, i in order}
        for future in concurrent.futures.as_completed(futures):
            name, install = futures[future]
            result, seconds, failures, counts, notes = future.result()
            with lock:
                results[(name, install)] = (result, seconds, failures, notes)
                record_time(db, name, install, seconds, result, notes=notes)
                for fn, (calls, bad) in counts.items():
                    totals[fn][0] += calls
                    totals[fn][1] += bad
                print(line(name, install, result, seconds, failures, notes))
                sys.stdout.flush()

    save_times({f'{n}-{i}': r[1] for (n, i), r in results.items()})
    ok = summary(jobs, results, totals, args, time.time() - started)
    if args.golden:
        ok = golden_check(jobs) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
