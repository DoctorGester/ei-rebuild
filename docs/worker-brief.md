# Worker brief

This repo is a readable C++ rebuild of Evil Islands' `game.exe` 1.06 that behaves like the original (the same world,
saves and network play), not one that matches it byte for byte. The product is `ei-rebuild.exe`: one 64-bit exe,
dropped into a GOG or Rebirth install. Nothing runs or reads the original `game.exe`; the goldens are our own runs.

A worker does ONE ticket, given in its prompt: do it, commit, report, stop.

## Start

1. Merge main into your worktree (once, now; never again before handing back), then `python tools/work.py start <ID>`.
2. Read `README.md`, `docs/rewrite-guide.md` (every rule comes from a real bug), `docs/comment-guide.md`, your
   ticket (`python tools/tickets.py show <ID>`: note, files, dependencies, its group's note and doc) and the notes
   of your area (`docs/notes/README.md` lists them). Read tickets one at a time (`tickets.py show`, `list`), not the whole folder.
3. Work in few turns: read whole sections at once, send independent reads and edits together.

## Tickets

One file per ticket, `docs/tickets/<ID>.toml`; the live status is in the shared work database
(`docs/notes/work-board.md`). Both only through `tools/tickets.py`, never by hand.
- Branch ready: `python tools/tickets.py set <ID> review`; commit your ticket's file (it carries the status line) with your work.
- A note: `tickets.py note <ID> "<text>" --append` adds to it; a plain `note` refuses to overwrite a note that is not
  empty (`--replace` says it is meant).
- Follow-up work you found: `python tools/tickets.py add <NEW-ID> --group <key> --title "..." --note "..."
  [--depends <ID>]` (your ticket's prefix plus a letter or digit); name them in your report.
- Leave other tickets' status alone; the coordinator sets done at merge.
- Ticket files do not conflict unless two branches changed the same ticket; then take main's file and re-apply your
  change with tickets.py (`status` lines: `tickets.py set <ID> <status>` rewrites them from the database).
- Board notes at milestones: `python tools/work.py note <ID> "<text>"` (what changed, what runs, what blocks).

## Build and test

- `build.cmd verify` (the image with the harness: what the runs use) and `build.cmd release` (the product), by full
  path, with no errors or warnings; both run the build checks (`tools/build_checks.py`), which must pass. Debug and asan builds are the coordinator's: debug as a compile check every few merges, asan with a suite run on it after memory-handling tickets and every few merges (user 2026-11-04).
- A fresh worktree's first build takes most objects from the compiler cache that every checkout shares
  (`docs/notes/standalone.md`, "The compiler cache"). Debug information then names the first compiling checkout's paths.
- Games only through `python tools/work.py test <ID> [run_all args]` (scenario names for a subset,
  `--rebirth-only`, `--golden`, at most `--jobs 4`): it queues on the machine's 16 game slots. Never call
  `tools/run_all.py` directly, never leave a game running. Equivalence sweeps stay under about 5 minutes per run.
- Without scenario names `work.py test` plays the smoke tier (`config/smoke_scenarios.csv`: the runs that reach the
  most code, both installs; H74). `--full` plays the whole image suite (and `--full --rebirth-only` the Rebirth set),
  as the coordinator's merge runs always do. `--converted` without names plays only when a file the converted installs
  read changed against main (`run_all.CONVERTED_PATHS`; it says so and stops otherwise).
- Timing runs (frame times, profiles, any number compared across builds) pass `--quiet`: each game then runs alone,
  with no other game and no build on the machine (builds wait for it). Keep them to the few scenarios measured: the
  whole machine waits on a quiet batch (`docs/notes/work-board.md`, "Scheduler").
- Every check run passes `--golden` (without it a run only checks itself and compares nothing); report its
  "N the same, N differ" line, not a pass count.
- A change that should not alter behaviour keeps every golden the same. An intended change re-records the affected
  goldens and says why in the commit (`docs/notes/standalone-verification.md`, "Recording goldens").
- Say how each change is checked: the scenario with a golden that reaches it, or the unit check (`tests/unit`).
- Keep the waits short (the work board's Waits tab measures them): while iterating, build verify only and run the
  scenarios that reach the change by name, then the smoke tier; build release and run the full image and Rebirth
  suites (`--full --golden`, `--full --rebirth-only --golden`) once, when the work is done (again only if a fix
  follows). Read a failed build's first errors and fix them all before building again.
- Long commands (builds, suites) run in the background under `python tools/limit.py SECONDS -- COMMAND...` (build
  900, `work.py test` 5400, else 600); wait for the completion notice, never poll. A timeout is a result: find out
  why.
- Cloud sessions (no compiler, no game data) get the Linux workflow on every push (build, warnings, ctest, the
  goldens on the stub data; `docs/notes/linux.md`, "The CI job"). The Windows Suites workflow on the self-hosted runner
  runs by hand only (`docs/notes/harness.md`, "The Suites workflow").

## Rules

The user's standing decisions:
- Same behaviour, checked by the goldens; heap layout is never preserved; goldens hold no frames.
- Normal C++ classes: no hand-built vtables, no layout rules, no needless forwarders, no trailing `_` on members.
- Zero MFC and STLport in game code, a minimal CRT of our own; hunt dead code.
- One uniform code path over branches that run only in edge cases.
- Renderer upgrades replace the old look (no render flags). The interface is drawn natively (no GDI).
- Text is UTF-8 everywhere; saves stay cp1251 on disk, converted at load and save.
- Client-only effects use their own random generator, so client draws never move game state.
- Floating point is plain C++ on SSE; fix rounding issues by judgement, no tests that hang on rounding.
- Rebirth's fixes are always on; widescreen is one layout for any aspect ratio.
- Every build is x64; the platform layer (`src/platform`, SDL3) is the direction; Linux builds and runs its checks.

Safety on the machine:
- Never write under `F:\games` (the installs) or the registry; scratch runs set `Work path` to
  `.\` in their own `ei_settings.ini`. Don't install anything.
- Never recursive-delete a folder that holds junctions: remove each link with `cmd /c rmdir <link>` first.
- Never switch the display mode or use exclusive full screen. A game lasts at most 60 s.
- The repo is public: never commit game data.
- Scratch files and one-off scripts go to your scratchpad folder, not `tools/`; name the scripts in your report.

Code:
- Deleting code deletes its comment in the same edit (`docs/comment-guide.md`).
- Shared headers: small additive changes only.
- Notes: update your area's notes file in place: current facts, how-tos, rules from real bugs, open questions; no
  log (git keeps the history; `docs/notes/README.md`).
- Commit small green groups; messages end with the co-author line given in your prompt.

## Report

Short: what changed (files), how each part is checked, risks, branch and commit, follow-up tickets.
