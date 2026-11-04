"""The ticket queue: docs/tickets/<ID>.toml says what each ticket is; the shared work database has its live status.

usage: python tools/tickets.py                      check, then write build/tickets/tickets.md and tickets.json
       python tools/tickets.py show ID [ID ...]     one ticket: status, note, files, its group's note and doc,
                                                    what it waits on and what it unblocks
       python tools/tickets.py list [--state S] [--group G]   one line per ticket (S: running, review, ready,
                                                    waiting, watch, later, done; G: a group key)
       python tools/tickets.py set ID STATUS        status, in the database and in the ticket's file (dates filled in:
                                                    started for running, done for done); from any worktree, shows at once
       python tools/tickets.py note ID TEXT [--append | --replace]
                                                    --append adds to the note; a plain note refuses to overwrite a
                                                    non-empty one (--replace says it is meant)
       python tools/tickets.py add ID --group G --title T [--depends A,B] [--note N] [--files F,G]
                                                    [--size N] [--model M] [--status S]   (a follow-up ticket)
       python tools/tickets.py check                check the files and the statuses; exit 1 on a problem

One file per ticket, docs/tickets/<ID>.toml (flat keys: id, title, group, optional size, model, depends, files, doc,
note, and `order`: the ticket's place in the list, a new ticket takes the largest plus one; equal orders sort by id),
so two branches that add or change different tickets never conflict. docs/tickets/groups.toml holds the queue's rules
and the [[group]] headers.

Status (running, review, queued, later, watch, done; with the started and done dates) lives live in the table
ticket_status of build/live/work.db in the main checkout (tools/work.py; WORK_DB overrides the path), so a change
shows everywhere at once. A ticket with no row there is queued, or takes the status / started / done lines of its file:
work.set_ticket_status (`set`, `work.py start`, `work.py done ID merged`) writes them there too, so the committed
files carry the statuses to checkouts without the database (cloud sessions). The database wins where it has a row.

Every change rewrites build/tickets/tickets.md and tickets.json. A queued ticket is "ready" when every ticket it
depends on is done, else "waiting". The published board (tools/tickets_board/index.html) reads tickets.json; `work.py
serve` shows the same page live on http://127.0.0.1:8765/tickets/.
"""
import argparse, contextlib, datetime, json, os, subprocess, sys, time, tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import work

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = work.TICKETS_DIR
GROUPS_FILE = 'groups.toml'
MARKDOWN = os.path.join(ROOT, 'build', 'tickets', 'tickets.md')
STATUSES = ('running', 'review', 'queued', 'later', 'watch', 'done')
STATES = ('running', 'review', 'ready', 'waiting', 'watch', 'later', 'done')
LOCK = os.path.join(ROOT, 'build', 'tickets', 'tickets.lock')
STATUS_FIELDS = work.TICKET_STATUS_FIELDS            # the database's, with a committed copy in the ticket's file
FIELDS = {'id', 'title', 'group', 'size', 'model', 'depends', 'files', 'doc', 'note', 'order'} | set(STATUS_FIELDS)


@contextlib.contextmanager
def toml_lock(timeout=120):
    """Holds this checkout's lock file around a read-modify-write of the ticket files and the files written
    from them, so two tickets.py running at once never both read the old file and drop one change.
    A byte lock: the system drops it when the process ends, so a killed run never leaves it taken."""
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    f = open(LOCK, 'a+b')
    deadline = time.time() + timeout
    while True:
        try:
            if os.name == 'nt':
                import msvcrt
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except OSError:
            if time.time() > deadline:
                f.close()
                sys.exit(f'tickets.py: {LOCK} still held after {timeout} s')
            time.sleep(0.05)
    try:
        yield
    finally:
        if os.name == 'nt':
            import msvcrt
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        f.close()


