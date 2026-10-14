"""The work board's live database: who works on which ticket where, their progress notes, and test batches.

usage: python tools/work.py start ID [--worktree P] [--branch B] [--agent A]
                                            assign the ticket to this checkout (cwd) and its branch
       python tools/work.py note ID TEXT... [--author A]
                                            a progress line
       python tools/work.py status [ID]     open assignments, last notes and commits, batches (one screen);
                                            with ID: that ticket's assignments, notes and batches
       python tools/work.py done ID [TEXT...]
                                            end the ticket's assignment (the coordinator, at merge); TEXT
                                            becomes a last note
       python tools/work.py db              where the database is, and its schema version
       python tools/work.py serve [--port 8765]
                                            the live board on http://127.0.0.1:PORT/ (tools/work_board/)
       python tools/work.py test ID [run_all args]
                                            run_all's runs as one batch through the scheduler; waits,
                                            prints run_all's lines and summary, returns its exit code
       python tools/work.py scheduler       the machine's one test scheduler (`test` starts it)

The database is build/live/work.db in the MAIN checkout (found from any worktree through
`git rev-parse --git-common-dir`), so every checkout shares it; WORK_DB overrides the path (tests).
SQLite in WAL mode: readers never wait, writers take short BEGIN IMMEDIATE transactions and wait for
each other through the busy timeout. docs/tickets.toml stays what a ticket is; this keeps what is
happening now. Schema and commands: docs/notes/work-board.md.
"""
import argparse, contextlib, datetime, os, sqlite3, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUSY_MS = 60000          # a writer waits this long for another's transaction before giving up

# Each entry moves the schema one version up (PRAGMA user_version); later tickets append, never edit.
MIGRATIONS = [
    # 1 (WB1): every table the board needs; the scheduler (WB2) and the server (WB3) fill batch, run, process.
    """
    CREATE TABLE assignment (
        id        INTEGER PRIMARY KEY,
        ticket    TEXT NOT NULL,
        worktree  TEXT NOT NULL,         -- absolute path of the checkout, forward slashes
        branch    TEXT,
        agent     TEXT,
        started   TEXT NOT NULL,
        ended     TEXT                   -- NULL while the assignment is open
    );
    CREATE UNIQUE INDEX assignment_open ON assignment(ticket) WHERE ended IS NULL;
    CREATE INDEX assignment_ticket ON assignment(ticket, id);

    CREATE TABLE note (
        id        INTEGER PRIMARY KEY,
        ticket    TEXT NOT NULL,
        time      TEXT NOT NULL,
        author    TEXT,
        text      TEXT NOT NULL
    );
    CREATE INDEX note_ticket ON note(ticket, id);

    CREATE TABLE batch (
        id        INTEGER PRIMARY KEY,   -- submission order
        ticket    TEXT,
        worktree  TEXT NOT NULL,         -- checkout whose tests/scenarios and run/ the games use
        build     TEXT NOT NULL,         -- its verify build folder (run_all's BUILD; runs go to <build>/runs)
        args      TEXT NOT NULL DEFAULT '[]',   -- JSON list: the run_all arguments
        state     TEXT NOT NULL DEFAULT 'queued'
                  CHECK (state IN ('queued', 'running', 'finished', 'cancelled')),
        submitted TEXT NOT NULL,
        started   TEXT,
        finished  TEXT,
        submitter INTEGER,               -- pid of the waiting `work.py test`
        exit_code INTEGER,               -- run_all's exit code
        summary   TEXT                   -- run_all's printed summary
    );
    CREATE INDEX batch_state ON batch(state, id);

    CREATE TABLE run (
        id        INTEGER PRIMARY KEY,
        batch     INTEGER NOT NULL REFERENCES batch(id) ON DELETE CASCADE,
        scenario  TEXT NOT NULL,
        install   TEXT NOT NULL,         -- vanilla, rebirth, native, rebirth-nohooks
        hooks     INTEGER NOT NULL DEFAULT 1,
        alone     INTEGER NOT NULL DEFAULT 0,   -- sets the display mode: runs with no other game
        expected  REAL,                  -- seconds from run_times.json (longest first)
        folder    TEXT,                  -- the run folder (ei_hook.log, harness_report.txt)
        state     TEXT NOT NULL DEFAULT 'queued'
                  CHECK (state IN ('queued', 'running', 'finished', 'cancelled')),
        attempt   INTEGER NOT NULL DEFAULT 0,   -- retries so far
        slot      INTEGER,               -- machine slot while running
        started   TEXT,
        seconds   REAL,
        result    TEXT,                  -- PASS, PASS*, FAIL, NO REPORT, ...
        failures  TEXT,                  -- JSON list of the report's FAILED: lines
        counts    TEXT,                  -- JSON {function: [calls, mismatches]}
        notes     TEXT                   -- JSON list (terminating, EXIT: lines)
    );
    CREATE INDEX run_batch ON run(batch, state);

    CREATE TABLE process (
        name      TEXT PRIMARY KEY,      -- 'scheduler', 'server'
        pid       INTEGER NOT NULL,
        started   TEXT NOT NULL,
        heartbeat TEXT NOT NULL,
        info      TEXT                   -- e.g. the server's URL
    );
    """,
]


