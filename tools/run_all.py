"""Runs every harness scenario on the vanilla install's data and on Rebirth's and prints a summary.

usage: python tools/run_all.py [scenario names... | --smoke | --full] [--rebirth | --rebirth-only | --all-rebirth]
                               [--jobs N] [--retries N] [--clock virtual|real[,...]] [--golden] [--unpacked | --converted]
                               [--dzn | --lavapipe]
       python tools/run_all.py --widescreen [scenario names...]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt but the screenshots_* ones, which take
                   the README's pictures and run by name only, and those with a line "# suite: named" (in_suite());
                   on Linux less those its driver cannot match, LINUX_LEFT_OUT). A line "# installs: vanilla" or
                   "# installs: rebirth" keeps a scenario's runs to that install, named or not (scenario_installs())
  default          the image, build\\verify\\ei-rebuild.exe (the game and the harness linked in), started by this
                   tool (launch()) on an install's data: EI_GAME_ROOT names the install, each run takes the
                   install folder's own EI_Plugin values as a player's exe does. Installs: "standalone" the vanilla data (vanilla goldens), "standalone-rebirth" Rebirth's
                   (Rebirth goldens); the folders are EI_GAME_VANILLA and EI_GAME_REBIRTH (install_dir()). Every
                   scenario runs on the vanilla data; Rebirth's data only on demand (SV9; main checks the Rebirth set
                   after every merge with --rebirth-only): --rebirth adds the Rebirth set (rebirth_scenarios(): config/rebirth_scenarios.csv's
                   marks, every scenario with a step for one install, plugin or widescreen, and
                   every scenario with a Rebirth golden) or the named scenarios, --rebirth-only runs those alone,
                   --all-rebirth every scenario on both.
  renderers        The image draws with our renderer (no DirectDraw device, its own swap chain, no display mode
                   change), so its runs never need the machine to themselves. EI_RENDER reaches no run: a scenario's
                   "render" step sets the image's renderer options (EI_RENDER_EXTRA adds words to them).
  --smoke          the smoke tier (H74): config/smoke_scenarios.csv's runs (scenario, install: the most coverage per
                   run, tools/suite_coverage.py --smoke-file), less those --vanilla-only / --rebirth-only leave out;
                   `work.py test` without names plays it unless --full. --full: the whole suite (run_all's default)
  --widescreen     Rebirth's WideScreen against our rewrite of it (docs/notes/rebirth.md, "WideScreen"): the
                   scenarios (default ui_widescreen, ui_widescreen_more, ui_widescreen_hud,
                   ui_widescreen_screens and ui_widescreen_dialogue; they set the desktop's mode and WideScreen with the harness's
                   "resolution" / "widescreen" steps) on the image on the Rebirth install
                   (<scenario>-standalone-rebirth), then each frame against addon's recorded frames
                   (compare_widescreen: tools/compare_frames.py with WIDESCREEN_COMPARE). The references are
                   addon's frames, recorded from game.exe until H40 in <main checkout>\\build\\widescreen; a
                   scenario whose steps changed since has none (the comparison names it)
  --jobs N         games running at once (default 16; under WSL on Windows' GPU the Windows game slots free at the
                   start, at most WSL_GPU_SLOTS, 8, or EI_HARNESS_SLOTS, then growing as more free and going back as the queue
                   empties: follow_need()); all runs on the machine together never
                   exceed EI_HARNESS_SLOTS (default 16) games, whatever --jobs says. Each run writes to its own folder,
                   build\\verify\\runs\\<scenario>-<install> (ei-rebuild.log, harness_report.txt and
                   harness_saves, emptied first, so every run starts without saves or texture cache).
  --retries N      re-run a failed scenario up to N times (default 1); fights against spawned
                   enemies are random, and the hero occasionally loses one on the original too.
                   A pass after a retry is reported as "PASS*".
  --clock virtual|real[,clocktrace][,randtrace]
                   run every scenario on the virtual clock or on real time, whatever its own lines say
                   (EI_HARNESS_CLOCK; tests/verify/harness_clock.h), with the clock or random trace
  --unpacked       the runs play the installs' unpacked copies (use_unpacked(), tools/unpack_install.py), made when
                   missing; every golden must stay the same
  --converted      the runs play the unpacked copies' converted copies (use_converted(), tools/convert_install.py:
                   the record files as TOML, the originals gone), made when missing or made by another ei-convert;
                   every golden must stay the same
  --converted      (through work.py test without names: only when a file under CONVERTED_PATHS changed against main,
                   converted_changes(); H74)
  --golden         runs the games with a state digest (EI_DIGEST, the goldens' period) and afterwards compares
                   every run that has a golden (tests/golden/<scenario>/<install>, tools/golden.py) with it:
                   the console, the digest records, the dumps and the saves. The image's runs on the vanilla data
                   compare with the vanilla golden, on Rebirth's with the Rebirth golden
  --dzn            Linux: the games draw on Dozen, Mesa's Vulkan on D3D12 (WSL's GPU), from ~/mesa-dzn, instead of
                   the system's driver (lavapipe without a GPU, CI's reference); EI_VK_ICD=dzn says the same,
                   EI_VK_ICD=<manifest .json> names any Vulkan driver (vulkan_driver(); docs/notes/linux.md, "The GPU
                   under WSL"). The default under WSL when ~/mesa-dzn is installed. Such games draw on Windows' GPU,
                   so the run also holds as many of Windows' game slots as it runs games at once
                   (hold_windows_slots(), through Windows' Python)
  --lavapipe       Linux: the system's Vulkan drivers (lavapipe under WSL and on GitHub's runner); EI_VK_ICD=system
                   says the same
exit code: 0 when every run passed

A run that outlives the scenario's time limit ("timeout <seconds>", default 60 s) is reported as TIMEOUT: the
game's watchdog ends it at the limit, launch() LIMIT_MARGIN_S later if the watchdog could not.
A scenario with a line "# expect: TIMEOUT" passes when it times out (tests/scenarios/watchdog_*.txt check this).
A line "# arguments: <words>" starts the exe with those arguments (effect_viewer_standalone.txt: an effect's name).
A line "# exe: ei-effects" plays the scenario with the effect viewer's exe instead of the game's (effect_viewer*.txt;
tests/verify/effects/effects_harness.cpp): its log is ei-effects.log, and it writes no digest (no world).
"""
import argparse, atexit, functools, collections, concurrent.futures, contextlib, glob, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile, threading, time

import convert_install, frame_store, game_process, install_watch, prepare_scenario, unpack_install

if os.name == 'nt':
    import msvcrt
else:
    import fcntl

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDESCREEN_SCENARIOS = ['ui_widescreen', 'ui_widescreen_more', 'ui_widescreen_hud', 'ui_widescreen_screens',
                        'ui_widescreen_dialogue']   # --widescreen without scenario names
STANDALONE_INSTALLS = ('standalone', 'standalone-rebirth')   # the image on the vanilla and the Rebirth data
BUILD = os.environ.get('EI_BUILD_DIR') or os.path.join(ROOT, 'build', 'verify')   # EI_BUILD_DIR: another verify build (timing runs)
GOLDEN_PERIOD = 20   # world updates between the digest records of the suite's goldens (--golden; tools/golden.py)
# Runs go to <BUILD>/runs. <BUILD>/run_times.json keeps each run's seconds from the last suite: the
# longest start first, so the last games to finish are short ones (alphabetical order left a 50 s run
# for the end). run(), plan() and summary() take root/build so tools/work_sched.py can point them at
# another checkout.

# Games running at once on this machine, over every checkout and every run_all (2026-10-14: three
# workers' suites, 24 games of ~640 MB video memory each rendering without a frame limit, hung the
# NVIDIA driver until Windows crashed twice). A slot is a byte lock on a file in the temp folder;
# the system drops it when the process ends, so a killed run never leaves a slot taken.
# Under WSL the temp folder is Linux's own /tmp: its games have a pool of their own, apart from Windows' 16, so there
# the default is 6 (WSL's memory is capped at 16 GB; docs/notes/linux.md, "The harness"). EI_HARNESS_SLOTS overrides
# both.
def under_wsl():
    # Linux only: Windows' Python started from WSL (hold_windows_slots()) works in a \\wsl.localhost folder, where
    # /proc/version is WSL's (that side then took WSL's pool of 6 as the most of Windows' 16).
    if not sys.platform.startswith('linux'):
        return False
    with contextlib.suppress(OSError), open('/proc/version', encoding='utf-8', errors='replace') as f:
        return 'microsoft' in f.read().lower()
    return False


