# Work board: tickets, workers and test runs in one live place

Asked 2026-10-14 (user): tickets carry their worker and worktree, schedule their test runs as batches
that run to completion, take progress notes, and a small local server shows all of it live. The
published ticket board stays as it is (made from docs/tickets.toml).

## Split

- **docs/tickets.toml** (git) keeps what a ticket is: title, group, depends, status, note. Unchanged.
- **build/live/work.db** (SQLite, in the main checkout, outside git) keeps what is happening now:
  assignments, progress notes, test batches and their runs. Every worktree finds it through
  `git rev-parse --git-common-dir`, so all checkouts share one database. SQLite in WAL mode with
  short transactions and a busy timeout serialises writers (item 7); the toml keeps its own
  check-before-write in tools/tickets.py and gains a lock file for the read-modify-write.

## Tables

- `assignment(ticket, worktree, branch, agent, started, ended)`: who works on what, where.
- `note(ticket, time, author, text)`: progress lines from workers and the coordinator.
- `batch(id, ticket, worktree, build, submitted, started, finished, state, args)`: one test request.
- `run(batch, scenario, install, state, started, seconds, result, failures, folder)`: its games.

## Commands (tools/work.py, for workers and the coordinator)

    work.py start <ID>                 assign the ticket to this worktree/branch (from its cwd)
    work.py note <ID> <text>           a progress line
    work.py test <ID> [run_all args]   submit a batch and wait; prints run_all's summary, same exit code
    work.py status [<ID>]              assignment, notes, batches, last commit (one screen)
    work.py done <ID>                  end the assignment (the coordinator, at merge)

## Scheduler (item 2)

One process (`work.py scheduler`, started on demand by the first `test`, exits when idle) owns the
machine's game slots (EI_HARNESS_SLOTS, 16). Batches run in submission order; a batch gets every
free slot until its runs are all started, and the next batch only fills slots the earlier one cannot
use (backfill). So one worker's suite finishes in about its own length instead of interleaving with
three others. Runs that set the display mode still run alone. The runner is run_all's `run()`,
pointed at the batch's worktree and build folder, so results and run folders stay where they are now.
run_all without the scheduler keeps working (its slot locks are the same files), so the switch is gradual.

## Server (items 4-6, 8)

`work.py serve` (localhost:8765): a JSON API over the database plus the toml and git (`git log -1`
on each assigned branch for the last commit and its age), and one page in the board's style:
- tickets with worker, worktree, last note, last commit and its age, and the latest batch's progress;
- a test-run page: queue, running batch with its games, history with durations and failures.
The page polls every few seconds; nothing is published.

## Rollout

WB1 database + work.py start/note/status/done; WB2 scheduler + `work.py test`; WB3 server and pages;
WB4 worker brief and area-worker switched to work.py, coordinator habits (start at spawn, done at merge).

## WB1: the database and work.py start / note / status / done (2026-10-14)

Built: tools/work.py (standard library only) and the lock in tools/tickets.py.

**Where.** `build/live/work.db` in the main checkout: work.py runs `git rev-parse --path-format=absolute
--git-common-dir` from its own checkout and takes that folder's parent, so every worktree opens the same
file. `WORK_DB=<path>` overrides it (tests). `work.py db` prints the path, schema version and row counts.

**Concurrency.** WAL journal (set by whoever creates the file; it persists), `synchronous=NORMAL`,
`foreign_keys=ON`, a 60 s busy timeout, autocommit connections (`isolation_level=None`) and writes only
inside `with work.transaction(db):`, which is `BEGIN IMMEDIATE ... COMMIT` (the write lock is taken at
BEGIN, so writers queue in the busy handler instead of failing to upgrade a read lock). Keep every write
transaction short: never hold one across a game, a subprocess or a sleep. Readers never wait.
Tested 2026-10-14: 12 processes starting on a new file at once through the CLI (half of them from another
worktree), then 16 writers x 150 transactions (some holding the lock 30 ms) beside 4 readers: no error,
no lost row, integrity ok.