# ---- where things are

def git(*args, cwd=ROOT):
    p = subprocess.run(['git', *args], cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    return p.stdout.strip() if p.returncode == 0 else None


def norm(path):
    return os.path.normpath(os.path.abspath(path)).replace('\\', '/')


def main_checkout():
    """The main checkout: the parent of the repository's common .git folder, from any worktree."""
    common = git('rev-parse', '--path-format=absolute', '--git-common-dir')
    if not common:
        sys.exit('work.py: not in a git checkout')
    return norm(os.path.dirname(norm(common)))


def db_path():
    return norm(os.environ.get('WORK_DB') or os.path.join(main_checkout(), 'build', 'live', 'work.db'))


def now():
    return datetime.datetime.now().astimezone().isoformat(timespec='seconds')


def age(stamp):
    """'40s', '12m', '3h', '2d' since an ISO time."""
    if not stamp:
        return ''
    try:
        t = datetime.datetime.fromisoformat(stamp)
    except ValueError:
        return stamp
    s = max(0, (datetime.datetime.now(t.tzinfo) - t).total_seconds())
    for unit, size in (('d', 86400), ('h', 3600), ('m', 60)):
        if s >= size:
            return f'{s / size:.0f}{unit}'
    return f'{s:.0f}s'


# ---- the database

def connect(path=None):
    """An autocommit connection (isolation_level None); write through `with transaction(db):`."""
    path = path or db_path()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    db = sqlite3.connect(path, timeout=BUSY_MS / 1000, isolation_level=None)
    db.row_factory = sqlite3.Row
    db.execute(f'PRAGMA busy_timeout = {BUSY_MS}')
    if db.execute('PRAGMA journal_mode').fetchone()[0].lower() != 'wal':
        db.execute('PRAGMA journal_mode = WAL')       # persistent: set once by whoever creates the file
    db.execute('PRAGMA synchronous = NORMAL')        # with WAL: never corrupt, may lose the last commit on power loss
    db.execute('PRAGMA foreign_keys = ON')
    migrate(db)
    return db


@contextlib.contextmanager
def transaction(db):
    """A short write transaction. IMMEDIATE takes the write lock at BEGIN, so two writers never both
    read and then fail to upgrade; the second waits in the busy handler instead."""
    db.execute('BEGIN IMMEDIATE')
    try:
        yield db
    except BaseException:
        db.execute('ROLLBACK')
        raise
    db.execute('COMMIT')


def migrate(db):
    if db.execute('PRAGMA user_version').fetchone()[0] >= len(MIGRATIONS):
        return
    with transaction(db):
        version = db.execute('PRAGMA user_version').fetchone()[0]   # again, under the write lock
        if version > len(MIGRATIONS):
            sys.exit(f'work.py: the database is schema {version}, newer than this work.py ({len(MIGRATIONS)}); merge main')
        for script in MIGRATIONS[version:]:
            statement = ''
            for line in script.splitlines(keepends=True):
                statement += line
                if sqlite3.complete_statement(statement):
                    db.execute(statement)
                    statement = ''
        db.execute(f'PRAGMA user_version = {len(MIGRATIONS)}')


# ---- tickets.toml (titles; whether an id exists)

def toml_tickets():
    """{id: ticket} from this checkout's and the main checkout's docs/tickets.toml (main wins)."""
    import tomllib
    out = {}
    for root in dict.fromkeys((ROOT, main_checkout())):
        try:
            with open(os.path.join(root, 'docs', 'tickets.toml'), 'rb') as f:
                out.update({t['id']: t for t in tomllib.load(f).get('ticket', []) if 'id' in t})
        except (OSError, ValueError):
            pass
    return out


# ---- the caller's checkout

def here():
    """(worktree, branch) of the current directory's checkout."""
    top = git('rev-parse', '--show-toplevel', cwd=os.getcwd())
    if not top:
        sys.exit('work.py: the current directory is not in a git checkout')
    return norm(top), git('rev-parse', '--abbrev-ref', 'HEAD', cwd=top)


def default_author(db, worktree):
    if os.environ.get('WORK_AGENT'):
        return os.environ['WORK_AGENT']
    if worktree == main_checkout():
        return 'coordinator'
    row = db.execute('SELECT agent FROM assignment WHERE worktree = ? AND ended IS NULL ORDER BY id DESC LIMIT 1',
                     (worktree,)).fetchone()
    return (row and row['agent']) or os.path.basename(worktree)


def last_commit(branch):
    """'abc1234 3h subject' of a branch, from the shared repository."""
    if not branch or branch == 'HEAD':
        return ''
    line = git('log', '-1', '--format=%h%x09%cI%x09%s', branch, '--', cwd=main_checkout())
    if not line:
        return ''
    h, when, subject = (line.split('\t') + ['', ''])[:3]
    return f'{h} {age(when)} {subject}'


def short(text, width):
    text = ' '.join((text or '').split())
    return text if len(text) <= width else text[:width - 3] + '...'


# ---- commands

def cmd_start(db, args):
    known = toml_tickets()
    if args.id not in known and not args.force:
        sys.exit(f'work.py: no ticket {args.id} in docs/tickets.toml (here or in the main checkout); --force to assign anyway')
    worktree, branch = here()
    worktree = norm(args.worktree) if args.worktree else worktree
    if args.worktree and not args.branch:
        branch = git('rev-parse', '--abbrev-ref', 'HEAD', cwd=worktree)
    branch = args.branch or branch
    agent = args.agent or os.environ.get('WORK_AGENT') or os.path.basename(worktree)
    with transaction(db):
        old = db.execute('SELECT * FROM assignment WHERE ticket = ? AND ended IS NULL', (args.id,)).fetchone()
        if old and old['worktree'] == worktree:
            db.execute('UPDATE assignment SET branch = ?, agent = ? WHERE id = ?', (branch, agent, old['id']))
            print(f'{args.id}: already assigned to {agent} since {old["started"]} ({worktree}, {branch})')
            return 0
        t = now()
        if old:
            db.execute('UPDATE assignment SET ended = ? WHERE id = ?', (t, old['id']))
            db.execute('INSERT INTO note (ticket, time, author, text) VALUES (?, ?, ?, ?)',
                       (args.id, t, agent, f'took over from {old["agent"]} ({old["worktree"]})'))
            print(f'{args.id}: was assigned to {old["agent"]} ({old["worktree"]}) since {old["started"]}; taken over')
        db.execute('INSERT INTO assignment (ticket, worktree, branch, agent, started) VALUES (?, ?, ?, ?, ?)',
                   (args.id, worktree, branch, agent, t))
    print(f'{args.id}: assigned to {agent} ({worktree}, {branch})')
    return 0


def cmd_note(db, args):
    text = ' '.join(args.text).strip()
    if not text:
        sys.exit('work.py: empty note')
    author = args.author or default_author(db, here()[0])
    with transaction(db):
        db.execute('INSERT INTO note (ticket, time, author, text) VALUES (?, ?, ?, ?)', (args.id, now(), author, text))
    return 0


def cmd_done(db, args):
    author = default_author(db, here()[0])
    with transaction(db):
        old = db.execute('SELECT * FROM assignment WHERE ticket = ? AND ended IS NULL', (args.id,)).fetchone()
        if not old:
            print(f'{args.id}: no open assignment')
            return 1
        t = now()
        db.execute('UPDATE assignment SET ended = ? WHERE id = ?', (t, old['id']))
        if args.text:
            db.execute('INSERT INTO note (ticket, time, author, text) VALUES (?, ?, ?, ?)',
                       (args.id, t, author, ' '.join(args.text)))
    print(f'{args.id}: assignment of {old["agent"]} ended after {age(old["started"])}')
    return 0


def batch_line(db, b):
    runs = db.execute('SELECT state, result FROM run WHERE batch = ?', (b['id'],)).fetchall()
    done = sum(r['state'] == 'finished' for r in runs)
    bad = sum(r['state'] == 'finished' and r['result'] not in ('PASS', 'PASS*') for r in runs)
    when = {'queued': b['submitted'], 'running': b['started']}.get(b['state'], b['finished'])
    extra = f' exit {b["exit_code"]}' if b['exit_code'] is not None else ''
    return (f'  #{b["id"]:<4} {b["ticket"] or "-":<6} {b["state"]:<9} {age(when):>4}  {done}/{len(runs)} runs'
            f'{f", {bad} not passed" if bad else ""}{extra}  {os.path.basename(b["worktree"])}')


def cmd_status(db, args):
    tickets = toml_tickets()
    if args.id:
        return status_one(db, args.id, tickets)
    rows = db.execute('SELECT * FROM assignment WHERE ended IS NULL ORDER BY started').fetchall()
    print(f'{len(rows)} open assignment(s)' + (':' if rows else ''))
    for a in rows:
        title = tickets.get(a['ticket'], {}).get('title', '')
        print(f'{a["ticket"]:<6} {short(title, 50):<50} {a["agent"] or "":<26} {age(a["started"]):>4}')
        n = db.execute('SELECT * FROM note WHERE ticket = ? ORDER BY id DESC LIMIT 1', (a['ticket'],)).fetchone()
        if n:
            print(f'       note   {age(n["time"]):>4}  {short(n["text"], 90)}')
        c = last_commit(a['branch'])
        if c:
            print(f'       commit {short(c, 96)}')
    batches = db.execute("SELECT * FROM batch WHERE state IN ('queued', 'running') ORDER BY id").fetchall()
    recent = db.execute("SELECT * FROM batch WHERE state NOT IN ('queued', 'running') ORDER BY id DESC LIMIT 3").fetchall()
    if batches or recent:
        print('\nbatches:')
        for b in list(batches) + list(reversed(recent)):
            print(batch_line(db, b))
    return 0


def status_one(db, ticket, tickets):
    t = tickets.get(ticket)
    print(f'{ticket}: {t["title"]} ({t["status"]})' if t else f'{ticket}: not in docs/tickets.toml')
    for a in db.execute('SELECT * FROM assignment WHERE ticket = ? ORDER BY id', (ticket,)):
        span = f'since {a["started"]} ({age(a["started"])})' if not a['ended'] else f'{a["started"]} to {a["ended"]}'
        print(f'  {"open " if not a["ended"] else "ended"} {a["agent"]}  {span}\n        {a["worktree"]}  {a["branch"]}')
        c = last_commit(a['branch'])
        if c and not a['ended']:
            print(f'        last commit {short(c, 90)}')
    notes = db.execute('SELECT * FROM (SELECT * FROM note WHERE ticket = ? ORDER BY id DESC LIMIT 15) ORDER BY id',
                       (ticket,)).fetchall()
    if notes:
        print('notes:')
        for n in notes:
            print(f'  {n["time"][5:16].replace("T", " ")}  {short(n["author"], 24):<24} {n["text"]}')
    batches = db.execute('SELECT * FROM batch WHERE ticket = ? ORDER BY id DESC LIMIT 5', (ticket,)).fetchall()
    if batches:
        print('batches:')
        for b in reversed(batches):
            print(batch_line(db, b))
    return 0


def cmd_db(db, args):
    print(db_path())
    print(f'schema {db.execute("PRAGMA user_version").fetchone()[0]}, journal {db.execute("PRAGMA journal_mode").fetchone()[0]}')
    for table in ('assignment', 'note', 'batch', 'run', 'process'):
        print(f'  {table:<10} {db.execute(f"SELECT count(*) FROM {table}").fetchone()[0]} rows')
    return 0


def cmd_serve(db, args):
    import work_board.server
    return work_board.server.serve(sys.modules[__name__], args.port)


def main():
    with contextlib.suppress(AttributeError):
        sys.stdout.reconfigure(errors='replace')
    ap = argparse.ArgumentParser(description='The work board (build/live/work.db in the main checkout).')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('start', help='assign a ticket to this checkout')
    p.add_argument('id'); p.add_argument('--worktree'); p.add_argument('--branch'); p.add_argument('--agent')
    p.add_argument('--force', action='store_true', help='assign an id tickets.toml does not have')
    p = sub.add_parser('note', help='a progress line'); p.add_argument('id'); p.add_argument('text', nargs='+')
    p.add_argument('--author')
    p = sub.add_parser('status', help='assignments, notes, batches'); p.add_argument('id', nargs='?')
    p = sub.add_parser('done', help='end the assignment'); p.add_argument('id'); p.add_argument('text', nargs='*')
    sub.add_parser('db', help='the database path and row counts')
    p = sub.add_parser('serve', help='the live board on localhost'); p.add_argument('--port', type=int, default=8765)
    p = sub.add_parser('test', help='run_all through the scheduler (tools/work_sched.py)')
    p.add_argument('id'); p.add_argument('run_all_args', nargs=argparse.REMAINDER)
    sub.add_parser('scheduler', help='run the batches on the game slots (started by test)')
    args = ap.parse_args()
    db = connect()
    try:
        if args.cmd in ('test', 'scheduler'):
            import work_sched
            return getattr(work_sched, 'cmd_' + args.cmd)(db, args)
        return {'start': cmd_start, 'note': cmd_note, 'status': cmd_status, 'done': cmd_done, 'db': cmd_db, 'serve': cmd_serve}[args.cmd](db, args)
    finally:
        db.close()


if __name__ == '__main__':
    sys.exit(main())
