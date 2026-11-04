# Worker brief

This repo is a readable C++ rebuild of Evil Islands' `game.exe` 1.06 that behaves like the original (the same world,
saves and network play), not one that matches it byte for byte. The product is `ei-rebuild.exe`: one 64-bit exe,
dropped into a GOG or Rebirth install. Nothing runs or reads the original `game.exe`; the goldens are our own runs.

A worker does ONE ticket, given in its prompt: do it, commit, report, stop.

## Start

1. Merge main into your worktree (once, now; never again before handing back), then `python tools/work.py start <ID>`.
2. Read `README.md`, `docs/rewrite-guide.md` (every rule comes from a real bug), `docs/comment-guide.md`, your
   ticket (`python tools/tickets.py show <ID>`: note, files, dependencies, its group's note and doc) and the notes
   of your area (`docs/notes/README.md` lists them). Don't read `docs/tickets.md` whole.
3. Work in few turns: read whole sections at once, send independent reads and edits together.

## Tickets

The text is in `docs/tickets.toml`, the status in the shared work database (`docs/notes/work-board.md`); both
only through `tools/tickets.py`, never by hand.
- Branch ready: `python tools/tickets.py set <ID> review`; commit the rewritten `docs/tickets.md` with your work.
- Follow-up work you found: `python tools/tickets.py add <NEW-ID> --group <key> --title "..." --note "..."
  [--depends <ID>]` (your ticket's prefix plus a letter or digit); name them in your report.
- Leave other tickets' status alone; the coordinator sets done at merge.
- A `docs/tickets.toml` / `docs/tickets.md` conflict: take main's toml, re-apply your changes with tickets.py and
  let it rewrite tickets.md (`python tools/tickets.py`).
- Board notes at milestones: `python tools/work.py note <ID> "<text>"` (what changed, what runs, what blocks).

## Build and test

- `build.cmd verify` (the image with the harness: what the runs use) and `build.cmd release` (the product), by full
  path, with no errors or warnings; both run the build checks (`tools/build_checks.py`), which must pass. Debug and asan builds are the coordinator's, at merge. `play.cmd` (the user plays with
  it) must keep working: change it with `build.cmd`'s arguments or outputs.
- Games only through `python tools/work.py test <ID> [run_all args]` (scenario names for a subset,
  `--rebirth-only`, `--golden`, at most `--jobs 4`): it queues on the machine's 16 game slots. Never call
  `tools/run_all.py` directly, never leave a game running. Equivalence sweeps stay under about 5 minutes per run.
- A change that should not alter behaviour keeps every golden the same. An intended change re-records the affected
  goldens and says why in the commit (`docs/notes/standalone-verification.md`, "Recording goldens").
- Say how each change is checked: the scenario with a golden that reaches it, or the unit check (`tests/unit`).
- Keep the waits short (the work board's Waits tab measures them): while iterating, build verify only and run the
  scenarios that reach the change by name; build release and run the full image and Rebirth suites once, when the work
  is done (again only if a fix follows). Read a failed build's first errors and fix them all before building again.
- Long commands (builds, suites) run in the background under `python tools/limit.py SECONDS -- COMMAND...` (build
  900, `work.py test` 5400, else 600); wait for the completion notice, never poll. A timeout is a result: find out
  why.
- Cloud sessions (no compiler, no game data) build and test through the Suites workflow on the self-hosted runner
  (`docs/notes/harness.md`, "The Suites workflow").

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
