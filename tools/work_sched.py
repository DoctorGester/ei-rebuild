"""Test batches through one scheduler (WB2): `work.py test` and `work.py scheduler`.

`work.py test ID [run_all args]` expands the run_all arguments into runs with run_all's own plan()
(from the checkout it is run in, with that checkout's verify build), submits them as one batch to
build/live/work.db, starts the scheduler if none is alive, prints each run's line as it finishes and
then run_all's summary, and returns run_all's exit code. Ctrl+C cancels the batch's queued runs.

`work.py scheduler` (one per machine; a second sees the first's lock and exits; it leaves after
WORK_SCHED_IDLE seconds, default 120, with nothing to do) runs the batches on the machine's game
slots, the same lock files run_all's machine_slot() takes, so a plain run_all beside it never
collides:
- batches smallest remaining work first, less the time they have waited (batch_priority, WB9): a batch
  takes every free slot until all its runs have started (its --jobs is ignored: run_all needs it to share
  the machine, the scheduler shares it already); the others only fill the slots it cannot use (backfill);
- within a batch the longest expected first; its display-mode runs (alone) last, and only once it is
  the earliest batch with runs left: a later batch's alone run never stops the backfill;
- an alone run waits until none of ours runs, holds the alone gate and every slot, and runs by itself;
- each game is run_all.run() pointed at the batch's checkout and build (run folders and run_times.json
  stay where run_all puts them), retried as run_all.run_with_retries() does; its slots go back as soon as its game
  ends (frame packing and the reference copy run without them, SV10h) and a retry's game takes slots again before
  anything new starts;
- results go into the run rows, the summary and exit code into the batch row;
- every finished game adds a row to the timing table (expected times: work.expected_times, WB6);
- when main's tools/work_sched.py, run_all.py or work.py change, no new game starts and, once its own games
  have ended, it exits so the next waiting `work.py test` starts it from the new code (queued runs stay queued);
- a batch whose `work.py test` process is gone is cancelled (its queued runs; started games finish).
Every database write is a short transaction in the scheduler's main loop; games run in threads.
docs/notes/work-board.md, 'Scheduler'.
"""
import collections, contextlib, ctypes, datetime, hashlib, io, json, os, queue, subprocess, sys, threading, time, traceback

import run_all
import work

