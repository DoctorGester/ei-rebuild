"""The work board's live server: `python tools/work.py serve [--port 8765]`.

A small standard-library HTTP server on 127.0.0.1 only. It reads build/live/work.db (never writes to it,
except its own heartbeat row in the `process` table), docs/tickets.toml of the main checkout and the
git branches the open assignments point at, and serves the pages in this folder. Nothing is published.

    GET /                      the page (index.html; #tickets and #runs are its two views)
    GET /tickets/              the ticket board (tools/tickets_board/index.html, the published page)
    GET /tickets/tickets.json  its data, live: tickets.py's tickets.json with the database's statuses now
    GET /api/status            everything below in one request (what the page polls)
    GET /api/tickets           tickets joined with assignment, last notes, last commit, latest batch
    GET /api/runs              queued and running batches with their games, and the history
    GET /api/batch/<id>        one batch with all its games
    GET /api/processes         scheduler / server alive or not (heartbeat age)
"""
import datetime, http.server, importlib, json, os, re, socketserver, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
HEARTBEAT_SECONDS = 5
ALIVE_SECONDS = 20               # a process whose heartbeat is older than this counts as not running
GIT_TTL = 10                     # seconds a `git for-each-ref` result is reused
DIFF_TTL = 30                    # seconds a worktree's diff stat is reused (refreshed off the request threads)
DIFF_RE = re.compile(r'(\d+) files? changed(?:, (\d+) insertions?\(\+\))?(?:, (\d+) deletions?\(-\))?')
# Windows: git for the diff stats runs below normal priority and without a console window.
GIT_FLAGS = 0x00004000 | 0x08000000 if os.name == 'nt' else 0
NOTES_PER_TICKET = 3
HISTORY = 40
PASSED = ('PASS', 'PASS*')
STATIC = {'/': ('index.html', 'text/html; charset=utf-8'), '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
          '/board.css': ('board.css', 'text/css; charset=utf-8')}
TICKET_BOARD = os.path.join(TOOLS, 'tickets_board', 'index.html')    # the published ticket board's page


def seconds_since(stamp):
    if not stamp:
        return None
    try:
        t = datetime.datetime.fromisoformat(stamp)
    except ValueError:
        return None
    return max(0.0, (datetime.datetime.now(t.tzinfo) - t).total_seconds())


def span(start, end):
    """Seconds between two ISO stamps (None when either is missing)."""
    try:
        return max(0.0, (datetime.datetime.fromisoformat(end) - datetime.datetime.fromisoformat(start)).total_seconds())
    except (TypeError, ValueError):
        return None


def jlist(text):
    try:
        v = json.loads(text) if text else []
        return v if isinstance(v, list) else []
    except ValueError:
        return []


def jdict(text):
    """The batch's recorded game variables (WB5), {} for a batch from before."""
    try:
        v = json.loads(text) if text else {}
        return v if isinstance(v, dict) else {}
    except ValueError:
        return {}


def shortstat(worktree):
    """{files, added, removed} of `worktree` against its merge base with main, or None. --no-optional-locks: the
    worker's own git commands never find index.lock taken by the board."""
    def git(*args):
        p = subprocess.run(['git', '--no-optional-locks', '-C', worktree, *args], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=20, creationflags=GIT_FLAGS)
        return p.stdout.strip() if p.returncode == 0 else None
    try:
        top = git('rev-parse', '--show-toplevel')   # a leftover folder of a removed worktree would answer for main
        if not top or os.path.normcase(os.path.abspath(top)) != os.path.normcase(os.path.abspath(worktree)):
            return None
        base = git('merge-base', 'main', 'HEAD')
        out = git('diff', '--shortstat', base) if base else None
    except (OSError, subprocess.SubprocessError):
        return None
    if out is None:
        return None
    m = DIFF_RE.search(out)
    return {'files': int(m.group(1)), 'added': int(m.group(2) or 0), 'removed': int(m.group(3) or 0)} if m else \
        {'files': 0, 'added': 0, 'removed': 0}


