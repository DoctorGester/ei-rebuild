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
- Run `python tools/check_stolen.py`, `python tools/check_x87.py` (after `build.cmd release`) and
  `tools\check_signatures.cmd` (after `build.cmd verify`)
  before handing back. The second compares every hooked, forwarded or wrapped declaration's stack
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
- `tests/verify/verify_<area>.cpp`: verify wrappers. `tests/verify/verified.txt` is updated centrally.
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
   - build.cmd runs `tools/check_returns.py`, which fails when a replacement declared void has game
     call sites reading EAX after the call. It cannot see calls through vtable slots.
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
   thunks and logs "virtual-call thunk". It checks every entry for the address, so a verify wrapper
   is refused too when the method's own registration is a thunk; the wrapper's call to the method
   would dispatch through the vtable in the same way. `build.cmd` also runs
   `tools/check_thunks.py`, which fails the build when an `EI_REPLACE_METHOD` names a method that is
   virtual, or overrides one, in its class or a base class (matched by name, from the headers).
5. **Not-yet-rewritten callees** are forwarders: `__declspec(naked) R Cls::Fn(args) EI_ORIGINAL(0xADDR)`.
   Look in `src/game/originals_<area>.h` first and use the shared declaration (`...Original`) when
   the function has one; when it has none and a second file needs it, move it there instead of
   declaring another forwarder. Decide its signature from the disassembly (RET n, what callers read
   in EAX/AL/ST0). `build.cmd` runs `tools/check_forwarders.py`, which fails on any function newly
   forwarded from more than one file. A rewritten function is called directly, except Rebirth-patched
   ones (rule 9) and calls that must keep running through a verify wrapper.
   Naked functions cannot have members with constructors/destructors in the class layout path; use raw
   pointers in layouts where needed (see `mfc::CFile`).
