# Cleanup tooling (ticket C1, cleanup plan Phase 0)

Tooling only; no source file under `src/` changed.

## `tools/progress.py`: cleanup artefacts

After the usual rewritten/verified numbers it prints a "cleanup artefacts" block over `src/*.cpp`
and `*.h` (comments blanked first, so a mention in a comment does not count). One small function per
count; the plain "how often does this appear" counts are two regex tables at the end of the file.
`tools/srcscan.py` holds what both scripts share (loading the sources, blanking comments, finding
the `EI_ORIGINAL(0x...)` sites).

How each number is defined, so later runs stay comparable:

- **EI_ORIGINAL forwarders**: sites of `EI_ORIGINAL(0x...)`, and distinct addresses.
- **forwarded from several files**: addresses with a forwarder in more than one file.
- **naked, not forwarders**: `__declspec(naked)` whose declaration reaches `{` before `EI_ORIGINAL`
  (a hand-written asm body). The plan's "328 x87/asm helpers" counted something wider.
- **files with #pragma float_control**: real pragmas only (the baseline's 83 included
  `particle_common.h`, which only mentions it in a comment).
- **unknown_XXX fields**: occurrences of `unknown_<hex>`.
- **At<> / Field<> uses**: occurrences of `At<` and `Field<`. **files defining their own At()**:
  any file with a function `At(...) {` (the baseline's 47 matched only the exact
  `T& At(void* object, int offset)` form; that form is 53 files today).
- **ei::VCall uses**: `VCall<`.
- **game data addresses**: distinct literals `0x0073xxxx`-`0x008Fxxxx` (data, bss, vtables, strings).
- **`__fastcall Fn(T* self, void*)` shims**: non-naked `__fastcall` definitions with a body whose
  first parameter is a pointer and whose second is `void*` (the EDX dummy of the thiscall trick),
  with any further parameters; the number in brackets is the strict two-parameter form. The
  baseline's 174 used a narrower pattern that we could not reproduce, so do not compare it with
  today's number.
- **struct names defined in several files**: `struct`/`class` with a body at column 0, same name in
  more than one file.
- Also reported from the plan's table: `reinterpret_cast`, `constexpr uintptr_t kX = 0x00...`,
  `*At` view structs, anonymous namespaces, `mfc::`/`stlp::`/`gamecrt::`/`gameapi::` uses,
  `EI_REPLACE` registrations.

## `tools/gen_originals.py`: `docs/originals-report.md`

Reads `src/` and writes the report (never edits sources): every original address that has an
`EI_ORIGINAL` forwarder in more than one file, with each file, line and declared signature (the
declaration back to the previous statement, comments and `__declspec(naked)` removed, parameter
name comments kept). Each address is tagged by how far the files agree (identical, same signature
with different helper names, differing types or calling convention) and sorted into three groups:
already rewritten (a direct call replaces the forwarders), library code (goes with the library
layer) and not yet ported (one shared declaration; the future `src/game/originals.h`).

The output is deterministic. `python tools/gen_originals.py --check` exits 1 when the committed
report is stale. "Rewritten" means an `EI_REPLACE(0x...)` / `EI_REPLACE_METHOD(0x...)` with a literal
address (the macro-built registrations in `ui_hud.cpp` are not seen, as in `progress.py`).

## `tools/check_returns.py`: void declarations whose callers use EAX (T1)

Two crashes came from rewrites declared `void` whose original leaves a value in EAX that callers
use (UnitActionPoint 0052FDF0 returned its out pointer, CUiWindow::AddSurface 005FFA90 the surface
it was given). For every `EI_REPLACE` / `EI_REPLACE_METHOD` / `EI_ORIGINAL` declared void (map +
undname; the parsing is shared with `check_signatures.py`, which now exposes `declarations()`,
`registrations()` and `candidates_for()`), the tool finds the direct `CALL rel32` sites in game.exe
(checked to be instruction starts by disassembling the containing function) and follows the code
after each call with capstone, both ways at conditional jumps, until EAX is written, a call, or
RET. Reading EAX/AX/AL/AH first flags the site. A path reaching RET (and a `JMP rel32` tail call)
means the caller hands EAX to its own callers, which are followed up to three levels. Sites inside
replaced functions are skipped. A write of AL/AX counts as a full write (MSVC's `MOV AL,..; PUSH
EAX` bool argument, `FNSTSW AX; SHL EAX,n`), and `XOR/SUB/SBB EAX,EAX`, `OR EAX,-1`, `AND EAX,0`
are writes; without those rules the first run had five false positives (Chunk::Destruct,
Chunk::WriteFloat, DriverSession::Pump, CUiWindow::ClearRegions, CWorldServer::UpdateWeather).

Not seen: calls through vtable slots and other indirect calls (UnitActionPoint is slot 53, so the
tool cannot find its crash); the summary counts void replacements with no direct call site.
`--address 005FFA90` checks an address whatever its declaration: it flags AddSurface's three sites
(0061C008, 0062BB3D, 0062BBA7, MOV reg,EAX).

build.cmd runs it after check_thunks with the build's own map. Replacement hits fail the build
unless listed in `tools/check_returns_allow.txt`; forwarder hits are warnings, counted and listed
with `-v` (our code ignores the value, and the check fails once such a function is replaced with
the void declaration). First run: 0 replacement hits, 22 forwarder warnings, e.g. constructors
returning `this` declared void (StringFromChars, MessageStringConstruct, RecordsConstruct),
FatalDirectX (returns 0 when it does not exit; callers compare it), BitStream::ReadString, SurfaceDraw::Begin/Text,
CampItems::ReturnAll, PartyAdd, SoundStop. No declarations needed fixing.
