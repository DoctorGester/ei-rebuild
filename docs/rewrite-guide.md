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
- Do not launch the game. Integration runs happen centrally, one game at a time.

## Where code goes

- `src/game/<area>.h/.cpp`: rewritten game code. Provisional names; comment each function with its
  original address. Classes mirror the original memory layout exactly, with `static_assert`s on sizes
  and offsets. Unknown bytes are `unknown_XXX` fields.
- `src/lib/`: shared compatibility code. `crt.h` (the game's own CRT: operator new/delete, string
  functions, file I/O), `mfc.h` (CString, CFile, CFileFind, CTypedPtrList, CPlex), `stlp.h` (STLport
  string/vector/hash_map on the game's node allocator), `gameapi.h` (Win32 through the game's import
  table; generated). Add to these only when needed, in small separate additions.
- `tests/verify/verify_<area>.cpp`: verify wrappers. `tests/verify/verified.txt` is updated centrally.
- Shared headers others use: `src/game/engine_object.h` (CEngineObject virtual interface),
  `maths.h` (Vector3, Quaternion), `ai_map.h`, `resource*.h`, `terrain.h`, `base_manager.h`.

## The rules

1. **Read the disassembly, not just the decompiler.** Ghidra misreads calling conventions, misses
   register arguments and drops return values.
2. **Return values:** check what the original leaves in EAX/AL/ST0 and what callers read.
   - A "void" setter that ends with `MOV EAX,ECX` returns `this` (the Vector3 constructor crashed the
     game when we returned void).
   - Functions that set all of EAX to 0/1 return `BOOL` (int); ones that set only AL return `bool`.
3. **Calling conventions:** `__thiscall` methods become C++ members registered with
   `EI_REPLACE_METHOD(addr, Class::Method)`. `__fastcall`/`__stdcall`/`__cdecl` free functions use
   `EI_REPLACE(addr, Function)`. Count `RET n` to confirm stack arguments.
4. **Virtual methods** cannot be registered by member pointer (that yields a vtable thunk). Declare
   the virtuals in vtable order, implement the body, and register a shim:
   `bool __fastcall FooHook(Cls* self, void*, args...) { return self->Cls::Foo(args...); }`.
   Do not give a non-virtual method the same name and signature as a base-class virtual
   (`CEngineObject::Create`, `Destroy`, `Release`, `Load`, `Save`, `GetName`, ...): it silently becomes
   an override, and the hook lands on a dispatch thunk that loops forever. The installer now refuses
   thunks and logs "virtual-call thunk".
5. **Not-yet-rewritten callees** are forwarders: `__declspec(naked) R Cls::Fn(args) EI_ORIGINAL(0xADDR)`.
   Naked functions cannot have members with constructors/destructors in the class layout path; use raw
   pointers in layouts where needed (see `mfc::CFile`).
6. **Memory and CRT:** allocate and free with `gamecrt::operator_new/operator_delete` (the game's heap),
   STL containers through `stlp::` (the game's node pool), MFC lists through `mfc::CTypedPtrList`.
   Call the game's CRT (`gamecrt::tolower`, `strnicmp`, `sprintf`, ...) and Win32 through `gameapi::`.
7. **Floating point:** the build is `/arch:IA32 /fp:fast` (x87, like VC6). Keep the original's order of
   operations exactly (it affects rounding). Use `x87::fsqrt/fsin/fcos` (not the modern CRT) and
   `x87::fistp` for FISTP (rounds to nearest; a C++ cast truncates). Copy floats that the original
   copies as dwords with `memcpy`.
8. **x87 compare semantics:** `FCOM; TEST AH,0x41; JNZ` means "jump if <= or NaN"; `TEST AH,1` means
   "less or NaN". Write `!(a > b)` / `!(a >= b)` so NaN behaves the same.
9. **Mods patch the original in memory.** The installer compares each function's whole body with the
   exe on disk and skips patched ones (they install only in the vanilla launch). When rewritten code
   calls a function that a mod patches, call it through its original address (an `EI_ORIGINAL`
   forwarder), not directly, so the mod's patch still runs.
10. **Keep the original's bugs** (leaks, odd messages) and note them under "Original bugs kept on
    purpose" in your notes file.

## Verification

Every replacement gets a verify wrapper in `tests/verify/`, registered with
`EI_VERIFY(addr, Wrapper, stolenBytes, originalPtr)`:

- **Pure functions** (no side effects): call the original through the trampoline and yours, compare
  bit for bit with `ei::verify::Check(name, expected, actual, fmt, ...)`, return the original's
  result. `stolenBytes` must cover whole instructions at the function start, at least 5, with no
  relative operands (no CALL/JMP/Jcc in them); read them from the disassembly.
- **Functions that only write an output object:** run the original into a scratch copy, yours into the
  real one, compare (see `FindUnitsVerify`, `SetMorphVerify`).
- **Stateful functions** (allocate, open files, change shared lists): run only yours (`stolenBytes`
  0), then check the resulting state is consistent, ideally using the original's own read-only
  lookups (see `GetFileVerify`).

## Notes

Record in `docs/notes/<area>.md`:
- forwarders you added (address, what they are) and anything else the standalone build must replace;
- original bugs you kept;
- functions you skipped and why;
- what each verify wrapper checks.

Commit on your branch with clear messages. End commit messages with the co-author line used in this
repo's history.
