# Rewrite guide

How to rewrite a function of game.exe in this repo. Read it fully before starting; every rule here
comes from a bug we already hit.

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
- Do not launch the game. Integration runs happen centrally, one game at a time, with the harness
  (`harness.cmd tests\scenarios\<scenario>.txt`, see README). You may add a scenario under
  `tests/scenarios/` that exercises your area (script commands via `@...`, checks via `check` and
  `waitfor`); it will be run at integration.
- Run `python tools/check_x87.py` (after `build.cmd release`) and `tools\check_signatures.cmd` (after
  `build.cmd verify`) before handing back, and `python tools/check_stolen.py` after adding a vector
  recorder. check_signatures compares every hooked, forwarded or recorded declaration's stack
  arguments with the original's RET n.

## Where code goes

- `src/game/<area>.h/.cpp`: rewritten game code. Provisional names; comment each function with its
  original address. Classes mirror the original memory layout exactly, with `static_assert`s on sizes
  and offsets. Unknown bytes are `unknown_XXX` fields.
- `src/lib/`: shared compatibility code. `crt.h` (the game's own CRT: operator new/delete, string
  functions, file I/O), `mfc.h` (CString, CFile, CFileFind, CArray, CList, CTypedPtrList, CPlex),
  `containers.h` (the engine's aligned arrays), `stlp.h` (STLport
  string/vector/hash_map on the game's node allocator), `gameapi.h` (Win32 through the game's import
  table; generated). Add to these only when needed, in small separate additions.
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
2. **Return values:** check what the original leaves in EAX/AL/ST0 and what callers read.
   - A "void" setter that ends with `MOV EAX,ECX` returns `this` (the Vector3 constructor crashed the
     game when we returned void).
   - Functions that set all of EAX to 0/1 return `BOOL` (int); ones that set only AL return `bool`.
3. **Calling conventions:** `__thiscall` methods become C++ members registered with
   `EI_REPLACE_METHOD(addr, Class::Method)`. `__fastcall`/`__stdcall`/`__cdecl` free functions use
   `EI_REPLACE(addr, Function)`. Count `RET n` to confirm stack arguments, and declare every one of
   them even when the function never reads it (five loaders declared with one argument too few left
   4 bytes on the caller's stack and crashed the game at startup).
4. **Virtual methods** cannot be registered by member pointer (that yields a vtable thunk). Declare
   the virtuals in vtable order, implement the body, and register a shim:
   `bool __fastcall FooHook(Cls* self, void*, args...) { return self->Cls::Foo(args...); }`.
   Do not give a non-virtual method the same name and signature as a base-class virtual
   (`CEngineObject::Create`, `Destroy`, `Release`, `Load`, `Save`, `GetName`, ...): it silently becomes
   an override, and the hook lands on a dispatch thunk that loops forever. The installer now refuses
   thunks and logs "virtual-call thunk". `build.cmd` also runs
   `tools/check_thunks.py`, which fails the build when an `EI_REPLACE_METHOD` names a method that is
   virtual, or overrides one, in its class or a base class (matched by name, from the headers).
5. **Not-yet-rewritten callees** are forwarders: `__declspec(naked) R Cls::Fn(args) EI_ORIGINAL(0xADDR)`.
   Look in `src/game/originals_<area>.h` first and use the shared declaration (`...Original`) when
   the function has one; when it has none and a second file needs it, move it there instead of
   declaring another forwarder. Decide its signature from the disassembly (RET n, what callers read
   in EAX/AL/ST0). `build.cmd` runs `tools/check_forwarders.py`, which fails on any function newly
   forwarded from more than one file. A rewritten function is called directly (rule 9), never
   through a forwarder: `python tools/direct_calls.py --report` lists the forwarders to rewritten
   code and `--apply --scope <files>` rewrites their call sites (C13; `check_cleanup` ratchets the
   `EI_ORIGINAL` count per file). A few stay forwarders on purpose: an entry the harness replaces
   (the fixed ISAAC seed 0049F660, tests/verify `JumpTo` / `JumpFrom`), Rebirth-patched callees,
   vector recorders and the rows of config/direct_calls_skip.csv.
   Naked functions cannot have members with constructors/destructors in the class layout path; use raw
   pointers in layouts where needed (see `mfc::CFile`).
6. **Memory and CRT:** allocate and free with `gamecrt::operator_new/operator_delete` (the game's heap),
   STL containers through `stlp::` (the game's node pool), MFC lists through `mfc::CTypedPtrList`.
   Call the game's CRT (`gamecrt::tolower`, `strnicmp`, `sprintf`, ...) and Win32 through `gameapi::`.
7. **Floating point.** Write float maths as plain C++ (`float`/`double`, the standard maths functions); do NOT hand-copy x87 instruction
   sequences (no new naked FPU helpers, `__asm` or `#pragma float_control`). The x87 code in the exe is a VC6 artifact, the game runs its main
   thread at 24-bit precision anyway, and the standalone build will use SSE2 (docs/cleanup-plan.md). The existing hand-copied helpers stay
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
   EI_Plugin.asi; what they change in game.exe is our code's to reproduce. (Until S21e rewrites also
   ran inside game.exe, where the installer skipped functions addon.dll had patched and rewritten
   callers had to reach them through their original addresses; C13 makes those calls direct;
   tools/direct_calls.py still leaves Rebirth-patched callees and shims as forwarders and lists
   them.) The image keeps Rebirth's data constants in memory as addon writes them (00742B08,
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
- fields are members of the class's one declaration (in its header, with `static_assert`s on
  offsets), not `At<>`/`Field<>` offsets or `*At` view structs; add the members you need;
- virtual calls go through declared and named virtual functions, not `VCall` and not a placeholder
  `SlotNN()` (name the slot when you call it);
- no byte arithmetic on pointers with literal offsets (`reinterpret_cast<uint8_t*>(p) + 0x..`);
- game globals are named (`constexpr` address in one header, or an accessor), not literal
  addresses in the code; hook registrations (`EI_REPLACE`, `EI_ORIGINAL`, `EI_RECORD`) keep theirs;
- no `__asm`, no `#pragma float_control` (rule 7);
- forwarders come from `src/game/originals_<area>.h` (rule 5);
- containers use the shared templates: `mfc::CArray<T>`, `mfc::CList<T>`, `mfc::CTypedPtrList<T>`
  (`src/lib/mfc.h`), `lib::AlignedArray<T>` (the engine's 32-byte aligned arrays,
  `src/lib/containers.h`) and `stlp::` (`src/lib/stlp.h`). Do not declare a container layout per
  file (`struct FooArray { void* vtable; T* data; int size; ... }`, `struct ListNode { next, prev }`)
  and do not hand-inline SetSize / RemoveAt / Add: call the template's member, or forward to the
  original instantiation with the template as its parameter type. Which original addresses belong
  to which template: `docs/notes/core.md`, "Shared containers".
`build.cmd` runs `tools/check_cleanup.py`: per file, the counts of these patterns may only go down
(config/cleanup_baseline.csv; a new file starts at zero). After a cleanup ticket lowers them, run
`python tools/check_cleanup.py --update`.

## Verification

The standalone image is the product (docs/notes/standalone.md section 5); nothing runs our code
inside game.exe, and the verify wrappers that compared each rewrite with the original in game were
deleted in S21d (git history has them). A rewrite is held to the original's behaviour by:

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
- forwarders you added (address, what they are) and anything else the standalone build must replace;
- original bugs you kept;
- functions you skipped and why;
- how each rewrite is checked (the scenario with a golden that reaches it, or its vectors).

Commit on your branch with clear messages. End commit messages with the co-author line used in this
repo's history.
