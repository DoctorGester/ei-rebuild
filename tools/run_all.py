"""Runs every harness scenario on the vanilla install's data and on Rebirth's and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only | --all-rebirth]
                               [--standalone | --no-hooks] [--fresh] [--jobs N] [--retries N]
                               [--clock virtual|real[,...]] [--golden]
       python tools/run_all.py --widescreen [--record] [scenario names...]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
  default          the standalone image (S21: the hook builds retire, docs/notes/standalone.md section 5), as
                   --standalone: ei_launch --standalone, build\\verify\\ei_standalone.exe lays out run\\vanilla's
                   game.exe without its code and loads ei_hook.dll, on the vanilla install's data with
                   EI_RENDER=new,present (install "standalone", vanilla goldens), and on the Rebirth install's data
                   with run\\rebirth's game.exe and the flags of config/ei_rebirth_standalone.ini as EI_REBIRTH (no
                   addon.dll, no EI_Plugin.asi; install "standalone-rebirth", Rebirth goldens). Every scenario runs
                   on the vanilla data; on Rebirth's those with a Rebirth golden, those config/rebirth_scenarios.csv
                   marks and any it does not list yet. Named scenarios and --all-rebirth run on both. An address
                   with no rewrite ends the run: "standalone: no code at ...".
  --no-hooks       game.exe with the original code only (ei_launch --no-hooks: the harness, no rewrite), on the
                   vanilla install (install "vanilla") and on Rebirth's (install "rebirth", for the scenarios
                   config/rebirth_scenarios.csv marks or does not list): the runs goldens are recorded from
                   (tools/golden.py), kept in the reference cache (--fresh). With EI_VECTORS=1 in the environment
                   the recorders are the only hooks and the runs write function vectors (tools/vectors.py). A
                   scenario's line "# no original: <install>..." (vanilla, rebirth) leaves its runs there out:
                   checks only our renderer can pass, flags the original lacks (S21a). Since S21c no tool runs
                   our code inside game.exe (the hooked runs, --native and --dgvoodoo are gone).
  renderers        The image draws with our renderer (EI_RENDER=new,present: no DirectDraw device, its own swap
                   chain, no display mode change), so its runs never need the machine to themselves. dgVoodoo
                   draws the original code (--no-hooks, --widescreen --record's rebirth-nohooks); such a run with
                   a "resolution" step runs alone, and a scenario at full screen at a mode of its own is left out
                   (it would switch the display). EI_RENDER never reaches the original code.
  --fresh          play the original code's runs again instead of taking them from the reference
                   cache (build\\reference in the main checkout: a passed run of the original code,
                   kept by scenario commit, install files, clock and EI_* settings; reference_key)
  --widescreen     Rebirth's WideScreen against our rewrite of it (docs/notes/rebirth.md, "WideScreen"): the
                   scenarios (default ui_widescreen, ui_widescreen_more, ui_widescreen_hud and
                   ui_widescreen_screens; they set the desktop's mode and WideScreen with the harness's
                   "resolution" / "widescreen" steps) on the image on the Rebirth install
                   (<scenario>-standalone-rebirth), then each frame against addon's recorded frames
                   (compare_widescreen: tools/compare_frames.py with WIDESCREEN_COMPARE). No addon game is
                   played: the references are recorded once, in <main checkout>\\build\\widescreen
  --record         with --widescreen: play addon's code instead (game.exe with Rebirth's addon.dll, no hooks,
                   dgVoodoo at the desktop's mode, alone: <scenario>-rebirth-nohooks) and keep its frames as the
                   scenarios' references (record_widescreen), by the scenario's steps: needed once, and again
                   when a scenario's steps change (a comment does not count; the comparison names it)
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
                   the console, the digest records, the dumps and the saves. The image's runs on the vanilla data
                   compare with the vanilla golden, on Rebirth's with the Rebirth golden
exit code: 0 when every run passed

A run that outlives the scenario's time limit ("timeout <seconds>", default 60 s) is reported as TIMEOUT: the
game's watchdog ends it at the limit, ei_launch 20 s later if the watchdog could not (tools/launcher/launch.cpp).
A scenario with a line "# expect: TIMEOUT" passes when it times out (tests/scenarios/watchdog_*.txt check this).
"""
import argparse, functools, collections, concurrent.futures, contextlib, glob, hashlib, io, json, msvcrt, os, re, shutil, struct, subprocess, sys, tempfile, threading, time

