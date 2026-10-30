# Handoff for cloud sessions

Read this first when you work on this repo from a cloud session (a Linux container with a fresh clone). The project
so far ran on Kirill's Windows machine, where a coordinator session gave tickets to worker sessions in git worktrees.
From 2026-10-30 work continues in cloud sessions. They have no compiler for this code (MSVC, Windows only), no game
data and no access to the machine. **Builds and games run on the self-hosted runner** (Kirill's machine, through the
Suites workflow).

## What the project is

`README.md` covers it. In short, this is a readable C++ rebuild of Evil Islands' `game.exe` 1.06 that behaves the
same, not one that matches it byte for byte. The product is `ei-rebuild.exe`, one exe in x86 and x64 builds that is
dropped into a GOG or Rebirth install. Nothing runs or reads the original `game.exe` any more (H40): the goldens are
the image's own, and the tag `original-oracle` (b06141b) has the last build that ran it.

The tests are scenario runs (`tests/scenarios/*.txt`) checked against goldens (`tests/golden/<scenario>/<install>/`).
A golden is game state: digests of the world, saves and console. Goldens never hold frames.

Read before changing code:
- `docs/rewrite-guide.md` (all rules come from real bugs) and `docs/comment-guide.md`.
- `docs/worker-brief.md`: the rules every worker followed. Its machine-specific parts, such as worktrees,
  `work.py start` and `limit.py`, do not apply in the cloud.
- The notes file of your area, `docs/notes/<area>.md`, for example `platform.md`, `renderer.md`, `harness.md`,
  `standalone-verification.md` or `x64.md`.

## Tickets

- The ticket text is in `docs/tickets.toml`. The statuses are in `docs/ticket_status.tsv`, which `tools/tickets.py`
  reads and writes. On the machine they also live in a SQLite work database. In the cloud, `tickets.py` makes a
  fresh one and fills it from the tsv. A status you set reaches the machine through git (the newer row wins).
- Commands:
  - `python tools/tickets.py list` and `show <ID>` (the note, its dependencies, its group's note).
  - `python tools/tickets.py set <ID> running|review|done|queued|later`.
  - `python tools/tickets.py add <ID> --group <key> --title ... --note ... [--depends ...]`. The groups are
    lowercase: cleanup, renderer, harness, port, widescreen, and so on.
  - `python tools/tickets.py` with no arguments regenerates `docs/tickets.md` and the tsv. Commit both with your
    change.
- Never edit the status lines or `docs/tickets.md` by hand. The toml is TOML, so escape a backslash as `\\` or write
  `/` in notes.
- The ticket board, a claude.ai artifact, is published from the machine and is not needed in the cloud. Do not
  publish it from a session: statuses travel through the committed tsv.

## Building and testing: the Suites workflow

`.github/workflows/suites.yml` runs on the self-hosted runner `ei-pc` (labels `self-hosted`, `ei-game`).

- **Trigger:** push to any `claude/**` branch, or run it by hand (Actions > Suites > Run workflow, or the API)
  with `ref` and `suites` = `full` | `quick`.
  - quick: the verify and release builds plus the image suite.
  - full: also debug, asan, play, x64 and verify-x64, plus the Rebirth and x64 suites.
- **Reading results:** the run's summary page has each suite's goldens block. A failing build prints its last 40
  log lines in the step log. From a session: `gh run list --workflow suites.yml`, `gh run view <id> --log-failed`,
  or the REST API (`/repos/DoctorGester/ei-rebuild/actions/runs/<id>/jobs`, then `/actions/jobs/<id>/logs`). The
  Summary step also prints each suite's failed runs and goldens block into the log. To wait for a run, poll
  `https://api.github.com/repos/DoctorGester/ei-rebuild/actions/workflows/suites.yml/runs` from a background command
  (it finishes when the run does and wakes the session); a scheduled reminder does not wake it.
- **Where two builds part** (the `diff_base` input, with optional `scenarios` and `resolution`): plays the
  scenarios on both installs with your branch's verify build and with the base ref's (a worktree in build/base) and
  prints, for each run that differs, the first differing digest record field by field, the console lines, the
  failures, the window size and the groundview / clickground lines. Use it before re-recording anything: every
  changed golden needs a cause.
- **Expected results on main** as of 2026-10-30 (goldens recorded from the image, H40):
  - Image suite: 152 the same, 3 without a golden (the watchdogs).
  - Rebirth suite: 32 the same.
  - x64 suite: 152 the same, 3 without a golden.
  - A change that should not alter behaviour keeps every golden the same. An intended change re-records the
    affected goldens and says why in the commit.
- **Limits:**
  - One run at a time (concurrency group `ei-game`), so others queue.
  - Games share the machine's 16 slots with Kirill's local work.
  - The runner is only up while Kirill is logged in, and a queued run waits until then.
  - Each push to a `claude/**` branch costs a full run (about 20 to 30 minutes), so batch your commits and push
    when a step is ready.
- **Golden recording** (H40): run the workflow by hand on your branch (not main) with `record` = both,
  vanilla or rebirth (optionally `scenarios`, space-separated, and `reason`). Its `record` job plays two passes
  that must agree, runs `tools/golden.py record` and pushes the goldens to the branch as a commit of
  its own (`git pull` it). The summary names the new and changed goldens; a golden whose content is the same is
  left alone, golden.json included. That push starts no run, so dispatch a full
  run on the branch after it. docs/notes/standalone-verification.md, section 2.
- **Dispatching from a session:** the GitHub MCP tool `actions_run_trigger` (workflow `suites.yml`, ref your branch,
  inputs as above; the `ref` input can stay main, which then means the branch the run was started on).
- **What you can run in the cloud without the runner:** the Python tools' own tests (`python -m unittest` in
  `tools/`, for example `tools/test_work_sched.py`) and the text checks that read only the source:
  - `tools/check_cleanup.py`
  - `tools/comments.py check`
  - `tools/tickets.py`
  - `tools/gen_iat.py --check`
  - `tools/gen_static_init.py --check-ports` (needs `pip install capstone==5.0.7`)
  - `tools/data_seam.py --check`

