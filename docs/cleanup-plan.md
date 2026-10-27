# Cleanup plan: from the ported code to readable standalone source

Central plan for turning the ported code into source that reads like the game's own (1999-2001 Nival
C++), builds as an ordinary executable with no tie to game.exe, and carries our changes as an explicit
layer of fixes on top. Workers take tickets from `docs/tickets.toml` (`python tools/tickets.py list`);
every ticket follows "Conventions". The rules for writing code are in `docs/rewrite-guide.md`.

`docs/standalone-cleanup.md` is the itemised list of what still ties the code to game.exe (and the
behaviour the replacements must keep); this file is the plan and the order of work.

## Where it stands

Every game function is ported, and our code runs only in the standalone image, which contains no
game.exe code. game.exe runs only as the original, for the goldens and the function vectors; the
harness patches it by address there. The goldens and the vectors are the check for every cleanup, and
neither depends on the heap or on memory layout.

## Target state

1. **Modules by subsystem, classes by file.** `src/` is organised the way the original most likely
   was: `engine/` (objects, managers, resources, archives, streams, maths), `world/` (server,
   client, zone, save/load, game flow), `units/` (units, combat, AI orders), `ai/` (AI map, paths,
   regions), `script/`, `items/` (items, spells, databases), `effects/` (spell effects, particles,
   lights, sound objects), `ui/`, `net/`, `sound/`, `render/` (the new sokol renderer),
   `app/` (startup, window, input, options). One class per `.h`/`.cpp` pair where the class is
   substantial. Not by address range, not by "round".
