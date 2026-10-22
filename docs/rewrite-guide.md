# Rewrite guide

How to write and change the game's code in this repo (ports, cleanup and fixes). Read it fully before
starting; every rule here comes from a bug we already hit.

Our code runs only in the standalone image (docs/notes/standalone.md), which contains no game.exe code.
game.exe runs only as the original game, for the harness's references (goldens, function vectors), and
the harness patches it by address there. Nothing of ours shares memory or calls with game.exe's code.

## Setup

- Ghidra (with game.exe analysed) is reachable through the `mcp__ghidra__*` tools, or directly at
  `http://127.0.0.1:8080` (`/decompile_function?address=0x...`, `/disassemble_function?address=0x...`,
  `/xrefs_to?address=0x...&limit=N`, `/get_function_by_address`, `/strings?filter=`, POST
  `/rename_function_by_address` with `function_address` and `new_name`).
- The original exe (read only, never modify anything under `F:\games`):
  `F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe`. Image base 0x400000; .text and .rdata have
  file offset == VA - 0x400000.
- Build: `build.cmd verify` and `build.cmd release` (PowerShell: `cmd /c "<repo>\build.cmd verify"`).
  Both must build without errors or warnings in files you touched.
- Games only through `python tools/work.py test <ID> [run_all args]` (docs/worker-brief.md). You may add a
  scenario under `tests/scenarios/` that exercises your area (script commands via `@...`, checks via
  `check` and `waitfor`); its golden is recorded centrally.
- Run `python tools/check_x87.py` (after `build.cmd release`) and `tools\check_signatures.cmd` (after
  `build.cmd verify`) before handing back, and `python tools/check_stolen.py` after adding a vector
  recorder. check_signatures compares the stack arguments of registered, forwarded and recorded
  declarations with the original's RET n: a recorder runs inside game.exe, and a registration or a
  forwarder is reached with the original's calling convention (rule 3).

## Where code goes

- `src/game/<area>.h/.cpp`: rewritten game code. Provisional names; comment each function with its
  original address. Classes are normal C++: real virtual functions, members of their real types, no
  hand-built vtables and no layout asserts. No game.exe code shares our objects, so nothing needs the
  original memory layout; goldens don't depend on it either. Keep game.exe's byte layout only where
  bytes leave the process (save files, network packets) or come from game.exe's data image (until
  C18/C25 finish), and say so where you do. The hand-built tables still in the code (`static void*
  Vtable()`, `ei::SetVtable`, the slot templates of engine_object_class.h, mfc_slots.h and
  compiled_vtable.h) are C28's to convert: add no new ones.
