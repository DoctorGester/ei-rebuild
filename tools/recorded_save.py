"""A "# prepare:" tool: the save that an earlier part of a cut recording makes (docs/notes/harness.md, "Recorded play").

    # prepare: recorded_save.py <earlier part's scenario> <slot> [vanilla|rebirth]

Called as every prepare tool is, with the run's harness_saves folder first. A long recording is cut into parts
(tests/verify/harness_record.h, "Parts"): the earlier part ends with the game's quick save (F5, slot saveq) and the next
part begins by loading it. Saves are made by the game from its data, so none is committed: this tool plays the earlier
part (its own prepare lines first, so a chain of parts plays each once) and copies the slot from that run into the
harness_saves folder. The save is kept in <build>/recorded_saves/<scenario>/ for the next part's run and the next, until
the verify build, the install or one of the scenarios changes. Two runs that want the same save wait for each other. The
game plays in the slot of the run being prepared (run_all holds it, EI_PREPARE_IN_SLOT); called by hand the tool takes one.
"""
import contextlib
import hashlib
import os
import shutil
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)

import prepare_scenario
import run_all

PREFIX = '# prepare: recorded_save.py'


def root_of(tools=TOOLS):
    return os.path.dirname(tools)


def scenario_file(name, root):
    return os.path.join(root, 'tests', 'scenarios', name + '.txt')


def chain(name, root):
    """The scenarios a part's save depends on: name and, through its prepare line, the parts before it."""
    found = [name]
    with open(scenario_file(name, root), encoding='utf-8', errors='replace') as f:
        for line in f:
            if line.startswith(PREFIX):
                found += chain(line[len('# prepare:'):].split()[1], root)
    return found


def cache_key(name, install, build, root):
    h = hashlib.sha256()
    exe = os.path.join(build, run_all.EXE)
    stat = os.stat(exe)
    h.update(f'{stat.st_size} {stat.st_mtime_ns} {install}'.encode())
    for part in chain(name, root):
        with open(scenario_file(part, root), 'rb') as f:
            h.update(f.read())
    return h.hexdigest()


@contextlib.contextmanager
def locked(path):
    """The lock file `path` (waits for it): one run at a time makes a given save."""
    f = None
    while f is None:
        f = run_all._lock(path)
        if f is None:
            time.sleep(0.5)
    try:
        yield
    finally:
        run_all._unlock(f)


def play(name, install, root, build, cache):
    """Plays the scenario `name` (a part of a cut recording) in <cache>\\run; its harness_saves is what it saved."""
    out = os.path.join(cache, 'run')
    if left := run_all.empty_folder(out):
        sys.exit(f'recorded_save: {out} cannot be emptied ({", ".join(left[:3])} left)')
    os.makedirs(out)
    scenario = scenario_file(name, root)
    env = dict(os.environ, EI_DIGEST=str(run_all.GOLDEN_PERIOD))   # as the golden runs of the part play
    # The game plays in the slot of the run being prepared (run_all holds one for it and its game has not started:
    # EI_PREPARE_IN_SLOT); called by hand, it takes a slot through the machine's scheduler and waits for it however long.
    # Never a slot beyond the machine's 16.
    in_slot = os.environ.get(prepare_scenario.IN_SLOT) == '1'
    with contextlib.nullcontext() if in_slot else run_all.machine_slot():
        prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'), in_slot=True)   # (the earlier parts' saves)
        code, text = run_all.launch(scenario, out, install, build, env)
    if code != 0:
        sys.exit(f'recorded_save: {name} (the part before) ended with code {code}: {os.path.join(out, "launch.txt")}; '
                 f'{os.path.join(out, "harness_report.txt")}')
    return os.path.join(out, 'harness_saves')


def make(saves, name, slot, install='vanilla', root=None, build=None):
    root = root or root_of()
    build = build or run_all.BUILD
    data = {'vanilla': 'vanilla', 'rebirth': 'rebirth'}[install]
    cache = os.path.join(build, 'recorded_saves', name)
    os.makedirs(cache, exist_ok=True)
    key = cache_key(name, data, build, root)
    kept = os.path.join(cache, slot)
    key_file = os.path.join(cache, 'key.txt')
    with locked(os.path.join(cache, 'lock')):
        current = os.path.exists(key_file) and open(key_file).read() == key and os.path.isdir(kept)
        if not current:
            with contextlib.suppress(OSError):
                os.remove(key_file)
            shutil.rmtree(kept, ignore_errors=True)
            made = play(name, data, root, build, cache)
            if not os.path.isdir(os.path.join(made, slot)):
                sys.exit(f'recorded_save: the run of {name} saved no {slot} (its harness_saves: {made})')
            shutil.copytree(os.path.join(made, slot), kept)
            with open(key_file, 'w') as f:
                f.write(key)
    shutil.copytree(kept, os.path.join(saves, slot))


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    make(sys.argv[1], sys.argv[2], sys.argv[3], *sys.argv[4:5])