MACHINE_SLOTS = int(os.environ.get('EI_HARNESS_SLOTS') or ('6' if under_wsl() else '16'))
SLOT_DIR = os.path.join(tempfile.gettempdir(), 'ei_harness_slots')


def _lock(path):
    """Takes the one-byte lock on path without waiting; the open file, or None if another holds it."""
    f = open(path, 'a+b')
    try:
        if os.name == 'nt':
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            fcntl.lockf(f, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 0)
        return f
    except OSError:
        f.close()
        return None


def _unlock(f):
    f.seek(0)
    if os.name == 'nt':
        msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
    else:
        fcntl.lockf(f, fcntl.LOCK_UN, 1, 0)
    f.close()


@contextlib.contextmanager
def machine_slot(alone=False, count=1, most=None, held=None):
    """count of the machine's MACHINE_SLOTS game slots (shared by every checkout's run_all; 2 for a pair's
    run, games_of()), or as many as are free up to most once count of them are (hold_slots()), or all of them
    (alone); yields how many it holds. All of them: for a game that sets the display mode, which takes the surfaces
    of every other game running then (they fail with DDERR_SURFACELOST: "Can't begin scene", untextured models).
    Games that want all the slots queue on one more lock, so two of them never split the slots; while
    one holds it, no other game takes a slot (a suite's next game would take each slot as it frees,
    and the one that wants them all waited for the end of every other suite on the machine).
    held: a list that holds the (index, lock) pairs of the slots taken, for a caller that gives some back or takes
    more while it holds them (adjust_hold()); what is left in it is released at the end."""
    os.makedirs(SLOT_DIR, exist_ok=True)
    held = [] if held is None else held
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
                if not alone and len(held) == (most or count):
                    break
                f = _lock(os.path.join(SLOT_DIR, f'slot{i:02}'))
                if f:
                    held.append((i, f))
            if (len(held) == MACHINE_SLOTS) if alone else (len(held) >= count):
                break
            if not alone:
                # A pair's run takes its slots together: none held while waiting, or two pairs could each hold one.
                for _, f in held:
                    _unlock(f)
                del held[:]
            time.sleep(0.5)
        yield len(held)
    finally:
        for _, f in held:
            _unlock(f)
        if gate:
            _unlock(gate)


def take_free_slot(held):
    """Takes one more slot now if one is free (not the gate's, and not one of held's), appends it to held and returns
    True; False when none is free."""
    waiting = _lock(os.path.join(SLOT_DIR, 'alone'))
    if not waiting:
        return False
    _unlock(waiting)
    for i in range(MACHINE_SLOTS):
        if i not in [h[0] for h in held] and (f := _lock(os.path.join(SLOT_DIR, f'slot{i:02}'))):
            held.append((i, f))
            return True
    return False


def adjust_hold(held, want):
    """Brings the slots in held (machine_slot's list) to want: gives the last ones back, or takes the free ones, one
    at a time, without waiting (take_free_slot()); returns how many are held."""
    while len(held) > want:
        _unlock(held.pop()[1])
    while len(held) < want and take_free_slot(held):
        pass
    return len(held)


# The installs whose data the runs play, read only (EI_GAME_VANILLA, EI_GAME_REBIRTH).
INSTALL_ENV = {'vanilla': 'EI_GAME_VANILLA', 'rebirth': 'EI_GAME_REBIRTH'}
DEFAULT_INSTALLS = {'vanilla': r'F:\games\EvilIslands', 'rebirth': r'F:\games\Evil Islands (Rebirth)'}


def install_dir(data, environ=None):
    """The folder of the install whose data a run plays: data 'vanilla' or 'rebirth' (GOLDEN_INSTALL's names), from
    environ (default: this process's environment). A batch of work.py test carries the submitter's (install_environment):
    the stub data's runs (tools/stub_data.py) go through the scheduler too."""
    return (os.environ if environ is None else environ).get(INSTALL_ENV[data]) or DEFAULT_INSTALLS[data]


def use_unpacked(say=print):
    """--unpacked: the runs play each install's unpacked copy (tools/unpack_install.py: every archive replaced by the
    folder of its name, docs/notes/mods.md "Loose files") in <main checkout>/build/unpacked/<vanilla|rebirth>, made
    from install_dir() when it is not there; EI_GAME_VANILLA and EI_GAME_REBIRTH name the copies from then on."""
    root = os.path.join(main_build_dir() or os.path.join(ROOT, 'build'), 'unpacked')
    for data, name in INSTALL_ENV.items():
        target = os.path.join(root, data)
        unpack_install.make(install_dir(data), target, say=say)
        os.environ[name] = target


def use_converted(say=print, build=BUILD):
    """--converted: the runs play each unpacked copy's converted copy (tools/convert_install.py: the record files as
    TOML, docs/notes/mods.md "Records as TOML") in <checkout>/build/converted/<vanilla|rebirth>, made by the build's
    ei-convert when it is not there or another ei-convert made it; EI_GAME_VANILLA and EI_GAME_REBIRTH name the copies
    from then on. The copies are this checkout's: their TOML is its converter's."""
    use_unpacked(say)
    root = os.path.join(ROOT, 'build', 'converted')
    for data, name in INSTALL_ENV.items():
        target = os.path.join(root, data)
        convert_install.make(os.environ[name], target, os.path.join(build, convert_install.EXE), say=say)
        os.environ[name] = target


def install_environment(environ=None):
    """The install variables set in environ (default: this process's): {name: folder}."""
    environ = os.environ if environ is None else environ
    return {name: environ[name] for name in INSTALL_ENV.values() if environ.get(name)}


# EI_* variables that are not settings of a game: where the build, the installs and the references are, the machine's
# slots, and what launch() sets itself. Every other EI_* variable (EI_PROFILE, EI_DIGEST, EI_SAVETRACE, ...) is read by
# a game.
NOT_GAME_ENV = ('EI_BUILD_DIR', 'EI_HARNESS_SLOTS', 'EI_OUT_DIR', 'EI_HARNESS', 'EI_WIDESCREEN_DIR', 'EI_PAIR',
                'EI_GAME_ROOT', 'EI_INSTALL_ROOTS', 'EI_GAME_VANILLA', 'EI_GAME_REBIRTH',
                'EI_SHOW_WINDOWS', 'EI_VK_ICD')


def peer_scenario(scenario):
    """The peer's scenario of a pair (SV5d): a scenario's "peer <name>" line makes launch() start a second game on
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


# launch()'s results besides 0 (passed), 1 (failed) and 2 (no report, not started): the run passed its limit plus
# LIMIT_MARGIN_S and the game had to be ended from outside (TIMEOUT_EXIT), or it stayed alive after that (a thread stuck
# in a driver call: UNKILLABLE_EXIT). The game's own watchdog ends it at the limit and writes that into the report.
TIMEOUT_EXIT = 4
UNKILLABLE_EXIT = 5
LIMIT_MARGIN_S = 20
HARNESS_START_S = 30         # the harness writes its report as it loads: none by then means it never started
EXIT_HANG_S = 3              # a game still running this long after its scenario quit is hung on exit
EXIT_SLOW_S = 2.0            # a game's own part of the quit (the log's "exit: the quit request" to "exit: the exit functions
                             # done") over this fails the run (PL22: quitting was instant; the process's end itself
                             # took 2-3 s under 16 games at once: the system tearing down the GPU's memory, only a note)
IN_GAME_TIMEOUT = 'scenario exceeded its'
EXPECT_PREFIX = '# expect:'


def time_scale(environ=None):
    """EI_TIME_SCALE (default 1): the factor on the harness's real-time limits, for runners slower than a desktop (CI's
    llvmpipe; docs/notes/linux.md, "The CI job"). The game reads the same variable (harness.cpp, TimeScale); the world's
    clock is not scaled. environ: the game's environment (default: this process's)."""
    with contextlib.suppress(ValueError):
        scale = float((os.environ if environ is None else environ).get('EI_TIME_SCALE') or 1)
        if scale >= 1:
            return scale
    return 1.0


def scenario_limit(scenario, environ=None):
    """The scenario's time limit in seconds: its last "timeout <seconds>" line, default 60 (harness.cpp), times
    EI_TIME_SCALE (whole seconds)."""
    limit = 60
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                m = re.match(r'\s*timeout\s+(\d+)', text)
                if m:
                    limit = int(m.group(1))
    return int(limit * time_scale(environ))


ARGUMENTS_PREFIX = '# arguments:'


def scenario_arguments(scenario):
    """The exe's arguments a scenario's line "# arguments: <words>" gives (effect_viewer_standalone.txt: the effect), else
    none: [words]."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(ARGUMENTS_PREFIX):
                    return text[len(ARGUMENTS_PREFIX):].split()
    return []


