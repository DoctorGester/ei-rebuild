# Work board: tickets, workers and test runs in one live place

Tickets carry their worker and worktree, test runs go through one scheduler as batches that run to completion,
workers leave progress notes, and a local server shows all of it live (user 2026-10-14). Per-ticket history (WB1-WB11):
`git log --grep=WB`.

## Split

- **docs/tickets.toml** (git) says what a ticket is: title, group, depends, files, note. Changed only through
  tools/tickets.py (`note ID TEXT` replaces a note, `--append` adds to it; `add`; `set ID STATUS`), which checks the
  file, holds a per-checkout lock file (build/tickets/tickets.lock) around each read-modify-write, and rewrites
  docs/tickets.md and build/tickets/tickets.json.
- **build/live/work.db** (SQLite, in the main checkout, outside git) says what is happening now: ticket status,
  assignments, notes, batches and their runs, run times. Every worktree finds it through `git rev-parse
  --git-common-dir`; `WORK_DB=<path>` overrides it (tests); `work.py db` prints the path, schema version and counts.

## Tables

Schema version in `PRAGMA user_version`; `MIGRATIONS` in work.py is one SQL script per version, applied in order under
the write lock. Append an entry (new table, `ALTER TABLE ... ADD COLUMN`), never edit an old one; an older work.py
refuses a newer database ("merge main"). Times are local ISO 8601 with offset, to the second (`work.now()`).

    assignment(id, ticket, worktree, branch, agent, started, ended, model)   one open row per ticket
    note(id, ticket, time, author, text)
    batch(id, ticket, worktree, build, args, state, submitted, started, finished, submitter, exit_code, summary, env)
        id: submission order; build: the verify build folder (runs in <build>/runs); args: JSON run_all arguments;
        submitter: pid of the waiting `work.py test`; env: the submitter's EI_* variables (JSON)
    run(id, batch, scenario, install, hooks, alone, expected, folder, state, attempt, slot, started, seconds,
        result, failures, counts, notes)
        hooks: 1 for a run of our code (the image); alone: runs with no other game; counts: always empty now
        (kept because other checkouts and the reference cache share the shape)
    process(name, pid, started, heartbeat, info)      'scheduler' and 'server'; alive while the heartbeat is < 20 s old
    timing(id, scenario, install, scenario_rev, seconds, time, batch)    every finished game, machine-wide
    ticket_status(ticket, status, started, done, changed, author)        no row: queued

**Concurrency.** WAL journal, `synchronous=NORMAL`, a 60 s busy timeout, autocommit connections, writes only inside
`with work.transaction(db):` (`BEGIN IMMEDIATE`: writers queue in the busy handler instead of failing to upgrade a read
lock). Keep every write transaction short: never across a game, a subprocess or a sleep. Readers never wait.

## Commands (tools/work.py)

    work.py start ID [--model M]       assign the ticket to this checkout and branch; status running (started today).
                                       From another worktree it takes over (noted). Refuses an unknown id (--force).
    work.py note ID TEXT...            a progress line (author: WORK_AGENT, "coordinator" in the main checkout, else
                                       the worktree's agent)
    work.py test ID [run_all args]     a batch through the scheduler; waits, prints run_all's lines and summary, same
                                       exit code; Ctrl+C cancels its queued runs (exit 130)
    work.py status [ID]                assignments, last notes and commits, batches (one screen)
    work.py done ID [TEXT...]          end the assignment (the coordinator, at merge); "merged ..." also sets done
    work.py serve [--port 8765]        the live board
    work.py prune [--vacuum]           clear old runs' counts; --vacuum shrinks the file (blocks writers while it copies)
    tickets.py set ID STATUS           running, review, queued, later, watch, done; from any checkout, shows at once

Workers: `start` first, `note` at milestones, `work.py test` for every game run, one scenario too; the coordinator
`done` at merge. run_all.py directly only if work.py is broken (it skips the queue but shares the slot lock files).

## Scheduler

`work.py scheduler` (tools/work_sched.py; started by the first `test`, singleton by a byte lock on
build/live/scheduler.lock, heartbeat every 5 s, leaves after WORK_SCHED_IDLE = 120 s with nothing queued; output in
build/live/scheduler.log) owns the machine's 16 game slots (EI_HARNESS_SLOTS; run_all's lock files).
- **Submission**: `test` parses the arguments with run_all's parser (bad ones fail at once, exit 2), expands them with
  `run_all.plan()` in the submitting checkout (its verify build, or EI_BUILD_DIR), and writes the batch and its runs in
  one transaction (expected times from the timing table). It starts a scheduler again within 5 s if none is alive.
