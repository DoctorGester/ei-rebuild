"""Runs a command with a time limit: on the limit the command's whole process tree is killed, a line saying so is
printed and posted as a work board note, and the exit code is 124. Workers run every long command through it
(docs/worker-brief.md), so a hung build, game or script ends instead of keeping its worker waiting.

usage: python tools/limit.py SECONDS [--ticket ID] -- COMMAND [ARGS...]
       python tools/limit.py 900 --ticket C51 -- cmd /c build.cmd verify

Output and exit code are the command's own when it finishes in time. --ticket: the note's ticket (default: the
ticket of this checkout's open assignment, if work.py knows one).
"""
import argparse, os, subprocess, sys, time

TIMED_OUT = 124   # as GNU timeout


def kill_tree(pid):
    if os.name == 'nt':
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        import signal
        try:
            os.killpg(os.getpgid(pid), signal.SIGKILL)
        except OSError:
            pass


def ticket_of_checkout():
    """The ticket of this checkout's open assignment (work.py's database), or None."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import work
        db = work.connect()
        top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
        rows = db.execute('SELECT ticket, worktree FROM assignment WHERE ended IS NULL').fetchall()
        for r in rows:
            if top and os.path.normcase(os.path.abspath(r['worktree'])) == os.path.normcase(os.path.abspath(top)):
                return r['ticket']
    except Exception:
        pass
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('seconds', type=float)
    p.add_argument('--ticket')
    argv = sys.argv[1:]
    if '--' not in argv:
        p.error('the command goes after --')
    split = argv.index('--')
    args = p.parse_args(argv[:split])
    command = argv[split + 1:]
    if not command:
        p.error('no command')
    flags = {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {'start_new_session': True}
    start = time.monotonic()
    proc = subprocess.Popen(command, **flags)
    try:
        return proc.wait(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        kill_tree(proc.pid)
        proc.wait()
        line = f'TIMED OUT after {time.monotonic() - start:.0f}s (limit {args.seconds:.0f}s), process tree killed: ' + ' '.join(command)
        print(line, flush=True)
        ticket = args.ticket or ticket_of_checkout()
        if ticket:
            subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work.py'),
                            'note', ticket, line[:300]], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return TIMED_OUT
    except KeyboardInterrupt:
        kill_tree(proc.pid)
        raise


if __name__ == '__main__':
    sys.exit(main())
