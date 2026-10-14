"""Test batches through one scheduler (WB2): `work.py test` and `work.py scheduler`.

`work.py test ID [run_all args]` expands the run_all arguments into runs with run_all's own plan()
(from the checkout it is run in, with that checkout's verify build), submits them as one batch to
build/live/work.db, starts the scheduler if none is alive, prints each run's line as it finishes and
then run_all's summary, and returns run_all's exit code. Ctrl+C cancels the batch's queued runs.

`work.py scheduler` (one per machine; a second sees the first's lock and exits; it leaves after
WORK_SCHED_IDLE seconds, default 120, with nothing to do) runs the batches on the machine's game
slots, the same lock files run_all's machine_slot() takes, so a plain run_all beside it never
collides:
- batches in submission order: a batch takes every free slot until all its runs have started (its
  --jobs is ignored: run_all needs it to share the machine, the scheduler shares it already); later
  batches only fill the slots it cannot use (backfill);
- within a batch the longest expected first; its display-mode runs (alone) last, and only once it is
  the earliest batch with runs left: a later batch's alone run never stops the backfill;
- an alone run waits until none of ours runs, holds the alone gate and every slot, and runs by itself;
- each game is run_all.run() pointed at the batch's checkout and build (run folders and run_times.json
  stay where run_all puts them), retried as run_all.run_with_retries() does, in the same slot;
- results go into the run rows, the summary and exit code into the batch row;
- every finished game adds a row to the timing table (expected times: work.expected_times, WB6);
- when main's tools/work_sched.py, run_all.py or work.py change, no new game starts and, once its own games
  have ended, it exits so the next waiting `work.py test` starts it from the new code (queued runs stay queued);
- a batch whose `work.py test` process is gone is cancelled (its queued runs; started games finish).
Every database write is a short transaction in the scheduler's main loop; games run in threads.
docs/notes/work-board.md, 'Scheduler' and 'WB2'.
"""
import collections, ctypes, datetime, hashlib, io, json, os, queue, subprocess, sys, threading, time, traceback

import run_all
import work

IDLE_EXIT = float(os.environ.get('WORK_SCHED_IDLE', '120'))
TICK = 0.5           # seconds between scheduling passes
CHECK_EVERY = 5      # seconds between heartbeats and submitter checks
CODE_FILES = ('work_sched.py', 'run_all.py', 'work.py')    # in the main checkout's tools: a change restarts the scheduler
UNKNOWN_SECONDS = 1000   # expected time of a run run_times.json does not have (as run_all: new ones first)


def live_dir():
    return os.path.dirname(work.db_path())


def lock_path():
    """Held by the running scheduler; the system drops it when the process ends."""
    return os.path.join(live_dir(), 'scheduler.lock')


_kernel32 = ctypes.WinDLL('kernel32', use_last_error=True) if os.name == 'nt' else None
if _kernel32:
    _kernel32.OpenProcess.restype = ctypes.c_void_p
    _kernel32.OpenProcess.argtypes = (ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong)
    _kernel32.GetExitCodeProcess.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong))
    _kernel32.CloseHandle.argtypes = (ctypes.c_void_p,)


def pid_alive(pid):
    if not pid:
        return False
    if not _kernel32:
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False
    h = _kernel32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return ctypes.get_last_error() == 5           # access denied: it exists
    try:
        code = ctypes.c_ulong()
        return bool(_kernel32.GetExitCodeProcess(h, ctypes.byref(code))) and code.value == 259   # STILL_ACTIVE
    finally:
        _kernel32.CloseHandle(h)


def code_files():
    """The files whose change makes a running scheduler leave: the main checkout's tools/work_sched.py, run_all.py and
    work.py (WORK_SCHED_CODE: other paths, os.pathsep-separated; tests)."""
    if os.environ.get('WORK_SCHED_CODE'):
        return os.environ['WORK_SCHED_CODE'].split(os.pathsep)
    tools = os.path.join(work.main_checkout(), 'tools')
    return [os.path.join(tools, f) for f in CODE_FILES]