EXE_PREFIX = '# exe:'
GAME_PROGRAM, EFFECTS_PROGRAM = 'ei-rebuild', 'ei-effects'


def scenario_program(scenario):
    """The program a scenario plays: its line "# exe: <name>" (ei-effects, the effect viewer), else the game's."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(EXE_PREFIX):
                    return text[len(EXE_PREFIX):].strip()
    return GAME_PROGRAM


def program_exe(program):
    """The program's file in a build folder (the game's an application bundle's on macOS)."""
    return EXE if program == GAME_PROGRAM else program + ('.exe' if os.name == 'nt' else '')


def program_log(program):
    """The log the program writes into its run folder."""
    return program + '.log'


INSTALLS_PREFIX = '# installs:'
SUITE_PREFIX = '# suite:'


def header_value(scenario, prefix):
    """The rest of a scenario's first line that starts with prefix ("# installs:", "# suite:"), stripped, or None."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(prefix):
                    return text[len(prefix):].strip()
    return None


def scenario_installs(name, root=ROOT):
    """The installs a scenario's line "# installs: vanilla" or "# installs: rebirth" (H74) restricts it to: its runs on
    any other are never made, named or not (a scenario whose steps are all for Rebirth's data, a Rebirth run whose code
    and checks the vanilla run already covers); None without one. --widescreen plays its scenarios on Rebirth's data
    whatever the line says (that comparison needs addon's WideScreen)."""
    value = header_value(os.path.join(root, 'tests', 'scenarios', name + '.txt'), INSTALLS_PREFIX)
    return set(value.split()) if value else None


def in_suite(name, root=ROOT):
    """Whether the suite plays the scenario when no names are given: not the screenshots_ ones, nor one with a line
    "# suite: named" (H74: kept for a tool or a workflow that names it, e.g. the CI gate boot, --widescreen's)."""
    if name.startswith(SCREENSHOT_PREFIX):
        return False
    return header_value(os.path.join(root, 'tests', 'scenarios', name + '.txt'), SUITE_PREFIX) != 'named'


def expected_result(scenario):
    """The result a scenario's line "# expect: <RESULT>" says it ends with (watchdog_*.txt: TIMEOUT), else None."""
    with contextlib.suppress(OSError):
        with open(scenario, encoding='utf-8', errors='replace') as f:
            for text in f:
                if text.startswith(EXPECT_PREFIX):
                    return text[len(EXPECT_PREFIX):].strip()
    return None


# When each run's games played, {run folder: [(start, end)]} in seconds since the epoch (a retry adds one): the install
# check (install_watch.py) names the runs whose game ran while an install changed.
RUN_WINDOWS = {}


def run(name, install, folder=None, root=ROOT, build=BUILD, take_slot=True, clock=None, game_env=None, held=1,
        slots=None):
    """One game of a scenario on an install (a pair's two games for a scenario with a "peer" line, the peer's in
    the run folder's peer\\): the image on standalone (the vanilla data) or standalone-rebirth. root: the checkout
    whose tests/scenarios it uses; build: the verify build folder (ei-rebuild.exe; the run folder is
    <build>/runs/<folder>).
    take_slot False: the caller already holds held machine slots for it (tools/work_sched.py); a pair whose caller
    holds one takes the second here. slots (with take_slot False): those slots as an object with release()
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


# ---- Starting a run's games
#
# launch() starts the image as a player's game starts (working directory and EI_GAME_ROOT the install), with the
# harness's variables: EI_HARNESS (the scenario), EI_OUT_DIR (the run folder: log, report, saves), EI_INSTALL_ROOTS (the
# installs no run may write in: tests/verify/harness.cpp's MapPath refuses those writes) and for a pair EI_PAIR. The games print into launch.txt
# in their run folders, followed by launch()'s own lines. A run ends whatever state the game is in: the game's watchdog
# ends it at the scenario's limit; if it cannot, launch() ends it LIMIT_MARGIN_S later. The games are in a
# game_process.Group, so none outlives this process.
#
# A pair (SV5d; tests/verify/harness_pair.h): a scenario's "peer <name>" line starts a second game beside the scenario's
# own on tests/scenarios/peer/<name>.txt, writing to <run folder>/peer. Each gets EI_PAIR=<pair name>,<side> (0 the
# scenario's game, 1 the peer) and the harness puts the two in lockstep on a network of their own. A game past its limit,
# or whose harness never started, ends both; the peer's result goes into the scenario's report (FAILED: peer: lines), so
# a pair has one result.

# On macOS the program is an application bundle's (docs/notes/macos.md).
EXE = ('ei-rebuild.exe' if os.name == 'nt' else
       os.path.join('ei-rebuild.app', 'Contents', 'MacOS', 'ei-rebuild') if sys.platform == 'darwin' else 'ei-rebuild')
BREAKPOINT_CODES = (0x80000003, -5)   # an INT3: the Windows exception code, SIGTRAP


class Game:
    """One game of a run: the scenario's own, or a pair's peer (label 'peer: ')."""

    def __init__(self, scenario, out, label='', environ=None):
        self.scenario, self.out, self.label = scenario, out, label
        self.limit = scenario_limit(scenario, environ)
        self.program = scenario_program(scenario)
        self.report = os.path.join(out, 'harness_report.txt')
        self.quit = os.path.join(out, 'harness_quit')   # the harness writes it when its scenario quits, after the report
        self.proc = None
        self.quit_seen = None
        self.exit_s = None   # seconds from the quit file to the process's end, seen while polling (watch)
        self.hung = False    # ended by watch after EXIT_HANG_S (a note of its own, not a slow exit)


def launch(scenario, out, data, build, env=None, command=None):
    """Plays the scenario with <build>/ei-rebuild.exe on the install `data` ('vanilla' or 'rebirth'), writing to out.
    env: the game's environment (None: this process's); command: what to start instead of the exe (tests). Returns
    (code, text): code 0 passed, 1 failed, 2 no report or not started, TIMEOUT_EXIT or UNKILLABLE_EXIT; text what the
    scenario's game printed and launch()'s own lines."""
    said = []
    games = [Game(scenario, out, environ=env)]
    peer = peer_scenario(scenario)
    if peer:
        if not os.path.exists(peer):
            return 2, f"the peer's scenario {peer} not found\n"
        games.append(Game(peer, os.path.join(out, 'peer'), 'peer: ', env))
    environ = os.environ if env is None else env
    game_root = install_dir(data, environ)
    base = dict(environ, EI_GAME_ROOT=game_root,
                EI_INSTALL_ROOTS=os.pathsep.join([game_root, install_dir('vanilla', environ), install_dir('rebirth', environ)]))
    pair = f'ei_pair_{os.getpid()}_{time.monotonic_ns()}'
    group = game_process.Group()
    outputs = []
    timed_out = unkillable = False
    try:
        for side, game in enumerate(games):
            os.makedirs(game.out, exist_ok=True)
            for path in (game.quit, game.report):     # an earlier run's report must not pass for this one
                with contextlib.suppress(OSError):
                    os.remove(path)
            game_env = dict(base, EI_OUT_DIR=game.out, EI_HARNESS=os.path.abspath(game.scenario))
            if len(games) > 1:
                game_env['EI_PAIR'] = f'{pair},{side}'
            outputs.append(open(os.path.join(game.out, 'launch.txt'), 'wb'))
            exe = program_exe(game.program)
            try:
                game.proc = group.start(command or [os.path.join(build, exe), *scenario_arguments(game.scenario)],
                                        game_root, game_env, outputs[-1])
            except OSError as e:
                said.append(f'cannot start {game.label}{os.path.join(build, exe)}: {e}')
                break
            said.append(f'started {game.label}{exe} (pid {game.proc.pid})')
        else:
            timed_out, unkillable = watch(games, group, said.append)
    finally:
        group.close()
        for game in games:
            if game.proc is not None:
                with contextlib.suppress(subprocess.TimeoutExpired):
                    game.proc.wait(5)
        for f in outputs:
            f.close()
    code = finish(games, game_root, data, timed_out, unkillable, said.append)
    for game in games:
        with contextlib.suppress(OSError):
            os.remove(game.quit)
    with contextlib.suppress(OSError), open(os.path.join(out, 'launch.txt'), 'a', encoding='utf8') as f:
        f.write(''.join(f'run_all: {line}\n' for line in said) + f'run_all: result code {code}\n')
    text = ''
    with contextlib.suppress(OSError), open(os.path.join(out, 'launch.txt'), encoding='utf8', errors='replace') as f:
        text = f.read()
    return code, text


def end_game(game, group, say):
    """Ends the game (and what it started), naming the windows it still has open (a dialog it waits on says why).
    False when it is still alive 15 s later (a thread stuck in a driver call keeps a killed process alive)."""
    for line in game_process.open_windows_bounded(game.proc.pid):
        say(f'{game.label}open window {line}')
    for _ in range(3):
        group.kill(game.proc)
        with contextlib.suppress(subprocess.TimeoutExpired):
            game.proc.wait(5)
            return True
    say(f'{game.label}the game (pid {game.proc.pid}) is still alive 15 s after it was ended: a thread of it is stuck in '
        'the kernel (a driver call) and the system cannot end it yet')
    return False


def watch(games, group, say):
    """Waits for the games, ending them at their limit plus LIMIT_MARGIN_S, when their harness has not started after
    HARNESS_START_S, or (that game alone) when it still runs EXIT_HANG_S after its scenario quit; where the threads of
    such a game wait goes to exit_hang.txt first. Returns (timed out, unkillable)."""
    started = time.monotonic()
    timed_out = unkillable = False
    also = ' and the other' if len(games) > 1 else ''

    def end_all():
        nonlocal unkillable
        for other in games:
            if other.proc.poll() is None:
                unkillable = not end_game(other, group, say) or unkillable

    while any(g.proc.poll() is None for g in games):
        running = next((g for g in games if g.proc.poll() is None), None)    # none: the last one ended just now
        if running:
            quitting = any(os.path.exists(g.quit) for g in games)     # after a quit the end is timed: poll closely
            with contextlib.suppress(subprocess.TimeoutExpired):
                running.proc.wait(0.02 if quitting else 0.5)
        elapsed = time.monotonic() - started
        for game in games:
            if game.proc.poll() is not None:
                if game.exit_s is None and os.path.exists(game.quit):
                    game.exit_s = max(0.0, time.time() - os.path.getmtime(game.quit))
                continue
            if elapsed >= game.limit + LIMIT_MARGIN_S:
                say(f"TIMEOUT: the {game.label}game is still running {elapsed:.0f} s after its start (the scenario's time "
                    f'limit is {game.limit} s); terminating it{also}')
                timed_out = True
                end_all()
                break
            if elapsed >= HARNESS_START_S and not os.path.exists(game.report):
                say(f'the harness did not start in the {game.label}game; terminating it{also}')
                end_all()
                break
            if not os.path.exists(game.quit):
                continue
            game.quit_seen = game.quit_seen or time.monotonic()
            if time.monotonic() - game.quit_seen >= EXIT_HANG_S:
                game.hung = True
                with contextlib.suppress(OSError), open(os.path.join(game.out, 'exit_hang.txt'), 'w') as f:
                    f.write(game_process.thread_stacks(game.proc))
                say(f'{game.label}game still running {EXIT_HANG_S} s after the scenario quit (its threads: exit_hang.txt); '
                    'terminating it')
                unkillable = not end_game(game, group, say) or unkillable
    return timed_out, unkillable


def read_lines(path):
    """The file's lines as bytes (line ends kept), or None when it cannot be read."""
    try:
        with open(path, 'rb') as f:
            return f.read().splitlines(keepends=True)
    except OSError:
        return None


