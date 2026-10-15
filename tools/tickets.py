"""The ticket queue: docs/tickets.toml says what each ticket is, the shared work database its status.

usage: python tools/tickets.py                      check, then write docs/tickets.md and build/tickets/tickets.json
       python tools/tickets.py show ID [ID ...]     one ticket: status, note, files, its group's note and doc,
                                                    what it waits on and what it unblocks
       python tools/tickets.py list [--state S] [--group G]   one line per ticket (S: running, review, ready,
                                                    waiting, watch, later, done; G: a group key)
       python tools/tickets.py set ID STATUS        status, in the database (dates filled in: started for
                                                    running, done for done); from any worktree, shows at once
       python tools/tickets.py note ID TEXT [--append]
       python tools/tickets.py add ID --group G --title T [--depends A,B] [--note N] [--files F,G]
                                                    [--size N] [--model M] [--status S]   (a follow-up ticket)
       python tools/tickets.py check                check the file and the statuses; exit 1 on a problem

Status (running, review, queued, later, watch, done; with the started and done dates) lives in the table
ticket_status of build/live/work.db in the main checkout (tools/work.py; WORK_DB overrides the path), so a
change shows everywhere at once and never conflicts in a merge. A ticket with no row there is queued.
`work.py start ID` sets running, a worker sets review (`set ID review`), `work.py done ID merged` sets done.

tickets.toml holds no status, started or done lines: `check` rejects them, and every other command that writes
moves them out first. A ticket the database has never seen takes the line's values (the one-time migration,
or a ticket an old branch added); a ticket it has keeps its row and the line is dropped (an old branch's status
edit is ignored).

Every change rewrites docs/tickets.md and tickets.json. A queued ticket is "ready" when every ticket it depends
on is done, else "waiting". The published board (tools/tickets_board/index.html) reads tickets.json; `work.py
serve` shows the same page live on http://127.0.0.1:8765/tickets/.
"""
import argparse, contextlib, datetime, json, os, subprocess, sys, time, tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import work

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'docs', 'tickets.toml')
MARKDOWN = os.path.join(ROOT, 'docs', 'tickets.md')
STATUSES = ('running', 'review', 'queued', 'later', 'watch', 'done')
STATES = ('running', 'review', 'ready', 'waiting', 'watch', 'later', 'done')
LOCK = os.path.join(ROOT, 'build', 'tickets', 'tickets.lock')
FIELDS = {'id', 'title', 'group', 'size', 'model', 'depends', 'files', 'doc', 'note'}
STATUS_FIELDS = ('status', 'started', 'done')       # the database's, never the toml's


@contextlib.contextmanager
def toml_lock(timeout=120):
    """Holds this checkout's lock file around a read-modify-write of tickets.toml and the files written
    from it, so two tickets.py running at once never both read the old file and drop one change.
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


def read_source():
    with open(SOURCE, encoding='utf-8') as f:
        return f.read()


def load():
    """(data, groups, tickets, problems) of tickets.toml; with_status gives the tickets their status."""
    with open(SOURCE, 'rb') as f:
        data = tomllib.load(f)
    groups, tickets = data.get('group', []), data.get('ticket', [])
    problems = []
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
        for field in [f for f in STATUS_FIELDS if f in t]:
            problems.append(f'{where}: {field} belongs in the work database (python tools/tickets.py moves it there)')
        for field in set(t) - FIELDS - set(STATUS_FIELDS):
            problems.append(f'{where}: unknown field {field}')
    return data, groups, tickets, problems


def with_status(tickets, statuses):
    """Gives each ticket status, started and done from the database's rows {id: row}. A ticket the database has
    never seen keeps its toml lines if it has any (before the migration), else it is queued. Returns problems."""
    problems = []
    for t in tickets:
        row = statuses.get(t.get('id'))
        if row is not None:
            for k in STATUS_FIELDS:
                t.pop(k, None)
                if row.get(k):
                    t[k] = row[k]
        t.setdefault('status', 'queued')
        if t['status'] not in STATUSES:
            problems.append(f'{t.get("id")}: status {t["status"]} is not one of {", ".join(STATUSES)}')
    return problems


def loaded(db):
    """load() with the database's statuses filled in and the states derived; the toml's problems included."""
    data, groups, tickets, problems = load()
    problems += with_status(tickets, work.ticket_statuses(db))
    running = dict(db.execute('SELECT ticket, model FROM assignment WHERE ended IS NULL AND model IS NOT NULL'))
    for t in tickets:   # the model a running worker was actually given (`work.py start --model`) over the planned one
        if t.get('id') in running:
            t['model'] = running[t['id']]
    derive(tickets)
    return data, groups, tickets, problems


def status_lines(lines):
    """Indexes of the status / started / done lines inside the toml's [[ticket]] blocks."""
    out, block = [], False
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith('['):
            block = s == '[[ticket]]'
        elif block and '=' in s and not s.startswith('#') and s.split('=')[0].strip() in STATUS_FIELDS:
            out.append(i)
    return out