- **Order**: batches by id; within one, expected time descending (unknown = 1000 s), alone runs last. A batch takes
  every free slot (its `--jobs` is ignored); later batches backfill what it cannot use. An alone run stops new starts,
  gathers every slot and runs by itself. Two runs of one folder never run at once.
- **Runs**: the game thread calls `run_all.run_with_retries(run(root=worktree, build=build, take_slot=False))` and keeps
  its slot for retries; the main thread does every database write. A finished batch stores run_all's summary and exit
  code. A batch whose submitter pid is gone is cancelled (its games are not killed). A new scheduler requeues runs a
  dead one left running.
- **Environment**: each batch's games get the scheduler's environment without its own game variables plus the batch's
  EI_* variables (all but EI_BUILD_DIR, EI_HARNESS_SLOTS, EI_OUT_DIR, EI_HARNESS); `os.environ` is not touched.
- **Expected times** (`work.expected_seconds`): median of the last 5 timing rows for (scenario, install, scenario
  commit), else for (scenario, install), else the build's run_times.json, else unknown. A changed scenario starts
  without its old estimates. Runs that ended ERROR or NO REPORT are not times.
- **New code**: the scheduler hashes the main checkout's work_sched.py, run_all.py and work.py; on a change it starts no
  new game and exits once its own games are done; the waiting `test` starts the new one (`WORK_SCHED_CODE` replaces the
  watched files in tests). Queued runs stay queued.
- **Unusable arguments** (an old run_all that cannot parse a newer batch): the batch ends with exit code 2 and a
  summary naming run_all's error.
- `--golden`, `--widescreen` and `--record` comparisons run in the submitting process after the batch.

## Server

`work.py serve` (tools/work_board/: server.py, index.html, app.js, board.css; 127.0.0.1 only, exclusive bind, Host
header check) on http://127.0.0.1:8765/: `#tickets` (worker, worktree, last note, last commit and age, the latest
batch's progress) and `#runs` (queue, running batch with its games, history with durations and failures); the page
polls every 3 s. `/tickets/` serves the ticket board page with live statuses (`/tickets/tickets.json`, redrawn on a
change). Reads only: one `query_only` connection per request; the one write is the server's heartbeat. Tickets come
from the main checkout's tickets.toml (reloaded on mtime change); last commits from one `git for-each-ref` reused for
10 s. JSON endpoints: `/api/status`, `/api/tickets`, `/api/runs`, `/api/batch/<id>`, `/api/processes`. Test page data
without the real database: `python tools/work_board/sample_db.py <path>`, then `WORK_DB=<path> python tools/work.py
serve`.

## Run data

- Frames: run() packs a run folder's 4K BMP captures as `<name>.bmp.z` (tools/frame_store.py: each byte XORed with the
  byte one pixel before, then zlib level 3; lossless, 2.3x-4.7x smaller) before the reference cache stores it.
  compare_frames.py, frame_preview and run_all's frame listings read either form; `frame_store.py pack FOLDER...` /
  `unpack FILE.bmp.z`. The harness itself writes and reads plain BMPs inside a running game.
- run.counts is NULL since the verify wrappers went (it was 4 GB of the database); `prune` clears old rows.
- Tests: tools/test_work_sched.py (fake runner, temp database and slots), tools/test_tickets.py, tools/test_frame_store.py.

## Games at once (WB13)

The machine (Ryzen 9 7950X 16 cores / 32 threads, 64 GB, RTX 4070 Ti SUPER 16 GB, driver 595.97) runs at most
EI_HARNESS_SLOTS = 16 harness games, every worker's together. 24 games under dgVoodoo (and 16 with 8x AA) crashed
Windows twice with NVIDIA TDR storms (System log 153 nvlddmkm / 4101 Display, last on 2026-10-14 07:36). Going above
16 needs the user's OK.

