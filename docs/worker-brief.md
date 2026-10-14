# Worker brief (common part)

You are working on a C++ rebuild of Evil Islands' game.exe (1.06, MSVC6) in the repo at your
current working directory (a git worktree of E:\workspace\ei). Rewritten functions replace the
originals in the running game through a hook DLL. Your task is ONE ticket, given in your prompt;
do it, commit, report, and stop. Don't start other work.

Read first: README.md (harness section), docs/rewrite-guide.md (all rules come from real bugs),
your ticket with `python tools/tickets.py show <ID>` (status, note, files, what it waits on, its
group's note and doc), and the notes file it names. For renderer work also docs/notes/renderer.md.
Don't read docs/tickets.md whole (it is generated; `tickets.py show` / `list` cover it).

Tickets (docs/tickets.toml, changed only through tools/tickets.py, never by hand):
- When your branch is ready: `python tools/tickets.py set <ID> review`, and commit it with your work.
- Follow-up work you found but did not do: `python tools/tickets.py add <NEW-ID> --group <key>
  --title "..." --note "..." [--depends <ID>]` (ids: your ticket's prefix plus a letter or number;
  `tickets.py list` shows what exists). Mention them in your report.
- Leave other tickets' status alone; the coordinator sets running and done.
- If docs/tickets.toml or docs/tickets.md conflicts when you merge main: take main's toml, re-apply
  your few changes with tickets.py, and let it rewrite tickets.md.

Rules:
- Never modify anything under F:\games or the registry. Don't install anything. Don't call any
  mcp__hearthbot__ tools.
- Games only through `python tools/run_all.py` (scenario names for a subset), at most `--jobs 4`
  (other workers share the machine's 16 game slots); never leave a game running. Your worktree has
  no run\ folder: `cmd //c mklink /J run E:\workspace\ei\run`, and `cmd //c rmdir run` before you
  finish (never delete through the junction).
- Merge main into your worktree first.
- Plain C++ float maths (rule 7); verify floats with ei::verify::CheckNear.
- Every rewrite gets a verify wrapper; say whether it compares against the original or checks
  ours only, and which scenario reaches it.
- Forwarders: use the shared declarations in src/game/originals_<area>.h (rewrite guide rule 5);
  build.cmd's check_forwarders fails on new duplicates.
- Write clean code (rewrite guide, "Clean code"): members not offsets, virtual calls not VCall,
  named globals; build.cmd's check_cleanup fails when a file's counts go up.
- Shared headers: small additive changes only (adding members a port needs is fine).
- Build `build.cmd verify` and `build.cmd release` (your worktree's, by full path) with no errors
  or warnings; `python tools/check_stolen.py`, `python tools/check_x87.py`, `tools\check_signatures.cmd`
  and check_thunks (run by build.cmd) at 0 problems.
- Notes: append what you did to the ticket's notes file.
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
