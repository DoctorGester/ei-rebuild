# Cleanup plan: from hook-era rewrite to readable standalone source

Central plan for turning the rewritten code into source that reads like the game's own
(1999-2001 Nival C++), builds as a standalone executable, and carries our changes as an explicit
layer of fixes on top. Workers take tickets from the subsystem table at the end; every ticket
follows the rules in "Conventions".

`docs/standalone-cleanup.md` stays the itemised list of shims (forwarders, kept bugs, Rebirth
sites); this file is the plan and the order of work.

## Target state

When the game runs as its own exe, the source should look like this:

1. **Modules by subsystem, classes by file.** `src/` is organised the way the original most likely
   was: `engine/` (objects, managers, resources, archives, streams, maths), `world/` (server,
   client, zone, save/load, game flow), `units/` (units, combat, AI orders), `ai/` (AI map, paths,
   regions), `script/`, `items/` (items, spells, databases), `effects/` (spell effects, particles,
   lights, sound objects), `ui/`, `net/`, `sound/`, `render/` (the new sokol renderer),
   `app/` (startup, window, input, options). One class per `.h`/`.cpp` pair where the class is
   substantial. Not by address range, not by "round".
2. **Real types.** Every class is declared once, with its base classes, virtual functions in
   vtable order, and named members. Code uses members, not offsets.
3. **One implementation of each library facility.** Containers, strings, files and the CRT come
   from one place (see "Library layer"), not from the game's copies.
4. **Plain C++ arithmetic.** No naked x87 transcriptions or `float_control` pragmas except where a
   documented test shows the plain version changes gameplay.
5. **Original behaviour by default, fixes on top.** The code does what the original did, original
   bugs included; every deviation (Rebirth's patches, our fixes) is a named, documented, switchable
   fix (see "Fixes layer").
6. **Comments explain the game, not the port.** One address tag per function
   (`// 0x00590700`) for archaeology; no "naked because", "rule 9", "verify wrapper" commentary.

## Hook-era leftovers the plan removes

`python tools/progress.py` reports the current counts of every artefact below (they only go down; the 2026-10-04 baseline is in git history).

| Artefact | Becomes |
|---|---|
| `EI_ORIGINAL` forwarders (some already rewritten and called through the original address, some library code, some game code not yet rewritten) | direct calls / library layer / ported code |
| same function forwarded from several files | one declaration (`originals_<area>.h`) |
| `__declspec(naked)` (forwarders and x87/asm helpers), `__asm` blocks, `#pragma float_control(precise)` | plain C++ or a documented exception; pragmas removed where tests allow |
| `unknown_XXX` fields | named members or padding with a reason |
| `At<>` / `Field<>` raw-offset access, `reinterpret_cast`, `*At` view structs | members, real types |
| `ei::VCall` slot calls | virtual calls |
| addresses of game data (`0x007xxxxx` etc., `constexpr uintptr_t kX = 0x00...`) | ordinary globals, constants, class statics |
| `__fastcall Fn(T* self, void*)` hook shims | member functions |
| struct names defined in more than one file, anonymous-namespace layouts | one definition in a header |
| `mfc::` / `stlp::` / `gamecrt::` / `gameapi::` uses | library layer |
| `EI_REPLACE` registrations | removed |

## Order of work

The key decision: **do the structural cleanup while we still run inside game.exe.** As long as
the hook DLL runs, every function still has the original next to it, and the verify wrappers and
the scenario suite catch a cleanup mistake in game. After the switch to a standalone exe that
safety net is gone. So everything that can be verified in process happens first.

### Phase 0: stop the growth (now, cheap)
- New code follows the conventions below. In particular: class layouts go in the subsystem's
  header, not in an anonymous namespace; one shared `At`-free accessor per class; forwarders are
  declared once per function (see `src/game/originals.h` below).
- Add `src/game/originals.h`: one generated declaration per forwarded original
  (`tools/gen_originals.py` from a table), so a forwarder is never written twice again.
- Extend `tools/progress.py` with the artefact counts from the table above, so every round
  reports them and they only go down.

### Phase 1: types and names (in the hook DLL, verified)
Per subsystem (tickets below):
1. Collect every layout of the subsystem's classes (header, anonymous namespaces, `*At` views,
   `At<>` offsets in other files) into one declaration per class, with `static_assert`s on size
   and offsets, base classes and the vtable as virtual functions.