def ticket_paths(source_dir=None):
    """Every ticket file of the folder, sorted by name (groups.toml is not one)."""
    source_dir = source_dir or SOURCE_DIR
    return [os.path.join(source_dir, f) for f in sorted(os.listdir(source_dir))
            if f.endswith('.toml') and f != GROUPS_FILE]


def load(source_dir=None):
    """(data, groups, tickets, problems) of the folder's ticket files and groups.toml. The tickets are in `order` (then
    id) order; `order` itself is not part of a ticket. with_status gives them their status."""
    source_dir = source_dir or SOURCE_DIR
    with open(os.path.join(source_dir, GROUPS_FILE), 'rb') as f:
        data = tomllib.load(f)
    groups = data.get('group', [])
    problems, tickets = [], []
    for path in ticket_paths(source_dir):
        name = os.path.basename(path)
        try:
            with open(path, 'rb') as f:
                t = tomllib.load(f)
        except (tomllib.TOMLDecodeError, UnicodeDecodeError) as e:
            problems.append(f'{name}: {e}')
            continue
        if t.get('id') and name != t['id'] + '.toml':
            problems.append(f'{name}: holds ticket {t["id"]}, the file must be called {t["id"]}.toml')
        tickets.append(t)
    tickets.sort(key=lambda t: (t['order'] if isinstance(t.get('order'), int) else float('inf'), str(t.get('id'))))
    keys = [g['key'] for g in groups]
    ids = [t.get('id') for t in tickets]
    for i in {i for i in ids if ids.count(i) > 1}:
        problems.append(f'duplicate id {i}')
    for t in tickets:
        where = t.get('id', '?')
        for field in ('id', 'title', 'group'):
            if not t.get(field):
                problems.append(f'{where}: no {field}')
        if t.get('group') not in keys:
            problems.append(f'{where}: unknown group {t.get("group")}')
        for d in t.get('depends', []):
            if d not in ids:
                problems.append(f'{where}: depends on unknown {d}')
        if 'order' in t and not isinstance(t['order'], int):
            problems.append(f'{where}: order is not an integer')
        for field in set(t) - FIELDS:
            problems.append(f'{where}: unknown field {field}')
        t.pop('order', None)
    return data, groups, tickets, problems


def with_status(tickets, statuses):
    """Gives each ticket status, started and done from the database's rows {id: row}. A ticket the database has no
    row for keeps the lines of its file if it has any (the committed copy), else it is queued. Returns problems."""
    problems = []
    for t in tickets:
        row = statuses.get(t.get('id'))
        if row is None:
            row = {k: t[k] for k in STATUS_FIELDS if k in t}
        for k in STATUS_FIELDS:
            t.pop(k, None)
            if row.get(k):
                t[k] = row[k]
        t.setdefault('status', 'queued')
        if t['status'] not in STATUSES:
            problems.append(f'{t.get("id")}: status {t["status"]} is not one of {", ".join(STATUSES)}')
    return problems


def with_worked(db, tickets):
    """Adds `worked`: the ticket's assignments as [started, ended] ISO times, for the board's elapsed time. An open
    assignment ends when the ticket went to review (the worker finished) or done; while running it stays null."""
    changed = dict(db.execute("SELECT ticket, changed FROM ticket_status WHERE status IN ('review', 'done')"))
    spans = {}
    for ticket, started, ended in db.execute('SELECT ticket, started, ended FROM assignment ORDER BY id'):
        spans.setdefault(ticket, []).append([started, ended or changed.get(ticket)])
    for t in tickets:
        if t.get('id') in spans:
            t['worked'] = spans[t['id']]


def loaded(db):
    """load() with the database's statuses filled in and the states derived; the files' problems included."""
    data, groups, tickets, problems = load()
    problems += with_status(tickets, work.ticket_statuses(db))
    with_worked(db, tickets)
    running = dict(db.execute('SELECT ticket, model FROM assignment WHERE ended IS NULL AND model IS NOT NULL'))
    for t in tickets:   # the model a running worker was actually given (`work.py start --model`) over the planned one
        if t.get('id') in running:
            t['model'] = running[t['id']]
    derive(tickets)
    return data, groups, tickets, problems