def write_lines(path, lines):
    with contextlib.suppress(OSError), open(path, 'wb') as f:
        f.write(b''.join(lines))


def add_failures(report, failures):
    """"FAILED: <failure>" lines in the report in place of its result line, then the result FAIL."""
    if not failures:
        return
    added = [f'FAILED: {f}\n'.encode('ascii', 'replace') for f in failures] + [b'RESULT: FAIL\n']
    lines = read_lines(report) or []
    at = next((i for i, l in enumerate(lines) if l.startswith(b'RESULT: ')), None)
    write_lines(report, lines + added if at is None else lines[:at] + added + lines[at + 1:])


EXIT_STAMP = re.compile(r'^\[(\d\d):(\d\d):(\d\d)\.(\d\d\d)\] exit: (the quit request|the exit functions done)')


def exit_failures(game, say):
    """PL22: how the game's quit went, for a game that ended by itself after its scenario quit. A game that took a
    crash's way out (an abort at exit: the exit code 0xC0000409) or whose own stages took over EXIT_SLOW_S fails; the
    time from the quit to the process's end is a note only (it is the system's, and grows with the games at once)."""
    failures = []
    if game.hung or game.proc is None or not os.path.exists(game.quit):
        return failures
    code = game.proc.returncode
    if code not in (0, None):
        failures.append(f'{game.label}the game ended with exit code {code & 0xFFFFFFFF:08X} after its scenario quit')
    stamps = {}
    for line in read_lines(os.path.join(game.out, program_log(game.program))) or []:
        m = EXIT_STAMP.match(line.decode('utf8', 'replace'))
        if m:
            h, mi, s, ms = (int(v) for v in m.groups()[:4])
            stamps[m.group(5)] = h * 3600 + mi * 60 + s + ms / 1000
    if len(stamps) == 2:
        took = stamps['the exit functions done'] - stamps['the quit request']
        if took < 0:
            took += 86400
        if took > EXIT_SLOW_S:
            failures.append(f'{game.label}the game took {took:.1f} s from the quit request to the end of its exit functions '
                            f'(limit {EXIT_SLOW_S:g} s)')
    if game.exit_s is not None and game.exit_s > EXIT_SLOW_S:
        say(f'{game.label}game ended {game.exit_s:.1f} s after the scenario quit')
    return failures


def finish(games, game_root, data, timed_out, unkillable, say):
    """The run's result code once its games have ended, the report completed: a game that died before its harness wrote
    a result, the peer's result, the settings file the scenario expects."""
    if not timed_out and not unkillable:
        for game in games:
            if game.proc is not None and not game.quit_seen:
                report_silent_end(game, say)
    main = games[0]
    if not timed_out and not unkillable and os.path.exists(main.report):
        add_failures(main.report, [f for g in games for f in exit_failures(g, say)])
    if not os.path.exists(main.report):
        if main.proc is not None:
            say(f'the game exited (code {main.proc.returncode}) without a harness report')
        return 2
    if len(games) > 1:
        merge_peer_report(main.report, games[1].report, say)
    if not timed_out and not unkillable:
        check_settings_file(main.report, main.scenario, main.out, data == 'rebirth', game_root, say)
    if timed_out:
        return UNKILLABLE_EXIT if unkillable else TIMEOUT_EXIT
    return 0 if any(l.startswith(b'RESULT: PASS') for l in read_lines(main.report) or []) else 1


def report_silent_end(game, say):
    """A game that ended by itself while its report still says RUNNING died before the harness could write a result
    (an INT3, a crash the harness did not see): the report gets the reason instead of a silent RUNNING."""
    lines = read_lines(game.report)
    at = next((i for i, l in enumerate(lines or []) if l.startswith(b'RESULT: RUNNING')), None)
    if at is None or game.proc.returncode is None:
        return
    code = game.proc.returncode
    if code in BREAKPOINT_CODES or code & 0xFFFFFFFF == BREAKPOINT_CODES[0]:
        why = (f'the game ended on a breakpoint (exit code {code & 0xFFFFFFFF:08X}) before the harness wrote its result: '
               'an INT3 of the image with no code there (ei-rebuild.log, the run notes)')
    else:
        why = f'the game ended (exit code {code}) before the harness wrote its result'
    say(f'{game.label}{why}')
    lines[at:at + 1] = [f'FAILED: {why}\n'.encode(), b'RESULT: FAIL\n']
    write_lines(game.report, lines)


def merge_peer_report(report, peer_report, say):
    """The peer's result in the scenario's report: a peer that did not pass (or whose window or network checks saw
    something) adds "FAILED: peer: ..." lines and makes the result FAIL."""
    lines = read_lines(peer_report)
    failures = []
    if lines is None:
        failures.append('peer: no report (peer/harness_report.txt)')
    else:
        text = [l.decode('utf8', 'replace').rstrip('\r\n') for l in lines]
        failures += ['peer: ' + l[8:] for l in text if l.startswith('FAILED: ')]
        failures += ['peer: ' + l for l in text if l.startswith('WINDOW: ') and not l.startswith('WINDOW: out of sight')]
        failures += ['peer: ' + l for l in text if l.startswith('NET: ') and not l.startswith('NET: loopback only')]
        if not failures and not any(l.startswith('RESULT: PASS') for l in text):
            failures.append('peer: did not pass (peer/harness_report.txt)')
    say(f'peer: {"passed" if not failures else "did not pass"}')
    add_failures(report, failures)