class Board:
    """The data behind the endpoints. One instance; the caches are shared by the request threads."""

    def __init__(self, work):
        self.work = work
        self.main = work.main_checkout()
        self.tickets_py = None
        self.lock = threading.Lock()
        self.toml_cache = (None, None)       # (mtime/size key, (groups, tickets))
        self.git_cache = (None, {})          # (monotonic time of the call, {branch: commit})
        self.diff_cache = {}                 # worktree -> (monotonic time, {files, added, removed} or None)
        self.diff_busy = False               # a refresh thread is running

    # ---- sources

    def connect(self):
        db = self.work.connect()
        db.execute('PRAGMA query_only = ON')   # this connection only reads
        return db

    def load_tickets(self, db):
        """(data, groups, tickets) of the main checkout's docs/tickets.toml (through tools/tickets.py's loader, parsed
        again when the file changes) with the statuses the database has now (ticket_status, WB10)."""
        path = os.path.join(self.main, 'docs', 'tickets.toml')
        with self.lock:
            st = os.stat(path)
            key = (st.st_mtime_ns, st.st_size)
            if self.toml_cache[0] != key:
                if self.tickets_py is None:
                    sys.path.insert(0, TOOLS)
                    self.tickets_py = importlib.import_module('tickets')
                self.tickets_py.SOURCE = path
                data, groups, tickets, problems = self.tickets_py.load()
                self.toml_cache = (key, (data, groups, tickets, self.tickets_py.toml_commit(self.main)))
            data, groups, tickets, commit = self.toml_cache[1]
        tickets = [dict(t) for t in tickets]
        self.tickets_py.with_status(tickets, self.work.ticket_statuses(db))
        self.tickets_py.with_worked(db, tickets)
        self.tickets_py.derive(tickets)
        return data, groups, tickets, commit

    def ticket_board(self, db):
        """tickets.json for the ticket board page (tools/tickets_board/index.html), live."""
        data, groups, tickets, commit = self.load_tickets(db)
        return self.tickets_py.board_data(data, groups, tickets, commit, live=True)

    def branches(self):
        """{branch: {hash, time, subject}} for every local branch: one git call, reused for GIT_TTL seconds."""
        with self.lock:
            when, cached = self.git_cache
            if when is not None and time.monotonic() - when < GIT_TTL:
                return cached
            out = {}
            try:
                p = subprocess.run(['git', 'for-each-ref', '--format=%(refname:short)%09%(objectname:short)%09%(committerdate:iso-strict)%09%(subject)',
                                    'refs/heads'], cwd=self.main, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)
                for line in p.stdout.splitlines():
                    name, h, t, subject = (line.split('\t') + ['', '', ''])[:4]
                    out[name] = {'hash': h, 'time': t, 'subject': subject}
            except (OSError, subprocess.SubprocessError):
                out = cached
            self.git_cache = (time.monotonic(), out)
            return out

    def diff_stats(self, worktrees):
        """{worktree: {files, added, removed}}: each worktree against the commit its branch left main at, uncommitted edits
        included (untracked files not). A request never waits on git: stale entries are refreshed by one background
        thread at a time, at most every DIFF_TTL seconds, and the request gets what the cache has."""
        now = time.monotonic()
        with self.lock:
            stale = [w for w in worktrees if now - self.diff_cache.get(w, (-DIFF_TTL, None))[0] >= DIFF_TTL]
            if stale and not self.diff_busy:
                self.diff_busy = True
                threading.Thread(target=self.refresh_diffs, args=(stale,), daemon=True).start()
            return {w: self.diff_cache[w][1] for w in worktrees if w in self.diff_cache and self.diff_cache[w][1]}

    def refresh_diffs(self, worktrees):
        try:
            main = os.path.normcase(os.path.abspath(self.main))
            for w in worktrees:
                stat = None
                if os.path.isdir(w) and os.path.normcase(os.path.abspath(w)) != main:
                    stat = shortstat(w)
                with self.lock:
                    self.diff_cache[w] = (time.monotonic(), stat)
        finally:
            with self.lock:
                self.diff_busy = False

    # ---- batches

    def batch_json(self, db, b, games=False):
        runs = db.execute('SELECT * FROM run WHERE batch = ? ORDER BY id', (b['id'],)).fetchall()
        done = [r for r in runs if r['state'] == 'finished']
        failed = [r for r in done if r['result'] not in PASSED]
        out = {
            'id': b['id'], 'ticket': b['ticket'], 'worktree': os.path.basename(b['worktree'] or ''), 'state': b['state'],
            'args': jlist(b['args']), 'env': jdict(b['env'] if 'env' in b.keys() else None), 'submitted': b['submitted'], 'started': b['started'], 'finished': b['finished'],
            'exit_code': b['exit_code'], 'summary': b['summary'],
            'queued_for': span(b['submitted'], b['started']) if b['started'] else seconds_since(b['submitted']) if b['state'] == 'queued' else None,
            'elapsed': span(b['started'], b['finished']) if b['finished'] else seconds_since(b['started']) if b['state'] == 'running' else None,
            'total': len(runs), 'queued': sum(r['state'] == 'queued' for r in runs),
            'running': sum(r['state'] == 'running' for r in runs), 'done': len(done),
            'passed': len(done) - len(failed), 'failed': len(failed),
            'expected': sum(r['expected'] or 0 for r in runs),
        }

        def game(r):
            elapsed = seconds_since(r['started']) if r['state'] == 'running' else r['seconds']
            return {'id': r['id'], 'scenario': r['scenario'], 'install': r['install'], 'state': r['state'], 'slot': r['slot'],
                    'alone': bool(r['alone']), 'attempt': r['attempt'], 'expected': r['expected'], 'elapsed': elapsed,
                    'result': r['result'], 'failures': jlist(r['failures']), 'notes': jlist(r['notes']), 'folder': r['folder']}

        if games:
            out['games'] = [game(r) for r in runs]
        else:
            out['failures'] = [game(r) for r in failed][:12]
        return out

    def runs(self, db):
        rows = db.execute("SELECT * FROM batch WHERE state IN ('queued', 'running') ORDER BY id").fetchall()
        hist = db.execute("SELECT * FROM batch WHERE state IN ('finished', 'cancelled') ORDER BY id DESC LIMIT ?", (HISTORY,)).fetchall()
        return {'running': [self.batch_json(db, b, games=True) for b in rows if b['state'] == 'running'],
                'queued': [self.batch_json(db, b, games=True) for b in rows if b['state'] == 'queued'],
                'history': [self.batch_json(db, b) for b in hist]}

    # ---- tickets

    def tickets(self, db):
        data, groups, tickets, commit = self.load_tickets(db)
        gname = {g['key']: g['name'] for g in groups}
        branches = self.branches()
        assign = {a['ticket']: a for a in db.execute('SELECT * FROM assignment WHERE ended IS NULL')}
        diffs = self.diff_stats(sorted({a['worktree'] for a in assign.values() if a['worktree']}))
        ended = {}
        for a in db.execute('SELECT * FROM assignment WHERE ended IS NOT NULL ORDER BY id'):
            ended[a['ticket']] = a
        notes = {}
        for n in db.execute('SELECT * FROM (SELECT *, row_number() OVER (PARTITION BY ticket ORDER BY id DESC) AS rn FROM note) '
                            'WHERE rn <= ? ORDER BY ticket, id DESC', (NOTES_PER_TICKET,)):
            notes.setdefault(n['ticket'], []).append({'time': n['time'], 'author': n['author'], 'text': n['text']})
        latest = {}
        for b in db.execute('SELECT * FROM batch WHERE id IN (SELECT max(id) FROM batch WHERE ticket IS NOT NULL GROUP BY ticket)'):
            latest[b['ticket']] = self.batch_json(db, b)
        # The last finished batch too, while a newer one is still queued or running.
        previous = {}
        for b in db.execute("SELECT * FROM batch WHERE id IN (SELECT max(id) FROM batch WHERE ticket IS NOT NULL "
                            "AND state IN ('finished', 'cancelled') GROUP BY ticket)"):
            if latest.get(b['ticket'], {}).get('state') in ('queued', 'running'):
                previous[b['ticket']] = self.batch_json(db, b)

        def assignment(a):
            return {'agent': a['agent'], 'worktree': a['worktree'], 'branch': a['branch'], 'started': a['started'],
                    'ended': a['ended'], 'open': a['ended'] is None,
                    'diff': diffs.get(a['worktree']) if a['ended'] is None else None}

        out = []
        for t in tickets:
            a = assign.get(t['id']) or ended.get(t['id'])
            out.append({'id': t['id'], 'title': t['title'], 'group': t['group'], 'group_name': gname.get(t['group'], t['group']),
                        'state': t['state'], 'status': t['status'], 'size': t.get('size'), 'model': (a['model'] if a else None) or t.get('model'),
                        'depends': t.get('depends', []), 'note': t.get('note', ''), 'files': t.get('files', []),
                        'started': t.get('started'), 'done': t.get('done'),
                        'assignment': assignment(a) if a else None, 'notes': notes.get(t['id'], []),
                        'commit': branches.get(a['branch']) if a else None, 'batch': latest.get(t['id']),
                        'previous': previous.get(t['id'])})
        known = {t['id'] for t in tickets}
        for tid, a in assign.items():       # assigned with `work.py start --force`: an id the toml does not have
            if tid not in known:
                out.append({'id': tid, 'title': '(not in docs/tickets.toml)', 'group': '', 'group_name': '', 'state': 'running',
                            'status': 'running', 'size': None, 'model': a['model'], 'depends': [], 'note': '', 'files': [],
                            'started': None, 'done': None, 'assignment': assignment(a), 'notes': notes.get(tid, []),
                            'commit': branches.get(a['branch']), 'batch': latest.get(tid)})
        return {'groups': groups, 'tickets': out}

    # ---- processes

    def processes(self, db):
        out = {name: {'alive': False, 'pid': None, 'heartbeat_age': None, 'started': None, 'info': None} for name in ('scheduler', 'server')}
        for r in db.execute('SELECT * FROM process'):
            age = seconds_since(r['heartbeat'])
            out[r['name']] = {'alive': age is not None and age < ALIVE_SECONDS, 'pid': r['pid'], 'heartbeat_age': age,
                              'started': r['started'], 'info': r['info']}
        return out

    def status(self, db):
        return {'time': self.work.now(), 'processes': self.processes(db), 'runs': self.runs(db), **self.tickets(db)}


