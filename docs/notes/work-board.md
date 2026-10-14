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
