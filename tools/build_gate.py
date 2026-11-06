"""The build gate (H64): builds wait while a quiet run holds the machine, and show their progress on the work board.

build.cmd runs itself again under this wrapper (once: the inner run sees EI_BUILD_STATUS):

    python tools/build_gate.py BUILD_CMD CONFIG [ARGS...]

1. The build's ticket: <live>/builds/<id>.lock (live: the folder of the work database, build/live of the main
   checkout), byte-locked for the wrapper's whole life. The board lists a build while its lock is held; the system
   drops the lock of a build that crashed or was killed, and the next lister removes its files.
2. Its turn: it takes <id>.compile, then looks at the alone gate (run_all's SLOT_DIR/alone, which the scheduler holds
   for a quiet run from when that run is at the head of the queue until its last game has ended). While the gate is
   held it lets go of <id>.compile, says once what it waits for, and tries again every second.
3. The build: build.cmd CONFIG again, with EI_BUILD_STATUS=<id>.json, holding <id>.compile to its end (compiles, links
   and the build checks).
A quiet run's scheduler takes the gate first and then waits while any <id>.compile is held. Each side takes its own
lock before it looks at the other's, so of a build and a quiet run starting at once at least one sees the other.

<id>.json is the build's status for the board: checkout, config, stage (waiting, configure, compile, checks), ninja's
[n/m] (build_retry.py calls progress() on its output lines) and times. Writes to it are best effort.

    python tools/build_gate.py stage NAME     the build's stage, from build.cmd
    python tools/build_gate.py list           the builds running now
docs/notes/work-board.md, "Builds".
"""
import datetime, json, os, re, subprocess, sys, time

if os.name == 'nt':
    import msvcrt
else:
    import fcntl

STATUS_ENV = 'EI_BUILD_STATUS'
NO_TICKET = '-'          # EI_BUILD_STATUS of a build that runs without a ticket (cmd counts an empty variable as unset)
NINJA_STATUS = re.compile(rb'^\[(\d+)/(\d+)\] ')
POLL = 1.0               # seconds between looks at the gate while it is held
PROGRESS_EVERY = 0.5     # seconds between writes of ninja's [n/m]
STALE_LOCK = 60          # seconds after which a ticket with no status file and no holder counts as left behind


def now():
    return datetime.datetime.now().astimezone().isoformat(timespec='seconds')


def folder():
    """<live>/builds: next to the work database (WORK_DB moves both, for tests)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import work
    return os.path.join(os.path.dirname(work.db_path()), 'builds')


def slot_dir():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import run_all
    return run_all.SLOT_DIR


# ---- byte locks (as run_all._lock: one byte at offset 0)

def try_lock(path, create=True):
    """The open file with its byte locked, or None when another handle holds it (or, without create, it is missing)."""
    try:
        f = open(path, 'a+b' if create else 'r+b')
    except OSError:
        return None
    try:
        f.seek(0)
        if os.name == 'nt':
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            fcntl.lockf(f, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 0)
        return f
    except OSError:
        f.close()
        return None


def unlock(f):
    f.seek(0)
    try:
        if os.name == 'nt':
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            fcntl.lockf(f, fcntl.LOCK_UN, 1, 0)
    finally:
        f.close()


def held(path):
    """True while another handle holds path's lock; a missing file is not held. A probe holds the lock for a moment."""
    if not os.path.exists(path):
        return False
    f = try_lock(path, create=False)
    if f is None:
        return os.path.exists(path)
    unlock(f)
    return False


def gate_held(slots=None):
    return held(os.path.join(slots or slot_dir(), 'alone'))


# ---- the status file

def read_status(path):
    try:
        with open(path, encoding='utf-8') as f:
            value = json.load(f)
        return value if isinstance(value, dict) else None
    except (OSError, ValueError):
        return None


def write_status(path, fields, tries=5):
    """Merges fields into the status file (a whole new file put in place; a reader holding the old one open makes the
    replace fail on Windows: tried a few times, then left for the next write). True when written."""
    for i in range(tries):
        value = read_status(path) or {}
        value.update(fields)
        tmp = f'{path}.{os.getpid()}.tmp'
        try:
            with open(tmp, 'w', encoding='utf-8') as f:
                json.dump(value, f)
            os.replace(tmp, path)
            return True
        except OSError:
            try:
                os.remove(tmp)
            except OSError:
                pass
            if i + 1 < tries:
                time.sleep(0.02)
    return False


def stage(name):
    """The running build's stage (no-op outside a gated build)."""
    path = os.environ.get(STATUS_ENV)
    if path and path != NO_TICKET:
        write_status(path, {'stage': name, 'done': None, 'total': None} if name != 'compile' else {'stage': name})


_last_progress = [0.0, None]


def progress(line):
    """One output line (bytes) of ninja: its [n/m] goes into the running build's status file (every PROGRESS_EVERY
    seconds at most, and the last step always)."""
    path = os.environ.get(STATUS_ENV)
    m = path and path != NO_TICKET and NINJA_STATUS.match(line)
    if not m:
        return
    done, total = int(m.group(1)), int(m.group(2))
    t = time.monotonic()
    if done < total and t - _last_progress[0] < PROGRESS_EVERY:
        return
    if write_status(path, {'stage': 'compile', 'done': done, 'total': total}, tries=1 if done < total else 5):
        _last_progress[0] = t


# ---- listing