IDLE_EXIT = float(os.environ.get('WORK_SCHED_IDLE', '120'))
TICK = 0.5           # seconds between scheduling passes at most (a finished game wakes the loop at once)
CHECK_EVERY = 5      # seconds between heartbeats and submitter checks
CODE_FILES = ('work_sched.py', 'run_all.py', 'work.py')    # in the main checkout's tools: a change restarts the scheduler
UNKNOWN_SECONDS = 1000   # expected time of a run run_times.json does not have (as run_all: new ones first)
AFTER_GAME_MAX = 16      # runs past their game (packing frames, copying a reference) before no new game starts


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
    """A new batch; runs: dicts with scenario, install, hooks (our code: the image), alone, expected, folder. env: the submitter's
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


def parse_batch_args(text):
    """run_all's parse of a stored argument list; ValueError with run_all's own message when it will not."""
    err = io.StringIO()
    try:
        with contextlib.redirect_stderr(err):
            return run_all.parse_args(json.loads(text))
    except SystemExit:
        lines = [l for l in err.getvalue().splitlines() if l.strip()]
        raise ValueError(lines[-1] if lines else 'run_all.parse_args exited')


def unusable_text(text, error):
    return f'run_all does not take these arguments ({error or "unparseable"}): {text}'


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


class GameSlots:
    """A run's game slots, between its thread and the scheduler's loop (SV10h): the run gives them back as soon as its
    game has ended (release), so its frame packing and reference copy run without them, and a retry's game asks for
    them again (acquire) and waits until the loop has taken them. keep: an alone run keeps every slot to its end."""

    def __init__(self, post, run_id, keep=False):
        self.post, self.run_id, self.keep, self.held = post, run_id, keep, True

    def release(self):
        if self.held and not self.keep:
            self.held = False
            self.post(('freed', self.run_id, None))

    def acquire(self):
        if not self.held:
            ready = threading.Event()
            self.post(('want', self.run_id, ready))
            ready.wait()
            self.held = True


def real_runner(batch, run, retries, on_attempt, slots=None):
    """run_all.run_with_retries() of one run row, in the batch's checkout and build, in a slot we hold (slots: a
    GameSlots, given back while no game of the run plays). A row of our code in game.exe (a submission from a checkout
    from before S21c) fails without a game."""
    install = run_all.launch_install(run['install'])
    if bool(run['hooks']) != run_all.our_code(install):
        on_attempt(0)
        return ('FAIL', 0.0, [f'{run["install"]} with hooks {"on" if run["hooks"] else "off"}: our code runs only in the '
                              'standalone image since S21c (merge main)'], {}, [])
    attempts = iter(range(retries + 1))
    env = batch_env(batch)
    try:
        args = parse_batch_args(batch['args'])     # --clock (WB8) and --fresh (R10), as run_all passes them
        clock, fresh = args.clock, args.fresh
    except (KeyError, IndexError, ValueError):
        clock, fresh = None, False

    def one(name, install, folder):
        on_attempt(next(attempts))
        return run_all.run(name, install, folder, root=batch['worktree'], build=batch['build'], take_slot=False,
                           clock=clock, game_env=env, fresh=fresh, held=run['slots_held'] if 'slots_held' in run.keys() else 1,
                           slots=slots)
    return run_all.run_with_retries(run['scenario'], install, retries, os.path.basename(run['folder']), runner=one,
                                    keep=lambda folder, attempt: run_all.keep_attempt(folder, attempt, build=batch['build']))


PRIORITY_UNKNOWN = 60   # seconds a run with no timing counts for in batch_priority


def batch_priority(batch, runs, slots, now=None):
    """Lower runs first: the batch's queued work in machine seconds (expected seconds over the slots) less the
    seconds it has waited. A one-run batch goes ahead of a full suite submitted just before it, and a big batch
    still gets its turn once it has waited about as long as it will take (WB9)."""
    work_left = sum(r['expected'] if r['expected'] is not None else PRIORITY_UNKNOWN for r in runs) / max(slots, 1)
    waited = (now or time.time()) - datetime.datetime.fromisoformat(batch['submitted']).timestamp()
    return (work_left - waited, batch['id'])


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
        self.finishing = {}        # batch id -> thread writing its summary (frame comparisons take minutes)
        self.events = queue.Queue()
        self.wake = threading.Event()        # set with every event: the loop's wait ends at once
        self.args = {}             # batch id -> parsed run_all arguments (None: unusable)
        self.arg_errors = {}       # batch id -> why run_all did not take them
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
        return bool(self.games) or bool(self.finishing) or self.db.execute(
            "SELECT 1 FROM batch WHERE state IN ('queued', 'running') OR (state = 'cancelled' AND finished IS NULL) LIMIT 1"
        ).fetchone() is not None

    def batch_args(self, batch):
        if batch['id'] not in self.args:
            try:
                self.args[batch['id']], self.arg_errors[batch['id']] = parse_batch_args(batch['args']), None
            except ValueError as e:
                self.args[batch['id']], self.arg_errors[batch['id']] = None, str(e)
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
        return self.stale and not self.games and not self.finishing

    def check_submitters(self):
        for b in self.db.execute("SELECT id, submitter FROM batch WHERE state IN ('queued', 'running')").fetchall():
            if b['submitter'] and not pid_alive(b['submitter']):
                cancel(self.db, b['id'])
                self.log(f'batch #{b["id"]}: its work.py test (pid {b["submitter"]}) is gone; queued runs cancelled')

    def post(self, event):
        self.events.put(event)
        self.wake.set()

    def collect(self):
        while True:
            try:
                kind, run_id, value = self.events.get_nowait()
            except queue.Empty:
                return
            if kind == 'summary':
                self.finishing.pop(run_id).join()
                self.write_finish(run_id, *value)
                continue
            if kind == 'attempt':
                if value:
                    with work.transaction(self.db):
                        self.db.execute('UPDATE run SET attempt = ? WHERE id = ?', (value, run_id))
                continue
            if kind == 'freed':          # its game has ended: the slots go to the next game now (SV10h)
                game = self.games[run_id]
                self.pool.release(game['slots'])
                game['slots'], game['held'] = [], game['held'] + time.time() - game['since']
                continue
            if kind == 'want':           # a retry's game: grant() gives it slots before anything new starts
                self.games[run_id]['want'] = value
                continue
            game = self.games.pop(run_id)
            game['thread'].join()
            if game['slots']:
                game['held'] += time.time() - game['since']
            self.pool.release(game['slots'])
            if game['alone']:
                self.pool.release_gate()
            result, seconds, failures, counts, notes = value
            with work.transaction(self.db):
                self.db.execute("UPDATE run SET state = 'finished', seconds = ?, result = ?, failures = ?, counts = ?, notes = ? "
                                "WHERE id = ?", (seconds, result, json.dumps(failures),
                                                 json.dumps({fn: list(c) for fn, c in counts.items()}), json.dumps(notes), run_id))
            if result not in ('ERROR', 'NO REPORT', 'TIMEOUT') and not run_all.is_cached(notes):
                try:
                    work.record_time(self.db, game['scenario'], game['install'], game['root'], seconds, game['batch'])
                except Exception:
                    self.log(traceback.format_exc())     # never costs the run
            self.log(f'batch #{game["batch"]}: {os.path.basename(game["folder"])} {result} {seconds:.0f}s '
                     f'(slot {game["held"]:.0f}s)')

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
        if not self.grant():
            return self.pool.gate is not None               # a retry waits for slots: nothing new starts before it
        if sum(not g['slots'] for g in self.games.values()) >= AFTER_GAME_MAX:
            return False                                     # enough runs packing frames
        if self.stale or any(g['alone'] for g in self.games.values()):
            return False                                     # new code to load first / it has the machine to itself
        batches = self.db.execute("SELECT * FROM batch WHERE state IN ('queued', 'running') ORDER BY id").fetchall()
        queued = collections.defaultdict(list)
        for r in self.db.execute("SELECT run.* FROM run JOIN batch ON batch.id = run.batch "
                                 "WHERE run.state = 'queued' AND batch.state IN ('queued', 'running')"):
            queued[r['batch']].append(r)
        busy = {g['folder'] for g in self.games.values()}   # never two games in one run folder
        # Only the earliest batch with runs left gathers the machine for an alone run; plain runs go
        # smallest remaining work first (batch_priority).
        head = next((b['id'] for b in batches if queued[b['id']]), None)
        batches = sorted(batches, key=lambda b: batch_priority(b, queued[b['id']], self.pool.count))
        for b in batches:
            args = self.batch_args(b)
            runs = sorted(queued[b['id']], key=order_key)
            if args is None:
                if runs:                                     # never startable: finish_batches fails it
                    self.log(f'batch #{b["id"]}: arguments not usable: {self.arg_errors[b["id"]]}')
                    with work.transaction(self.db):
                        self.db.execute("UPDATE run SET state = 'cancelled' WHERE batch = ? AND state = 'queued'", (b['id'],))
                continue
            if not runs:
                continue
            plain = [r for r in runs if not r['alone']]
            for r in plain:
                if r['folder'] in busy:
                    continue
                if not self.pool.gate_free():
                    return False                             # a plain run_all gathers for a display-mode game
                # A pair's run (a "peer" line, SV5d) plays two games: two slots, taken together.
                need = run_all.games_of(os.path.join(b['worktree'], 'tests', 'scenarios', r['scenario'] + '.txt'))
                slots = []
                while len(slots) < need and (slot := self.pool.take_one()) is not None:
                    slots.append(slot)
                if len(slots) < need:
                    self.pool.release(slots)
                    return False                             # the machine is full
                if self.start(b, r, args, slots):
                    busy.add(r['folder'])
            alone = [r for r in runs if r['alone'] and r['folder'] not in busy]
            if b['id'] == head and alone and not plain:     # its other runs have all started
                if not self.pool.take_all() or self.games:
                    return True                              # nothing else starts until it has run
                self.start(b, alone[0], args, list(self.pool.held))
                return False
        return False

    def grant(self):
        """Slots for the runs whose retry waits for them (GameSlots.acquire): those gathered for an alone run first,
        then free ones. False while one still waits."""
        for run_id, g in self.games.items():
            if not g['want']:
                continue
            if not self.pool.gate_free():
                return False                                 # a plain run_all gathers for a display-mode game
            in_use = {i for x in self.games.values() for i in x['slots']}
            slots = [i for i in self.pool.held if i not in in_use][:g['need']]
            taken = []
            while len(slots) < g['need'] and (slot := self.pool.take_one()) is not None:
                slots.append(slot)
                taken.append(slot)
            if len(slots) < g['need']:
                self.pool.release(taken)
                return False
            g['slots'], g['since'] = slots, time.time()
            g['want'].set()
            g['want'] = None
            with work.transaction(self.db):
                self.db.execute('UPDATE run SET slot = ? WHERE id = ?', (slots[0], run_id))
        return True

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
        thread = threading.Thread(target=self.play, args=(dict(batch), dict(run, slots_held=len(slots)), args.retries),
                                  daemon=True)
        self.games[run['id']] = {'batch': batch['id'], 'slots': slots, 'folder': run['folder'],
                                 'alone': bool(run['alone']), 'thread': thread, 'scenario': run['scenario'],
                                 'install': run['install'], 'root': batch['worktree'],
                                 'need': len(slots), 'want': None, 'since': time.time(), 'held': 0.0}
        self.log(f'batch #{batch["id"]}: {os.path.basename(run["folder"])} started in slot {slots[0]}'
                 + (' (alone)' if run['alone'] else ''))
        thread.start()
        return True

    def play(self, batch, run, retries):
        slots = GameSlots(self.post, run['id'], keep=bool(run['alone']))
        try:
            value = self.runner(batch, run, retries, lambda n: self.post(('attempt', run['id'], n)), slots)
        except Exception as e:
            value = ('ERROR', 0.0, [f'{type(e).__name__}: {e}'], {}, [])
        self.post(('done', run['id'], value))

    def finish_batches(self):
        rows = self.db.execute(
            "SELECT * FROM batch WHERE finished IS NULL AND state IN ('queued', 'running', 'cancelled') AND NOT EXISTS "
            "(SELECT 1 FROM run WHERE run.batch = batch.id AND run.state IN ('queued', 'running')) ORDER BY id").fetchall()
        for b in rows:
            if b['id'] in self.finishing or any(g['batch'] == b['id'] for g in self.games.values()):
                continue
            self.finish(b)

    def finish(self, b):
        runs = self.db.execute('SELECT * FROM run WHERE batch = ? ORDER BY id', (b['id'],)).fetchall()
        args = self.batch_args(b)
        if b['state'] == 'cancelled':
            done = sum(r['state'] == 'finished' for r in runs)
            text, code = f'\nbatch #{b["id"]} cancelled: {done} of {len(runs)} runs finished\nSOME RUNS FAILED\n', 1
        elif args is None:
            text, code = f'\nbatch #{b["id"]} FAILED: {unusable_text(b["args"], self.arg_errors.get(b["id"]))}\n', 2
        else:
            # The summary runs the batch's post-game checks (--widescreen's frame comparisons took minutes
            # here before S21b; they now run in the submitting work.py test, as --golden): off the main loop,
            # so games keep starting and the heartbeat keeps going. collect() writes the result.
            thread = threading.Thread(target=self.summarise, args=(b, runs, args), daemon=True)
            self.finishing[b['id']] = thread
            thread.start()
            return
        self.write_finish(b['id'], text, code)

    def summarise(self, b, runs, args):
        try:
            jobs = [(r['scenario'], r['install']) for r in runs]
            results = {(r['scenario'], r['install']): (r['result'], r['seconds'], json.loads(r['failures']), json.loads(r['notes']))
                       for r in runs}
            out = io.StringIO()
            try:
                ok = run_all.summary(jobs, results, args, seconds_since(b['started']), out,
                                     root=b['worktree'], build=b['build'])
            except Exception:
                out.write('\n' + traceback.format_exc())
                ok = False
            run_all.save_times({f'{r["scenario"]}-{r["install"]}': r['seconds'] for r in runs if r['result'] != 'ERROR'},
                               b['build'])
            text, code = out.getvalue(), 0 if ok else 1
        except Exception:
            text, code = '\n' + traceback.format_exc(), 1
        self.post(('summary', b['id'], (text, code)))

    def write_finish(self, batch_id, text, code):
        with work.transaction(self.db):
            self.db.execute("UPDATE batch SET state = CASE state WHEN 'cancelled' THEN 'cancelled' ELSE 'finished' END, "
                            "finished = ?, exit_code = ?, summary = ? WHERE id = ?", (work.now(), code, text, batch_id))
            # The per-function counts (76 KB a run, from the verify wrappers; empty since S21d): nothing reads them.
            self.db.execute('UPDATE run SET counts = NULL WHERE batch = ? AND counts IS NOT NULL', (batch_id,))
        self.args.pop(batch_id, None)
        self.arg_errors.pop(batch_id, None)
        self.log(f'batch #{batch_id}: done, exit {code}')

    def wait_games(self):
        """Until every game of ours has ended (a retry's game still gets its slots)."""
        while self.games:
            self.wake.wait(TICK)
            self.wake.clear()
            self.collect()
            self.grant()


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
            s.wake.wait(TICK)
            s.wake.clear()
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
    env = run_all.game_environment()
    if ra.golden:
        # The digest the goldens were recorded with; the comparison runs here, after the batch, so the
        # scheduler gets run_all's arguments without --golden.
        env['EI_DIGEST'] = str(run_all.GOLDEN_PERIOD)
        argv = [a for a in argv if a != '--golden']
    # --widescreen's frames are compared (or with --record kept as references) here too, after the batch: its runs
    # are plain runs of the image on the Rebirth install (or of addon's code: the original, hooks off in the run rows)
    argv = [a for a in argv if a not in ('--widescreen', '--record')]
    runs = []
    for name, install in jobs:
        scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
        if not os.path.exists(scenario):
            sys.exit(f'work.py: no scenario {name} ({scenario})')
        launch = run_all.launch_install(install)
        # R10: only dgVoodoo runs that set a resolution are alone; a run of the original code the reference cache
        # holds plays no game (it is copied in a moment) and is expected to take no time
        cached = not ra.fresh and run_all.cached_reference(name, launch, root, ra.clock, env)
        runs.append({'scenario': name, 'install': install, 'hooks': run_all.our_code(launch),
                     'alone': not cached and run_all.runs_alone(scenario, launch),
                     'expected': 1 if cached else expected[(name, install)],
                     'folder': work.norm(os.path.join(build, 'runs', f'{name}-{install}'))})
    batch = submit(db, args.id, root, build, argv, runs, os.getpid(), env=env)
    ahead = db.execute("SELECT count(*) FROM batch WHERE id < ? AND state IN ('queued', 'running')", (batch,)).fetchone()[0]
    print(f'batch #{batch} ({args.id}): {len(runs)} runs, {ahead} batch(es) ahead (work.py status shows the queue)')
    ensure_scheduler()
    print(run_all.HEADER)
    sys.stdout.flush()
    code = wait(db, batch, root, build)
    if ra.widescreen and code != 130:
        ok = run_all.widescreen_check(jobs, ra.record, root=root, build=build)
        code = code or (0 if ok else 1)
    if ra.golden and code != 130:
        ok = run_all.golden_check(jobs, root=root, build=build)
        code = code or (0 if ok else 1)
    return code


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