def derive(tickets):
    """Adds `state`: the status, with queued split into ready / waiting by its depends."""
    by_id = {t['id']: t for t in tickets}
    for t in tickets:
        state = t['status']
        if state == 'queued':
            open_deps = [d for d in t.get('depends', []) if d in by_id and by_id[d]['status'] != 'done']
            state = 'waiting' if open_deps else 'ready'
            t['waiting_on'] = open_deps
        t['state'] = state
    return tickets


def markdown(data, groups, tickets):
    out = ['# Ticket queue', '',
           'Generated from docs/tickets/ and the statuses in the work database by tools/tickets.py: never edit this '
           'file; read one ticket with `python tools/tickets.py show ID` and change tickets through the script. '
           'The ticket board shows the same data.', '']
    out += [f'- {r}' for r in data.get('queue', {}).get('rules', [])] + ['']
    order = {k: i for i, k in enumerate(STATES)}
    for g in groups:
        mine = sorted((t for t in tickets if t['group'] == g['key']), key=lambda t: order[t['state']])
        if not mine:
            continue
        out.append(f'## {g["name"]}' + (f' ({g["doc"]})' if g.get('doc') else ''))
        out.append('')
        if g.get('note'):
            out += [g['note'], '']
        for t in mine:
            state = t['state']
            if state in ('running', 'review') and t.get('started'):
                state += f' since {t["started"]}'
            if state == 'done' and t.get('done'):
                state += f' {t["done"]}'
            if state == 'waiting':
                state += ' on ' + ', '.join(t['waiting_on'])
            extra = [x for x in (t.get('model'), f'{t["size"]} session' + ('' if t['size'] == 1 else 's') if t.get('size') else None) if x]
            line = f'- **{t["id"]}** {t["title"]} ({state}' + ('; ' + ', '.join(extra) if extra else '') + ').'
            if t.get('note'):
                line += f' {t["note"]}'
            if t.get('files'):
                line += ' Files: ' + ', '.join(f'`{f}`' for f in t['files']) + '.'
            if t.get('depends') and t['state'] != 'waiting':
                line += ' After: ' + ', '.join(t['depends']) + '.'
            out.append(line)
        out.append('')
    return '\n'.join(out)


def toml_commit(root=ROOT):
    """(short hash, ISO time) of the last commit that changed docs/tickets in that checkout."""
    out = subprocess.run(['git', 'log', '-1', '--format=%h %cI', '--', 'docs/tickets'], cwd=root,
                         capture_output=True, text=True).stdout.split()
    return (out[0], out[1]) if len(out) == 2 else (None, None)


def board_data(data, groups, tickets, commit=(None, None), live=False):
    """What the ticket board page reads (tickets.json); live: served by `work.py serve`, statuses as of now."""
    return {'generated': datetime.datetime.now().isoformat(timespec='minutes'), 'live': live,
            'commit': commit[0], 'committed': commit[1],
            'rules': data.get('queue', {}).get('rules', []), 'groups': groups, 'tickets': tickets}


def generate(db, json_path=None):
    data, groups, tickets, problems = loaded(db)
    if problems:
        for p in problems:
            print('tickets:', p)
        return 1
    os.makedirs(os.path.dirname(MARKDOWN), exist_ok=True)
    with open(MARKDOWN, 'w', encoding='utf-8', newline='\n') as f:
        f.write(markdown(data, groups, tickets))
    json_path = json_path or os.path.join(ROOT, 'build', 'tickets', 'tickets.json')
    os.makedirs(os.path.dirname(json_path), exist_ok=True)
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(board_data(data, groups, tickets, toml_commit()), f, ensure_ascii=False, indent=1)
    counts = {s: sum(t['state'] == s for t in tickets) for s in STATES}
    print(f'{len(tickets)} tickets: ' + ', '.join(f'{n} {s}' for s, n in counts.items()))
    return 0