# The image's settings file after the run (ST1a): a scenario line "# settings-file: <expected>" (a path from the
# scenario's folder) says the run leaves an ei_settings.ini in its folder that must equal the expected file line by line
# (line breaks apart; "{install}" in it stands for the run's install folder): what the game saved on exit in a "settingsfile writable|import" run
# (tests/verify/harness_settings.h). On the Rebirth install the expected file is <name>.rebirth<ext> (none: not
# checked). The exit save comes after the report, so the check is here, once the game has ended.
SETTINGS_FILE_PREFIX = '# settings-file:'


def expected_settings_file(scenario, rebirth):
    """The expected file the scenario names, or None (none named, or none for this install)."""
    with contextlib.suppress(OSError), open(scenario, encoding='utf-8', errors='replace') as f:
        for text in f:
            if not text.startswith(SETTINGS_FILE_PREFIX):
                continue
            name = text[len(SETTINGS_FILE_PREFIX):].strip()
            if not name:
                return None
            if rebirth:
                stem, ext = os.path.splitext(name)
                name = stem + '.rebirth' + ext
            path = os.path.join(os.path.dirname(scenario), *name.split('/'))
            return None if rebirth and not os.path.exists(path) else path
    return None


def check_settings_file(report, scenario, out, rebirth, install, say):
    expected_path = expected_settings_file(scenario, rebirth)
    if not expected_path or not any(l.startswith(b'RESULT: PASS') for l in read_lines(report) or []):
        return   # nothing to check, or another failure comes first
    failures = []
    actual_path = os.path.join(out, 'ei_settings.ini')
    if os.path.exists(actual_path + '.tmp'):
        failures.append('settings file: ei_settings.ini.tmp was left behind (the save did not finish its move)')
    actual, expected = read_lines(actual_path), read_lines(expected_path)
    if actual is None:
        failures.append('settings file: the run left no ei_settings.ini')
    elif expected is None:
        failures.append(f'settings file: cannot read the expected file ({expected_path})')
    else:
        a = [l.rstrip(b'\r\n').decode('utf8', 'replace') for l in actual]
        # "{install}" in the expected file: the run's install folder (the game writes its own folder's paths).
        e = [l.rstrip(b'\r\n').decode('utf8', 'replace').replace('{install}', install) for l in expected]
        shown = lambda lines, i: '(none)' if i >= len(lines) else '"' + lines[i][:150] + ('...' if len(lines[i]) > 150 else '') + '"'
        differing = [i for i in range(max(len(a), len(e))) if a[i:i + 1] != e[i:i + 1]]
        for i in differing[:6]:
            failures.append(f'settings file line {i + 1}: expected {shown(e, i)}, got {shown(a, i)}')
        if len(differing) > 6:
            failures.append(f'settings file: {len(differing)} lines differ from the expected file ({expected_path})')
    say(f'settings file: {"as expected" if not failures else "differs"}')
    add_failures(report, failures)


def empty_folder(out, tries=10):
    """Removes a run folder before its run, trying again for tries seconds when a file in it is still open (rmtree
    leaves such a file silently, and the game would then write beside a stale file or fail to create its own).
    Returns the names left in it, [] when it is gone."""
    left = []
    for attempt in range(tries):
        shutil.rmtree(out, ignore_errors=True)
        left = os.listdir(out) if os.path.isdir(out) else []
        if not left:
            return []
        time.sleep(1)
    return left


def missing_digest(result, environ, out):
    """The failure for a run that passed in digest mode (EI_DIGEST, --golden) without writing digest.txt: the golden
    compare would count it as differing and nothing else says why (H54)."""
    wanted = environ.get('EI_DIGEST') or '0'
    if result == 'PASS' and wanted.isdigit() and int(wanted) and not os.path.exists(os.path.join(out, 'digest.txt')):
        return f'no digest.txt in the run folder although EI_DIGEST={wanted} asked for one (the run passed without a state record)'
    return None


def run_once(name, install, folder, root, build, take_slot, clock, game_env, held=1, slots=None):
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(build, 'runs', folder or f'{name}-{install}')
    if install not in STANDALONE_INSTALLS:
        return 'FAIL', 0.0, [f'unknown install {install!r} (the image\'s: {", ".join(STANDALONE_INSTALLS)}; game.exe '
                             'is no longer run since H40)'], {}, []
    if low_disk := wait_for_disk(build):
        return 'FAIL', 0.0, [low_disk], {}, []
    if left := empty_folder(out):
        return 'FAIL', 0.0, [f'the run folder could not be emptied ({", ".join(left[:5])} left: a file in it is still open)'], {}, []
    os.makedirs(out, exist_ok=True)
    env = None
    if game_env is not None:
        env = {k: v for k, v in os.environ.items() if k not in game_environment()}
        env.update(game_env)
    # The prepare tools get the batch's variables and build, never the scheduler's own (H65): recorded_save.py plays its
    # games in this build.
    prepare_env = dict(os.environ if env is None else env, EI_BUILD_DIR=build)
    try:
        prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'), in_slot=not take_slot,
                                 environ=prepare_env)   # "# prepare:" lines
    except subprocess.CalledProcessError as error:
        tail = (error.stderr or error.stdout or b'').decode('utf8', 'replace').strip().splitlines()[-3:]
        return 'FAIL', 0.0, [f'a "# prepare:" tool failed ({" ".join(str(a) for a in error.cmd[1:])}; exit code '
                             f'{error.returncode}){": " + " | ".join(tail) if tail else ""}'], {}, []
    except OSError as error:
        return 'FAIL', 0.0, [f'a "# prepare:" tool could not run: {error}'], {}, []
    environ = os.environ if env is None else env
    if 'EI_RENDER' in environ:
        # The image's renderer options are its scenario's "render" step, never the environment's.
        env = {k: v for k, v in environ.items() if k != 'EI_RENDER'}
    if clock:
        env = dict(os.environ if env is None else env, EI_HARNESS_CLOCK=clock)
    games = games_of(scenario)
    # A pair whose caller holds one slot takes the second here.
    with (GAME_SLOTS.hold(games) if GAME_SLOTS else machine_slot(count=games)) if take_slot else \
            machine_slot(count=games - held) if games > held else contextlib.nullcontext():
        if slots:
            slots.acquire()
        start = time.time()
        try:
            code, text = launch(scenario, out, GOLDEN_INSTALL[install], build, env)
        finally:
            if slots:
                slots.release()       # the game has ended: the rest of the run needs no slot
        seconds = time.time() - start
        RUN_WINDOWS.setdefault(out, []).append((start, start + seconds))
    path = os.path.join(out, 'harness_report.txt')
    report = open(path, encoding='cp1251', errors='replace').read() if os.path.exists(path) else ''
    result = next((l[8:] for l in report.splitlines() if l.startswith('RESULT: ')), 'NO REPORT')
    failures = [l[8:] for l in report.splitlines() if l.startswith('FAILED: ')]
    if result == 'RUNNING' and not failures and code not in (TIMEOUT_EXIT, UNKILLABLE_EXIT):
        failures = [f'the game ended before the harness wrote its result (result code {code}; the game\'s output and '
                    'run_all\'s lines in launch.txt)']
    # The per-function totals (verify wrappers, until S21d); the slot stays in the result (the work database's run
    # rows keep its shape), always empty.
    counts = {}
    notes = [l for l in text.splitlines()
             if 'terminating' in l or 'not found' in l or 'open window' in l or 'still alive' in l
             or l.startswith('ei-rebuild: ')]
    if code in (TIMEOUT_EXIT, UNKILLABLE_EXIT) or any(f.startswith(IN_GAME_TIMEOUT) for f in failures):
        result = 'TIMEOUT'
        if not failures:
            failures = [f'scenario exceeded its {scenario_limit(scenario, environ)} s time limit; run_all ended the game'
                        + ('' if code != UNKILLABLE_EXIT else ' (it did not die: a thread is stuck in the kernel)')]
        if code == UNKILLABLE_EXIT:
            failures.append('the game process is still alive after it was ended')
    if any('the game crashed' in f or 'no code at' in f for f in failures):
        notes.append(crash_stack_note(out, build, root))
    if asan := asan_failure(out):
        failures.append(asan)
        if result == 'PASS':
            result = 'FAIL'
    expected = expected_result(scenario)
    if not expected and scenario_program(scenario) == GAME_PROGRAM and (lost := missing_digest(result, environ, out)):
        failures.append(lost)
        result = 'FAIL'
        add_failures(path, [lost])
    if expected and result == expected and code != UNKILLABLE_EXIT:
        result = 'PASS'
        failures = []
        notes.append(f'ended as the scenario expects ({expected})')
    notes += [l for l in report.splitlines() if l.startswith('EXIT: ')]   # a message box after quit
    notes += [l for l in report.splitlines() if l.startswith('WINDOW: in sight')]   # WB12: a window the user saw
    notes += [l for l in report.splitlines() if l.startswith('NET: outside')]   # WB14: a bind the firewall asks about
    return result, seconds, failures, counts, notes