# ---- the server process's own row

def heartbeat_loop(work, info, stop):
    """Registers the server in the process table and refreshes the row every few seconds (short transactions)."""
    started = work.now()
    db = work.connect()
    try:
        while True:
            try:
                with work.transaction(db):
                    db.execute('INSERT INTO process (name, pid, started, heartbeat, info) VALUES (?, ?, ?, ?, ?) '
                               'ON CONFLICT(name) DO UPDATE SET pid = excluded.pid, started = excluded.started, '
                               'heartbeat = excluded.heartbeat, info = excluded.info',
                               ('server', os.getpid(), started, work.now(), info))
            except Exception as e:                    # a busy database must not kill the server
                print('heartbeat:', e, file=sys.stderr)
            if stop.wait(HEARTBEAT_SECONDS):
                break
        with work.transaction(db):
            db.execute("DELETE FROM process WHERE name = 'server' AND pid = ?", (os.getpid(),))
    finally:
        db.close()


def make_handler(board):
    class Handler(http.server.BaseHTTPRequestHandler):
        server_version = 'work-board'

        def log_message(self, *args):
            pass

        def send(self, code, body, ctype):
            if isinstance(body, str):
                body = body.encode('utf-8')
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)

        def json(self, value, code=200):
            self.send(code, json.dumps(value, ensure_ascii=False), 'application/json; charset=utf-8')

        def do_GET(self):
            host = (self.headers.get('Host') or '').rsplit(':', 1)[0].strip('[]').lower()
            if host not in ('127.0.0.1', 'localhost', '::1'):      # DNS rebinding guard: this board is local only
                return self.json({'error': 'local only'}, 403)
            path = self.path.split('?', 1)[0]
            try:
                if path in STATIC:
                    name, ctype = STATIC[path]
                    with open(os.path.join(HERE, name), 'rb') as f:
                        return self.send(200, f.read(), ctype)
                if path == '/tickets':
                    self.send_response(301)
                    self.send_header('Location', '/tickets/')
                    self.send_header('Content-Length', '0')
                    return self.end_headers()
                if path == '/tickets/':
                    with open(TICKET_BOARD, 'rb') as f:
                        return self.send(200, f.read(), 'text/html; charset=utf-8')
                if path.startswith('/api/') or path == '/tickets/tickets.json':
                    db = board.connect()
                    try:
                        if path == '/tickets/tickets.json':
                            return self.json(board.ticket_board(db))
                        if path == '/api/tickets':
                            return self.json(board.tickets(db))
                        if path == '/api/runs':
                            return self.json(board.runs(db))
                        if path == '/api/processes':
                            return self.json(board.processes(db))
                        if path == '/api/status':
                            return self.json(board.status(db))
                        if path.startswith('/api/batch/') and path[11:].isdigit():
                            b = db.execute('SELECT * FROM batch WHERE id = ?', (int(path[11:]),)).fetchone()
                            return self.json(board.batch_json(db, b, games=True) if b else {'error': 'no such batch'}, 200 if b else 404)
                    finally:
                        db.close()
                self.json({'error': 'not found'}, 404)
            except (BrokenPipeError, ConnectionError):
                pass
            except Exception as e:
                self.json({'error': f'{type(e).__name__}: {e}'}, 500)

    return Handler


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = False      # a second server on the same port must fail, not share it


def serve(work, port=8765):
    board = Board(work)
    try:
        httpd = Server(('127.0.0.1', port), make_handler(board))
    except OSError as e:
        sys.exit(f'work.py serve: cannot bind 127.0.0.1:{port} (already running?): {e}')   # exclusive bind: one server per port
    url = f'http://127.0.0.1:{port}/'
    stop = threading.Event()
    beat = threading.Thread(target=heartbeat_loop, args=(work, url, stop), daemon=True)
    beat.start()
    print(f'work board on {url}  (database {work.db_path()}); Ctrl+C to stop', flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        beat.join(timeout=10)
        httpd.server_close()
    return 0