def code_signature(paths):
    """Content hashes of the files (None for one that is missing): equal while the code is the code that was loaded."""
    out = []
    for p in paths:
        try:
            with open(p, 'rb') as f:
                out.append(hashlib.sha1(f.read()).hexdigest())
        except OSError:
            out.append(None)
    return out


def seconds_since(stamp):
    if not stamp:
        return 0.0
    t = datetime.datetime.fromisoformat(stamp)
    return max(0.0, (datetime.datetime.now(t.tzinfo) - t).total_seconds())


# ---- batches in the database (shared by `test` and the scheduler)

def submit(db, ticket, worktree, build, argv, runs, submitter=None, env=None):
    """A new batch; runs: dicts with scenario, install, hooks, alone, expected, folder. env: the submitter's
    game variables ({name: value}, run_all.game_environment()); its games run with exactly these. Its id."""
    with work.transaction(db):
        cur = db.execute('INSERT INTO batch (ticket, worktree, build, args, submitted, submitter, env) VALUES (?, ?, ?, ?, ?, ?, ?)',
                         (ticket, worktree, build, json.dumps(argv), work.now(), submitter,
                          json.dumps(env, sort_keys=True) if env is not None else None))
        batch = cur.lastrowid
        db.executemany('INSERT INTO run (batch, scenario, install, hooks, alone, expected, folder) VALUES (?, ?, ?, ?, ?, ?, ?)',
                       [(batch, r['scenario'], r['install'], int(r['hooks']), int(r['alone']), r['expected'], r['folder'])
                        for r in runs])
    return batch


def cancel(db, batch):
    """Cancels a batch's queued runs; its started games finish and are recorded."""
    with work.transaction(db):
        db.execute("UPDATE batch SET state = 'cancelled' WHERE id = ? AND state IN ('queued', 'running')", (batch,))
        db.execute("UPDATE run SET state = 'cancelled' WHERE batch = ? AND state = 'queued'", (batch,))


# ---- the machine's game slots

class SlotPool:
    """run_all's machine slots (byte locks on files in SLOT_DIR) held by this process."""

    def __init__(self, count=None, folder=None):
        self.count = count or run_all.MACHINE_SLOTS
        self.folder = folder or run_all.SLOT_DIR
        os.makedirs(self.folder, exist_ok=True)
        self.held = {}       # slot -> open locked file
        self.gate = None     # the 'alone' lock while we gather every slot

    def _path(self, name):
        return os.path.join(self.folder, name)

    def gate_free(self):
        """No one else gathers every slot for a display-mode game (run_all probes the same way)."""
        if self.gate:
            return True
        f = run_all._lock(self._path('alone'))
        if f:
            run_all._unlock(f)
        return bool(f)

    def take_one(self):
        for i in range(self.count):
            if i not in self.held:
                f = run_all._lock(self._path(f'slot{i:02}'))
                if f:
                    self.held[i] = f
                    return i
        return None

    def take_all(self):
        """The gate and every free slot, gathered over calls while other games end; True once all are held."""
        if not self.gate:
            self.gate = run_all._lock(self._path('alone'))
            if not self.gate:
                return False
        for i in range(self.count):
            if i not in self.held:
                f = run_all._lock(self._path(f'slot{i:02}'))
                if f:
                    self.held[i] = f
        return len(self.held) == self.count

    def release(self, slots):
        for i in slots:
            f = self.held.pop(i, None)
            if f:
                run_all._unlock(f)

    def release_gate(self):
        if self.gate:
            run_all._unlock(self.gate)
            self.gate = None


# ---- running games

def batch_env(batch):
    """The batch's game variables {name: value}, or None for a batch from before they were recorded (its games
    inherit the scheduler's environment)."""
    try:
        text = batch['env']
    except (KeyError, IndexError):
        return None
    return json.loads(text) if text else None


def real_runner(batch, run, retries, on_attempt):
    """run_all.run_with_retries() of one run row, in the batch's checkout and build, in a slot we hold."""
    attempts = iter(range(retries + 1))
    env = batch_env(batch)

    def one(name, install, hooks, folder):
        on_attempt(next(attempts))
        return run_all.run(name, install, hooks, folder, root=batch['worktree'], build=batch['build'], take_slot=False,
                           game_env=env)
    return run_all.run_with_retries(run['scenario'], run['install'].split('-')[0], bool(run['hooks']), retries,
                                    os.path.basename(run['folder']), runner=one)


