"""Tests of ticket status (WB10) and of the ticket files (H57): tools/tickets.py, work.py start / done, the live ticket board.

usage: python tools/test_tickets.py [-v]
Each test has its own docs/tickets folder (one file per ticket and groups.toml), tickets.md, lock and database in a
temporary folder under build/ (WORK_DB points there), so the checkout's files and build/live/work.db are never touched.
"""
import argparse, contextlib, io, json, os, shutil, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tickets, work
from work_board import server

GROUPS = '''# header comment
[queue]
rules = ["one rule"]

[[group]]
key = "g"
name = "Group"
'''

# The ticket files: A1 done and A2 running as the committed copies, A3 and A4 without (or with a queued) status.
FILES = {
    'A1': '''id = "A1"
title = "Done one"
group = "g"
note = "first"
order = 1
status = "done"
done = "2026-10-01"
''',
    'A2': '''id = "A2"
title = "Running one"
group = "g"
depends = ["A1"]
order = 2
status = "running"
started = "2026-10-10"
''',
    'A3': '''id = "A3"
title = "Queued behind A2"
group = "g"
depends = ["A2"]
order = 4
''',
    'A4': '''id = "A4"
title = "Requeued with an old start"
group = "g"
order = 3
status = "queued"
started = "2026-09-30"
''',
}