2. Replace offset access, `VCall` and `*At` views with members and virtual calls; turn
   `__fastcall Fn(T* self, void*)` shims into members (the hook macro can register a member).
3. Name what is understood; leave `unknown_XXX` only with a comment on what is known.
4. Replace data addresses with declarations of the game's globals in one place per subsystem
   (`extern` objects placed at their addresses by the hook build, ordinary globals later).
5. Merge duplicate helpers and forwarders.
Verification: build, check scripts, full scenario suite on both installs, no new mismatches.
Rewritten functions keep their hooks; nothing changes behaviour.

### Phase 2: library layer (in the hook DLL, verified)
- `stlp::` containers: switch to `std::` where iteration order cannot be observed; where it can
  (hash_map iteration, e.g. `CMappedResource::FreeAllFiles`), keep a small own container that
  reproduces the order. Audit each use; list the order-sensitive ones.
- `mfc::` (`CString`, `CArray`, `CList`, `CMap`, `CTypedPtrList`, `CFile`, `CFileFind`, `CPlex`):
  one own minimal implementation with the same semantics (growth, copy-on-write if relied on);
  MFC is not available to link.
- `gamecrt::`: standard CRT except the documented differences (locale tables for bytes
  0x80-0xFF in `tolower`/`strnicmp`, `rand`, `_ftol` rounding); keep those as named functions.
- The 731 forwarded library functions disappear with this phase.
Verification as in phase 1; while we run inside game.exe the game's own heap and node pool must
stay in use for objects the original code also frees, so this phase swaps implementations only
for objects our code fully owns, and finishes after phase 3.

### Phase 3: the switch to a standalone exe
- Port what never runs under the hooks: static initialisers, early startup, `WinMain`
  (Rebirth-patched), and the remaining forwarded game functions (1,159 today, falling as the
  port continues).
- A standalone build target that links `src/` without `src/hook/`; the harness becomes a test
  mode of the exe (same scenario files, same report), so the suite keeps working.
- The sokol renderer becomes the only renderer; the old Direct3D path and dgVoodoo go.
- Platform layer: SDL3 for the window, input (replacing DirectInput and window messages) and the
  main loop (`SDL_PollEvent` where the game calls `PeekMessage`, so WinMain's loop and the nested
  message pumps keep their shape); sokol_gfx renders into the SDL3 window. Decided 2026-10-04
  (sokol_app was rejected: it owns the loop). SDL3 is added to the repo only when this phase starts.
- Before switching: record golden outputs from the original game for the regression net
  (console logs, script expression values and save files of every scenario at fixed points;
  captures for the renderer), so the standalone exe has something to be compared against.

### Phase 4: standalone-only cleanup
- Remove `EI_REPLACE` / `EI_VERIFY` / `EI_ORIGINAL`, `src/hook/`, the launcher, `gameapi::`
  (import-table calls) and Rebirth detection (`FollowAddon`, site tables).
- x87: remove naked helpers, `__asm` and `float_control` pragmas; build for SSE2. For the
  precision-sensitive functions (damage and hit rolls, AI path costs, movement, terrain height,
  the delta-coded network floats) first write table-driven tests from values recorded in the
  hook era, and keep an exception only where plain C++ changes gameplay results.
- Convert the pure verify wrappers into unit tests with recorded inputs and outputs.

### Phase 5: layout and fixes
- Move files into the module layout above; split the multi-class "round" files.
- Introduce the fixes layer and move Rebirth's patches and our fixes into it.
- Write `docs/source-guide.md`: module map, class hierarchy, how to add a fix.

## Fixes layer

