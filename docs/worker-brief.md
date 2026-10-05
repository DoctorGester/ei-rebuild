# Worker brief (common part)

You are working on a C++ rebuild of Evil Islands' game.exe (1.06, MSVC6) in the repo at your
current working directory (a git worktree of E:\workspace\ei). Rewritten functions replace the
originals in the running game through a hook DLL. Your task is ONE ticket, given in your prompt;
do it, commit, report, and stop. Don't start other work.

Read first: README.md (harness section), docs/rewrite-guide.md (all rules come from real bugs),
and the notes file named in your ticket. For renderer work also docs/notes/renderer.md.

Rules:
- Never modify anything under F:\games. Don't launch the game (runs are central). Don't install
  anything. Don't call any mcp__hearthbot__ tools.
- Merge main into your worktree first.
- Plain C++ float maths (rule 7); verify floats with ei::verify::CheckNear.
- Every rewrite gets a verify wrapper; say whether it compares against the original or checks
  ours only, and which scenario reaches it.
- Forwarders: use the shared declarations in src/game/originals_<area>.h (rewrite guide rule 5);
  build.cmd's check_forwarders fails on new duplicates.
- Shared headers: small additive changes only.
- Build `build.cmd verify` and `build.cmd release` (your worktree's, by full path) with no errors
  or warnings; `python tools/check_stolen.py`, `python tools/check_x87.py`, `tools\check_signatures.cmd`
  and check_thunks (run by build.cmd) at 0 problems.
- Notes: append what you did to the ticket's notes file.
- Commit messages end with: Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>

Report (short): what you changed (addresses, files), how each is verified, risks, branch/commit.