def watch_installs(installs, environ=None):
    """H47a: the install_watch.Watch of the installs the runs of `installs` (their run_all names: standalone,
    standalone-rebirth) play; None when listing them fails (the check is then left out, not the suite)."""
    try:
        return install_watch.Watch([install_dir(GOLDEN_INSTALL[i], environ) for i in sorted(set(installs)) if i in GOLDEN_INSTALL])
    except Exception as e:
        print(f'install trees not listed ({type(e).__name__}: {e}): runs are not checked for writes into them')
        return None


def check_installs(watch, jobs, results, out=None, build=BUILD):
    """H47a: after the suite, results {(scenario, install): (result, seconds, failures, notes)} with the runs that
    overlap a change in an install failed (install_watch.Watch.apply). Never raises."""
    if watch is None:
        return results
    try:
        folders = {job: os.path.join(build, 'runs', f'{job[0]}-{job[1]}') for job in jobs}
        windows = {f: list(RUN_WINDOWS.get(f, ())) for f in folders.values()}      # this suite's runs only
        return watch.apply(results, folders, {f: w for f, w in windows.items() if w}, out)
    except Exception as e:
        print(f'install trees not checked ({type(e).__name__}: {e})', file=out or sys.stdout)
        return results


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


ATTEMPT_FILES = ('ei-rebuild.log', 'ei-effects.log', 'harness_report.txt')


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


# A scenario line that makes the run depend on the install (SV9): a step for one install only, EI_Plugin's values
# (which a Rebirth install's EI_Plugin.ini sets) or the WideScreen switch.
REBIRTH_LINE = re.compile(r'^\s*(?:rebirth|vanilla|plugin|widescreen)\s', re.M)