# ---- editing: line by line, so a ticket file's layout stays as it is

def toml_value(v):
    if isinstance(v, list):
        return '[' + ', '.join(json.dumps(x, ensure_ascii=False) for x in v) + ']'
    if isinstance(v, (int, float)):
        return repr(v)
    return json.dumps(v, ensure_ascii=False)     # a TOML basic string takes JSON's escapes


def ticket_path(ticket_id):
    return os.path.join(SOURCE_DIR, ticket_id + '.toml')


def write_checked(path, text, before):
    """Writes the ticket file, checks the whole folder, and puts the old file back (or removes a new one) on a problem."""
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    problems = load()[3]
    if problems:
        if before is None:
            os.remove(path)
        else:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(before)
        for p in problems:
            print('tickets:', p)
        sys.exit('tickets.py: change refused, the files would not check (see above); nothing written')


def edit(ticket_id, updates):
    path = ticket_path(ticket_id)
    if not os.path.exists(path):
        sys.exit(f'tickets.py: no ticket {ticket_id}')
    with open(path, encoding='utf-8') as f:
        before = f.read()
    lines = before.rstrip('\n').split('\n')
    for key, value in updates.items():
        for i, line in enumerate(lines):
            if line.split('=')[0].strip() == key:
                lines[i] = f'{key} = {toml_value(value)}'
                break
        else:       # a new key goes before `order` and the status lines
            at = next((i for i, line in enumerate(lines) if line.split('=')[0].strip() in ('order',) + STATUS_FIELDS), len(lines))
            lines.insert(at, f'{key} = {toml_value(value)}')
    write_checked(path, '\n'.join(lines) + '\n', before)


def max_order():
    top = 0
    for path in ticket_paths():
        with open(path, 'rb') as f:
            o = tomllib.load(f).get('order')
        top = max(top, o if isinstance(o, int) else 0)
    return top


def add(fields):
    order = max_order() + 1
    path = ticket_path(fields['id'])
    if os.path.exists(path):
        sys.exit(f'tickets.py: ticket {fields["id"]} exists')
    lines = [f'{k} = {toml_value(v)}' for k, v in fields.items()] + [f'order = {order}']
    write_checked(path, '\n'.join(lines) + '\n', None)


def describe(t, by_id, groups):
    g = groups[t['group']]
    state = t['state'] + (f' since {t["started"]}' if t.get('started') and t['state'] in ('running', 'review') else '') + \
        (f' {t["done"]}' if t.get('done') and t['state'] == 'done' else '')
    out = [f'{t["id"]}: {t["title"]}',
           f'  status: {state}' + ''.join(f'; {k} {t[k]}' for k in ('model', 'size') if t.get(k)),
           f'  group: {g["name"]}' + (f' ({g["doc"]})' if g.get('doc') else '')]
    if t.get('doc'):
        out.append(f'  doc: {t["doc"]}')
    if t.get('note'):
        out.append(f'  note: {t["note"]}')
    if t.get('files'):
        out.append('  files: ' + ', '.join(t['files']))
    if t.get('depends'):
        out.append('  after: ' + ', '.join(f'{d} ({by_id[d]["state"]})' for d in t['depends']))
    unblocks = [o['id'] for o in by_id.values() if t['id'] in o.get('depends', []) and o['state'] != 'done']
    if unblocks:
        out.append('  unblocks: ' + ', '.join(unblocks))
    if g.get('note'):
        out.append(f'  group note: {g["note"]}')
    return '\n'.join(out)