import frame_store, prepare_scenario

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDESCREEN_SCENARIOS = ['ui_widescreen', 'ui_widescreen_more', 'ui_widescreen_hud', 'ui_widescreen_screens']   # --widescreen without scenario names
STANDALONE_INSTALLS = ('standalone', 'standalone-rebirth')   # the image (our code)
ORIGINAL_INSTALLS = ('vanilla', 'rebirth')                    # game.exe with the original code only (--no-hooks)
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
    only on the image (plan() leaves out its game.exe runs)."""
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


def our_code(install):
    """Whether a run on install (one of plan()'s) runs our code: the standalone image's installs, which draw with our
    renderer. Every other run is game.exe with the original code only (--no-hooks, --widescreen --record's
    rebirth-nohooks), drawn by dgVoodoo: since S21c no tool runs our code inside game.exe."""
    return install in STANDALONE_INSTALLS


def runs_alone(scenario, install):
    """A run that keeps the machine to itself: a dgVoodoo run (the original code) that sets the display mode. Our
    renderer changes no display mode (full screen is a window over the monitor), so the image's runs share the
    machine."""
    return sets_display_mode(scenario) and not our_code(install)


# EI_* variables that are not settings of a game: where the build and the references are, the machine's slots, and
# what ei_launch sets itself. Every other EI_* variable (EI_PROFILE, EI_RENDER, EI_VECTORS, EI_COVER, ...) is read by a
# game.
NOT_GAME_ENV = ('EI_BUILD_DIR', 'EI_HARNESS_SLOTS', 'EI_OUT_DIR', 'EI_HARNESS', 'EI_REFERENCE_DIR', 'EI_WIDESCREEN_DIR')


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


NO_ORIGINAL_PREFIX = '# no original:'


def no_original(scenario):
    """The installs (vanilla, rebirth) a scenario's line "# no original: <install>... <why>" names: the original
    code cannot pass it there (renderer-only checks, flags the original lacks; S21a), so plan() leaves those
    --no-hooks runs out. Words after the install names are the reason."""
    installs = set()
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(NO_ORIGINAL_PREFIX):
                    for word in text[len(NO_ORIGINAL_PREFIX):].split():
                        if word not in ('vanilla', 'rebirth'):
                            break
                        installs.add(word)
    return installs


def run(name, install, folder=None, root=ROOT, build=BUILD, take_slot=True, clock=None, game_env=None, fresh=False):
    """One game of a scenario on an install: the image on standalone / standalone-rebirth, the original code
    (game.exe, ei_launch --no-hooks) on vanilla / rebirth. root: the checkout whose tests/scenarios and run/ it uses;
    build: the verify build folder (ei_launch.exe; the run folder is <build>/runs/<folder>).
    take_slot False: the caller already holds a machine slot for it (tools/work_sched.py).
    clock overrides the scenario's clock (EI_HARNESS_CLOCK: 'virtual' or 'real', ',clocktrace',
    ',randtrace').
    game_env: None, the game inherits this process's environment; a dict (also {}), the game's EI_* variables
    are exactly these (this process's other game variables are dropped), for a scheduler running other
    submitters' batches. It goes to the subprocess only; os.environ stays as it is.
    A run of the original code is taken from the reference cache when it holds it (reference_key),
    unless fresh; a passed one is put there."""
    folder = folder or f'{name}-{install}'
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    environ = os.environ if game_env is None else game_env
    key = reference_key(name, install, root, clock, environ) if not our_code(install) else None
    if key and not fresh:
        cached = reference_from_cache(name, install, key, os.path.join(build, 'runs', folder))
        if cached:
            return cached
    result, seconds, failures, counts, notes = run_once(name, install, folder, root, build, take_slot, clock, game_env)
    frame_store.pack_folder(os.path.join(build, 'runs', folder))      # WB11: 4K frames stored compressed
    if key and result == 'PASS':
        store_reference(name, install, key, os.path.join(build, 'runs', folder), (result, seconds, failures, counts, notes))
    return result, seconds, failures, counts, notes


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


def crash_stack_note(out, build, cmd, root=ROOT):
    """The note under a crashed run's line: its stack by function names (tools/resolve_crash.py, X40a), the
    last frames before the crash; never fails the run."""
    try:
        import resolve_crash
        exe = cmd[cmd.index('--exe') + 1] if '--exe' in cmd else None
        text = resolve_crash.named_stack(out, build=build, game_exe=exe, root=root, max_frames=10)
        return f'stack: {text}' if text else f'stack: not resolved (python tools/resolve_crash.py {os.path.relpath(out, root)})'
    except Exception as e:           # a tool problem must not hide the crash itself
        return f'stack: not resolved ({type(e).__name__}: {e})'


def run_once(name, install, folder, root, build, take_slot, clock, game_env):
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(build, 'runs', folder or f'{name}-{install}')
    if install not in STANDALONE_INSTALLS + ORIGINAL_INSTALLS:
        return 'FAIL', 0.0, [f'unknown install {install!r} (the image: {", ".join(STANDALONE_INSTALLS)}; the original '
                             f'code: {", ".join(ORIGINAL_INSTALLS)})'], {}, []
    if low_disk := wait_for_disk(build):
        return 'FAIL', 0.0, [low_disk], {}, []
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'))   # "# prepare:" lines
    cmd = [os.path.join(build, 'ei_launch.exe'), '--harness', scenario, '--out', out]
    env = None
    if game_env is not None:
        env = {k: v for k, v in os.environ.items() if k not in game_environment()}
        env.update(game_env)
    if install == 'standalone':
        # Never the display mode: our renderer whatever the environment says.
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(run_dir(root), 'vanilla', 'game.exe'), '--standalone']
        env = dict(os.environ if env is None else env, EI_RENDER=OUR_RENDERER)
    elif install == 'standalone-rebirth':
        # ei_launch's default game folder is the Rebirth install's.
        cmd += ['--exe', os.path.join(run_dir(root), 'rebirth', 'game.exe'), '--standalone']
        env = dict(os.environ if env is None else env, EI_RENDER=OUR_RENDERER, EI_REBIRTH=standalone_rebirth_flags(root))
    else:
        # The original code only (S21c: no tool runs ours inside game.exe), drawn by dgVoodoo.
        if install == 'vanilla':
            cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(run_dir(root), 'vanilla', 'game.exe')]
        elif os.path.exists(os.path.join(run_dir(root), 'rebirth', 'game.exe')):
            # Rebirth's engine copied with our own dgVoodoo.conf (tools/setup_rebirth_run.cmd).
            cmd += ['--exe', os.path.join(run_dir(root), 'rebirth', 'game.exe')]
        cmd.append('--no-hooks')
        environ = os.environ if env is None else env
        if 'EI_RENDER' in environ:
            # With the harness's capture taking the new renderer's frame (present), which nothing draws in the
            # original code, its frames came out empty.
            env = {k: v for k, v in environ.items() if k != 'EI_RENDER'}
    if clock:
        env = dict(os.environ if env is None else env, EI_HARNESS_CLOCK=clock)
    with machine_slot(alone=runs_alone(scenario, install)) if take_slot else contextlib.nullcontext():
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
    # The verify wrappers' per-function totals until S21d; the slot stays in the result (the reference cache
    # and the work database's run rows keep its shape), always empty.
    counts = {}
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
    if any('the game crashed' in f or 'no code at' in f for f in failures):
        notes.append(crash_stack_note(out, build, cmd, root))
    expected = expected_result(scenario)
    if expected and result == expected and proc.returncode != UNKILLABLE_EXIT:
        result = 'PASS'
        failures = []
        notes.append(f'ended as the scenario expects ({expected})')
    notes += [l for l in report.splitlines() if l.startswith('EXIT: ')]   # a message box after quit
    notes += [l for l in report.splitlines() if l.startswith('WINDOW: in sight')]   # WB12: a window the user saw
    notes += [l for l in report.splitlines() if l.startswith('NET: outside')]   # WB14: a bind the firewall asks about
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


def cached_reference(name, install, root=ROOT, clock=None, environ=None):
    """Whether run() would take this run from the reference cache (work.py test: then it is not alone)."""
    if our_code(install):
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
# Rebirth's WideScreen is addon.dll's code, on the Rebirth install only. Its frames are recorded once with the
# original game's harness (`--widescreen --record`: game.exe with addon, EI_NO_HOOKS, drawn by dgVoodoo at the
# desktop's mode; the run "<scenario>-rebirth-nohooks") and kept as references; `--widescreen` runs the image on
# the Rebirth install ("<scenario>-standalone-rebirth", our renderer) and compares its frames with them. No addon
# game is played for a comparison. The references live beside the reference cache, in
# <main checkout>/build/widescreen/<scenario>/<steps key>/: frames/<frame>.bmp.z (the frames, packed as
# tools/frame_store.py packs a run's), skip/<frame>.bmp.z (what moved in addon's run between <frame> and its
# _later / _moving twins, left out of the comparison) and recorded.json. The key is the scenario's steps (its lines
# without comments): a changed step needs a new recording, a comment does not. WIDESCREEN_KEEP keys are kept per
# scenario (other worktrees' scenario versions).

WIDESCREEN_KEEP = 3
WIDESCREEN_SIZE = (3840, 2160)      # the frame size WIDESCREEN_AREAS' rectangles are given at (scaled to others)
# compare_frames.py's settings for our renderer against dgVoodoo (docs/notes/standalone.md, S21b: measured over
# the four scenarios' 19 frames). A pixel differs when a channel lies more than 8 outside the range of the other
# frame's pixels around it (--shift 1: the renderers sample magnified textures a fraction of a texel apart and
# rasterise lines, model and alpha-tested edges a pixel apart); a frame fails when more than 0.6% of its pixels
# differ (the scene's edges: at most 0.39%, the menu's trees) or more than a quarter of a 64-pixel block does
# (a moved element; at most 11% seen, the options window's slider thumbs).
WIDESCREEN_COMPARE = ['--tolerance', '8', '--shift', '1', '--max-fraction', '0.006', '--block', '64',
                      '--max-block', '0.25']
# Areas that differ between the renderers whatever WideScreen does, left out of the comparison:
# {(scenario or '*', frame or '*'): [((x0, y0, x1, y1) in a WIDESCREEN_SIZE frame, x1 and y1 excluded, why), ...]}.
WIDESCREEN_AREAS = {
    ('*', '*'): [((2712, 0, 3771, 14), 'the battle screen\'s top 14 rows right of the top panel: our renderer shows a '
                                       'strip there that dgVoodoo does not, also in hooked runs (R10c)')],
    ('ui_widescreen_more', 'save'): [((1954, 360, 3002, 1154), 'the Save window\'s picture: a copy of the screen, '
                                                               'scaled down by each renderer')],
}


def widescreen_dir():
    """<main checkout>/build/widescreen (shared by every worktree, beside the reference cache), or None outside a
    repository. EI_WIDESCREEN_DIR: another folder (tests)."""
    if os.environ.get('EI_WIDESCREEN_DIR'):
        return os.environ['EI_WIDESCREEN_DIR']
    base = reference_dir()
    return os.path.join(os.path.dirname(base), 'widescreen') if base else None


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


def record_widescreen(name, out=None, root=ROOT, build=BUILD):
    """--widescreen --record: keeps the frames of <name>-rebirth-nohooks (addon's WideScreen) as the scenario's
    references, with what moved in that run between each frame and its twins (compare_frames.py --write-skip).
    False when the run did not pass or had no frames."""
    out = out or sys.stdout
    run = os.path.join(build, 'runs', f'{name}-rebirth-nohooks')
    frames = os.path.join(run, 'frames')
    names = primary_frames(frames)
    base = widescreen_dir()
    why = 'the run did not pass' if not run_passed(run) else 'no frames' if not names else 'no references folder' if not base else None
    if why:
        print(f'{name}: not recorded ({why})', file=out)
        return False
    key = steps_key(os.path.join(root, 'tests', 'scenarios', name + '.txt'))
    home = os.path.join(base, name)
    entry = os.path.join(home, key)
    tmp = f'{entry}.{os.getpid()}.tmp'
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, 'frames'))
    os.makedirs(os.path.join(tmp, 'skip'))
    tool = os.path.join(root, 'tools', 'compare_frames.py')
    commands = []
    size = None
    for frame in names:
        source = os.path.join(frames, frame + '.bmp')
        data = frame_store.read_frame(source)
        with open(os.path.join(tmp, 'frames', frame + '.bmp'), 'wb') as f:
            f.write(data)
        size = size or list(struct.unpack_from('<ii', data, 18))
        excludes = twin_excludes(frames, frame)
        if excludes:
            commands.append([sys.executable, tool, source, source, *excludes, '--exclude-grow', '8',
                             '--write-skip', os.path.join(tmp, 'skip', frame + '.bmp')])
    ok = run_compares(commands, io.StringIO())
    frame_store.pack_folder(tmp)
    with open(os.path.join(tmp, 'recorded.json'), 'w') as f:
        json.dump({'scenario': name, 'steps': key, 'frames': names, 'size': [size[0], abs(size[1])],
                   'time': time.strftime('%Y-%m-%d %H:%M'), 'run': run}, f, indent=1)
    if not ok:
        shutil.rmtree(tmp, ignore_errors=True)
        print(f'{name}: not recorded (the skip masks could not be written)', file=out)
        return False
    shutil.rmtree(entry, ignore_errors=True)
    os.replace(tmp, entry)
    keys = sorted((e for e in os.scandir(home) if e.is_dir() and not e.name.endswith('.tmp')),
                  key=lambda e: e.stat().st_mtime, reverse=True)
    for old in keys[WIDESCREEN_KEEP:]:
        shutil.rmtree(old.path, ignore_errors=True)
    print(f'{name}: {len(names)} frames recorded in {entry} ({", ".join(names)})', file=out)
    return True


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
        print(f'no references of {name} for its steps: record them with '
              f'python tools\\run_all.py --widescreen --record {name}', file=out)
        return False
    with open(os.path.join(reference, 'recorded.json')) as f:
        info = json.load(f)
    print(f'(references recorded {info.get("time", "?")}: {reference})', file=out)
    ours = os.path.join(build, 'runs', f'{name}-{install}', 'frames')
    with contextlib.suppress(OSError, struct.error):
        width, height = struct.unpack_from('<ii', frame_store.read_frame(os.path.join(ours, info['frames'][0] + '.bmp')), 18)
        if info.get('size') and [width, abs(height)] != info['size']:
            print(f'the references are {info["size"][0]}x{info["size"][1]}, the run\'s frames {width}x{abs(height)} (the '
                  f'desktop\'s mode changed?): record them again with python tools\\run_all.py --widescreen --record {name}',
                  file=out)
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


def widescreen_check(jobs, record=False, out=None, root=ROOT, build=BUILD):
    """After --widescreen's runs: records addon's frames as references (record) or compares the image's runs with
    them. Prints each scenario's frames and a verdict; True when every one was recorded or matched."""
    out = out or sys.stdout
    ok = True
    for name, install in jobs:
        if record:
            print(f'\nreferences of {name}: addon\'s frames ({name}-{install})', file=out)
            out.flush()
            ok = record_widescreen(name, out, root, build) and ok
        else:
            print(f'\nframes of {name}: the image ({name}-{install}) against addon\'s', file=out)
            out.flush()
            ok = compare_widescreen(name, install, out, root, build) and ok
    print(('WIDESCREEN RECORDED' if ok else 'WIDESCREEN NOT RECORDED') if record else
          ('WIDESCREEN FRAMES MATCH' if ok else 'WIDESCREEN FRAMES DIFFER'), file=out)
    return ok


def standalone_rebirth_flags(root=ROOT):
    """EI_REBIRTH for the standalone image on the Rebirth install: the patch names config/ei_rebirth_standalone.ini
    sets to 1, comma-separated (S19)."""
    names = []
    with open(os.path.join(root, 'config', 'ei_rebirth_standalone.ini'), encoding='utf-8') as f:
        for text in f:
            m = re.match(r'\s*(\w+)\s*=\s*1\s*$', text)
            if m and m.group(1) not in ('Default', 'FollowAddon', 'NPlayers', 'Pause'):
                names.append(m.group(1))
    return ','.join(names)


def launch_install(install):
    """The install run() is given for one of plan()'s: --widescreen --record's "rebirth-nohooks" is "rebirth" (the
    original code, with addon; the name keeps addon's recording runs in folders of their own)."""
    return install[:-len('-nohooks')] if install.endswith('-nohooks') else install


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
    ap.add_argument('--standalone', action='store_true')
    ap.add_argument('--no-hooks', action='store_true')
    ap.add_argument('--fresh', action='store_true')
    ap.add_argument('--widescreen', action='store_true')
    ap.add_argument('--record', action='store_true')
    ap.add_argument('--golden', action='store_true')
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--clock')
    ap.add_argument('--jobs', type=int, default=16)
    return ap.parse_args(argv)


def standalone_mode(args):
    """Whether args ask for the standalone image's runs: the default since S21, also with --widescreen. --no-hooks and
    --widescreen --record run game.exe with the original code."""
    return not (args.no_hooks or getattr(args, 'record', False))


def has_golden(name, install, root=ROOT):
    """Whether tests/golden holds a golden of the scenario for install ('vanilla' or 'rebirth')."""
    return os.path.exists(os.path.join(root, 'tests', 'golden', name, install, 'golden.json'))


def plan(args, root=ROOT):
    """The runs args ask for: (scenario, install) pairs in the summary's order. install is standalone,
    standalone-rebirth, vanilla, rebirth (--no-hooks) or rebirth-nohooks (--widescreen --record); the run folder
    is <scenario>-<install>. Also used by tools/work_sched.py (`work.py test`), so a batch holds the runs run_all
    would make."""
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(root, 'tests', 'scenarios', '*.txt')))
    standalone = standalone_mode(args)
    if args.standalone and not standalone:
        sys.exit('--standalone runs the image: not with --no-hooks or --record')
    if getattr(args, 'record', False) and not args.widescreen:
        sys.exit("--record records --widescreen's references: --widescreen --record [scenario names]")
    if args.widescreen and (args.no_hooks or args.vanilla_only):
        sys.exit("--widescreen compares the image on the Rebirth install with addon's recorded frames (--record "
                 'records them): not with --no-hooks or --vanilla-only')
    installs = ['vanilla', 'rebirth']
    if args.vanilla_only: installs = ['vanilla']
    if args.rebirth_only: installs = ['rebirth']
    if standalone:
        installs = ['standalone'] if args.vanilla_only else ['standalone-rebirth'] if args.rebirth_only else list(STANDALONE_INSTALLS)
    if args.scenarios or args.all_rebirth or args.rebirth_only:
        rebirth = set(names)
    else:
        rebirth = rebirth_scenarios(names, root)
        if standalone:      # the image is checked by the goldens: every Rebirth golden runs
            rebirth |= {n for n in names if has_golden(n, 'rebirth', root)}
    jobs = [(name, install) for name in names for install in installs
            if install not in ('rebirth', 'standalone-rebirth') or name in rebirth]
    if args.widescreen:
        # addon's WideScreen exists on the Rebirth install only: the image there, or addon's runs to record
        names = args.scenarios or WIDESCREEN_SCENARIOS
        jobs = [(name, 'rebirth-nohooks' if args.record else 'standalone-rebirth') for name in names]
    path = lambda name: os.path.join(root, 'tests', 'scenarios', name + '.txt')
    kept = [(n, i) for n, i in jobs if not switches_display(path(n)) or our_code(i)]
    for n, i in jobs:
        if (n, i) not in kept:
            print(f'{n}-{i}: left out, full screen at its own mode without our renderer would switch the display',
                  file=sys.stderr)
    if args.no_hooks:
        for n, i in list(kept):
            if i in no_original(path(n)):
                kept.remove((n, i))
                print(f'{n}-{i}: left out, the original cannot run it ("{NO_ORIGINAL_PREFIX}" in the scenario)',
                      file=sys.stderr)
    return kept


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
        print(f'\n{len(exits)} run(s) showed a message box after the scenario quit (EXIT: lines; stack in ei_hook.log): '
              + ', '.join(f'{n}-{i}' for n, i in exits), file=out)
    window_check(jobs, out, build)
    net_check(jobs, out, build)
    print(f'\n{len(jobs)} runs in {elapsed:.0f}s with {args.jobs} at once', file=out)
    print('ALL PASS' if ok else 'SOME RUNS FAILED', file=out)
    return ok