def rebirth_scenarios(names, root=ROOT, goldens=True):
    """The Rebirth set among names (SV9: Rebirth's data only where it decides something): the scenarios
    config/rebirth_scenarios.csv marks 1 (Rebirth's maps, scripts, resources), those with a REBIRTH_LINE and (goldens)
    every one with a Rebirth golden (H51a: a golden no suite plays goes stale unseen after a golden-moving merge)."""
    path = os.path.join(root, 'config', 'rebirth_scenarios.csv')
    marked = set()
    with contextlib.suppress(OSError), open(path, encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2 and parts[1] == '1':
                marked.add(parts[0])
    smoke = set()
    for n in names:
        installs = scenario_installs(n, root)
        if installs is not None:
            if 'rebirth' in installs:
                smoke.add(n)
            continue
        if n in marked or (goldens and has_golden(n, 'rebirth', root)):
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
    tier = ap.add_mutually_exclusive_group()
    tier.add_argument('--smoke', action='store_true')   # the smoke tier (H74); work.py test's default without names
    tier.add_argument('--full', action='store_true')    # the whole suite (run_all's default; work.py test's choice)
    copies = ap.add_mutually_exclusive_group()
    copies.add_argument('--unpacked', action='store_true')
    copies.add_argument('--converted', action='store_true')
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--clock')
    ap.add_argument('--jobs', type=int)      # default: 16, under WSL on Windows' GPU main()'s (WSL_GPU_SLOTS)
    driver = ap.add_mutually_exclusive_group()
    driver.add_argument('--dzn', action='store_true')
    driver.add_argument('--lavapipe', action='store_true')
    ap.add_argument('--hold-slots', type=int, help=argparse.SUPPRESS)   # hold_windows_slots()'s Windows side
    ap.add_argument('--hold-most', type=int, help=argparse.SUPPRESS)
    return ap.parse_args(argv)


def has_golden(name, install, root=ROOT):
    """Whether tests/golden holds a golden of the scenario for install ('vanilla' or 'rebirth')."""
    return os.path.exists(os.path.join(root, 'tests', 'golden', name, install, 'golden.json'))


# The scenarios that take the README's and the release notes' screenshots (docs/screenshots): by name only, never in
# a suite (no goldens; 4K frames with msaa 8).
SCREENSHOT_PREFIX = 'screenshots_'


def suite_names(root=ROOT, left_out=()):
    """The scenarios a run without names plays: every tests/scenarios/*.txt in the suite (in_suite()) but left_out."""
    return sorted(n for p in glob.glob(os.path.join(root, 'tests', 'scenarios', '*.txt'))
                  if (n := os.path.splitext(os.path.basename(p))[0]) not in left_out and in_suite(n, root))


def allowed(name, install, root=ROOT):
    """Whether the scenario runs on install (standalone or standalone-rebirth) at all (scenario_installs())."""
    installs = scenario_installs(name, root)
    return installs is None or ('rebirth' if install == 'standalone-rebirth' else 'vanilla') in installs


# What the converted installs (--converted) read that the others do not (H74: the converted pass runs only when one
# of these changed): the converter and its install copies, the loaders of the converted formats (TOML records and
# maps' objects, PNG textures and heights, glTF figures and animations, Lua scripts, map folders, zips) and the
# repository's own content (assets: vanilla content through the modders' path).
CONVERTED_PATHS = ('tools/convert/', 'tools/convert_install.py', 'tools/unpack_install.py', 'src/engine/data/',
                   'src/engine/models/figure_gltf', 'src/engine/models/animation_db', 'src/engine/models/texture_png', 'src/engine/mods/',
                   'src/game/map/mob_', 'src/game/script/script_lua', 'src/lib/json', 'src/lib/png', 'src/lib/toml',
                   'src/platform/zip_folders', 'assets/')


def git_lines(root, *command):
    result = subprocess.run(['git', *command], cwd=root, capture_output=True, text=True)
    return result.stdout.split() if result.returncode == 0 else None


def converted_base(root=ROOT):
    """What a checkout's changes are counted against: on main, origin/main (everything not pushed yet: the merge and
    any ticket commits after it; HEAD^1 missed a merge followed by a status commit), else HEAD^1; on a branch, its
    merge base with main."""
    branch = git_lines(root, 'rev-parse', '--abbrev-ref', 'HEAD')
    if branch == ['main']:
        pushed = git_lines(root, 'rev-parse', '--verify', '-q', 'origin/main')
        ahead = pushed and git_lines(root, 'rev-list', '--count', 'origin/main..HEAD')
        return 'origin/main' if ahead and ahead != ['0'] else 'HEAD^1'
    base = git_lines(root, 'merge-base', 'HEAD', 'main')
    return base[0] if base else 'HEAD'


def converted_changes(root=ROOT):
    """The changed files (committed or not, against converted_base()) under CONVERTED_PATHS."""
    changed = git_lines(root, 'diff', '--name-only', converted_base(root)) or []
    return [f for f in changed if f.startswith(CONVERTED_PATHS)]


SMOKE_FILE = os.path.join('config', 'smoke_scenarios.csv')


def smoke_runs(root=ROOT):
    """The smoke tier's runs (H74; config/smoke_scenarios.csv, written by tools/suite_coverage.py --smoke-file: the
    runs with the most coverage per run): [(scenario, install)], install standalone or standalone-rebirth."""
    runs = []
    with open(os.path.join(root, SMOKE_FILE), encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2 and not line.startswith('#') and parts[0] != 'scenario':
                runs.append((parts[0], 'standalone-rebirth' if parts[1] == 'rebirth' else 'standalone'))
    return runs


def plan(args, root=ROOT, left_out=()):
    """The runs args ask for: (scenario, install) pairs in the summary's order. install is standalone (the vanilla
    data) or standalone-rebirth; the run folder is <scenario>-<install>. Also used by tools/work_sched.py
    (`work.py test`), so a batch holds the runs run_all would make. left_out: scenarios that no names given means
    leaving out (linux_left_out())."""
    names = args.scenarios or suite_names(root, left_out)
    if args.smoke and args.scenarios:
        sys.exit('--smoke plays the smoke tier\'s list (config/smoke_scenarios.csv): not with scenario names')
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
    if args.rebirth_only:
        names = [n for n in names if n in rebirth]
    jobs = [(name, install) for name in names for install in installs
            if (install != 'standalone-rebirth' or name in rebirth) and allowed(name, install, root)]
    if args.smoke:
        # The smoke tier (H74): its list's runs, on the installs asked for (--vanilla-only, --rebirth-only)
        jobs = [(name, install) for name, install in smoke_runs(root) if install in installs and allowed(name, install, root)]
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
    # The scheduler's batches (tools/work_sched.py) ignore --jobs: they take every free slot.
    print(f'\n{len(jobs)} runs in {elapsed:.0f}s' + (f' with {args.jobs} at once' if args.jobs else ''), file=out)
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
GOLDEN_DETAIL_RUNS = 6   # --golden: runs that differ shown in full (golden.py explain's summary); the rest one line each


def golden_check(jobs, out=None, root=ROOT, build=BUILD):
    """--golden: every run of jobs whose scenario has a golden for its install against it (tools/golden.py
    compare). Prints the runs that differ and a count; True when none differs."""
    import golden
    out = out or sys.stdout
    counts = collections.Counter()
    heads = []
    print('\ngoldens (tests/golden):', file=out)
    tasks = [(name, GOLDEN_INSTALL.get(install, install), os.path.join(build, 'runs', f'{name}-{install}'))
             for name, install in jobs]
    # The comparisons run in a pool (a core each); their results come back in the jobs' order, so the text is the same.
    for (name, install), (result, lines) in zip(jobs, golden.compare_many(tasks, root=root)):
        counts[result] += 1
        if result == 'DIFFERS':
            # golden_explain's headline (the first difference, whether outcomes changed, the largest unit move)
            # (a run that did not pass: its failure, golden.compare_one's first line)
            heads.append((name, install, lines[0] if lines and 'the run did not pass' in lines[0] else
                          next((t for t in lines if t.startswith('digest:')), lines[0] if lines else '')))
        if result != 'NO GOLDEN' and (result != 'SAME' or len(jobs) == 1):
            if result == 'DIFFERS' and len(heads) > GOLDEN_DETAIL_RUNS:
                continue   # past the first few, the roll-up below says what differs
            print(f'  {name:<18} {install:<8} {result}', file=out)
            for text in lines:
                print(f'      {text}', file=out)
    if len(heads) > 1:
        print(f'  the {len(heads)} that differ, one line each (golden.py explain / compare for the details):', file=out)
        for name, install, head in heads:
            print(f'    {name} {install}: {head[:260]}', file=out)
    print(f'  {counts["SAME"]} the same, {counts["DIFFERS"]} differ, {counts["NO RUN"]} without a run, '
          f'{counts["NO GOLDEN"]} without a golden', file=out)
    ok = not counts['DIFFERS'] and not counts['NO RUN']
    print('GOLDENS THE SAME' if ok else 'GOLDENS DIFFER', file=out)
    return ok


# The headless display's size: the goldens' machine has a 4K desktop and the scenarios that say "resolution desktop"
# (ui_widescreen_*: the layout, the clicks) take its size, so a smaller screen plays them differently (H47d).
HEADLESS_SCREEN = '3840x2160x24'


def start_headless_display(environ=None):
    """Linux, without EI_SHOW_WINDOWS=1: starts one Xvfb on a free display number and points DISPLAY at it for
    every game (WAYLAND_DISPLAY unset), so a local run opens no window on the desktop, as Windows' runs are out of
    sight. Stopped when this process ends. Returns the Xvfb's Popen or None (not Linux, windows asked for, or no
    Xvfb: the games then use the session's display)."""
    environ = os.environ if environ is None else environ
    if not sys.platform.startswith('linux') or environ.get('EI_SHOW_WINDOWS'):
        return None
    xvfb = shutil.which('Xvfb')
    if not xvfb:
        print('no Xvfb installed: the games open their windows on the session display (EI_SHOW_WINDOWS=1 says so on purpose)')
        return None
    # xvfb-run's way: an explicit number from 99 down, the server dies on a number that is taken and the next is
    # tried; -displayfd gives the number once it is up. (Xvfb choosing the number itself counts a refused socket as a
    # taken number, and under WSL, where /tmp/.X11-unix is WSLg's, every number refuses one.)
    for display in range(99, 49, -1):
        read_end, write_end = os.pipe()
        proc = subprocess.Popen([xvfb, f':{display}', '-screen', '0', HEADLESS_SCREEN, '-nolisten', 'tcp',
                                 '-displayfd', str(write_end)], pass_fds=(write_end,), stdin=subprocess.DEVNULL,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_end)
        with os.fdopen(read_end, encoding='ascii', errors='replace') as f:
            number = f.readline().strip()   # empty when the server died
        if number.isdigit():
            break
        proc.wait()
    else:
        print('Xvfb did not start: the games open their windows on the session display')
        return None
    environ['DISPLAY'] = f':{number}'
    environ.pop('WAYLAND_DISPLAY', None)
    atexit.register(lambda: (proc.terminate(), proc.wait()))
    return proc


# Dozen's driver manifest where the Mesa build of linux.md installs it (docs/notes/linux.md, "The GPU under WSL").
DZN_ICD = os.path.join('~', 'mesa-dzn', 'share', 'vulkan', 'icd.d', 'dzn_icd.x86_64.json')

# Linux, no scenario names given: the scenarios left out (docs/notes/linux.md, "The harness" and "The GPU under WSL").
# On the system's driver (lavapipe, the CPU): CI's LINUX_SKIP (.github/workflows/suites-linux.yml) and a few more
# that pass 60 s on llvmpipe. render_batch's render_terrain_seams (the level steps) is lavapipe's own difference; it
# passes on a GPU.
# On a GPU (dzn): net_pair_master, whose peer list shows the server one poll sooner on any Linux.
# The losedevice scenarios (D7a) remove the Direct3D 12 device, which Linux has no way to; left out on every Linux run.
D3D12_ONLY_PREFIX = 'render_device_lost_'

LINUX_LEFT_OUT = {
    'system': ('net_pair_kick', 'net_pair_leave', 'net_pair_master', 'net_pair_quest', 'net_pair_speed',
               'render_batch', 'camera_batch', 'render_stats', 'zone_gz15h_tornado'),
    'gpu': ('net_pair_master',),
}

# Under WSL on Windows' GPU (dzn), games at once by default: the Windows game slots free when the run starts, at most
# this many (EI_HARNESS_SLOTS sets the most instead). Half the machine's 16, so Windows' suites (work.py test's batches
# of up to 4 jobs) keep the other half, and about the most WSL's 16 GB holds: dzn's GPU memory counts there, 8 games
# peaked at 12 GB (docs/notes/linux.md, "The GPU under WSL").
WSL_GPU_SLOTS = 8


def linux_left_out(gpu):
    """The scenarios a Linux run leaves out when no names are given (LINUX_LEFT_OUT), for the games' Vulkan driver
    gpu (vulkan_driver()'s manifest, or None for the system's); () elsewhere."""
    if not sys.platform.startswith('linux'):
        return ()
    folder = os.path.join(ROOT, 'tests', 'scenarios')
    names = os.listdir(folder) if os.path.isdir(folder) else []
    d3d12_only = tuple(n[:-4] for n in names if n.startswith(D3D12_ONLY_PREFIX) and n.endswith('.txt'))
    return LINUX_LEFT_OUT['gpu' if gpu else 'system'] + d3d12_only


def vulkan_driver(choice=None, environ=None, wsl=None):
    """Linux: points the games' Vulkan loader at one driver manifest (VK_DRIVER_FILES, and VK_ICD_FILENAMES for older
    loaders) and returns it, or None for the system's drivers (nothing set). choice: 'dzn' (--dzn), 'system'
    (--lavapipe), else EI_VK_ICD's: dzn, system or a manifest's path; neither: Dozen's (DZN_ICD) under WSL when it is
    installed (the local default; GitHub's runner is no WSL and stays on lavapipe), else the system's. Exits when the
    one asked for is missing."""
    environ = os.environ if environ is None else environ
    choice = choice or environ.get('EI_VK_ICD', '')
    if not sys.platform.startswith('linux'):
        if choice:
            sys.exit('--dzn / --lavapipe / EI_VK_ICD choose the Vulkan driver of the Linux games only')
        return None
    if not choice and (under_wsl() if wsl is None else wsl) and os.path.isfile(os.path.expanduser(DZN_ICD)):
        choice = 'dzn'
    if not choice or choice == 'system':
        print("Vulkan driver: the system's")
        return None
    manifest = os.path.expanduser(DZN_ICD if choice == 'dzn' else choice)
    if not os.path.isfile(manifest):
        sys.exit(f'no Vulkan driver manifest {manifest} (docs/notes/linux.md, "The GPU under WSL")')
    environ['VK_DRIVER_FILES'] = environ['VK_ICD_FILENAMES'] = manifest
    print(f'Vulkan driver: {manifest}')
    return manifest


class GameSlots:
    """The games a run on Windows' GPU may have going at once (WSL; hold_windows_slots()): count of them for a game
    (2 for a pair), as many as capacity, which follows the slots held on Windows' side (rebalance()). Games wait in
    the order they came."""

    def __init__(self, capacity):
        self.cond = threading.Condition()
        self.capacity = capacity
        self.used = 0
        self.queue = []

    @contextlib.contextmanager
    def hold(self, count):
        ticket = object()
        with self.cond:
            self.queue.append(ticket)
            try:
                self.cond.wait_for(lambda: self.queue[0] is ticket and self.used + count <= self.capacity)
            finally:
                self.queue.remove(ticket)
                self.cond.notify_all()
            self.used += count
        try:
            yield
        finally:
            with self.cond:
                self.used -= count
                self.cond.notify_all()

    def resize(self, capacity):
        """Sets the capacity, but not below what the running games use; returns it."""
        with self.cond:
            self.capacity = max(capacity, self.used)
            self.cond.notify_all()
            return self.capacity


# Seconds between two looks at how many Windows slots the run wants and could have (follow_need()).
HOLD_FOLLOW_S = 2.0
GAME_SLOTS = None      # main(): the GameSlots of a run on Windows' GPU under WSL


def rebalance(hold, slots, remaining, most):
    """One step of a WSL run's hold on Windows' game slots (hold: WindowsHold, or anything with want(n) -> held): the
    run wants as many slots as it has game units left (remaining: running and queued games, a pair's two), at most
    most. Fewer than it holds: the unused ones go back (the running games keep theirs). More: it takes the free ones,
    one at a time, without waiting. slots (GameSlots) follows what is held. Returns how many are held."""
    want = min(most, remaining)
    if want < slots.capacity:
        return hold.want(slots.resize(want))
    if want > slots.capacity:
        return slots.resize(hold.want(want))
    return slots.capacity


def follow_need(hold, slots, remaining, most, stop, period=None):
    """Thread body of a WSL run on Windows' GPU: rebalance() every period seconds (HOLD_FOLLOW_S) until stop (a
    threading.Event) is set; remaining() -> the game units left."""
    while not stop.wait(HOLD_FOLLOW_S if period is None else period):
        if remaining() > 0:
            rebalance(hold, slots, remaining(), most)


class WindowsHold:
    """Windows' side of hold_windows_slots(): the Windows process that holds the slots (hold_slots()), and its
    stdin commands; held is how many."""

    def __init__(self, proc, held):
        self.proc, self.held, self.lock = proc, held, threading.Lock()

    def want(self, count):
        """Asks for count slots (hold_slots()): the Windows side takes the free ones and gives back the ones above
        count at once. Returns how many it holds."""
        with self.lock:
            self.proc.stdin.write(f'want {count}\n')
            self.proc.stdin.flush()
            reply = self.proc.stdout.readline().split()
            if len(reply) != 2 or reply[0] != 'held' or not reply[1].isdigit():
                sys.exit("Windows' game slots could not be changed")
            self.held = int(reply[1])
            return self.held


def hold_windows_slots(count, most=None):
    """WSL, games on Windows' GPU (vulkan_driver()): Windows' Python (python.exe through WSL's interop) runs this file
    with --hold-slots count (and --hold-most most), which takes that many of Windows' machine slots (or as many as
    are free up to most, once count are; machine_slot(), the same lock files as Windows' suites and work.py's
    scheduler) and keeps them until this process ends, so Windows' and WSL's games on the GPU together stay within
    Windows' 16 (24 crashed Windows: the NVIDIA driver hung). Waits until they are held; returns a WindowsHold, whose
    want() takes more slots or gives some back later. Exits when Windows' Python cannot be started."""
    python = os.environ.get('EI_WINDOWS_PYTHON') or shutil.which('python.exe')
    if not python:
        sys.exit("no Windows python.exe on PATH (EI_WINDOWS_PYTHON names one): the games on Windows' GPU need its slots")
    script = subprocess.run(['wslpath', '-w', os.path.abspath(__file__)], capture_output=True, text=True).stdout.strip()
    print(f"waiting for {count}{f' to {most}' if most and most > count else ''} of Windows' game slots")
    sys.stdout.flush()
    proc = subprocess.Popen([python, script, '--hold-slots', str(count)] + (['--hold-most', str(most)] if most else []),
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    reply = proc.stdout.readline().split()
    if len(reply) != 2 or reply[0] != 'held' or not reply[1].isdigit():
        proc.kill()
        sys.exit("Windows' game slots could not be taken")
    atexit.register(lambda: (proc.stdin.close(), proc.wait()))
    print(f"holding {reply[1]} of Windows' game slots")
    return WindowsHold(proc, int(reply[1]))


def hold_slots(count, most=None, stdin=None):
    """--hold-slots (Windows' side of hold_windows_slots()): count machine slots, or as many as are free up to most,
    until stdin closes; prints "held <n>" once they are held. A line "want <n>" on stdin brings the hold to n slots
    (adjust_hold(): the free ones are taken without waiting, the ones above n given back) and prints "held <m>"."""
    stdin = sys.stdin if stdin is None else stdin
    held = []
    with machine_slot(count=count, most=most, held=held) as n:
        print(f'held {n}')
        sys.stdout.flush()
        while line := stdin.readline():
            words = line.split()
            if len(words) == 2 and words[0] == 'want' and words[1].isdigit():
                adjust_hold(held, int(words[1]))
                print(f'held {len(held)}')
                sys.stdout.flush()
    return 0


def main():
    global GAME_SLOTS
    args = parse_args()
    if args.hold_slots:
        return hold_slots(args.hold_slots, args.hold_most)
    if args.unpacked:
        use_unpacked()
    if args.converted:
        use_converted()
    if args.golden:
        os.environ['EI_DIGEST'] = str(GOLDEN_PERIOD)
    start_headless_display()
    gpu = vulkan_driver('dzn' if args.dzn else 'system' if args.lavapipe else None)
    jobs = plan(args, left_out=linux_left_out(gpu))
    hold = most = None
    if gpu and under_wsl():
        # The slots held on Windows' side bound this run's games; WSL's own pool (6, for lavapipe's memory) does not.
        games = max([games_of(os.path.join(ROOT, 'tests', 'scenarios', n + '.txt')) for n, _ in jobs] or [1])
        if args.jobs:
            hold = hold_windows_slots(min(16, max(1, args.jobs) * games))
        else:
            most = max(games, int(os.environ.get('EI_HARNESS_SLOTS') or WSL_GPU_SLOTS))
            hold = hold_windows_slots(games, most)
        GAME_SLOTS = GameSlots(hold.held)
        args.jobs = args.jobs or most
    args.jobs = max(1, args.jobs or 16)
    build = BUILD
    db = timing_db()
    times = expected_times(jobs, build=build, db=db)
    order = sorted(jobs, key=lambda j: -(times[j] if times[j] is not None else 1000))   # new scenarios first

    results = {}
    lock = threading.Lock()
    started = time.time()
    print(HEADER)
    sys.stdout.flush()

    watch = watch_installs([i for _, i in jobs])
    left = {j: games_of(os.path.join(ROOT, 'tests', 'scenarios', j[0] + '.txt')) for j in jobs} if most else {}
    stop = threading.Event()
    if left:
        # Windows' slots follow the run's need: more as they free, back as the queue empties (H50a1).
        threading.Thread(target=follow_need, args=(hold, GAME_SLOTS, lambda: sum(left.values()), most, stop),
                         daemon=True).start()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_with_retries, n, i, args.retries, f'{n}-{i}',
                               functools.partial(run, build=build, clock=args.clock),
                               functools.partial(keep_attempt, build=build)): (n, i)
                   for n, i in order}
        for future in concurrent.futures.as_completed(futures):
            name, install = futures[future]
            result, seconds, failures, _, notes = future.result()
            with lock:
                left.pop((name, install), None)
                results[(name, install)] = (result, seconds, failures, notes)
                record_time(db, name, install, seconds, result, notes=notes)
                print(line(name, install, result, seconds, failures, notes, build=build))
                sys.stdout.flush()

    stop.set()
    save_times({f'{n}-{i}': r[1] for (n, i), r in results.items()}, build=build)
    check_installs(watch, jobs, results, build=build)
    ok = summary(jobs, results, args, time.time() - started, build=build)
    if args.widescreen:
        ok = widescreen_check(jobs, build=build) and ok
    if args.golden:
        ok = golden_check(jobs, build=build) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