def absorb(db):
    """Moves the toml's status lines into the database (work.migrate_ticket_status: a ticket it has never seen takes
    them, one it has keeps its row) and rewrites the toml without them. Nothing to do once the toml is clean."""
    text = read_source()
    lines = text.split('\n')
    drop = set(status_lines(lines))
    if not drop:
        return
    found = {t['id']: {k: t[k] for k in STATUS_FIELDS if k in t}
             for t in tomllib.loads(text).get('ticket', []) if t.get('id') and any(k in t for k in STATUS_FIELDS)}
    migrated, ignored = work.migrate_ticket_status(db, found)
    with open(SOURCE, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(line for i, line in enumerate(lines) if i not in drop))
    print(f'tickets.toml: {len(drop)} status lines removed; {len(migrated)} tickets took theirs into the work database, '
          f'{len(ignored)} kept the database\'s' + (f' ({", ".join(ignored)})' if 0 < len(ignored) <= 20 else ''))


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
           'Generated from docs/tickets.toml and the statuses in the work database by tools/tickets.py: never edit this '
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
    """(short hash, ISO time) of the last commit that changed docs/tickets.toml in that checkout."""
    out = subprocess.run(['git', 'log', '-1', '--format=%h %cI', '--', 'docs/tickets.toml'], cwd=root,
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
            print('tickets.toml:', p)
        return 1
    with open(MARKDOWN, 'w', encoding='utf-8', newline='\n') as f:
        f.write(markdown(data, groups, tickets))
    json_path = json_path or os.path.join(ROOT, 'build', 'tickets', 'tickets.json')
    os.makedirs(os.path.dirname(json_path), exist_ok=True)
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(board_data(data, groups, tickets, toml_commit()), f, ensure_ascii=False, indent=1)
    counts = {s: sum(t['state'] == s for t in tickets) for s in STATES}
    print(f'{len(tickets)} tickets: ' + ', '.join(f'{n} {s}' for s, n in counts.items()))
    return 0


# ---- editing: line by line, so the file's comments, order and layout stay as they are

def toml_value(v):
    if isinstance(v, list):
        return '[' + ', '.join(json.dumps(x, ensure_ascii=False) for x in v) + ']'
    if isinstance(v, (int, float)):
        return repr(v)
    return json.dumps(v, ensure_ascii=False)     # a TOML basic string takes JSON's escapes


def ticket_block(lines, ticket_id):
    """(first, end) line indexes of the [[ticket]] block holding id = "ticket_id"."""
    for i, line in enumerate(lines):
        if line.strip() == f'id = {json.dumps(ticket_id)}':
            start = i
            while start > 0 and lines[start - 1].strip() != '[[ticket]]':
                start -= 1
            end = i
            while end < len(lines) and lines[end].strip() and not lines[end].startswith(('[', '#')):
                end += 1
            return start, end
    sys.exit(f'tickets.py: no ticket {ticket_id}')


def write_checked(text, before):
    with open(SOURCE, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    problems = load()[3]
    if problems:
        with open(SOURCE, 'w', encoding='utf-8', newline='\n') as f:
            f.write(before)
        for p in problems:
            print('tickets.toml:', p)
        sys.exit('tickets.py: change refused, the file would not check (see above); nothing written')


def edit(ticket_id, updates):
    before = read_source()
    lines = before.split('\n')
    start, end = ticket_block(lines, ticket_id)
    for key, value in updates.items():
        for i in range(start, end):
            if lines[i].split('=')[0].strip() == key:
                lines[i] = f'{key} = {toml_value(value)}'
                break
        else:
            lines.insert(end, f'{key} = {toml_value(value)}')
            end += 1
    write_checked('\n'.join(lines), before)


def add(fields):
    before = read_source()
    lines = before.split('\n')
    same_group = [t['id'] for t in load()[2] if t['group'] == fields['group']]
    at = ticket_block(lines, same_group[-1])[1] if same_group else len(lines)
    block = ['', '[[ticket]]'] + [f'{k} = {toml_value(v)}' for k, v in fields.items()]
    write_checked('\n'.join(lines[:at] + block + lines[at:]), before)


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
    ap = argparse.ArgumentParser(description='The ticket queue (docs/tickets.toml; status in the work database).')
    ap.add_argument('--json', help='where the board data goes (default build/tickets/tickets.json)')
    sub = ap.add_subparsers(dest='cmd')
    sub.add_parser('check')
    p = sub.add_parser('show'); p.add_argument('ids', nargs='+')
    p = sub.add_parser('list'); p.add_argument('--state', choices=STATES); p.add_argument('--group')
    p = sub.add_parser('set'); p.add_argument('id'); p.add_argument('status', choices=STATUSES)
    p = sub.add_parser('note'); p.add_argument('id'); p.add_argument('text'); p.add_argument('--append', action='store_true')
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
            absorb(db)
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
            print('tickets.toml:', p)
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
        work.set_ticket_status(db, args.id, args.status, author(db))
    elif args.cmd == 'note':
        old = next((t.get('note', '') for t in tickets if t['id'] == args.id), '')
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
            work.set_ticket_status(db, args.id, args.status, author(db))
    return generate(db, args.json)


if __name__ == '__main__':
    sys.exit(main())