def main():
    if hasattr(sys.stdout, 'reconfigure'):  # ticket text may hold Cyrillic zone names
        sys.stdout.reconfigure(encoding='utf-8')
    ap = argparse.ArgumentParser(description='The ticket queue (docs/tickets/; live status in the work database).')
    ap.add_argument('--json', help='where the board data goes (default build/tickets/tickets.json)')
    sub = ap.add_subparsers(dest='cmd')
    sub.add_parser('check')
    p = sub.add_parser('show'); p.add_argument('ids', nargs='+')
    p = sub.add_parser('list'); p.add_argument('--state', choices=STATES); p.add_argument('--group')
    p = sub.add_parser('set'); p.add_argument('id'); p.add_argument('status', choices=STATUSES)
    p = sub.add_parser('note'); p.add_argument('id'); p.add_argument('text')
    how = p.add_mutually_exclusive_group()
    how.add_argument('--append', action='store_true', help='add to the ticket\'s note')
    how.add_argument('--replace', action='store_true', help='overwrite a note that is not empty')
    p = sub.add_parser('add'); p.add_argument('id')
    for opt in ('--group', '--title'):
        p.add_argument(opt, required=True)
    for opt in ('--depends', '--note', '--files', '--size', '--model'):
        p.add_argument(opt)
    p.add_argument('--status', choices=STATUSES, default='queued')
    args = ap.parse_args()
    db = work.connect()
    try:
        if args.cmd in ('check', 'show', 'list'):
            return command(db, args)
        with toml_lock():     # everything else reads, changes and rewrites the files
            return command(db, args)
    finally:
        db.close()


def author(db):
    try:
        return work.default_author(db, work.here()[0])
    except SystemExit:
        return None


def command(db, args):
    if args.cmd == 'check':
        problems = loaded(db)[3]
        for p in problems:
            print('tickets:', p)
        return 1 if problems else 0
    if args.cmd is None:
        return generate(db, args.json)
    if args.cmd in ('show', 'list'):
        data, groups, tickets, problems = loaded(db)
        by_id = {t['id']: t for t in tickets}
        groups = {g['key']: g for g in groups}
        if args.cmd == 'show':
            for i in args.ids:
                print(describe(by_id[i], by_id, groups) if i in by_id else f'no ticket {i}')
        else:
            for t in tickets:
                if (not args.state or t['state'] == args.state) and (not args.group or t['group'] == args.group):
                    print(f'{t["id"]:<6} {t["state"]:<8} {t["group"]:<12} {t["title"]}')
        return 0
    tickets = load()[2]
    if args.cmd == 'set':
        if args.id not in {t['id'] for t in tickets} and args.id not in work.toml_tickets():
            sys.exit(f'tickets.py: no ticket {args.id}')
        work.set_ticket_status(db, args.id, args.status, author(db), files=SOURCE_DIR)
    elif args.cmd == 'note':
        old = next((t.get('note', '') for t in tickets if t['id'] == args.id), '')
        if old and not args.append and not args.replace:
            sys.exit(f'tickets.py: {args.id} has a note already; --append adds to it, --replace overwrites it (the old note:\n{old})')
        edit(args.id, {'note': (old + ' ' + args.text).strip() if args.append else args.text})
    elif args.cmd == 'add':
        fields = {'id': args.id, 'title': args.title, 'group': args.group}
        if args.size:
            fields['size'] = float(args.size) if '.' in args.size else int(args.size)
        if args.model:
            fields['model'] = args.model
        if args.depends:
            fields['depends'] = [d.strip() for d in args.depends.split(',') if d.strip()]
        if args.files:
            fields['files'] = [f.strip() for f in args.files.split(',') if f.strip()]
        if args.note:
            fields['note'] = args.note
        if args.id in {t['id'] for t in tickets}:
            sys.exit(f'tickets.py: ticket {args.id} exists')
        add(fields)
        if args.status != 'queued' or args.id in work.ticket_statuses(db):
            work.set_ticket_status(db, args.id, args.status, author(db), files=SOURCE_DIR)
    return generate(db, args.json)


if __name__ == '__main__':
    sys.exit(main())