def window_check(jobs, out, build=BUILD):
    """WB12: the standalone runs keep the game's windows out of sight (cloaked tool windows, never in front); each
    report's WINDOW: line says whether any check (harness.cpp, noforeground::Watch) saw one. Prints how many runs
    had the check and the runs that saw a window."""
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


GOLDEN_INSTALL = {'vanilla': 'vanilla', 'standalone': 'vanilla', 'standalone-rebirth': 'rebirth', 'rebirth': 'rebirth',
                  'rebirth-nohooks': 'rebirth'}


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

    results = {}
    lock = threading.Lock()
    started = time.time()
    print(HEADER)
    sys.stdout.flush()

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        # (--widescreen --record's "rebirth-nohooks": the original code on Rebirth, in its own folder)
        futures = {pool.submit(run_with_retries, n, launch_install(i), args.retries, f'{n}-{i}',
                               functools.partial(run, clock=args.clock, fresh=args.fresh), keep_attempt): (n, i)
                   for n, i in order}
        for future in concurrent.futures.as_completed(futures):
            name, install = futures[future]
            result, seconds, failures, _, notes = future.result()
            with lock:
                results[(name, install)] = (result, seconds, failures, notes)
                record_time(db, name, install, seconds, result, notes=notes)
                print(line(name, install, result, seconds, failures, notes))
                sys.stdout.flush()

    save_times({f'{n}-{i}': r[1] for (n, i), r in results.items()})
    ok = summary(jobs, results, args, time.time() - started)
    if args.widescreen:
        ok = widescreen_check(jobs, args.record) and ok
    if args.golden:
        ok = golden_check(jobs) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