- One registry, `fixes/` (grown from today's `ei::rebirth` flags): every fix has a name, a source
  (`rebirth:<addon patch name>`, `ei:<our name>`), a description, a default, and the functions it
  touches. Settings come from one ini next to the exe; a preset reproduces Rebirth.
- In code a fix is a single, visible branch at the point of change:
  `if (fixes::On(Fix::RepairPrice)) { ... } else { ...original... }`. No fix logic inside
  unrelated helpers; no silent behaviour changes.
- Candidates for our own fixes: the 35 "original bug kept" comments in `src/` and the lists in
  `docs/standalone-cleanup.md` and the notes; each becomes a ticket with a decision (fix as an
  opt-in or leave).

## Compiler artefacts and the NaN audit

Two rules in the rewrite guide come from how VC6's x87 code behaves, not from Nival's intent. In
the standalone build they turn into review items:

- **NaN in comparisons (done, C16c; `docs/notes/cleanup-tooling.md` "NaN audit").** VC6 compiled `a == 0.0f`, `a < b` etc. as
  FCOMP / FNSTSW / TEST AH, where a NaN operand sets the "unordered" flags and so counts as
  *equal* (or *less*, depending on the test). The rewrites reproduced that with explicit
  `|| x87::IsNaN(...)` and `x87::CompareStatus` terms (about 116 sites); in the source the programmer
  simply wrote the comparison, and NaN was never meant to reach these places. They are plain
  comparisons now (a behaviour change for NaN only), with `EI_NAN_AUDIT` (`src/lib/nan_audit.h`)
  logging the first NaN per site in the verify and debug builds; each NaN that does show up is a
  real bug upstream (see the next item). Left: the `!(a >= b)`-style NaN-aware helpers (C16e).
- **NaN that actually happens (an original bug).** An object with a NaN view depth makes the unit
  sort's comparator treat `Less(x, x)` as true, and STLport's unguarded partition then walks off
  the array: the rare Rebirth crash in `docs/open-questions.md`. Fix candidate for the fixes
  layer: find where the NaN depth comes from while a new game loads, and/or guard the sort.
- **Unsigned 32-bit to float (`x87::FromUnsigned`, checked by `tools/check_x87.py`).** VC6
  converted with an exact FILD of a 64-bit value; modern MSVC targeting x87 adds 2^32 with an FADD
  that rounds at the game's 24-bit precision, so random words and tick counts came out slightly
  different. This is a porting hazard, not a game bug: SSE2 conversion in the standalone build is
  exact, so `FromUnsigned` becomes a plain cast.
- **`v != v` under `/fp:fast`.** Also a porting hazard (the compiler folds it to false); it goes
  away with the NaN audit above.

## Conventions (all cleanup tickets)

- Behaviour stays identical; a cleanup commit never mixes in a fix.
- One subsystem per ticket and per worker; a ticket touching another subsystem's header asks its
  owner (or the coordinator) first. Shared headers change additively only.
- Every ticket ends with: `build.cmd verify` and `release` clean, `check_stolen`, `check_x87`,
  `check_signatures` at 0, the artefact counts for the subsystem reported before/after, and the
  full scenario suite run centrally on both installs.
- Names: the original's where known (RTTI, strings, Rebirth's and the community's names), else
  descriptive; `CClassName` for game classes (Nival style), members `name_`, no Hungarian
  additions of our own.
- Keep the one-line address tag per function; move notes to `docs/notes/` rather than long
  comments in code.

## Subsystem tickets

Subsystems, in the order they were taken: 1 engine base (objects, managers, archives, streams: `core_*`, `base_manager*`, `chunk_*`, `model*`, `object*`),
2 world server / game flow / save-load (`world_*`), 3 AI map, paths, regions (`ai_*`), 4 units and combat (`unit_*`, `combat_*`), 5 scripts (`script_*`),
6 items, spells, databases, 7 effects, particles, lights, 8 interface (`ui_*`), 9 network (`net_*`), 10 sound, 11 resources, textures, files, 12 app,
input, options, startup, 13 library layer (`src/lib`), 14 tooling (the `originals.h` generator, artefact counts in `progress.py`). Their state is in
`docs/tickets.toml` (`python tools/tickets.py list`) and the `cleanup-*.md` notes; cleanup of a subsystem starts once it is mostly ported, so its
classes are seen whole.