def order_key(run):
    """The longest expected first."""
    expected = run['expected'] if run['expected'] is not None else UNKNOWN_SECONDS
    return (-expected, run['id'])


class Scheduler:
    def __init__(self, db, runner=real_runner, slots=None, log=None, code=None):
        """code: paths of the scheduler's own code (default code_files()); when their content changes, no new game
        starts and should_restart() turns True once the running ones have ended."""
        self.db = db
        self.code = code if code is not None else code_files()
        self.code_at_start = code_signature(self.code)
        self.stale = False
        self.runner = runner
        self.pool = slots or SlotPool()
        self.log = log or (lambda text: None)
        self.games = {}            # run id -> {batch, slots, folder, alone, thread}
        self.events = queue.Queue()
        self.args = {}             # batch id -> parsed run_all arguments (None: unusable)
        self.last_check = 0.0

    # one pass of the main loop; True while there is work (batches or games)
    def tick(self):
        self.collect()
        if time.time() - self.last_check >= CHECK_EVERY:
            self.last_check = time.time()
            self.heartbeat()
            self.check_submitters()
            self.check_code()
        self.schedule()
        self.finish_batches()
        return bool(self.games) or self.db.execute(
            "SELECT 1 FROM batch WHERE state IN ('queued', 'running') OR (state = 'cancelled' AND finished IS NULL) LIMIT 1"
        ).fetchone() is not None

    def batch_args(self, batch):
        if batch['id'] not in self.args:
            try:
                self.args[batch['id']] = run_all.parse_args(json.loads(batch['args']))
            except (SystemExit, ValueError):
                self.args[batch['id']] = None
        return self.args[batch['id']]

    def heartbeat(self):
        with work.transaction(self.db):
            self.db.execute("UPDATE process SET heartbeat = ? WHERE name = 'scheduler' AND pid = ?", (work.now(), os.getpid()))

    def check_code(self):
        if not self.stale and code_signature(self.code) != self.code_at_start:
            self.stale = True
            self.log('scheduler code changed on main; starting no new games, leaving when the running ones end')

    def should_restart(self):
        """The code changed and none of our games runs: leave (queued runs stay queued, the next `test` starts a new one)."""
        return self.stale and not self.games

    def check_submitters(self):
        for b in self.db.execute("SELECT id, submitter FROM batch WHERE state IN ('queued', 'running')").fetchall():
            if b['submitter'] and not pid_alive(b['submitter']):
                cancel(self.db, b['id'])
                self.log(f'batch #{b["id"]}: its work.py test (pid {b["submitter"]}) is gone; queued runs cancelled')

    def collect(self):
        while True:
            try:
                kind, run_id, value = self.events.get_nowait()
            except queue.Empty:
                return
            if kind == 'attempt':
                if value:
                    with work.transaction(self.db):
                        self.db.execute('UPDATE run SET attempt = ? WHERE id = ?', (value, run_id))
                continue
            game = self.games.pop(run_id)
            game['thread'].join()
            self.pool.release(game['slots'])
            if game['alone']:
                self.pool.release_gate()
            result, seconds, failures, counts, notes = value
            with work.transaction(self.db):
                self.db.execute("UPDATE run SET state = 'finished', seconds = ?, result = ?, failures = ?, counts = ?, notes = ? "
                                "WHERE id = ?", (seconds, result, json.dumps(failures),
                                                 json.dumps({fn: list(c) for fn, c in counts.items()}), json.dumps(notes), run_id))
            if result not in ('ERROR', 'NO REPORT', 'TIMEOUT'):
                try:
                    work.record_time(self.db, game['scenario'], game['install'], game['root'], seconds, game['batch'])
                except Exception:
                    self.log(traceback.format_exc())     # never costs the run
            self.log(f'batch #{game["batch"]}: {os.path.basename(game["folder"])} {result} {seconds:.0f}s')

    def schedule(self):
        gathering = False
        try:
            gathering = self._schedule()
        finally:
            if not gathering:      # let go of slots gathered for a display-mode run that no longer waits
                in_use = {i for g in self.games.values() for i in g['slots']}
                self.pool.release([i for i in self.pool.held if i not in in_use])
                if not any(g['alone'] for g in self.games.values()):
                    self.pool.release_gate()

    def _schedule(self):
        """Starts what can start now; True while gathering every slot for a display-mode run."""
        if self.stale or any(g['alone'] for g in self.games.values()):
            return False                                     # new code to load first / it has the machine to itself
        batches = self.db.execute("SELECT * FROM batch WHERE state IN ('queued', 'running') ORDER BY id").fetchall()
        queued = collections.defaultdict(list)
        for r in self.db.execute("SELECT run.* FROM run JOIN batch ON batch.id = run.batch "
                                 "WHERE run.state = 'queued' AND batch.state IN ('queued', 'running')"):
            queued[r['batch']].append(r)
        busy = {g['folder'] for g in self.games.values()}   # never two games in one run folder
        head = True        # the earliest batch with runs left: only it gathers the machine for an alone run
        for b in batches:
            args = self.batch_args(b)
            runs = sorted(queued[b['id']], key=order_key)
            if args is None or not runs:
                continue
            plain = [r for r in runs if not r['alone']]
            for r in plain:
                if r['folder'] in busy:
                    continue
                if not self.pool.gate_free():
                    return False                             # a plain run_all gathers for a display-mode game
                slot = self.pool.take_one()
                if slot is None:
                    return False                             # the machine is full
                if self.start(b, r, args, [slot]):
                    busy.add(r['folder'])
            alone = [r for r in runs if r['alone'] and r['folder'] not in busy]
            if head and alone and not plain:                 # its other runs have all started
                if not self.pool.take_all() or self.games:
                    return True                              # nothing else starts until it has run
                self.start(b, alone[0], args, list(self.pool.held))
                return False
            head = False
        return False

    def start(self, batch, run, args, slots):
        t = work.now()
        with work.transaction(self.db):
            if self.db.execute("UPDATE run SET state = 'running', slot = ?, started = ?, attempt = 0 WHERE id = ? AND state = 'queued'",
                               (slots[0], t, run['id'])).rowcount != 1:
                started = False
            else:
                started = True
                self.db.execute("UPDATE batch SET state = 'running', started = ? WHERE id = ? AND state = 'queued'", (t, batch['id']))
        if not started:
            self.pool.release(slots)
            return False
        thread = threading.Thread(target=self.play, args=(dict(batch), dict(run), args.retries), daemon=True)
        self.games[run['id']] = {'batch': batch['id'], 'slots': slots, 'folder': run['folder'],
                                 'alone': bool(run['alone']), 'thread': thread, 'scenario': run['scenario'],
                                 'install': run['install'], 'root': batch['worktree']}
        self.log(f'batch #{batch["id"]}: {os.path.basename(run["folder"])} started in slot {slots[0]}'
                 + (' (alone)' if run['alone'] else ''))
        thread.start()
        return True

    def play(self, batch, run, retries):
        try:
            value = self.runner(batch, run, retries, lambda n: self.events.put(('attempt', run['id'], n)))
        except Exception as e:
            value = ('ERROR', 0.0, [f'{type(e).__name__}: {e}'], {}, [])
        self.events.put(('done', run['id'], value))

    def finish_batches(self):
        rows = self.db.execute(
            "SELECT * FROM batch WHERE finished IS NULL AND state IN ('queued', 'running', 'cancelled') AND NOT EXISTS "
            "(SELECT 1 FROM run WHERE run.batch = batch.id AND run.state IN ('queued', 'running')) ORDER BY id").fetchall()
        for b in rows:
            if any(g['batch'] == b['id'] for g in self.games.values()):
                continue
            self.finish(b)

    def finish(self, b):
        runs = self.db.execute('SELECT * FROM run WHERE batch = ? ORDER BY id', (b['id'],)).fetchall()
        args = self.batch_args(b)
        if b['state'] == 'cancelled':
            done = sum(r['state'] == 'finished' for r in runs)
            text, code = f'\nbatch #{b["id"]} cancelled: {done} of {len(runs)} runs finished\nSOME RUNS FAILED\n', 1
        elif args is None:
            text, code = f'\nbatch #{b["id"]}: run_all does not take these arguments: {b["args"]}\n', 2
        else:
            jobs = [(r['scenario'], r['install']) for r in runs]
            results = {(r['scenario'], r['install']): (r['result'], r['seconds'], json.loads(r['failures']), json.loads(r['notes']))
                       for r in runs}
            totals = collections.defaultdict(lambda: [0, 0])
            for r in runs:
                for fn, (calls, bad) in json.loads(r['counts']).items():
                    totals[fn][0] += calls
                    totals[fn][1] += bad
            out = io.StringIO()
            try:
                ok = run_all.summary(jobs, results, totals, args, seconds_since(b['started']), out,
                                     root=b['worktree'], build=b['build'])
            except Exception:
                out.write('\n' + traceback.format_exc())
                ok = False
            run_all.save_times({f'{r["scenario"]}-{r["install"]}': r['seconds'] for r in runs if r['result'] != 'ERROR'},
                               b['build'])
            text, code = out.getvalue(), 0 if ok else 1
        with work.transaction(self.db):
            self.db.execute("UPDATE batch SET state = CASE state WHEN 'cancelled' THEN 'cancelled' ELSE 'finished' END, "
                            "finished = ?, exit_code = ?, summary = ? WHERE id = ?", (work.now(), code, text, b['id']))
        self.args.pop(b['id'], None)
        self.log(f'batch #{b["id"]}: done, exit {code}')

    def wait_games(self):
        for g in list(self.games.values()):
            g['thread'].join()
        self.collect()


