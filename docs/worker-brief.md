# Worker brief (common part)

You are working on a C++ rebuild of Evil Islands' game.exe (1.06, MSVC6) in the repo at your
current working directory (a git worktree of E:\workspace\ei). The product is the standalone image:
ei_standalone.exe lays out game.exe's data without its code, and ei_hook.dll writes a jump to each
rewritten function at its original address. Your task is ONE ticket, given in your prompt;
do it, commit, report, and stop. Don't start other work.

First commands: merge main into your worktree, then `python tools/work.py start <ID>` (registers you
on the work board; `python tools/work.py status` shows it). Then read: README.md (harness section), docs/rewrite-guide.md (all rules come from real bugs),
your ticket with `python tools/tickets.py show <ID>` (status, note, files, what it waits on, its
group's note and doc), and the notes file it names. For renderer work also docs/notes/renderer.md.
Don't read docs/tickets.md whole (it is generated; `tickets.py show` / `list` cover it).

Tickets (text in docs/tickets.toml, status in the shared work database; both only through tools/tickets.py,
never by hand). `work.py start` set yours running.
- When your branch is ready: `python tools/tickets.py set <ID> review` (shows on the board at once; commit
  the rewritten docs/tickets.md with your work).
- Follow-up work you found but did not do: `python tools/tickets.py add <NEW-ID> --group <key>
  --title "..." --note "..." [--depends <ID>]` (ids: your ticket's prefix plus a letter or number;
  `tickets.py list` shows what exists). Mention them in your report.
- Leave other tickets' status alone; the coordinator sets done at merge.
- If docs/tickets.toml or docs/tickets.md conflicts when you merge main: take main's toml, re-apply
  your few changes with tickets.py, and let it rewrite tickets.md (`python tools/tickets.py`; it also
  drops any status line that came back: the database's status wins).

Rules:
- Never modify anything under F:\games or the registry. Don't install anything. Don't call any
  mcp__hearthbot__ tools.
- Games only through `python tools/work.py test <ID> [run_all args]` (same arguments as run_all:
  scenario names for a subset, `--vanilla-only`, ...; same summary and exit code), at most `--jobs 4`.
  It queues behind other workers' batches on the machine's 16 game slots and runs to completion, also
  for one scenario (the board shows it); don't call tools/run_all.py directly, it skips the queue.
  The runs are the standalone image's (installs standalone / standalone-rebirth, checked with `--golden`);
  `--no-hooks` runs the original (golden recording; with `EI_VECTORS=1`, vector recording). game.exe only ever runs
  as the original game (S21e: ei_launch always sets EI_NO_HOOKS for it; the vector recorders are its only hooks). Never leave a game running. Your worktree has no run\ folder: `cmd //c mklink /J run
  E:\workspace\ei\run`, and `cmd //c rmdir run` before you finish (never delete through the junction).
- Plain C++ float maths (rule 7).
- Say how each rewrite is checked (rewrite guide, "Verification"): which scenario with a golden
  reaches it, or which function vectors replay it.
- Forwarders: use the shared declarations in src/game/originals_<area>.h (rewrite guide rule 5);
  build.cmd's check_forwarders fails on new duplicates.
- Write clean code (rewrite guide, "Clean code"): members not offsets, virtual calls not VCall,
  named globals; build.cmd's check_cleanup fails when a file's counts go up.
- Shared headers: small additive changes only (adding members a port needs is fine).
- Build `build.cmd verify` (the harness build: the image plus the harness and the vector recorders,
  what the runs use) and `build.cmd release` (the product) (your worktree's, by full path) with no errors
  or warnings; `python tools/check_x87.py`, `tools\check_signatures.cmd`, `python tools/check_stolen.py`
  (vector recorders) and the checks build.cmd runs at 0 problems.
- Notes: append what you did to the ticket's notes file.
- Board notes: `python tools/work.py note <ID> "<text>"` at milestones (short: what changed, what is
  running, what blocks), e.g. after a green build, before a suite, when stuck. The coordinator ends
  your assignment (`work.py done`) at merge; you only start and note.
- Work in few turns (each tool call costs a model turn; that is most of a worker's time): start
  from the notes, functions and files the ticket names; read with Read / Grep, whole sections at
  once, and send independent reads, searches and edits together in one message.
- Long commands (both builds, a suite) go in the background (`run_in_background`); then end your
  turn and wait for the completion notice. Never poll: no `until` / `while sleep` loops, no
  Wait-Process, no repeated tail of an output file. Build `verify` while iterating, `release`
  before each commit.
- Scratch files go under the scratchpad's folder for your ticket (the scratchpad is shared).
- Commit messages end with: Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>

Report (short): what you changed (addresses, files), how each is verified, risks, branch/commit.