- `src/lib/`: shared compatibility code. `crt.h` (the game's own CRT: operator new/delete, string
  functions, file I/O), `mfc.h` (CString, CFile, CFileFind, CArray, CList, CTypedPtrList, CPlex),
  `containers.h` (the engine's aligned arrays), `stlp.h` (STLport
  string/vector/hash_map on the game's node allocator), `gameapi.h` (the Win32 imports; generated: the
  ones the harness or the image routes go through the image's import slots). C14a-d move the code to
  standard containers and strings with scripted rewrites. Add to these only when needed, in small
  separate additions.
- `tests/vectors/`: function vectors (recorders, replay checks; see Verification).
- Shared headers others use: `src/game/engine_object.h` (CEngineObject virtual interface),
  `src/lib/vecmath.h` (Vector2/3/4, Quaternion, Matrix4 and inline helpers with named sum
  groupings, docs/notes/vector-math.md; `maths.h` includes it), `ai_map.h`, `resource*.h`,
  `terrain.h`, `base_manager.h`.

## The rules

1. **Read the disassembly, not just the decompiler.** The Ghidra MCP disassembly listing can also
   drop instructions (it omitted an FNSTSW in 006C1C00, making a min-select look like a bit test):
   when a sequence looks odd, check the raw bytes in game.exe (capstone) before reproducing it. Ghidra misreads calling conventions, misses
   register arguments and drops return values.
2. **Return values:** check what the original leaves in EAX/AL/ST0 and which callers read it: that is
   the function's result. Functions that set all of EAX to 0/1 return `BOOL` (int); ones that set
   only AL return `bool`. A float left unrounded in ST0 is rule 7's.
3. **Registrations and calling conventions.** A rewrite is an ordinary function or member, called by
   name. Write `EI_REWRITTEN(addr, Function)` next to it: it registers nothing and keeps the function
   in the tools' function-to-original map (check_fp_fold, check_signatures, coverage, progress; C20d).
   Register it at its address (`EI_REPLACE` / `EI_REPLACE_METHOD`) only while something still reaches
   that address: a code cell of game.exe's data image (a static initialiser, a CRT / MFC table) or a code
   literal (`python tools/registration_readers.py` names the readers; C20 retires them). Such a function keeps the original's calling convention and declares every stack
   argument the original's `RET n` pops, read or not (five loaders declared with one argument too few
   left 4 bytes on the caller's stack and crashed the game at startup). `build.cmd` runs
   `tools/check_registrations.py` (one registration per address).
4. **Virtual methods are never registered**: a pointer to a virtual member is a dispatch thunk, which
   jumps through the object's table back into itself (`build.cmd` runs `tools/check_thunks.py`, which
   fails on an `EI_REPLACE_METHOD` naming a method that is virtual, or overrides one, by name). The
   tables are the compiler's (C19), so nothing reaches a slot's address: a virtual is `EI_REWRITTEN`.
   The `...Hook` shims and `...Impl` bodies left from registering slots are C28's to fold into the
   virtuals. Constructors and destructors are C++ ones, tagged `EI_REWRITTEN_SPECIAL(addr, X::X)` /
   `(addr, X::~X)` (docs/notes/standalone.md, "Real classes"). Do not give a non-virtual method the same name and signature as a base-class virtual
   (`CEngineObject::Create`, `Destroy`, `Release`, `Load`, `Save`, `GetName`, ...): it silently
   becomes an override.
5. **No forwarders.** Every function of game.exe is ported: call it by name (include its header, or
   declare it there). src has no `EI_ORIGINAL` (C29; `check_cleanup` keeps it at 0). Where the harness
   needs to change what our code does, it sets a named seam our code exposes (the fixed ISAAC seed:
   `IsaacRandom::SetSeedHook`; the traced draws: `IsaacRandom::SetDrawObserver`,
   `gamecrt::SetRandObserver`), never an address. Only tests/verify's game.exe side and the vector
   recorders call game.exe's code by address (`tests/verify/harness_original.h`).
6. **Memory and CRT:** allocate and free game objects with `gamecrt::operator_new` / `operator_delete`
   (src/lib/crt.h; other code frees them there), containers through `stlp::` / `mfc::` until C14 moves
   them to standard types. Call the CRT functions whose game behaviour differs from the host's through
   `gamecrt::` (C26 decides which those are) and Win32 through `gameapi::`.