**Schema** (version in `PRAGMA user_version`; `MIGRATIONS` in work.py is a list of SQL scripts, one per
version, applied in order under the write lock. Later tickets append an entry (new tables, `ALTER TABLE
... ADD COLUMN`), never edit an old one; an older work.py refuses a newer database). Times are local
ISO 8601 with offset, to the second (`work.now()`); JSON columns hold text from `json.dumps`.

    assignment(id, ticket, worktree, branch, agent, started, ended)
        worktree: absolute path, forward slashes. ended NULL while open; a unique index allows one open
        assignment per ticket.
    note(id, ticket, time, author, text)
    batch(id, ticket, worktree, build, args, state, submitted, started, finished, submitter, exit_code, summary)
        id is the submission order. build: the verify build folder (run_all's BUILD; runs in <build>/runs).
        args: JSON list of run_all arguments. state: queued, running, finished, cancelled.
        submitter: pid of the waiting `work.py test` (the scheduler can cancel a batch whose submitter
        is gone). exit_code and summary: what `work.py test` prints and returns.
    run(id, batch, scenario, install, hooks, alone, expected, folder, state, attempt, slot, started,
        seconds, result, failures, counts, notes)
        One game of a batch (ON DELETE CASCADE). install: vanilla, rebirth, native, rebirth-nohooks.
        alone: sets the display mode, runs with no other game. expected: seconds from run_times.json
        (longest first). state: queued, running, finished, cancelled. attempt: retries so far. slot:
        the machine slot while running. result, failures (JSON list of FAILED: lines), counts (JSON
        {function: [calls, mismatches]}) and notes (JSON list): run_all.run()'s return values.
    process(name, pid, started, heartbeat, info)
        One row per long-lived process ('scheduler', 'server'): the page shows whether it is alive;
        a second scheduler sees the first.

**CLI** (identity comes from the current directory, so run it in the checkout the command is about):

    work.py start ID [--worktree P] [--branch B] [--agent A] [--force]
        Opens an assignment for this checkout (git toplevel of cwd) and its branch; agent defaults to
        WORK_AGENT or the worktree's folder name (agent-<id>). Again from the same worktree: updates
        branch / agent. From another worktree: ends the old one and notes "took over from ...".
        Refuses an id neither this checkout's nor the main checkout's tickets.toml has (--force).
    work.py note ID TEXT...  [--author A]
        Author: WORK_AGENT, else "coordinator" in the main checkout, else this worktree's open
        assignment's agent, else the folder name.
    work.py status [ID]
        Without ID: open assignments with title, agent, age, last note, last commit on the branch
        (log -1 in the shared repository), then queued / running batches and the last 3 finished.
        With ID: every assignment (open and ended), the last 15 notes, the last 5 batches.
    work.py done ID [TEXT...]
        Ends the open assignment (exit 1 if there is none); TEXT becomes a last note.
    work.py db

**tickets.py lock.** Every command but check / show / list now runs under `toml_lock()`: a one-byte
lock on build/tickets/tickets.lock in that checkout (msvcrt, or flock elsewhere), held from reading
tickets.toml to writing it, tickets.md and tickets.json, waiting up to 120 s. Each checkout has its own
toml, so the lock is per checkout; the system drops it when a process dies. Tested in a second worktree
with 24 concurrent `tickets.py note WB1 <token> --append`: without the lock 7 of 24 tokens survived, and
one of three rounds left tickets.toml unparseable (one process's restore of `before` raced another's
write); with it, 24 of 24 in three rounds and the file checks. The same worktree's `work.py db` opened
E:/workspace/ei/build/live/work.db.

## WB3: the live server and pages (2026-10-14)

Built: `work.py serve [--port 8765]` (only the `serve` subcommand is in work.py) and tools/work_board/:
server.py (standard library; `ThreadingHTTPServer` on 127.0.0.1 only, exclusive bind so a second one on
the port fails, a Host-header check against DNS rebinding), index.html + app.js + board.css (the ticket
board's tokens and light/dark), sample_db.py. Start it from any checkout: `python tools/work.py serve`,
open http://127.0.0.1:8765/ (`#tickets` and `#runs` are the two views; the page polls /api/status every 3 s).
Nothing is published.

**Reads only.** Each request opens its own connection with `query_only` (no transaction held across
requests); the server's one write is its heartbeat row (`process` name 'server', every 5 s, short
transaction; the row is deleted on a clean exit). Tickets come from the main checkout's docs/tickets.toml
through tools/tickets.py's `load()` / `derive()` (reloaded when the file's mtime changes). Last commits:
one `git for-each-ref refs/heads` in the main checkout, reused for 10 s, so polling never forks git per
ticket. A process counts as alive when its heartbeat is under 20 s old (the scheduler must refresh its
`process` row at least that often).

**Endpoints** (GET, JSON): `/api/status` = everything the page needs (time, processes, runs, tickets);
`/api/tickets` (every ticket with state, `assignment` {agent, worktree, branch, started, open}, the last 3
`notes`, `commit` {hash, time, subject} of the assigned branch, and `batch`: the ticket's latest batch with
counts total/queued/running/done/passed/failed and up to 12 failed games); `/api/runs` (`running` and
`queued` batches with all their `games`: scenario, install, state, slot, elapsed vs expected, result,
failure lines; `history`: the last 40 finished or cancelled batches with duration and failed games);
`/api/batch/<id>` (one batch with all games); `/api/processes` (scheduler and server: alive, pid,
heartbeat_age). A game counts as passed when its result is PASS or PASS*.

**Testing without WB2's data.** `python tools/work_board/sample_db.py <path>` writes a database with
assignments, notes, a running batch with games, a queued one, finished ones with failures, and a fresh
scheduler row (it refuses the real build/live/work.db); then `WORK_DB=<path> python tools/work.py serve`.
Checked 2026-10-14 in the built-in browser (both views, no console errors) and with an empty database.