# ---- commands

def scheduler_alive():
    f = run_all._lock(lock_path())
    if f:
        run_all._unlock(f)
    return not f


def ensure_scheduler():
    """Starts `work.py scheduler` in the background when none is alive: the main checkout's (it outlives
    worktrees), or this checkout's while main has no tools/work_sched.py yet."""
    if scheduler_alive():
        return False
    main = work.main_checkout()
    tools = os.path.join(main, 'tools') if os.path.exists(os.path.join(main, 'tools', 'work_sched.py')) \
        else os.path.dirname(os.path.abspath(__file__))
    os.makedirs(live_dir(), exist_ok=True)
    log = open(os.path.join(live_dir(), 'scheduler.log'), 'a')
    cmd = [sys.executable, os.path.join(tools, 'work.py'), 'scheduler']
    flags = 0x00000200 | 0x08000000      # CREATE_NEW_PROCESS_GROUP (no Ctrl+C from this console), CREATE_NO_WINDOW
    kw = dict(cwd=os.path.dirname(tools), stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, close_fds=True)
    try:   # out of the caller's job object, so it outlives the shell that ran `work.py test`
        subprocess.Popen(cmd, creationflags=flags | 0x01000000, **kw)   # CREATE_BREAKAWAY_FROM_JOB
    except OSError:
        subprocess.Popen(cmd, creationflags=flags, **kw)
    log.close()
    return True