def _ids(builds):
    try:
        names = os.listdir(builds)
    except OSError:
        return set()
    return {n.rsplit('.', 1)[0] for n in names if n.endswith(('.lock', '.json', '.compile'))}


def _remove(base):
    for suffix in ('.json', '.compile', '.lock'):
        try:
            os.remove(base + suffix)
        except OSError:
            pass


def builds(folder_path=None, clean=True):
    """The builds whose ticket is held: their status dicts plus id and compiling (holds <id>.compile: a quiet run
    waits for it), oldest first. clean: removes the files of tickets nobody holds (a crashed build)."""
    folder_path = folder_path or folder()
    out = []
    for bid in sorted(_ids(folder_path)):
        base = os.path.join(folder_path, bid)
        status = read_status(base + '.json')
        if status is None and os.path.exists(base + '.json'):
            status = {}                       # being replaced right now
        if held(base + '.lock'):
            if status is not None:
                out.append(dict(status, id=bid, compiling=held(base + '.compile')))
            continue
        if clean:
            try:
                young = status is None and time.time() - os.path.getmtime(base + '.lock') < STALE_LOCK
            except OSError:
                young = False
            if not young:                     # a ticket still being made has its lock file only, for a moment
                _remove(base)
    return out


def compiling(folder_path=None):
    """The builds a quiet run waits for (their status dicts)."""
    return [b for b in builds(folder_path) if b['compiling']]


# ---- the wrapper

def quiet_batch():
    """'#N (ticket)' of the quiet batch holding the gate, from the work database; '' when that fails."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import work
        db = work.connect()
        try:
            row = db.execute("SELECT batch.id, batch.ticket FROM batch JOIN run ON run.batch = batch.id WHERE run.alone "
                             "AND batch.state IN ('queued', 'running') ORDER BY batch.id LIMIT 1").fetchone()
        finally:
            db.close()
        return f' #{row["id"]} ({row["ticket"]})' if row else ''
    except Exception:
        return ''


class Ticket:
    """A build's ticket in folder_path (see the module's header)."""

    def __init__(self, checkout, config, folder_path=None, slots=None):
        self.folder = folder_path or folder()
        self.slots = slots
        os.makedirs(self.folder, exist_ok=True)
        builds(self.folder)                   # clears tickets left by builds that died
        self.id = f'{time.strftime("%Y%m%d-%H%M%S")}-{os.getpid()}-{os.urandom(3).hex()}'
        self.base = os.path.join(self.folder, self.id)
        self.alive = try_lock(self.base + '.lock')
        self.compile = None
        self.status = self.base + '.json'
        write_status(self.status, {'pid': os.getpid(), 'checkout': checkout.replace('\\', '/'), 'config': config,
                                   'queued': now(), 'started': None, 'stage': 'waiting', 'done': None, 'total': None})

    def take_compile(self):
        """<id>.compile (a lister's probe may hold it for a moment)."""
        end = time.time() + 10
        while not (f := try_lock(self.base + '.compile')):
            if time.time() > end:
                raise OSError(f'build_gate: cannot lock {self.base}.compile')
            time.sleep(0.02)
        return f

    def wait_turn(self, say=print, poll=POLL):
        """Holds <id>.compile once the alone gate is free; says once what it waits for."""
        said = False
        while True:
            f = self.take_compile()
            if not gate_held(self.slots):
                self.compile = f
                write_status(self.status, {'stage': 'configure', 'started': now()})
                return
            unlock(f)
            if not said:
                said = True
                say(f'build_gate: a quiet run{quiet_batch()} holds the machine; this build starts when it ends', flush=True)
            time.sleep(poll)

    def close(self):
        for f in (self.compile, self.alive):
            if f:
                try:
                    unlock(f)
                except OSError:
                    pass
        self.compile = self.alive = None
        _remove(self.base)


def run(bat, args, folder_path=None, slots=None):
    """build.cmd's outer run: takes the ticket, waits its turn, runs bat args again inside it; bat's exit code."""
    config = args[0] if args else 'release'
    checkout = os.path.dirname(os.path.abspath(bat))
    try:
        ticket = Ticket(checkout, config, folder_path, slots)
    except Exception as e:                      # never stops a build: it runs without the gate
        print(f'build_gate: no build ticket ({type(e).__name__}: {e}); building without the gate', flush=True)
        ticket = None
    try:
        if ticket:
            ticket.wait_turn()
        env = dict(os.environ)
        env[STATUS_ENV] = ticket.status if ticket else NO_TICKET
        comspec = os.environ.get('ComSpec', 'cmd.exe')
        # /s: cmd strips the outer quotes and runs "bat" args as written
        command = f'"{comspec}" /d /s /c ""{bat}" {subprocess.list2cmdline(args)}"'
        return subprocess.call(command, env=env)
    except KeyboardInterrupt:
        return 130
    finally:
        if ticket:
            ticket.close()


def main(argv):
    if argv[:1] == ['stage'] and len(argv) == 2:
        stage(argv[1])
        return 0
    if argv == ['list']:
        for b in builds():
            done = f' [{b["done"]}/{b["total"]}]' if b.get('total') else ''
            print(f'{b["id"]}: {b.get("checkout")} {b.get("config")} {b.get("stage")}{done} since {b.get("started") or b.get("queued")}')
        return 0
    if argv and argv[0].lower().endswith('.cmd'):
        return run(argv[0], argv[1:])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
