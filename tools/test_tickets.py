"""Tests of ticket status in the work database (WB10): tools/tickets.py, work.py start / done, the live ticket board.

usage: python tools/test_tickets.py [-v]
Each test has its own tickets.toml, tickets.md, lock and database in a temporary folder under build/ (WORK_DB
points there), so the checkout's files and build/live/work.db are never touched.
"""
import argparse, contextlib, io, json, os, shutil, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tickets, work
from work_board import server

TOML = '''# header: status lines in a comment stay
# status = "queued"

[queue]
rules = ["one rule"]

[[group]]
key = "g"
name = "Group"

[[ticket]]
id = "A1"
title = "Done one"
group = "g"
status = "done"
done = "2026-10-01"
note = "first"

[[ticket]]
id = "A2"
title = "Running one"
group = "g"
status = "running"
started = "2026-10-10"
depends = ["A1"]

[[ticket]]
id = "A3"
title = "Queued behind A2"
group = "g"
status = "queued"
depends = ["A2"]

[[ticket]]
id = "A4"
title = "Requeued with an old start"
group = "g"
status = "queued"
started = "2026-09-30"
'''


class TicketsTest(unittest.TestCase):
    def setUp(self):
        base = os.path.join(tickets.ROOT, 'build')
        os.makedirs(base, exist_ok=True)
        self.tmp = tempfile.mkdtemp(prefix='wb10_', dir=base)
        self.toml = os.path.join(self.tmp, 'tickets.toml')
        with open(self.toml, 'w', encoding='utf-8', newline='\n') as f:
            f.write(TOML)
        self.json = os.path.join(self.tmp, 'tickets.json')
        patches = [mock.patch.object(tickets, 'SOURCE', self.toml),
                   mock.patch.object(tickets, 'MARKDOWN', os.path.join(self.tmp, 'tickets.md')),
                   mock.patch.object(tickets, 'LOCK', os.path.join(self.tmp, 'tickets.lock')),
                   mock.patch.dict(os.environ, {'WORK_DB': os.path.join(self.tmp, 'work.db'), 'WORK_AGENT': 'tester'})]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        self.db = work.connect()
        self.addCleanup(lambda: (self.db.close(), shutil.rmtree(self.tmp, ignore_errors=True)))

    def run_tickets(self, *argv):
        out = io.StringIO()
        with mock.patch.object(sys, 'argv', ['tickets.py', '--json', self.json, *argv]), contextlib.redirect_stdout(out):
            try:
                code = tickets.main()
            except SystemExit as e:
                code = e.code
        return code, out.getvalue()

    def rows(self):
        return {r['ticket']: (r['status'], r['started'], r['done']) for r in self.db.execute('SELECT * FROM ticket_status')}

    def text(self):
        with open(self.toml, encoding='utf-8') as f:
            return f.read()

    def board(self):
        with open(self.json, encoding='utf-8') as f:
            return {t['id']: t for t in json.load(f)['tickets']}

    def test_before_migration_reads_the_toml(self):
        code, out = self.run_tickets('show', 'A2')
        self.assertIn('status: running since 2026-10-10', out)
        self.assertEqual(self.rows(), {})                              # show writes nothing
        self.assertEqual(self.run_tickets('check')[0], 1)              # status lines are rejected

    def test_migration_keeps_every_status_and_date(self):
        code, out = self.run_tickets()
        self.assertEqual(code, 0, out)
        self.assertEqual(self.rows(), {'A1': ('done', None, '2026-10-01'), 'A2': ('running', '2026-10-10', None),
                                       'A3': ('queued', None, None), 'A4': ('queued', '2026-09-30', None)})
        text = self.text()
        self.assertNotIn('\nstatus =', text)
        self.assertNotIn('\nstarted =', text)
        self.assertNotIn('\ndone =', text)
        self.assertIn('# status = "queued"', text)                     # comments stay
        self.assertEqual(self.run_tickets('check')[0], 0)
        b = self.board()
        self.assertEqual((b['A2']['state'], b['A2']['started']), ('running', '2026-10-10'))
        self.assertEqual(b['A3']['state'], 'waiting')
        self.assertEqual(b['A4']['state'], 'ready')
        with open(tickets.MARKDOWN, encoding='utf-8') as f:
            md = f.read()
        self.assertIn('**A1** Done one (done 2026-10-01)', md)
        self.assertIn('**A2** Running one (running since 2026-10-10)', md)

    def test_old_branch_status_lines_after_migration(self):
        self.run_tickets()
        # A merged old branch: its status edit of A2 and a ticket it added with a status line.
        text = self.text().replace('id = "A2"\ntitle = "Running one"\n', 'id = "A2"\ntitle = "Running one"\nstatus = "review"\n')
        text += '\n[[ticket]]\nid = "A5"\ntitle = "From the old branch"\ngroup = "g"\nstatus = "later"\n'
        with open(self.toml, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        self.assertEqual(self.run_tickets('check')[0], 1)
        code, out = self.run_tickets()
        self.assertEqual(code, 0, out)
        self.assertIn('1 tickets took theirs into the work database, 1 kept the database\'s (A2)', out)
        self.assertEqual(self.rows()['A2'], ('running', '2026-10-10', None))     # ignored
        self.assertEqual(self.rows()['A5'], ('later', None, None))               # migrated
        self.assertEqual(self.run_tickets('check')[0], 0)

    def test_set_and_add(self):
        self.run_tickets()
        self.run_tickets('set', 'A3', 'running')
        started = self.rows()['A3'][1]
        self.assertTrue(started)
        self.db.execute("UPDATE ticket_status SET started = '2026-01-01' WHERE ticket = 'A3'")
        self.run_tickets('set', 'A3', 'running')                       # already running: start date kept
        self.assertEqual(self.rows()['A3'][1], '2026-01-01')
        self.run_tickets('set', 'A3', 'review')
        self.assertEqual(self.board()['A3']['state'], 'review')
        self.run_tickets('set', 'A3', 'done')
        self.assertEqual(self.rows()['A3'], ('done', '2026-01-01', started))
        self.assertNotEqual(self.run_tickets('set', 'NOPE', 'done')[0], 0)
        self.run_tickets('add', 'A6', '--group', 'g', '--title', 'Follow-up', '--depends', 'A2')
        self.assertNotIn('A6', self.rows())                            # queued: no row
        self.assertEqual(self.board()['A6']['state'], 'waiting')
        self.run_tickets('add', 'A7', '--group', 'g', '--title', 'Watched', '--status', 'watch')
        self.assertEqual(self.rows()['A7'][0], 'watch')
        self.assertNotIn('status', self.text().split('id = "A7"')[1])
        self.run_tickets('note', 'A7', 'more', '--append')
        self.assertEqual(self.run_tickets('check')[0], 0)

    def test_start_and_done(self):
        self.run_tickets()
        args = argparse.Namespace(id='A3', worktree=self.tmp, branch='b', agent='w1', force=True, model=None)
        with contextlib.redirect_stdout(io.StringIO()):
            work.cmd_start(self.db, args)
            self.assertEqual(self.rows()['A3'][0], 'running')
            work.cmd_done(self.db, argparse.Namespace(id='A3', text=['handed', 'back']))
            self.assertEqual(self.rows()['A3'][0], 'running')          # only "merged" finishes the ticket
            work.cmd_start(self.db, args)
            self.assertEqual(work.cmd_done(self.db, argparse.Namespace(id='A3', text=['merged', 'abc123'])), 0)
        self.assertEqual(self.rows()['A3'][0], 'done')
        self.assertTrue(self.rows()['A3'][2])

    def test_live_board(self):
        os.makedirs(os.path.join(self.tmp, 'docs'))
        shutil.copy(self.toml, os.path.join(self.tmp, 'docs', 'tickets.toml'))   # still with status lines
        board = server.Board(work)
        board.main = self.tmp
        work.set_ticket_status(self.db, 'A3', 'review')
        d = board.ticket_board(self.db)
        self.assertTrue(d['live'])
        states = {t['id']: (t['state'], t.get('started')) for t in d['tickets']}
        self.assertEqual(states['A2'], ('running', '2026-10-10'))      # no row yet: the toml's line
        self.assertEqual(states['A3'][0], 'review')                    # the database's row
        work.set_ticket_status(self.db, 'A2', 'done')
        states = {t['id']: t['state'] for t in board.ticket_board(self.db)['tickets']}
        self.assertEqual((states['A2'], states['A3']), ('done', 'review'))
        self.assertEqual({t['id']: t['state'] for t in board.tickets(self.db)['tickets']}['A2'], 'done')


if __name__ == '__main__':
    unittest.main()