def cmd_scheduler(db, args):
    lock = run_all._lock(lock_path())
    if not lock:
        row = db.execute("SELECT pid FROM process WHERE name = 'scheduler'").fetchone()
        print(f'work.py scheduler: one is already running (pid {row["pid"] if row else "?"})')
        return 0

    def log(text):
        print(f'{work.now()[:19]} {text}', flush=True)
    try:
        with work.transaction(db):
            db.execute("INSERT OR REPLACE INTO process (name, pid, started, heartbeat, info) VALUES ('scheduler', ?, ?, ?, ?)",
                       (os.getpid(), work.now(), work.now(), json.dumps({'checkout': work.norm(work.ROOT),
                                                                         'slots': run_all.MACHINE_SLOTS})))
            # games of a scheduler that died: run them again
            lost = db.execute("UPDATE run SET state = 'queued', slot = NULL, started = NULL WHERE state = 'running'").rowcount
        log(f'scheduler pid {os.getpid()} from {work.ROOT}, {run_all.MACHINE_SLOTS} slots'
            + (f'; {lost} run(s) of a scheduler that died queued again' if lost else ''))
        s = Scheduler(db, log=log)
        idle, errors = time.time(), 0
        while True:
            try:
                busy = s.tick()
                if busy:
                    idle = time.time()
                if s.should_restart():      # no game of ours runs; queued runs stay queued for the next scheduler
                    with work.transaction(db):
                        db.execute("DELETE FROM process WHERE name = 'scheduler' AND pid = ?", (os.getpid(),))
                    log('new scheduler code; exiting (the next waiting `work.py test` starts a new scheduler)')
                    return 0
                if not busy and time.time() - idle > IDLE_EXIT:
                    with work.transaction(db):    # a batch submitted just now keeps it (else `test` starts another)
                        leave = not db.execute("SELECT 1 FROM batch WHERE state IN ('queued', 'running') LIMIT 1").fetchone()
                        if leave:
                            db.execute("DELETE FROM process WHERE name = 'scheduler' AND pid = ?", (os.getpid(),))
                    if leave:
                        log('idle; exiting')
                        return 0
                errors = 0
            except Exception:
                errors += 1
                log(traceback.format_exc())
                if errors >= 20:
                    log('20 errors in a row; waiting for the games and exiting')
                    s.wait_games()
                    return 1
            time.sleep(TICK)
    finally:
        run_all._unlock(lock)


