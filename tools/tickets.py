"""The ticket queue in docs/tickets.toml: read it, change it, and write docs/tickets.md and the board's data.

usage: python tools/tickets.py                      check, then write docs/tickets.md and build/tickets/tickets.json
       python tools/tickets.py show ID [ID ...]     one ticket: status, note, files, its group's note and doc,
                                                    what it waits on and what it unblocks
       python tools/tickets.py list [--state S] [--group G]   one line per ticket (S: running, review, ready,
                                                    waiting, watch, later, done; G: a group key)
       python tools/tickets.py set ID STATUS        status (dates filled in: started for running, done for done)
       python tools/tickets.py note ID TEXT [--append]
       python tools/tickets.py add ID --group G --title T [--depends A,B] [--note N] [--files F,G]
                                                    [--size N] [--model M] [--status S]   (a follow-up ticket)
       python tools/tickets.py check                only check the file; exit 1 on a problem

Every change rewrites docs/tickets.md and tickets.json. Workers read their ticket with `show`, set it to
`review` when their branch is ready, and record follow-ups with `add`; the coordinator sets running / done.
A queued ticket is "ready" when every ticket it depends on is done, else "waiting". The board
(tools/tickets_board/index.html) reads tickets.json; it is republished whenever tickets.toml changes.
"""
import argparse, datetime, json, os, subprocess, sys, tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'docs', 'tickets.toml')
MARKDOWN = os.path.join(ROOT, 'docs', 'tickets.md')
STATUSES = ('running', 'review', 'queued', 'later', 'watch', 'done')
STATES = ('running', 'review', 'ready', 'waiting', 'watch', 'later', 'done')
FIELDS = {'id', 'title', 'group', 'status', 'size', 'model', 'depends', 'files', 'doc', 'started', 'done', 'note'}


def load():
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
        for field in ('id', 'title', 'group', 'status'):
            if not t.get(field):
                problems.append(f'{where}: no {field}')
        if t.get('group') not in keys:
            problems.append(f'{where}: unknown group {t.get("group")}')
        if t.get('status') not in STATUSES:
            problems.append(f'{where}: status {t.get("status")} is not one of {", ".join(STATUSES)}')
        for d in t.get('depends', []):
            if d not in ids:
                problems.append(f'{where}: depends on unknown {d}')
        for field in set(t) - FIELDS:
            problems.append(f'{where}: unknown field {field}')
    return data, groups, tickets, problems


def derive(tickets):
    """Adds `state`: the status, with queued split into ready / waiting by its depends."""
    by_id = {t['id']: t for t in tickets}
    for t in tickets:
        state = t['status']
        if state == 'queued':
            open_deps = [d for d in t.get('depends', []) if by_id[d]['status'] != 'done']
            state = 'waiting' if open_deps else 'ready'
            t['waiting_on'] = open_deps
        t['state'] = state
    return tickets


def markdown(data, groups, tickets):
    out = ['# Ticket queue', '',
           'Generated from docs/tickets.toml by tools/tickets.py: never edit this file; read one ticket with '
           '`python tools/tickets.py show ID` and change tickets through the script. The ticket board shows the same data.', '']
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


def generate(json_path=None):
    data, groups, tickets, problems = load()
    if problems:
        for p in problems:
            print('tickets.toml:', p)
        return 1
    derive(tickets)
    with open(MARKDOWN, 'w', encoding='utf-8', newline='\n') as f:
        f.write(markdown(data, groups, tickets))
    json_path = json_path or os.path.join(ROOT, 'build', 'tickets', 'tickets.json')
    commit = subprocess.run(['git', 'log', '-1', '--format=%h %cI', '--', 'docs/tickets.toml'], cwd=ROOT,
                            capture_output=True, text=True).stdout.split()
    os.makedirs(os.path.dirname(json_path), exist_ok=True)
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump({'generated': datetime.datetime.now().isoformat(timespec='minutes'),
                   'commit': commit[0] if commit else None, 'committed': commit[1] if commit else None,
                   'rules': data.get('queue', {}).get('rules', []), 'groups': groups, 'tickets': tickets},
                  f, ensure_ascii=False, indent=1)
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
    before = open(SOURCE, encoding='utf-8').read()
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
    before = open(SOURCE, encoding='utf-8').read()
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
    ap = argparse.ArgumentParser(description='The ticket queue (docs/tickets.toml).')
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

    if args.cmd == 'check':
        problems = load()[3]
        for p in problems:
            print('tickets.toml:', p)
        return 1 if problems else 0
    if args.cmd is None:
        return generate(args.json)
    data, groups, tickets, problems = load()
    if args.cmd in ('show', 'list'):
        derive(tickets)
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
    today = datetime.date.today().isoformat()
    if args.cmd == 'set':
        updates = {'status': args.status}
        if args.status == 'running':
            updates['started'] = today
        if args.status == 'done':
            updates['done'] = today
        edit(args.id, updates)
    elif args.cmd == 'note':
        old = next((t.get('note', '') for t in tickets if t['id'] == args.id), '')
        edit(args.id, {'note': (old + ' ' + args.text).strip() if args.append else args.text})
    elif args.cmd == 'add':
        fields = {'id': args.id, 'title': args.title, 'group': args.group, 'status': args.status}
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
        add(fields)
    return generate(args.json)


if __name__ == '__main__':
    sys.exit(main())
