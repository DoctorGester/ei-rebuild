# Work board: tickets, workers and test runs

## Split

- **docs/tickets/<ID>.toml** (git), one file per ticket, says what a ticket is: title, group, depends, files, note,
  and `order` (its place in the list; a new ticket takes the largest plus one, equal orders sort by id). Two branches
  that touch different tickets never conflict. `docs/tickets/groups.toml` holds the queue's rules and the group
  headers. Changed only through tools/tickets.py (`note ID TEXT [--append | --replace]`, `add`, `set ID STATUS`), which
  checks the folder, holds a per-checkout lock (build/tickets/tickets.lock) around each read-modify-write and
  rewrites build/tickets/tickets.md and tickets.json (build outputs, not in git).
- **build/live/work.db** (SQLite, in the main checkout, outside git) says what is happening now: ticket status,
  assignments, notes, batches and their runs, run times. Every worktree finds it through `git rev-parse
  --git-common-dir`; `WORK_DB=<path>` overrides it; `work.py db` prints the path, schema version and counts.
- **The status lines of a ticket's file** (`status`, `started`, `done`) are the committed copy of the database's row:
  `work.set_ticket_status` (so `tickets.py set`, `work.py start`, `work.py done ID merged`) writes them into the file
  of the checkout it runs in, so a status change touches only that ticket's file. A checkout without the database
  (cloud sessions) reads them; where the database has a row, the database wins. The file may lag (a ticket set in a
  checkout other than the one that is merged); the coordinator's `done` rewrites it.

## Tables

Schema version in `PRAGMA user_version`; `MIGRATIONS` in work.py is one SQL script per version, applied in order under
the write lock: append an entry, never edit an old one (an older work.py refuses a newer database). Times are local
ISO 8601 with offset. Tables: `assignment` (one open row per ticket), `note`, `batch` (a `work.py test` submission:
build folder, run_all arguments as JSON, state, submitter pid, summary, EI_* environment), `run` (one game: scenario,
install, alone, expected seconds, slot, result), `process` (scheduler and server heartbeats; alive while under 20 s
old), `timing` (every finished game), `ticket_status` (no row: queued).

Concurrency: WAL journal, a 60 s busy timeout, writes only inside `with work.transaction(db):` (`BEGIN IMMEDIATE`).
Keep every write transaction short: never across a game, a subprocess or a sleep.

## Commands (tools/work.py)

    work.py start ID [--model M]       assign the ticket to this checkout; status running
    work.py note ID TEXT...            a progress line
    work.py test ID [run_all args]     a batch through the scheduler; waits, prints run_all's lines and summary, same
                                       exit code; Ctrl+C cancels its queued runs (exit 130)
    work.py status [ID]                assignments, last notes and commits, batches
    work.py done ID [TEXT...]          end the assignment (the coordinator, at merge); "merged ..." also sets done
    work.py serve [--port 8765]        the live board
    work.py prune [--vacuum]           clear old runs' data
    tickets.py set ID STATUS           running, review, queued, later, watch, done

## Scheduler

`work.py scheduler` (tools/work_sched.py; started by the first `test`, a singleton by a byte lock on
build/live/scheduler.lock, leaves after 120 s with nothing queued; log in build/live/scheduler.log) owns the machine's
16 game slots (`EI_HARNESS_SLOTS`; run_all's lock files).
- `test` parses the arguments with run_all's parser (bad ones fail at once), expands them with `run_all.plan()` in the
  submitting checkout (its verify build, or EI_BUILD_DIR) and writes the batch and its runs in one transaction.
- Order: batches by id; within one, expected time descending, alone runs last. A batch takes every free slot; later
  batches backfill. An alone run gathers every slot and runs by itself. A retry's game is granted before anything new.
- Each batch's games get the scheduler's environment plus the batch's EI_* variables.
- Expected times: the median of the last 5 timings for (scenario, install, scenario commit), else (scenario,
  install), else the build's run_times.json.
- New code: on a change of the main checkout's work_sched.py, run_all.py or work.py the scheduler starts no new game
  and exits once its games are done; the next `test` starts the new one.
- The scheduler lives for hours and serves every checkout: what run_all reads from a checkout is reused for 30 s at
  most, never cached for life (a cached harness key once served an old harness's runs).
- A batch whose submitter is gone is cancelled; a new scheduler requeues runs a dead one left running. `--golden`,
  `--widescreen` and `--record` comparisons run in the submitting process after the batch.

## Server

`work.py serve` (tools/work_board/; 127.0.0.1 only, Host header check) on http://127.0.0.1:8765/: `#tickets` (worker,
last note, last commit, the latest batch) and `#runs` (queue, running batch, history); `/tickets/` is the ticket board
with live statuses. Read-only connections; JSON endpoints `/api/status`, `/api/tickets`, `/api/runs`,
`/api/batch/<id>`, `/api/processes`. Test data: `python tools/work_board/sample_db.py <path>`, then `WORK_DB=<path>
python tools/work.py serve`.

## Run data

- Frames are packed as `<name>.bmp.z` (tools/frame_store.py: each byte XORed with the byte one pixel before, then zlib
  level 3; lossless) in processes of their own at below-normal priority; compare_frames.py and the frame listings read
  either form.
- Tests: tools/test_work_sched.py, tools/test_tickets.py, tools/test_frame_store.py, tools/test_install_watch.py.

## Games at once

At most 16 harness games on the machine, every worker's together. 24 games (under the old dgVoodoo path) crashed
Windows twice with NVIDIA TDR storms. Measured with our renderer: a game takes ~250 MiB of GPU memory and one CPU
thread (the virtual clock never sleeps); at 16 games the GPU is 84-90% busy and throughput flattens from 12 on, so more
games mostly queue. Going above 16 needs the user's OK: measure with `tools/game_load.py run --games N ...` (it samples
GPU memory, utilisation and TDR events and exits 2 on any), step to 20 only on an otherwise idle machine and go back to
16 at any TDR event, GPU memory above 13 GB, a failure that passes at 16 or desktop stutter.

## Waits

`tools/worker_waits.py` (served at /waits/, refreshed every five minutes; `--print` for the console) reads the area
workers' transcripts under ~/.claude/projects/<repo>/*/subagents (only agents with an assignment here) and this
database. Per worker: wall time, reasoning (the gaps before assistant turns), foreground tool calls by kind, background
jobs from launch to their task notification, and every build and test call. Findings, largest first: golden suites
beyond image + Rebirth, more than three build calls, batches that queued over two minutes, scenarios averaging over
40 s. Per-transcript results are cached in build/live/waits_cache.json. The coordinator looks at it after merges and
acts on the largest avoidable wait (user 2026-11-04).
