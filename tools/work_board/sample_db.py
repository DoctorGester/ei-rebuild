"""Fills a work.db with sample rows to look at the board's pages: python tools/work_board/sample_db.py PATH.

Run the server against it with WORK_DB=PATH python tools/work.py serve. Refuses to touch the real
database (build/live/work.db) and replaces whatever the file held.
"""
import datetime, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import work


def ago(seconds):
    return (datetime.datetime.now().astimezone() - datetime.timedelta(seconds=seconds)).isoformat(timespec='seconds')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = work.norm(sys.argv[1])
    if path == work.norm(os.path.join(work.main_checkout(), 'build', 'live', 'work.db')):
        sys.exit('sample_db.py: not the real database')
    for suffix in ('', '-wal', '-shm'):
        if os.path.exists(path + suffix):
            os.remove(path + suffix)
    db = work.connect(path)
    here, branch = work.here()
    with work.transaction(db):
        for ticket, agent, started, branch_ in (('WB3', 'agent-a6c769f1', 2400, branch), ('WB2', 'agent-b7d880a2', 5400, 'main'),
                                                ('WB4', 'agent-c1e990b3', 600, 'no-such-branch')):
            db.execute('INSERT INTO assignment (ticket, worktree, branch, agent, started) VALUES (?, ?, ?, ?, ?)',
                       (ticket, here.replace('agent-a6c769f1', ticket.lower()), branch_, agent, ago(started)))
        db.execute('INSERT INTO assignment (ticket, worktree, branch, agent, started, ended) VALUES (?, ?, ?, ?, ?, ?)',
                   ('WB1', here, 'main', 'agent-old', ago(90000), ago(80000)))
        for ticket, author, text, t in (('WB3', 'agent-a6c769f1', 'server module written, page next', 1500),
                                        ('WB3', 'agent-a6c769f1', 'API returns tickets joined with notes', 700),
                                        ('WB3', 'agent-a6c769f1', 'pages render with sample rows', 90),
                                        ('WB2', 'agent-b7d880a2', 'scheduler loop passes the slot test', 1200),
                                        ('WB1', 'coordinator', 'merged', 80000)):
            db.execute('INSERT INTO note (ticket, time, author, text) VALUES (?, ?, ?, ?)', (ticket, ago(t), author, text))

        def batch(ticket, state, submitted, started, finished, exit_code, games):
            cur = db.execute('INSERT INTO batch (ticket, worktree, build, args, state, submitted, started, finished, exit_code, summary) '
                             'VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
                             (ticket, here, here + '/build/verify', json.dumps(['--jobs', '4']), state, ago(submitted),
                              ago(started) if started else None, ago(finished) if finished else None, exit_code, None))
            for scenario, install, rstate, slot, seconds, expected, result, failures in games:
                db.execute('INSERT INTO run (batch, scenario, install, state, slot, started, seconds, expected, result, failures, alone) '
                           'VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
                           (cur.lastrowid, scenario, install, rstate, slot, ago(seconds) if rstate == 'running' else None,
                            seconds if rstate == 'finished' else None, expected, result, json.dumps(failures), int(scenario == 'display_modes')))
            return cur.lastrowid

        batch('WB2', 'finished', 9000, 8900, 8300, 0, [('newgame', 'rebirth', 'finished', 0, 120, 110, 'PASS', []),
                                                       ('combat', 'vanilla', 'finished', 1, 240, 200, 'PASS*', [])])
        batch('WB1', 'finished', 5000, 4900, 4000, 1, [('newgame', 'rebirth', 'finished', 0, 130, 110, 'PASS', []),
                                                       ('combat', 'rebirth', 'finished', 1, 600, 200, 'FAIL',
                                                        ['FAILED: Unit::Update mismatch 3 at frame 812', 'FAILED: Map::Load size differs']),
                                                       ('loadsave', 'native', 'finished', 2, 90, 100, 'NO REPORT', [])])
        batch('WB3', 'finished', 3000, 2950, 2700, 0, [('newgame', 'rebirth', 'finished', 0, 115, 110, 'PASS', [])])
        batch('WB2', 'running', 400, 380, None, None, [
            ('newgame', 'rebirth', 'finished', 0, 118, 110, 'PASS', []),
            ('combat', 'rebirth', 'running', 3, 150, 200, None, []),
            ('pathing', 'rebirth', 'running', 4, 260, 200, None, []),
            ('display_modes', 'vanilla', 'queued', None, None, 90, None, []),
            ('loadsave', 'native', 'queued', None, None, 100, None, []),
            ('economy', 'rebirth', 'finished', 1, 400, 380, 'FAIL', ['FAILED: Economy::Tick mismatch at frame 77'])])
        batch('WB3', 'queued', 100, None, None, None, [('newgame', 'rebirth', 'queued', None, None, 110, None, []),
                                                       ('combat', 'rebirth', 'queued', None, None, 200, None, [])])
        for name, beat, info in (('scheduler', 3, None),):
            db.execute('INSERT INTO process (name, pid, started, heartbeat, info) VALUES (?, ?, ?, ?, ?)', (name, os.getpid(), ago(3600), ago(beat), info))
    db.close()
    print(f'sample rows in {path}')


if __name__ == '__main__':
    main()