7. **Floating point.** Write float maths as plain C++ (`float`/`double`, the standard maths functions); do NOT hand-copy x87 instruction
   sequences (no new naked FPU helpers, `__asm` or `#pragma float_control`). The x87 code in the exe is a VC6 artifact, the game runs its main
   thread at 24-bit precision anyway, and an SSE2 build is planned (C16d, docs/cleanup-plan.md). The existing hand-copied helpers stay
   until the cleanup removes them. When an integer or a branch follows from a float that sits on a rounding edge, a rare difference there is
   expected: say so in the notes rather than copying the FPU sequence.
   - The build is `/arch:IA32 /fp:precise` (x87): the grouping written is the grouping compiled, and a `static_cast<float>`, a float local or a float
     return is rounded to float (a store, which changes nothing at the main thread's 24 bits). `/fp:fast` had distributed and refolded sums
     (FearMode's `(30x + d) * (1/31)` became `30/31 x + d/31` and settled on another fixed point) and kept float locals unrounded (unrounded atan2
     angles flipped a facing quaternion's sign at 180 degrees, 1 call in 300k); round with `static_cast<float>` or a float variable.
   - Under precise, `std::sin / cos / atan2 / exp / log / log10` call the modern CRT (SSE2 code paths): use `x87::fsin / fcos /
     fpatan` (`vecmath::Atan2`) `/ fexp / flog / flog10`, the instructions the original's VC6 code inlines. `std::sqrt` is FSQRT at the
     precision control on this target (C16, docs/notes/cleanup-tooling.md "SSE2 build"); give it a double: `std::sqrt(static_cast<double>(x))` for a float expression, as `std::sqrt(float)` is `sqrtf`. A product of two constants that the
     original computes at run time (FLD; FMUL / FDIV) is rounded to 24 bits there, while the compiler folds it at double precision: write it as the
     float (`static_cast<float>(kPi * kPerDegree)`, checking the rounding) or keep the operation; `tools/check_fp_fold.py` (build.cmd) finds such
     double constants (a helper the compiler did not inline is compared with the originals of the registered functions that call it, C17). The build inlines at /Ob2 (R16c5), so a constant behind a helper function (`Pi() * FaceStep()`) folds too.
   - Rounding mode matters: FISTP rounds to nearest (use `x87::RoundToInt`, float or double, or `x87::fistp`, float), a C++ cast
     truncates, `_ftol` truncates. Not `std::lrint`: the UCRT's reads the FP environment on every call, follows MXCSR's rounding
     rather than the x87 one, and gives 0 with errno EDOM where FISTP gives 0x80000000 (SV10j).
   - The game's main thread runs at 24-bit precision (control word 007F, set by Direct3D; measured at every frame boundary in both installs; other
     threads run at 53 bits). A float the original returns unrounded in ST0 must not be rounded on return: declare the function `double` and let
     callers round when they store (see `CItemWeapon::RollDamage`); it is exact at 24 and 53 bits and callers that keep using ST0 need it
     (docs/notes/rebirth.md).
   - Convert unsigned 32-bit values (random words, tick counts) with `x87::FromUnsigned`, never `static_cast<double>`: MSVC adds 2^32 with an FADD
     that rounds; `tools/check_x87.py` checks this. Where detecting NaN is the code's purpose, test it with `x87::IsNaN` (a bit test; `v != v`
     was folded to false under /fp:fast and the standalone's compiler flags may change again).

8. **Comparisons are plain C++ (the NaN audit, C16c).** Decode VC6's FCOM / FNSTSW / TEST AH to the comparison the
   source had: C3 (`TEST AH,0x40`) is `==`, C0 (`TEST AH,1`) `<`, C0|C3 (`TEST AH,0x41`) `<=` (its negation `>`), C0 as a
   select mask `a < b ? x : y`. The original counted a NaN operand as equal and less; the plain comparison follows IEEE, a
   behaviour change only for NaN (none reaches these places in the suite). Next to a comparison whose result changes for a
   NaN, `EI_NAN_AUDIT(values...)` (`lib/nan_audit.h`) logs the first NaN per site to ei_hook.log in the verify and debug
   builds. Where an equally plain form keeps the original's NaN result (`a > b`; `x < low ? x : low` for a bound a NaN never
   replaces; `if (!(gap > limit)) break;` for a loop a NaN must still end), prefer it. addon.dll was built by an IEEE
   compiler (its tests read C2): its status tests are exact plain comparisons. A select whose C0 mask picks `ifLess` is
   `a >= b ? otherwise : ifLess` (exact for NaN). The audit is complete (C16c-C16g; docs/notes/cleanup-tooling.md
   names the few original tests kept and why).
9. **Rebirth's patches are flags.** The standalone image never loads Rebirth's addon.dll or
   EI_Plugin.asi; what they change in game.exe is our code's to reproduce, inside our functions behind
   flags; a patched function is called by name like any other. The image keeps Rebirth's data constants in memory as addon writes them (00742B08,
   the interface's 800-pixel layout width, becomes its widescreen width in WideScreen's startup
   action): read such constants from memory, as the original does, instead of compiling their values
   in. A rewrite may reproduce a Rebirth patch behind its flag (`ei::rebirth::Enabled(Patch::<name>)`,
   `include/ei/rebirth.h`; what each patch does is in `docs/notes/rebirth.md` and
   `config/rebirth_patches.csv`, whose status column records which rewrite carries it).
   Flags are off by default; with a flag off the code must behave exactly as the original; the image's
   Rebirth goldens check the flags on (config/ei_rebirth_standalone.ini).