**Measuring**: `python tools/game_load.py run --games N --ticket ID --out build/<ID>/x.jsonl [--scenarios K | --probe
PREFIX] [-- run_all args]` samples the machine every ~1.1 s (tools/game_load_sample.ps1: nvidia-smi, the GPU Process
Memory / GPU Engine / processor / memory counters, every ei_standalone.exe and game.exe started since) while `work.py
test` plays the batch; with N under the slots it holds the others itself (run_all's lock files, from the top; it lets
them go while a batch gathers every slot), so it can only lower the number of games. It prints medians by the number
of games running and the TDR events since its start (exit 2 when there are any); `report FILE...` and `tdr --since`
work on their own. A long steady probe: N copies `<prefix>01..` of a zone scenario (gz15h with weather, god mode,
`wait 900000`, timeout 150; made for the measurement in tests/scenarios and deleted after), run with `--vanilla-only`.
Suite scenarios are too short for it (3-10 s each at 16, half of it boot).

**Per game** (steady state in the probe, 2026-10-16; idle machine 3.9 GB GPU memory from the desktop apps):

| | standalone (our renderer, D3D11) | --no-hooks (game.exe on dgVoodoo) |
|---|---|---|
| dedicated GPU memory | 247 MiB (peaks ~430 during loading) | 291 MiB |
| private memory | 208 MB | 263 MB |
| CPU | one busy thread (~104% of a core: the virtual clock never sleeps) | ~115% of a core |

**Machine** (standalone probe; nvidia-smi memory / utilisation, median W, total CPU median, seconds per probe game):

| games | GPU memory med (peak) | GPU util med (max) | power | CPU | probe game | games / min |
|---|---|---|---|---|---|---|
| 4 | 4.9 GB (5.3) | 37% (55) | 79 W | 15% | 25 s | 9.6 |
| 8 | 5.9 GB (6.6) | 61% (65) | 129 W | 31% | 31 s | 15.5 |
| 12 | 6.9 GB (8.2) | 75-78% (84) | 142 W | 43-47% | 38-48 s | 15-19 |
| 16 | 7.8 GB (9.6-10.7) | 84% (90) | 153 W (peak 176) | 57-63% | 47-55 s | 17-20 |

--no-hooks at 16: 8.5 GB, GPU 91%, 121 W, 45 s per probe game. No TDR event in any of these runs; GPU at most 58 C.
The 12 / 16 ranges are two runs each: other workers' builds share the CPU, so throughput is noisy; the per-game
figures are steady across every run.

**Reading**: the old ~640 MB per game under dgVoodoo is gone (291 MiB now, our renderer 247), so 24 games would need
about 3.9 + 24 x 0.25 = 9.8 GB steady and ~14 GB at the loading peaks: under the 16 GB that 24 x 640 MB overran. But
the GPU is 84-90% busy at 16 and throughput flattens from 12 on (each game is one CPU thread spinning on the virtual
clock and presents as fast as it can), so more games mostly queue on the GPU and on the 16 physical cores; a step
above 16 buys little and spends the headroom. **Cap: stay at 16.**

**Step-up plan above 16** (only with the user's OK; one person at the machine, no other worker's games):
1. Let the scheduler exit (2 min idle) so the next one starts with the new count: the scheduler takes
   EI_HARNESS_SLOTS from the environment of the `work.py test` that starts it, and keeps it until it exits.
2. Make 20 probe copies, then `EI_HARNESS_SLOTS=20 python tools/game_load.py run --games 20 --probe <prefix>
   --ticket ID --out ... -- --vanilla-only`; then the suite at 20 (`--scenarios 50`).
3. Stop at once and go back to 16 on any of: a 153 or 4101 event, nvidia-smi memory above 13 GB (80%), a game that
   fails or times out that passes at 16, a frozen window or desktop stutter. Keep 20 only if games / min is at least
   10% above 16's on the same probe; 24 only after a clean 20, by the same rules.
4. Afterwards let the scheduler exit again, so other workers' batches start at 16.