def cmd_test(db, args):
    argv = args.run_all_args
    ra = run_all.parse_args(argv)           # run_all's own errors and exit
    jobs = run_all.plan(ra)
    root, build = work.norm(run_all.ROOT), work.norm(run_all.BUILD)
    if not os.path.exists(os.path.join(build, 'ei_launch.exe')):
        sys.exit(f'work.py: {build}/ei_launch.exe is missing; build.cmd verify first')
    expected = work.expected_times(db, root, jobs, run_all.load_times())    # timing table, else run_times.json
    runs = []
    for name, install in jobs:
        scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
        if not os.path.exists(scenario):
            sys.exit(f'work.py: no scenario {name} ({scenario})')
        runs.append({'scenario': name, 'install': install, 'hooks': run_all.uses_hooks(install, ra),
                     'alone': run_all.sets_display_mode(scenario), 'expected': expected[(name, install)],
                     'folder': work.norm(os.path.join(build, 'runs', f'{name}-{install}'))})
    env = run_all.game_environment()
    batch = submit(db, args.id, root, build, argv, runs, os.getpid(), env=env)
    ahead = db.execute("SELECT count(*) FROM batch WHERE id < ? AND state IN ('queued', 'running')", (batch,)).fetchone()[0]
    print(f'batch #{batch} ({args.id}): {len(runs)} runs, {ahead} batch(es) ahead (work.py status shows the queue)')
    ensure_scheduler()
    print(run_all.HEADER)
    sys.stdout.flush()
    return wait(db, batch, root, build)


def wait(db, batch, root, build):
    printed = set()
    checked = time.time()
    try:
        while True:
            b = db.execute('SELECT * FROM batch WHERE id = ?', (batch,)).fetchone()
            for r in db.execute("SELECT * FROM run WHERE batch = ? AND state = 'finished' ORDER BY id", (batch,)):
                if r['id'] not in printed:
                    printed.add(r['id'])
                    print(run_all.line(r['scenario'], r['install'], r['result'], r['seconds'], json.loads(r['failures']),
                                       json.loads(r['notes']), root=root, build=build))
                    sys.stdout.flush()
            if b['finished']:
                print(b['summary'], end='')
                return b['exit_code']
            if time.time() - checked > CHECK_EVERY:      # it left (idle) just as we submitted, or died
                checked = time.time()
                if ensure_scheduler():
                    print('(started the scheduler again)')
            time.sleep(1)
    except KeyboardInterrupt:
        cancel(db, batch)
        print(f'\nbatch #{batch} cancelled; games already started finish')
        return 130