10. **Keep the original's bugs** (leaks, odd messages) and note them under "Original bugs kept on
    purpose" in your notes file.

## Clean code

New and changed code is written the way the cleanup plan (docs/cleanup-plan.md) wants it, so it
does not need a second pass:
- fields are members of the class's one declaration (in its header), not `At<>`/`Field<>` offsets or `*At` view structs; add the members you need;
- classes are normal C++ ("Where code goes"): real virtual functions, no hand-built tables, no layout
  asserts unless the bytes leave the process or come from game.exe's data image;
- virtual calls go through declared and named virtual functions, not `VCall` and not a placeholder
  `SlotNN()` (name the slot when you call it);
- no byte arithmetic on pointers with literal offsets (`reinterpret_cast<uint8_t*>(p) + 0x..`);
- game globals are named variables (C18, `tools/named_data.py`), not literal addresses in the code;
  `EI_REWRITTEN`, the registrations, the forwarders and `EI_RECORD` keep their address;
- no `__asm`, no `#pragma float_control` (rule 7);
- no new forwarders (rule 5);
- containers use the shared templates: `mfc::CArray<T>`, `mfc::CList<T>`, `mfc::CTypedPtrList<T>`
  (`src/lib/mfc.h`), `lib::AlignedArray<T>` (the engine's 32-byte aligned arrays,
  `src/lib/containers.h`) and `stlp::` (`src/lib/stlp.h`). Do not declare a container layout per
  file (`struct FooArray { void* vtable; T* data; int size; ... }`, `struct ListNode { next, prev }`)
  and do not hand-inline SetSize / RemoveAt / Add: call the template's member. Which original addresses belong
  to which template: `docs/notes/core.md`, "Shared containers". stlp::list's and vector's
  members are templates on the element policy (`SectorList::Erase<stlp::HeldObject>(node)`,
  `v.InsertOverflow<Abi::Element>(...)`): call them on the typed container rather than casting it
  to its `ListAbi` / `VectorAbi`;
- parameters and fields carry their types (C21): no `void*` that every caller and the body cast;
  raw blocks come typed from `gamecrt::operator_new_as<T>()` / `operator_new_zeroed_as<T>()` /
  `operator_new_array<T>(count)`, `operator_new_bytes_as<T>(bytes)` (a block shorter or longer
  than T: a figure strip, a CString's data) and `stlp::allocate_as<T>(count)` (`src/lib/crt.h`,
  `src/lib/stlp_alloc.h`), not a `static_cast` at each allocation (tools/progress.py counts the
  pointer casts; check_cleanup does not); objects of a manager come typed from
  `CBaseManager::CreateObjectAs<T>` / `FindObjectAs<T>`, item records by kind from
  `itemdb::RecordAs<T>`, and a manager's context is a `CManagerContext` (CWorld, CParty; its
  slots 19, 20 and 22 return `CMapObjectManager*`, `CPlayerManager*` and `CSpellManager*`). A typed
  stlp::vector is resized with `v.ResizeDefault<Element>(n)`, not through a `VectorAbi` downcast;
  a window surface is drawn on through `CUiSurface::GetDC` / `ReleaseDC` / `DrawLine`, not a
  per-file view struct derived from it. A window class that shares another's layout derives from
  it (CUiItemStrip : CUiItemPanel, CMpHeroScreen : CItemScreen) instead of holding a `CUiWindow
  window` member and being punned; the item panels' owner_ is the CItemScreen. A container's input
  side is `CUiContainer::ContainerInput()`. An item screen's slots 42-54 are called through
  `CItemScreen::Virtual*` (VirtualHeroRecord, VirtualPrice, ...), a panel's Build through
  `CUiWindow::VirtualBuild`, not a `VirtualSlot` with a local function type; three pointers that
  are an STLport vector are an `stlp::vector<T>` member, called with the element policy
  (`entries_.InsertOverflow<recordvec::SaveEntryElement>(...)`). The interface's active screen is
  `CInterface3D::ActiveWindowAs<Screen>()`; a counted helper a world holds is a `RefPtrTo<T>` field
  read with `Get()`; the new connections a player source hands over are a `JoinedLinks`
  (`mfc::CTypedPtrList<netlink::NetLink>`); an inlined free of a 12-byte node is
  `stlp::PushSmallNode(node)`; a map whose element policy is not the default is subscripted with a
  named one (`headers_.Subscript<lib_trees::LinkHeaderElement>(name)`); a window's handled flag is
  its `inputHandled_` member. A counted object is tested with `IsLive(object)` (core_refcount.h: set and
  not detached), not a per-file `Live(const void*)`; the server session's connections are `Connection*`
  and its player records `PlayerMessage*` (FindGuestConnection, FindPlayer, Kick, Ban); a file
  written by a transform slot is an `mfc::CFileVirtuals*`, and so is a chunk's `Chunk::File()`. A use order's spots
  are a `UseSpotArray` (map_object.h, `mfc::CArray<Vector3>`: slot 44, the levers, FindUseSpot); a pair message's
  connection is a `RefPtrTo<Connection>`; the changed variables are an `stlp::vector<VariableNode*>`; a map object's
  slots are called on the typed object or through `ObjectSlots(object)`, not a hand-written vtable call or a cast per file.
