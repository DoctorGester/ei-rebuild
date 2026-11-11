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
                                       exit code; Ctrl+C cancels its queued runs (exit 130); --quiet: a quiet batch
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
  batches backfill. A retry's game is granted before anything new.
- Quiet batches (`work.py test ID --quiet ...`, for timing runs): every run is an alone run. Once the earliest
  batch with queued runs has only alone runs left, the scheduler takes the alone gate (run_all's SLOT_DIR/alone): no
  new game of any scheduler or run_all and no new build (below) starts from then on, and no later batch backfills.
  It gathers every slot and starts the run once none of its own games plays (frame packing included) and no build
  compiles; the run keeps every slot to its end and the gate stays held between the batch's runs, so a quiet batch of
  several scenarios plays them one at a time on an otherwise idle machine. A quiet batch behind others waits its turn
  as any batch; the games already running when it gathers finish (they are never stopped).
- A run folder (`<build>/runs/<scenario>-<install>`) belongs to the earlier batch that has a run in it until that batch's
  `work.py test` has ended (its `--golden` comparison reads the folders after the last game): a later batch's run in the
  same folder waits (`reserved_folders`; only earlier batches block, so batches cannot wait on each other). Else a
  rerun by name of a suite's scenario would empty the folder under the suite's comparison, which would count the
  passing run as differing (no digest.txt, no saves).
- A quiet head whose every remaining run folder is still an earlier batch's keeps its place: no later batch starts,
  the alone gate is not taken (builds go on; it gathers once the folder is free), the log says once "waits for batch
  #N's folder", and the scheduler's process row (`info.waits`) gives the board's quiet panel and queue its "waits for
  #N's folder". The holder is earlier and never waits on the head.
- Each batch's games get the scheduler's environment plus the batch's EI_* variables.
- Expected times: the median of the last 5 timings for (scenario, install, scenario commit), else (scenario,
  install), else the build's run_times.json.
- New code: on a change of the main checkout's work_sched.py, run_all.py or work.py the scheduler starts no new game
  and exits once its games are done; the next `test` starts the new one.
- The scheduler lives for hours and serves every checkout: what run_all reads from a checkout is reused for 30 s at
  most, never cached for life (a cached harness key once served an old harness's runs).
- A batch whose submitter is gone is cancelled; a new scheduler requeues runs a dead one left running. `--golden`,
  `--widescreen` and `--record` comparisons run in the submitting process after the batch.
- "Submitter alive" is the pid and a process creation time no later than the batch's submitted time: Windows reuses
  pids, and a later `work.py test` given a finished batch's pid once held that batch's run folders against its own run
  forever. A process the scheduler may not open (a third of the machine's: services, the search indexer's hosts) has no
  creation time to read: it counts only when the process list names a Python as its program.

## Builds

build.cmd runs itself again under tools/build_gate.py, which holds a build ticket in `<live>/builds` (next to
work.db; WORK_DB moves it) for the whole build, configure to build checks:
- `<id>.lock`, byte-locked for the wrapper's life: the build exists. A killed or crashed build's lock goes with its
  process; the next lister (a build starting, the scheduler) removes its files. Never marker files alone.
- `<id>.compile`, held while it builds. Before it starts, the build takes it and then looks at the alone gate; while a
  quiet run holds the gate it lets go of it, prints once that it waits (with the quiet batch), and looks again every
  second. The scheduler takes the gate first and then waits while any `.compile` is held: each side locks its own
  file before it looks at the other's, so of the two starting at once at least one sees the other.
- `<id>.json`: checkout, config, stage (waiting, configure, compile, checks), ninja's `[n/m]` (tools/build_retry.py
  writes it from ninja's status lines, at most twice a second) and times. Best effort: a replace that meets a reader
  is tried again or left for the next line.
`python tools/build_gate.py list` prints the running builds. A build whose wrapper cannot take a ticket builds without
the gate (and says so). Tests: tools/test_build_gate.py.

## Server

`work.py serve` (tools/work_board/; 127.0.0.1 only, Host header check) on http://127.0.0.1:8765/: `#tickets` (worker,
last note, last commit, the latest batch) and `#runs` (queue, running batch, history); `/tickets/` is the ticket board
with live statuses. Read-only connections; JSON endpoints `/api/status`, `/api/tickets`, `/api/runs`,
`/api/batch/<id>`, `/api/processes`, `/api/machine` (the builds and the quiet run). `#runs` shows the quiet run
(holding the machine, clearing it with the games and builds it waits for, or queued) with what waits on it, and a
Builds lane (ticket, checkout, config, `[n/m]`, stage, time); a worker's card shows its build in progress; the header
has a `quiet run #N` pill while one holds or clears the machine. Test data: `python tools/work_board/sample_db.py
<path>`, then `WORK_DB=<path> python tools/work.py serve`.

## Run data

- Frames are packed as `<name>.bmp.z` (tools/frame_store.py: each byte XORed with the byte one pixel before, then zlib
  level 3; lossless) in processes of their own at below-normal priority; compare_frames.py and the frame listings read
  either form.
- Tests: tools/test_work_sched.py, tools/test_build_gate.py, tools/test_tickets.py, tools/test_frame_store.py,
  tools/test_install_watch.py.

## Games at once

At most 16 harness games on the machine, every worker's together. 24 games once crashed Windows twice with NVIDIA TDR
storms. Measured: a game takes ~250 MiB of GPU memory and one CPU
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
acts on the largest avoidable wait.

The page picks its window: length buttons 6 h, 1 day, 3 days, 7 days, 30 days, Earlier / Later / Now (steps of one
window length), kept in the URL hash as `#w-<length>-<steps back>` (`#w-7d-0`, `#w-1d-2`). `waits.json` therefore holds
timestamped rows over the last 30 days (about 1.3 MB): per worker its times, fg/bg seconds per kind and its build and
test calls; per submitted batch [id, ticket, submitted, queue, run, games, retries, args]; game time per hour, and per
hour for the 100 slowest standalone scenarios (the slowest table lists 25). waits.html totals a window client-side
(the block between its `the model` markers); `worker_waits.view()` is the same computation for `--print
[--window-days N]`, and tools/test_waits.py checks the two against each other (node) and that adjacent windows partition the rows. Window
edges are whole hours; a worker belongs to the window where it finished (or still runs), a batch to where it was
submitted, game time to the hour it was played.