def write_files(folder, files=FILES, groups=GROUPS):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'groups.toml'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(groups)
    for name, text in files.items():
        with open(os.path.join(folder, name + '.toml'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)


class TicketsTest(unittest.TestCase):
    def setUp(self):
        base = os.path.join(tickets.ROOT, 'build')
        os.makedirs(base, exist_ok=True)
        self.tmp = tempfile.mkdtemp(prefix='wb10_', dir=base)
        self.dir = os.path.join(self.tmp, 'tickets')
        write_files(self.dir)
        self.json = os.path.join(self.tmp, 'tickets.json')
        patches = [mock.patch.object(tickets, 'SOURCE_DIR', self.dir),
                   mock.patch.object(work, 'TICKETS_DIR', self.dir),
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

    def text(self, ticket=None):
        names = [ticket] if ticket else sorted(FILES)
        out = []
        for n in names:
            with open(os.path.join(self.dir, n + '.toml'), encoding='utf-8') as f:
                out.append(f.read())
        return '\n'.join(out)

    def board(self):
        with open(self.json, encoding='utf-8') as f:
            return {t['id']: t for t in json.load(f)['tickets']}

    def test_without_database_the_files_say(self):
        code, out = self.run_tickets('show', 'A2')
        self.assertIn('status: running since 2026-10-10', out)
        self.assertEqual(self.rows(), {})                              # show writes nothing
        self.assertEqual(self.run_tickets('check')[0], 0)
        self.assertEqual(self.run_tickets()[0], 0)
        b = self.board()
        self.assertEqual((b['A1']['state'], b['A1']['done']), ('done', '2026-10-01'))
        self.assertEqual(b['A3']['state'], 'waiting')
        self.assertEqual(b['A4']['state'], 'ready')
        self.assertNotIn('order', b['A1'])
        self.assertEqual(list(b), ['A1', 'A2', 'A4', 'A3'])            # by order, not by name
        self.assertEqual(self.rows(), {})                              # generating copies nothing into the database
        with open(tickets.MARKDOWN, encoding='utf-8') as f:
            md = f.read()
        self.assertIn('**A1** Done one (done 2026-10-01)', md)
        self.assertIn('**A2** Running one (running since 2026-10-10)', md)

    def test_database_row_wins_over_the_file(self):
        work.set_ticket_status(self.db, 'A2', 'review', files=self.dir)
        self.assertIn('status = "review"', self.text('A2'))
        with open(os.path.join(self.dir, 'A2.toml'), 'a', encoding='utf-8', newline='\n') as f:
            f.write('\n')
        text = self.text('A2').replace('status = "review"', 'status = "later"')       # an old branch's copy
        with open(os.path.join(self.dir, 'A2.toml'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        self.run_tickets()
        self.assertEqual(self.board()['A2']['state'], 'review')
        self.assertEqual(self.run_tickets('check')[0], 0)

    def test_status_changes_reach_the_ticket_file(self):
        self.run_tickets('set', 'A3', 'running')
        text = self.text('A3')
        self.assertIn('status = "running"', text)
        self.assertIn('started = ', text)
        self.assertEqual(text.count('status ='), 1)
        self.run_tickets('set', 'A3', 'done')
        text = self.text('A3')
        self.assertIn('status = "done"', text)
        self.assertIn('done = ', text)
        self.assertEqual(text.count('status ='), 1)
        self.assertEqual(self.text('A1'), FILES['A1'])                 # other tickets' files are untouched
        self.assertEqual(tomllib_load(os.path.join(self.dir, 'A3.toml'))['order'], 4)

    def test_one_file_per_ticket(self):
        self.run_tickets('add', 'A8', '--group', 'g', '--title', 'New one', '--note', 'n')
        self.assertTrue(os.path.exists(os.path.join(self.dir, 'A8.toml')))
        with open(os.path.join(self.dir, 'A8.toml'), encoding='utf-8') as f:
            self.assertIn('order = 5', f.read())
        self.assertNotEqual(self.run_tickets('add', 'A8', '--group', 'g', '--title', 'again')[0], 0)
        self.assertEqual(list(self.board())[-1], 'A8')
        with open(os.path.join(self.dir, 'A9.toml'), 'w', encoding='utf-8') as f:    # the file name must be the id
            f.write('id = "A10"\ntitle = "t"\ngroup = "g"\n')
        code, out = self.run_tickets('check')
        self.assertEqual(code, 1)
        self.assertIn('the file must be called A10.toml', out)
        os.remove(os.path.join(self.dir, 'A9.toml'))
        with open(os.path.join(self.dir, 'A9.toml'), 'w', encoding='utf-8') as f:
            f.write('id = "A9"\ntitle = "t"\ngroup = "nope"\nstray = 1\n')
        out = self.run_tickets('check')[1]
        self.assertIn('unknown group nope', out)
        self.assertIn('unknown field stray', out)

    def test_note_refuses_to_overwrite(self):
        code, out = self.run_tickets('note', 'A1', 'second')
        self.assertNotEqual(code, 0)
        self.assertIn('note = "first"', self.text('A1'))
        self.run_tickets('note', 'A1', 'second', '--append')
        self.assertIn('note = "first second"', self.text('A1'))
        self.run_tickets('note', 'A1', 'third', '--replace')
        self.assertIn('note = "third"', self.text('A1'))
        self.run_tickets('note', 'A2', 'a first note')                  # an empty note takes plain text
        self.assertIn('note = "a first note"', self.text('A2'))
        self.assertLess(self.text('A2').index('note ='), self.text('A2').index('order ='))

    def test_set_and_add(self):
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
        self.assertNotIn('A6', self.rows())                            # queued: no row, no status line
        self.assertNotIn('status', self.text('A6'))
        self.assertEqual(self.board()['A6']['state'], 'waiting')
        self.run_tickets('add', 'A7', '--group', 'g', '--title', 'Watched', '--status', 'watch')
        self.assertEqual(self.rows()['A7'][0], 'watch')
        self.assertIn('status = "watch"', self.text('A7'))
        self.run_tickets('note', 'A7', 'more', '--append')   # a note on a ticket with none yet
        self.assertEqual(self.run_tickets('check')[0], 0)

    def test_start_and_done(self):
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
        write_files(os.path.join(self.tmp, 'docs', 'tickets'))
        board = server.Board(work)
        board.main = self.tmp
        work.set_ticket_status(self.db, 'A3', 'review')
        d = board.ticket_board(self.db)
        self.assertTrue(d['live'])
        states = {t['id']: (t['state'], t.get('started')) for t in d['tickets']}
        self.assertEqual(states['A2'], ('running', '2026-10-10'))      # no row yet: the file's lines
        self.assertEqual(states['A3'][0], 'review')                    # the database's row
        work.set_ticket_status(self.db, 'A2', 'done')
        states = {t['id']: t['state'] for t in board.ticket_board(self.db)['tickets']}
        self.assertEqual((states['A2'], states['A3']), ('done', 'review'))
        self.assertEqual({t['id']: t['state'] for t in board.tickets(self.db)['tickets']}['A2'], 'done')


def tomllib_load(path):
    import tomllib
    with open(path, 'rb') as f:
        return tomllib.load(f)


if __name__ == '__main__':
    unittest.main()