- one typed view instead of a cast per accessor (C15): an object embedded in another and built by
  the owner's code is a `lib::InPlace<T>` member, or a `lib::AbstractInPlace<T>` when T has pure
  virtual slots (`src/lib/static_object.h`), not a byte array with a casting accessor. A counted
  pointer the game's helpers take by address is a field of the holder type (`CountedRef<T>`,
  `HeldTexture`, `HeldDevice`, `HeldObject<T>`, `AIRefTo<T>` (engine_object.h): reads see the object, a plain store counts nothing),
  not a `T*` field and a `Holder::Of(&field)` cast; a record that starts with another struct's
  fields derives from it (UnitAction : AIOrder, SectorBox : ClipBounds) and the functions that
  read only the prefix take the base.
`build.cmd` runs `tools/check_cleanup.py`: per file, the counts of these patterns may only go down
(config/cleanup_baseline.csv; a new file starts at zero). After a cleanup ticket lowers them, run
`python tools/check_cleanup.py --update`.

## Verification

A change is held to the original's behaviour by the two checks below. Neither depends on the heap or
on memory layout, so a change of a class's layout is checked like any other change.

- **Goldens** (tests/golden, docs/notes/standalone-verification.md): a scenario recorded from the
  original game (`--no-hooks`) and replayed on the image (`work.py test <ID> --golden`), its
  state digest and saves compared field by field. Reach the function from a scenario that has a
  golden, or add one under `tests/scenarios/` (its golden is recorded centrally).
- **Function vectors** (tests/vectors): a recorder (`EI_RECORD(addr, Recorder, stolenBytes,
  originalPtr)` in `tests/vectors/record_<family>.cpp`) samples the original's calls in a
  `--no-hooks` run with `EI_VECTORS=1`, and `ei_vectors.exe` (build.cmd) replays them against ours
  without the game. For pure functions and ones that only write an output object. `stolenBytes`
  must cover whole instructions at the function start, at least 5, with no relative operands (no
  CALL/JMP/Jcc in them); `python tools/check_stolen.py` checks every recorder.

## Notes

Record in `docs/notes/<area>.md`:
- anything you leave tied to game.exe (an address, a byte layout, and why), also in
  docs/standalone-cleanup.md;
- original bugs you kept;
- functions you skipped and why;
- how each rewrite is checked (the scenario with a golden that reaches it, or its vectors).

Commit on your branch with clear messages. End commit messages with the co-author line used in this
repo's history.