6. **Memory and CRT:** allocate and free with `gamecrt::operator_new/operator_delete` (the game's heap),
   STL containers through `stlp::` (the game's node pool), MFC lists through `mfc::CTypedPtrList`.
   Call the game's CRT (`gamecrt::tolower`, `strnicmp`, `sprintf`, ...) and Win32 through `gameapi::`.
7. **Floating point.** Since 2026-10-04: write float maths as plain C++ (`float`/`double`, the
   standard maths functions); do NOT hand-copy x87 instruction sequences (no new naked FPU helpers,
   `__asm` or `#pragma float_control`). The x87 code in the exe is a VC6 artifact, the game runs its
   main thread at 24-bit precision anyway, and the standalone build will use SSE2 (docs/cleanup-plan.md).
   Verify float results with `ei::verify::CheckNear` (a few ULPs), not the bitwise `Check`. When an
   integer or a branch follows from a float that sits on a rounding edge, a rare mismatch there is
   expected: say so in the wrapper and the notes rather than copying the FPU sequence. Rounding mode
   still matters: FISTP rounds to nearest (use `x87::fistp` or `std::lrint`), a C++ cast truncates,
   `_ftol` truncates. The existing hand-copied helpers stay until the cleanup removes them.
   The build is `/arch:IA32 /fp:precise` (x87; since SV7d1a, before that `/fp:fast`): the grouping
   written is the grouping compiled, and a `static_cast<float>`, a float local or a float return is
   rounded to float (a store, which changes nothing at the main thread's 24 bits). `/fp:fast` had
   distributed and refolded sums (FearMode's `(30x + d) * (1/31)` became `30/31 x + d/31` and
   settled on another fixed point, SV7d1) and kept float locals unrounded (ticket X3: unrounded
   atan2 angles flipped a facing quaternion's sign at 180 degrees, 1 call in 300k); the volatile
   float store helpers and `float_control(precise)` pragmas written against that are redundant now.
   Under precise, `std::sqrt / sin / cos / atan2 / exp / log / log10` call the modern CRT (SSE2 code
   paths): use `x87::fsqrt / fsin / fcos / fpatan` (`vecmath::Atan2`) `/ fexp / flog / flog10`,
   the instructions the original's VC6 code inlines. A product of two constants that the original
   computes at run time (FLD; FMUL / FDIV) is rounded to 24 bits there, while the compiler folds it
   at double precision: write it as the float (`static_cast<float>(kPi * kPerDegree)`, checking the
   rounding) or keep the operation; `tools/check_fp_fold.py` (build.cmd) finds such double constants.
   The rules below about NaN tests and unsigned conversion still apply: they are about getting the
   same results at all, not the last bit. (Earlier text kept for the existing code:) the old rule was
   to keep the original's order of operations exactly and copy floats the original copies as dwords
   with `memcpy`.
   The game's main thread runs at 24-bit precision (control word 007F, set by Direct3D; measured at
   every frame boundary in both installs; other threads run at 53 bits). Still, a float the original
   returns unrounded in ST0 must not be rounded on return: declare the function `double` and let
   callers round when they store (see `CItemWeapon::RollDamage`); it is exact at 24 and 53 bits and
   callers that keep using ST0 need it. The verify build logs any wrapped function it sees running
   above 24 bits ("FPU precision ..." in ei_hook.log, listed in harness_report.txt); see
   docs/notes/rebirth.md.
   Convert unsigned 32-bit values (random words, tick counts) with `x87::FromUnsigned`, never
   `static_cast<double>`: MSVC adds 2^32 with an FADD that rounds; `tools/check_x87.py` checks this.
   Test NaN with `x87::IsNaN` (a bit test; `v != v` was folded to false under /fp:fast and the
   standalone's compiler flags may change again). The `!(a > b)` idioms below keep their NaN
   behaviour (MSVC emits FCOMP; FNSTSW; TEST AH,5/0x41; JNP).
8. **x87 compare semantics:** `FCOM; TEST AH,0x41; JNZ` means "jump if <= or NaN"; `TEST AH,1` means
   "less or NaN". Write `!(a > b)` / `!(a >= b)` so NaN behaves the same.
9. **Mods patch the original in memory.** The installer compares each function's whole body with the
   exe on disk and skips patched ones (they install only in the vanilla launch). When rewritten code
   calls a function that a mod patches, call it through its original address (an `EI_ORIGINAL`
   forwarder), not directly, so the mod's patch still runs. `docs/rebirth-patched-functions.txt`
   lists every function Rebirth patches; check it for each callee you call directly. Rebirth also
   overwrites some data constants in the exe at startup (00742B08, the interface's 800-pixel layout
   width, becomes its widescreen width): read constants from the exe's memory, as the original
   does, instead of compiling their values in. (The UI
   scaling functions are on it; calling our copies made `CUiWindow::RegionAt` disagree under Rebirth.)
   A rewrite may reproduce a Rebirth patch behind its flag (`ei::rebirth::Enabled(Patch::<name>)`,
   `include/ei/rebirth.h`; what each patch does is in `docs/notes/rebirth.md` and
   `config/rebirth_patches.csv`). Declare it with `EI_REBIRTH_IMPLEMENTS(addr, Patch::<name>)`: the
   function is then hooked in a Rebirth launch too, its wrapper comparing with the patched original.
   Flags are off by default; with a flag off the code must behave exactly as the original.
   `tools/check_patched_calls.py` (build.cmd, both builds) scans ei_hook.dll for direct call / jmp sites of our
   rewrite of any address in that list and fails on them; the reviewed exceptions (the script parser /
   evaluator family behind one flag, calls the original inlines) are in `tools/check_patched_calls_allow.txt`.
   It does not see calls the compiler inlined: for a small patched function, grep the sources for its name.
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
  addresses in the code; hook registrations (`EI_REPLACE`, `EI_ORIGINAL`, `EI_VERIFY`) keep theirs;
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

Every replacement gets a verify wrapper in `tests/verify/`, registered with
`EI_VERIFY(addr, Wrapper, stolenBytes, originalPtr)`:

- **Pure functions** (no side effects): call the original through the trampoline and yours, compare
  bit for bit with `ei::verify::Check(name, expected, actual, fmt, ...)`, return the original's
  result. `stolenBytes` must cover whole instructions at the function start, at least 5, with no
  relative operands (no CALL/JMP/Jcc in them); read them from the disassembly.
  `python tools/check_stolen.py` checks every registration (a count that ends inside an instruction
  crashes only when that function first runs, which can be deep into a game session).
- **Functions that only write an output object:** run the original into a scratch copy, yours into the
  real one, compare (see `FindUnitsVerify`, `SetMorphVerify`). The scratch must be a valid object
  for the original code: copy-construct members such as STLport strings (a default `stlp::string`
  has null pointers, and the original's assign crashed on it). Watch for callers that pass the same
  object for two outputs (`CTextManager::GetItemTexts`): alias the scratch copies the same way.
  Fresh allocations contain leftover heap bytes: when both versions allocate, either seed the
  original's allocation with ours' bytes (`verify_particle_emitters.cpp`) or compare only what the
  function writes (`verify_figure_mesh.cpp`); otherwise never-written bytes show as mismatches.
- **Stateful functions** (allocate, open files, change shared lists): run only yours (`stolenBytes`
  0), then check the resulting state is consistent, ideally using the original's own read-only
  lookups (see `GetFileVerify`).

- **Static initialisers** (code that only runs before the hooks are installed): no wrapper can see
  them. Write the port so it can build into storage of your own, and register a `void()` check with
  `EI_STARTUP_CHECK(fn)`: it runs once at the end of `InstallHooks`, when what the original built
  is still untouched, and compares that with what the port builds (see
  `verify_ai_static_tables.cpp`). Pure data only; skip a global that something may have used
  already.

- Never copy a game stream (or any `__declspec(novtable)` class) by value in a wrapper: the copy
  leaves the vtable pointer unwritten. Use `RawCopy` (tests/verify/fake_stream.h) or memcpy. Two
  wrappers broke this way (false mismatches in C4, a crash in X16).

## Notes

Record in `docs/notes/<area>.md`:
- forwarders you added (address, what they are) and anything else the standalone build must replace;
- original bugs you kept;
- functions you skipped and why;
- what each verify wrapper checks.

Commit on your branch with clear messages. End commit messages with the co-author line used in this
repo's history.