2. **Real types.** Every class is declared once, with its base classes, real virtual functions (the
   compiler's tables) and named members of their real types. Code uses members, not offsets. game.exe's
   byte layout is kept only where bytes leave the process (save files, network packets).
3. **One implementation of each library facility.** Containers and strings are the standard ones;
   files, the CRT and Win32 come from one place each, not from ports of the game's copies, except where a
   golden shows the game's behaviour differs.
4. **Plain C++ arithmetic.** No naked x87 transcriptions or `float_control` pragmas except where a
   documented test shows the plain version changes gameplay.
5. **Original behaviour by default, fixes on top.** The code does what the original did, original
   bugs included; every deviation (Rebirth's patches, our fixes) is a named, documented, switchable
   fix (see "Fixes layer").
6. **No addresses.** Functions are called by name, data are ordinary variables, and nothing in the
   build reads game.exe. An address stays only in the harness's game.exe side, in the registrations
   until C20 retires them, and in the rdata literal tags `/*0073B86C*/`; never in a comment (C22).
7. **Comments explain the game, not the port** (C22, `docs/comment-guide.md`): no "naked because",
   "rule 9", "verify wrapper" commentary, no addresses or ticket ids.

## What is left and what removes it

`python tools/progress.py` reports the current counts (they only go down; `check_cleanup` ratchets
them per file).

| Artefact | Becomes | Ticket |
|---|---|---|
| hand-built tables (`static void* Vtable()`, `ei::SetVtable`, slot templates and slot calls), `...Hook` / `...Impl` slot shims | real virtual classes | C28 |
| `offsetof` / `sizeof` asserts | dropped, except where bytes leave the process or come from the data image | C28 |
| data addresses in code | named globals (the build reads no game.exe since C25c) | C18, C18r, C18t |
| import table cells, `gameapi::` | typed imports; direct Win32 calls except the routed ones | C18p |
| `EI_REPLACE` registrations, code cells of the data image | `EI_REWRITTEN` only (the tools' function-to-original map); no address map of game.exe's code | C20, C20e-g |
| `mfc::` / `stlp::` containers and strings, `CFile` / `CArchive` | `std::` types (scripted rewrites; iteration order kept where it feeds game logic) | C14a-d |
| `gamecrt::` | the host CRT, except the families a golden proves differ | C26 |
| `reinterpret_cast`, pointer `static_cast` | typed fields, parameters and objects | C15, C21 |
| `__declspec(naked)`, `__asm`, `#pragma float_control`, x87 helpers | plain C++; an SSE2 build | C16d |
| porting comments | comments about the game | C22 |

## Order of work

1. **Types first** (now): C28 (real virtual classes), C29 (forwarders and the harness's seams), C18 /
   C25a (the last data bytes), C15 / C21 (casts). These change no behaviour; goldens are the check.
2. **Library layer**: C14a-d move containers, strings and archives to standard types with scripted
   rewrites (one rule per call shape, hand work only where goldens differ); C26 puts each CRT family on
   the host CRT and keeps a port only where goldens differ; C18p the imports. STLport's `hash_map`
   iteration follows pointer values: audit each use whose order is observable before swapping
   (docs/standalone-cleanup.md, "Behaviour the replacements must keep").
3. **Addresses out**: C20 drops the registrations and the code cells once nothing reaches game
   addresses; the image then needs no address map of game.exe's code, and the build reads no game.exe.
4. **Platform**: the sokol renderer becomes the only renderer (the old Direct3D 7 path behind it goes,
   `docs/notes/renderer.md`); SDL3 for the window, input (replacing DirectInput and window messages) and
   the main loop (`SDL_PollEvent` where the game calls `PeekMessage`, so WinMain's loop and the nested
   message pumps keep their shape), sokol_gfx rendering into the SDL3 window (decided 2026-10-04;
   sokol_app was rejected: it owns the loop). SDL3 is added to the repo only when this starts.
5. **SSE2** (C16d): doubles that stand for x87 registers, the chop rounding, `check_x87`; at the switch
   the goldens are re-recorded from the SSE2 image after checking it against the x87 image at the
   behaviour level (docs/notes/cleanup-tooling.md, "SSE2 build").
6. **Layout and fixes**: move files into the module layout above, split the multi-class "round" files,
   introduce the fixes layer and move Rebirth's patches and our fixes into it, write
   `docs/source-guide.md` (module map, class hierarchy, how to add a fix).

## Fixes layer

- One registry, `fixes/` (grown from today's `ei::rebirth` flags): every fix has a name, a source
  (`rebirth:<addon patch name>`, `ei:<our name>`), a description, a default, and the functions it
  touches. Settings come from one ini next to the exe; a preset reproduces Rebirth.
- In code a fix is a single, visible branch at the point of change:
  `if (fixes::On(Fix::RepairPrice)) { ... } else { ...original... }`. No fix logic inside
  unrelated helpers; no silent behaviour changes.
- Candidates for our own fixes: the "original bug kept" comments in `src/` and the lists in
  `docs/standalone-cleanup.md` and the notes; each becomes a ticket with a decision (fix as an
  opt-in or leave).

## Compiler artefacts and the NaN audit

Two rules in the rewrite guide come from how VC6's x87 code behaves, not from Nival's intent:

- **NaN in comparisons (done, C16c; `docs/notes/cleanup-tooling.md` "NaN audit").** VC6 compiled `a == 0.0f`, `a < b` etc. as
  FCOMP / FNSTSW / TEST AH, where a NaN operand sets the "unordered" flags and so counts as
  *equal* (or *less*, depending on the test). The comparisons are plain C++ (a behaviour change for
  NaN only), with `EI_NAN_AUDIT` (`src/lib/nan_audit.h`) logging the first NaN per site in the verify
  and debug builds; each NaN that does show up is a real bug upstream (see the next item).
- **NaN that actually happens (an original bug).** An object with a NaN view depth makes the unit
  sort's comparator treat `Less(x, x)` as true, and STLport's unguarded partition then walks off
  the array: the rare Rebirth crash in `docs/open-questions.md`. Fix candidate for the fixes
  layer: find where the NaN depth comes from while a new game loads, and/or guard the sort.
- **Unsigned 32-bit to float (`x87::FromUnsigned`, checked by `tools/check_x87.py`).** VC6
  converted with an exact FILD of a 64-bit value; modern MSVC targeting x87 adds 2^32 with an FADD
  that rounds at the game's 24-bit precision, so random words and tick counts came out slightly
  different. A porting hazard, not a game bug: SSE2 conversion is exact, so `FromUnsigned` becomes a
  plain cast there.

## Conventions (all cleanup tickets)

- Behaviour stays identical; a cleanup commit never mixes in a fix.
- One subsystem or one mechanical rule per ticket and per worker; a ticket touching another
  subsystem's header asks its owner (or the coordinator) first. Shared headers change additively only.
- A mechanical change across many files is a script under `tools/` (one rule per call shape), run,
  built and checked by the suite; hand work only where goldens differ.
- Every ticket ends with: `build.cmd verify` and `release` clean, the build checks, `check_x87`,
  `check_signatures` and `check_stolen` at 0, the artefact counts reported before / after, and the
  suite with `--golden` (`python tools/work.py test <ID> --golden`). The original suite
  (`--no-hooks --golden --fresh`) only when the change touches the harness's game.exe side.
- Names: the original's where known (RTTI, strings, Rebirth's and the community's names), else
  descriptive; `CClassName` for game classes (Nival style), members `name_`, no Hungarian
  additions of our own.
- Notes go to `docs/notes/` rather than long comments in code.
