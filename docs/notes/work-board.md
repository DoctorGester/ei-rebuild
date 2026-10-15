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
three others. Runs that set the display mode still run alone (since R10 only dgVoodoo runs with a `resolution`
step: runs of our code draw with our renderer, run_all.runs_alone; a reference run the cache holds is never alone). The runner is run_all's `run()`,
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

Coordinator: at spawn, `work.py start <ID>` if the worker has not registered (`work.py status` shows it);
`work.py done <ID>` at merge; `work.py serve` for the live page (http://127.0.0.1:8765/). The worker side is
in docs/worker-brief.md and .claude/agents/area-worker.md (untracked: .claude/ is ignored): start first,
note at milestones, `work.py test` for every game run, a single scenario too (it queues fairly and shows on
the board; its few seconds of scheduler start are the only cost). run_all.py directly only if work.py is broken.

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

## WB2: `work.py test` and the scheduler (2026-10-14)

Built: tools/work_sched.py (both commands; work.py only gained the two subcommands), tools/run_all.py split
into `parse_args`, `plan` (the scenario/install selection), `uses_hooks`, `load_times`/`save_times`, `line`,
`summary`, with `run`, `compare_widescreen`, `rebirth_scenarios` taking `root`/`build` (and `run` a
`take_slot`). Plain run_all output and behaviour are unchanged. No schema change.

**`work.py test ID [run_all args]`** parses the arguments with run_all's parser, expands them with
`run_all.plan()` in the checkout the work.py lives in (its verify build: EI_BUILD_DIR or build/verify),
and writes the batch and its runs in one transaction (alone from `sets_display_mode`, expected from that
build's run_times.json, folder = `<build>/runs/<scenario>-<install>`, submitter = its pid). It starts the
scheduler if none holds the lock, prints run_all's header and each run's `line()` as rows finish, then the
batch's summary, and exits with its exit code. Every 5 s it starts a scheduler again if none is alive (one
that left on idle just as the batch came in, or died). Ctrl+C cancels the batch's queued runs (exit 130).
The summary's "N runs in Ts" counts from the batch's first game, not from submission.

**`work.py scheduler`**: singleton by a byte lock on build/live/scheduler.lock (next to the database);
registers `process('scheduler')`, heartbeat every 5 s, leaves after WORK_SCHED_IDLE (120) s with nothing
queued. `test` starts it with CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP (breakaway from the job if
allowed), from the main checkout's tools when main has work_sched.py, output appended to
build/live/scheduler.log. Each 0.5 s pass: record finished games, cancel batches whose submitter pid is
gone, start games, finish batches. Order: batches by id; within one, expected descending (unknown =
1000 s, as run_all), alone runs last. A batch takes every free slot (its `--jobs` is ignored, changed
2026-10-14: workers' habitual `--jobs 4` held the machine at 4 games); then the next batch gets what is
left (backfill). Only the earliest batch with runs left gathers for its alone runs, once its other runs
have started, so a later batch's alone run never stops the backfill. An alone run stops all new starts, takes the `alone` gate and gathers every
slot while ours and others' games end, then runs by itself. Slots are run_all's lock files (probing the
gate as machine_slot does), so plain run_all and the scheduler share the 16. Two runs of one folder (two
batches from one build) never run at once. The game thread calls `run_all.run_with_retries` with
`run(root=worktree, build=build, take_slot=False)` and stays in its slot for retries (attempt in the row);
the main thread does every DB write. A finished batch gets `run_all.summary()`'s text and exit code and its
build's run_times.json is updated. A new scheduler requeues runs left `running` by a dead one.

**Tested.** tools/test_work_sched.py (fake runner, temp database and slot folder, 8 s): FIFO + backfill +
longest first, `--jobs` letting the next batch in, an alone run with nothing beside it, another process
holding two slots and then the gate (a plain run_all), retries (PASS*, attempt) and summary/exit code and
run_times.json, a dead submitter cancelling queued runs, one game per folder, singleton + idle exit through
the CLI. Real: `work.py test WB2 smoke console ui_options_bind --vanilla-only --jobs 4` (ALL PASS, exit 0,
waited ~35 s for slots on a full machine); then two batches at once, A (5 runs, --jobs 3) and B (2 runs,
--jobs 1, EI_BUILD_DIR=another build folder): B's first game started only after A's last had started, in
the slot A's game freed; both ALL PASS. A plain `run_all.py smoke --vanilla-only` beside other suites passed.

**Not done / risks.** Games of a cancelled batch are not killed. A killed scheduler leaves its games running
and the next one runs them again (its run folders are emptied under them). The pid check can be fooled by
pid reuse (only delays a cancel). A batch from an old work.py is read by whichever scheduler is running.

## WB5: the submitter's EI_* variables reach the games (2026-10-14)

`work.py test` records `run_all.game_environment()` (every EI_* variable of its environment except
EI_BUILD_DIR, EI_HARNESS_SLOTS, EI_OUT_DIR, EI_HARNESS: the build, the slots and what ei_launch sets itself; so
EI_PROFILE, EI_PLAIN, EI_RENDER, EI_COVER, EI_UI_CENSUS, EI_REBIRTH, EI_NO_HOOKS, EI_GAME_ROOT, EI_RESOLUTION,
EI_WIDESCREEN, ...) as JSON in the new `batch.env` column (MIGRATIONS entry 2, `ALTER TABLE`). The scheduler's
`real_runner` passes it as `run_all.run(game_env=...)`: the game subprocess gets this process's environment
without its own game variables plus the batch's, so the scheduler's own shell (EI_PLAIN set where it was
started) never leaks into a batch and batches running at once do not see each other's; `os.environ` is not
touched. `run_all.run(game_env=None)` (plain run_all) inherits as before; native still forces EI_RENDER.
A batch with `env` NULL (submitted before) inherits the scheduler's environment as before. The test-runs
page shows `env: EI_PLAIN=1 EI_PROFILE=1` under the arguments of queued/running batches and in the history row.
Risk: an older scheduler process already running ignores `env` (its games inherit its own environment) until
it idles out (120 s) and the next `test` starts the new one.
Tested: tools/test_work_sched.py (`test_game_environment_per_batch`: env per batch, scheduler's own EI_PROFILE
not leaking, NULL/no column inherit); real: `EI_PLAIN=1 EI_PROFILE=1 python tools/work.py test WB5 ui_main_menu --native`.

## WB6: machine-wide expected run times; the scheduler leaves for new code (2026-10-14)

**Timing table** (MIGRATIONS entry 3): `timing(id, scenario, install, scenario_rev, seconds, time, batch)`, index on
(scenario, install, scenario_rev, id). Every finished game appends a row: the scheduler in `collect()` (batch = its
id, root = the batch's worktree) and plain `run_all.py` in its result loop (batch NULL; `run_all.timing_db()` opens
the same work.db through `work.connect()`, and every failure there is swallowed: a run never fails because of it).
Runs that ended `ERROR` or `NO REPORT` are not times. `scenario_rev` = the commit that last changed
`tests/scenarios/<name>.txt` in the run's checkout (`work.scenario_revs(root)`: one `git log --name-only` walk for all
scenarios, stopped when every file has its commit, cached 30 s per checkout), `<hash>+dirty` while the file has
uncommitted edits, `''` outside a repository.

**Expected time** (`work.expected_seconds`): median of the last 5 rows for (scenario, install, rev); else of the last 5
for (scenario, install) at any rev; else the build's run_times.json entry; else unknown (1000 s, as before). A
`+dirty` scenario skips the first step. Used by `work.py test` (`run.expected`) and by plain run_all's ordering; the
scheduler orders by the stored `run.expected` as before. run_times.json is still written (scheduler's `finish`,
run_all's `main`). So a fresh worktree has estimates from its first run, and a changed scenario drops its old ones
until it has run once.

**Restart on new code.** The scheduler hashes (sha1 of content) the main checkout's tools/work_sched.py, run_all.py and
work.py at start. Every 5 s (the heartbeat pass) it compares; on a change it logs it, starts no new game
(`Scheduler.stale`; slots gathered for a display-mode run are let go) and, once none of its own games runs
(`should_restart()`), deletes its process row, releases the lock and exits 0. Running games are never touched and
queued runs stay queued; a waiting `work.py test` starts a scheduler again within 5 s from the new code (a batch
needs a waiting `test`, which is also what cancels it when its submitter dies). `WORK_SCHED_CODE` (paths, os.pathsep)
replaces the watched files (tests). A scheduler started before this merge does not know any of it and leaves only by
its idle exit (120 s with nothing queued), so the first restart on this code is by hand or at a quiet moment.

**Risk at merge.** The first work.py that connects after the merge migrates the shared database to schema 3; a
work.py from a branch that has not merged main refuses a newer schema ("merge main") until it does.

Tested: tools/test_work_sched.py (lookup fallbacks, git revs on a temporary repository, rows from the scheduler, the
class's and the process's exit on new code with queued runs left queued).

## WB7: a batch with unusable arguments fails visibly (2026-10-15)

`batch_args()` returning None used to leave the batch's runs queued forever (an old run_all without `--clock`
could not parse a newer batch's arguments). Now the scheduler logs it, cancels the batch's queued runs, and
`finish()` ends the batch `finished` with exit code 2 and a summary `batch #N FAILED: run_all does not take these
arguments (<run_all's own argparse error>): <args>`; the board shows it and the waiting `work.py test` prints it
and exits 2. `work_sched.parse_batch_args` captures argparse's stderr. Submission already refused bad arguments:
`cmd_test` runs `run_all.parse_args(argv)` before queuing (argparse prints the error, exit 2).
Tested: tools/test_work_sched.py `test_unparseable_arguments_fail_the_batch`.
