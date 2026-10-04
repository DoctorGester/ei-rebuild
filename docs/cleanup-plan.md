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

## Where we are (2026-10-04 baseline)

108k lines in 252 `.cpp` / 88 `.h` under `src/`, plus 196 verify files (34k lines). Measured
leftovers of the hook era:

| Artefact | Count | Becomes |
|---|---|---|
| `EI_ORIGINAL` forwarders | 3,765 sites, 2,655 distinct functions: 765 already rewritten (called through the original address), 731 library code, 1,159 game code not yet rewritten | direct calls / library layer / ported code |
| same function forwarded from several files | 538 functions | one declaration |
| `__declspec(naked)` | 3,885, of which 328 are x87/asm helpers (the rest forwarders) | plain C++ or a documented exception |
| `__asm` blocks | 233 | plain C++ |
| `#pragma float_control(precise)` | 83 files | removed where tests allow |
| `unknown_XXX` fields | 1,289 | named members or padding with a reason |
| `At<>` / `Field<>` raw-offset access | 1,569 / 617 (47 files define their own `At`) | members |
| `reinterpret_cast` | 4,186 | mostly gone with real types |
| `ei::VCall` slot calls | 581 | virtual calls |
| addresses of game data (`0x007xxxxx` etc.) | 2,294 distinct; 451 `constexpr uintptr_t kX = 0x00...` | ordinary globals, constants, class statics |
| `__fastcall Fn(T* self, void*)` hook shims | 174 | member functions |
| `*At` view structs over raw memory | 57 | the real class |
| struct names defined in more than one file | 72 | one definition in a header |
| anonymous-namespace blocks (many hold local layouts) | 563 | shared headers / real helpers |
| `mfc::` / `stlp::` / `gamecrt::` / `gameapi::` uses | 1,400 / 148 / 500 / 140 | library layer |
| `EI_REPLACE` registrations | 2,094 | removed |

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

- **NaN in comparisons (30 sites, `x87::IsNaN`).** VC6 compiled `a == 0.0f`, `a < b` etc. as
  FCOMP / FNSTSW / TEST AH, where a NaN operand sets the "unordered" flags and so counts as
  *equal* (or *less*, depending on the test). Our rewrites reproduce that with explicit
  `|| x87::IsNaN(...)` terms. In the source the programmer simply wrote the comparison; NaN was
  never meant to reach these places. Sites today: `ai_map.cpp` (line-step length),
  `ai_motion.cpp` (turn sign, 2), `ai_movement.cpp` (radius test), `particle_callbacks.cpp`
  (13: zero tests on sizes, bends, alphas, lengths), `sound_manager.cpp` (silent height),
  `terrain_sector.cpp` (min/max height), `ui_hud.cpp`, `ui_hud_frame.cpp`, `ui_hud_portraits.cpp`,
  `ui_item_info.cpp`, `ui_item_view.cpp`, `ui_objectives.cpp` (4), `world_units.cpp` (water level).
  Standalone: write the plain comparison, and in debug builds log when a NaN shows up there; each
  NaN that does show up is a real bug upstream (see the next item).
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

Status per subsystem; workers pick the top unclaimed row. "Ported" is today's share of the
subsystem's game code; cleanup of a subsystem should start once it is mostly ported, so its
classes are seen whole.

| # | Subsystem | Main files today | Ported | Phase 1 | Phase 2 | Owner |
|---|---|---|---|---|---|---|
| 1 | Engine base: objects, managers, archives, streams | `core_*`, `base_manager*`, `chunk_*`, `model*`, `object*` | ~70% | todo | todo | |
| 2 | World server, game flow, save/load | `world_*` (22 files) | ~79% | todo | todo | |
| 3 | AI map, paths, regions | `ai_*` (13 files) | high | todo | todo | |
| 4 | Units and combat | `unit_*`, `combat_*` | ~37% | later | later | |
| 5 | Scripts | `script_*` (13 files) | ~55% | todo | todo | |
| 6 | Items, spells, databases | `item*`, `spell*`, `class_templates`, `party_records` | ~34% | later | later | |
| 7 | Effects, particles, lights | `particle_*`, `world_effect*`, `spell_effect_*`, `lightning` | partial | later | later | |
| 8 | Interface | `ui_*` (15+ files) | ~19-41% | later | later | |
| 9 | Network | `net_*` (12 files) | ~70% | todo | todo | |
| 10 | Sound | `sound_*` | partial | later | later | |
| 11 | Resources, textures, files | `resource*`, `texture*` | partial | later | later | |
| 12 | App, input, options, startup | `app*`, options | ~35% | later | later | |
| 13 | Library layer (`src/lib`) | `crt`, `mfc`, `stlp`, `gameapi` | n/a | n/a | todo | |
| 14 | Tooling: `originals.h` generator, artefact counts in `progress.py` | `tools/` | n/a | todo | n/a | |

Start with 14 (tooling), then 2, 3, 9 and 1: they are the most complete and the most depended on.