## Merging

When a branch's full Suites run is green (no golden differs), merge it into `main` and push. The Suites workflow
also runs on pushes to `main`. Set the ticket to done with `tickets.py set <ID> done` in the merge.

For `docs/tickets.toml` conflicts, keep both sides' tickets (each is a `[[ticket]]` block). `docs/tickets.md` merges
as ours, so regenerate it. `docs/ticket_status.tsv` merges as a union, so regenerate it too.

## Rules (decisions Kirill made; keep them)

Goal and code:
- The goal is the same behaviour, checked by the goldens. The heap layout is never preserved, and goldens hold no
  frames.
- Write normal C++ classes: no hand-built vtables, no layout rules, no needless forwarders. No trailing `_` on
  members (C59).
- Zero MFC and zero STLport in game code, with a minimal CRT of our own. Hunt dead code. Style reviews feed tickets.
- Prefer one uniform code path over branches that run only in edge cases.
- Renderer upgrades replace the old look, with no render flags.
- The interface is native: P8 removed all GDI drawing.
- Text is UTF-8 everywhere. Saves stay cp1251 on disk and are converted at load and save.
- Client-only effects use their own random generator, so client draws never move game state.
- SSE2 floating point: no tests that hang on rounding differences. Fix such issues by judgement.
- Equivalence sweeps stay under about 5 minutes per run.
- Rebirth's fixes are moving to always on (C68). Widescreen means one layout for any aspect ratio (W12), with no
  WideScreen flag branches.
- `play.cmd` (Kirill plays with it) must keep working. If you change `build.cmd`'s arguments or outputs, update it.

Platform direction:
- The x64 build and the platform layer (SDL3, `src/platform`) are the current direction.
- A Linux and macOS port was estimated on 2026-10-30 at about 10 to 15 tickets but is not filed yet: the renderer
  through sokol for GL and Metal, a clang build, PL9b, case-insensitive file lookup, and the harness on SDL (PL12).
  Ask Kirill before starting it.

Safety on Kirill's machine (these apply to anything the workflow runs):
- Never write into `F:\games\*` (the installs).
  - Scratch runs set `Work path` and `CD-ROM Path` to `.\` in their own `ei_settings.ini`.
  - Copy `Config` instead of linking it.
- Never recursive-delete a folder that holds junctions (`Remove-Item -Recurse`, `rm -rf` or `shutil.rmtree`
  followed one on 2026-10-30 and wiped the install's data). Remove each link with `cmd /c rmdir <link>` first,
  and write link paths out in full.
- Games run only through `tools/work.py test` (its scheduler keeps the machine at 16 games or fewer).
  - A game lasts at most 60 s.
  - Never leave a game open.
  - Never switch the display mode or use exclusive full screen.
- The repo is public, so never commit game data. H41 covers getting test data to CI privately.
- Commits end with the `Co-Authored-By:` line that git commits in this repo carry.

## Where things stand (2026-10-30)

Recently done:
- A2 (Bink): an open decoder.
- A1 (Miles): our own mixer on SDL3.
- PL9: SDL3 for the window and input.
- PL10: the platform review, in `build/reviews/pl10_platform.md` on the machine. Its plan is in PL11's note and
  the PL11a-f tickets.
- PL11a and PL11b merged (files simplified).
- R39: the menu fog default.
- The goldens were re-recorded on the re-downloaded GOG data.
- The release workflow: Actions > Release with a version, which publishes the x86 and x64 exes. v0.1.0 is out.

Next, in rough order:
1. **H40** is done: the goldens are the image's own (recorded by the workflow's `record` input from two agreeing
   runs), and the harness, launcher, tools and build have no game.exe side left (no `--no-hooks`, reference cache,
   harness key, `# no original` lines, ei_harness.dll, hook framework, vector recorders, coverage breakpoints or
   heap jitter). The recorded vectors and their replay stay. The tag `original-oracle` has everything removed.
2. **W12** is merged (a32e6b8): one layout for every window. Vanilla image runs play at 960x720, Rebirth's at
   1280x720 with the wide layout; a scenario's `vanilla` / `rebirth` prefix on a window step picks one install.
3. **C68** is done: Rebirth's bug fixes, the scripting / console additions and three multiplayer patches are the
   rebuilt code's own, and the whole map is drawn (no draw radii). Left as flags for later (user): MQClient,
   DisableExitOnCrashReport, the mod-folder patches and EI_Plugin's features.
4. **C69** is done: `src/game/settings_catalog.inc` describes every setting; new settings files carry the
   descriptions; `docs/settings.md` is generated (`tools/gen_settings_doc.py`, in the build checks).
5. **PL11c-f**: the rest of the platform simplification (see PL11's note). **PL11s**: delete the shared-opens
   check (the suites logged only read-while-read opens).
6. PL9a, PL9b, PL12, C66b, C30, C30a1, R37a1 and R37e2: see `tickets.py show`.
7. **H41** (later): test data for CI.
