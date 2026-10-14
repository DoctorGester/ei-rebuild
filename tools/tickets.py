"""The ticket queue: checks docs/tickets.toml, writes docs/tickets.md from it and the board's data.

usage: python tools/tickets.py [--json build/tickets/tickets.json] [--check]
  --check  only check the file (ids unique, groups and depends known, statuses valid); exit 1 on a problem

The board (tools/tickets_board/index.html) reads tickets.json; republish it with that file whenever
tickets.toml changes. A queued ticket is "ready" when every ticket it depends on is done, else "waiting".
"""
import argparse, datetime, json, os, subprocess, sys, tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'docs', 'tickets.toml')
MARKDOWN = os.path.join(ROOT, 'docs', 'tickets.md')
STATUSES = ('running', 'queued', 'later', 'watch', 'done')
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
           'Generated from docs/tickets.toml by tools/tickets.py: edit the toml, then run the script. '
           'The ticket board shows the same data.', '']
    out += [f'- {r}' for r in data.get('queue', {}).get('rules', [])] + ['']
    order = {'running': 0, 'ready': 1, 'waiting': 2, 'watch': 3, 'later': 4, 'done': 5}
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
            if state == 'running' and t.get('started'):
                state += f' since {t["started"]}'
            if state == 'done' and t.get('done'):
                state += f' {t["done"]}'
            if state == 'waiting':
                state += ' on ' + ', '.join(t['waiting_on'])
            extra = [x for x in (t.get('model'), f'{t["size"]} session' + ('' if t['size'] == 1 else 's') if t.get('size') else None) if x]
            line = f'- **{t["id"]}** {t["title"]} ({state}' + ('; ' + ', '.join(extra) if extra else '') + ')'
            line += '.'
            if t.get('note'):
                line += f' {t["note"]}'
            if t.get('files'):
                line += ' Files: ' + ', '.join(f'`{f}`' for f in t['files']) + '.'
            if t.get('depends') and t['state'] != 'waiting':
                line += ' After: ' + ', '.join(t['depends']) + '.'
            out.append(line)
        out.append('')
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--json', default=os.path.join(ROOT, 'build', 'tickets', 'tickets.json'))
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    data, groups, tickets, problems = load()
    for p in problems:
        print('tickets.toml:', p)
    if problems or args.check:
        return 1 if problems else 0
    derive(tickets)
    with open(MARKDOWN, 'w', encoding='utf-8', newline='\n') as f:
        f.write(markdown(data, groups, tickets))
    commit = subprocess.run(['git', 'log', '-1', '--format=%h %cI', '--', 'docs/tickets.toml'], cwd=ROOT,
                            capture_output=True, text=True).stdout.split()
    os.makedirs(os.path.dirname(args.json), exist_ok=True)
    with open(args.json, 'w', encoding='utf-8') as f:
        json.dump({'generated': datetime.datetime.now().isoformat(timespec='minutes'),
                   'commit': commit[0] if commit else None, 'committed': commit[1] if commit else None,
                   'rules': data.get('queue', {}).get('rules', []), 'groups': groups, 'tickets': tickets},
                  f, ensure_ascii=False, indent=1)
    counts = {s: sum(t['state'] == s for t in tickets) for s in ('running', 'ready', 'waiting', 'watch', 'later', 'done')}
    print(f'{len(tickets)} tickets: ' + ', '.join(f'{n} {s}' for s, n in counts.items()))
    print(f'wrote {os.path.relpath(MARKDOWN, ROOT)} and {args.json}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
