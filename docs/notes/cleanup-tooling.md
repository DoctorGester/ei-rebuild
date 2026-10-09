# Cleanup tooling and the shared forwarder headers

Tools (all in `tools/`; each file's docstring says how to run it) and the state of the forwarder cleanup. The plan is
`docs/cleanup-plan.md`; the rules for new code are in `docs/rewrite-guide.md`.

## Checks

`build.cmd` runs `check_thunks`, `check_returns`, `check_forwarders` and `check_cleanup` (a failure stops the build). The others are run
by hand before handing work back.

| Tool | What it catches |
|---|---|
| `check_stolen.py` | the stolen-byte count of every `EI_VERIFY` ends inside an instruction, is under 5, or copies IP-relative instructions (a wrong count crashes only when the function first runs) |
| `check_x87.py` (after `release`) | unsigned 32-bit to float done the modern way (`FILD` + `FADD 2^32` rounds at the precision control): use `x87::FromUnsigned` |
| `check_signatures.py` / `check_signatures.cmd` (after `verify`) | a hooked, forwarded or wrapped declaration whose stack arguments differ from the original's `RET n` (argument order is not checked) |
| `check_thunks.py` | an `EI_REPLACE_METHOD` naming a method that is virtual, or overrides one, in its class or a base (it would hook a vcall thunk: rewrite-guide rule 4) |
| `check_returns.py` | a replacement or forwarder declared `void` whose original leaves a value in EAX that callers use |
| `check_forwarders.py` | a function newly forwarded from more than one file (baseline `config/forwarder_duplicates.txt`, `--update` drops fixed ones) |
| `check_cleanup.py` | per file, the counts of offset access (`At<>` / `Field<>`), `VCall`, `__asm`, `float_control`, raw game addresses and `*At` views may only go down (`config/cleanup_baseline.csv`; a new file starts at zero; `--update` after a cleanup) |
| `progress.py` | rewritten / verified numbers plus the cleanup artefact counts (below) |
| `gen_originals.py` | `docs/originals-report.md` (generated; do not read it whole: grep the address). `--check` exits 1 when it is stale |
| `find_dead_code.py` | rewrites `config/dead_code.csv` from scratch (drops hand-added rows; refuses arguments) |

`check_returns.py` follows the code after each direct `CALL rel32` of a void-declared function with capstone (both ways at conditional
jumps) until EAX is written, a call or RET; reading EAX / AX / AL / AH first flags the site; a path reaching RET (or a tail `JMP`)
hands EAX to the caller's callers, followed up to three levels. A write of AL / AX counts as a full write (`MOV AL,..; PUSH EAX` bool
arguments, `FNSTSW AX; SHL EAX,n`), and `XOR/SUB/SBB EAX,EAX`, `OR EAX,-1`, `AND EAX,0` are writes. Not seen: calls through vtable slots and
other indirect calls (the crash it was written for, `UnitActionPoint` slot 53, is invisible to it); `--address 005FFA90` checks an address
whatever its declaration. Replacement hits fail the build unless listed in `tools/check_returns_allow.txt`; forwarder hits are warnings
(`-v` lists them; our code ignores the value but the check fails once such a function is replaced with the void declaration).

## `progress.py` cleanup artefacts

Counted over `src/*.cpp` and `*.h` with comments blanked first (`tools/srcscan.py`, shared with `gen_originals.py`). Definitions, so runs
stay comparable: **EI_ORIGINAL forwarders** (sites and distinct addresses); **forwarded from several files**; **naked, not forwarders**
(`__declspec(naked)` whose declaration reaches `{` before `EI_ORIGINAL`); **files with `#pragma float_control`** (real pragmas only);
**unknown_XXX fields**; **At<> / Field<> uses** and **files defining their own `At()`** (any file with a function `At(...) {`);
**ei::VCall uses**; **game data addresses** (distinct literals `0x0073xxxx`-`0x008Fxxxx`); **`__fastcall Fn(T* self, void*)` shims**
(non-naked, first parameter a pointer, second `void*`, any further ones; the bracketed number is the strict two-parameter form);
**struct names defined in several files** (`struct` / `class` with a body at column 0); also `reinterpret_cast`, `constexpr uintptr_t kX =
0x00...`, `*At` view structs, anonymous namespaces, `mfc::` / `stlp::` / `gamecrt::` / `gameapi::` uses and `EI_REPLACE` registrations. The
baseline numbers in the plan used narrower patterns for a few of these, so do not compare them with today's.
`gen_originals.py` reads the same sources: every address with an `EI_ORIGINAL` forwarder in more than one file, with file, line and declared
signature (comments and `__declspec(naked)` removed), tagged by how far the files agree and sorted into already rewritten (a direct call
replaces the forwarders), library code, and not yet ported. "Rewritten" means an `EI_REPLACE(0x...)` / `EI_REPLACE_METHOD(0x...)` with a
literal address (the macro-built registrations in `ui_hud.cpp` are not seen).

## Shared forwarder headers (cleanup ticket C3)

Method: one declaration per forwarded address in `src/game/originals_<area>.h` (`core`, `engine`, `script`, `render`, `units`, `world`,
`ai`, `ui`, `gameplay`, `effects` by address range; `library` for STLport / MFC / CRT code), each `__declspec(naked) inline ...
EI_ORIGINAL(addr)` with a comment giving RET n and what EAX / ST0 holds, and forward declarations of the types it names (so including it
pulls in no class headers or globals). The signature is decided from the game.exe disassembly (RET n, the code before each RET, callers' use
of EAX / ST0 / AL through the `check_returns` logic over all call sites, including those inside replaced functions). Every other site keeps
its local name and signature as a one-line inline wrapper (class members are plain definitions) over the shared forwarder, so call sites do
not change. Rewritten targets stay forwarders (called through the original address) so the hook and its verify wrapper still see the call;
the "already rewritten" group is handled by direct calls instead (a separate step).

Conventions that came out of it (keep following them):
- A plain `RET` function declared `(T*, void*)` dropped its dummy EDX: declare `__fastcall` with only the real arguments; a function that
  never reads ECX takes no `this` (`IsNetworkClientOriginal()`, `GameFlowOriginal()`, `SetServerFlagOriginal(int)`: the old `this` arguments
  were dummies).
- Constructors and copies return `this` (`void*`); results callers never read are `void`; a result in ST0 is declared `double` (so it stays
  unrounded) and `float` arguments the function only copies are passed as raw dword bits (`std::bit_cast`; no FPU load touches a NaN payload).
- CString: 006FDA6D is `Right(nCount)` and 006FD9B4 `Mid(nFirst)` (core.md, "MFC CString: Mid and Right"); 006FDAE9 `Left` is
  `CString CString::Left(int) const` (RET 8: result slot, count). The iostream inserters are distinct addresses: 006FC9C6 "%d", 006FC14E
  "%u", 006FC91C "%ld", 006FC7FA double (RET 8), 006FC0B0 char, 006FC31D string, 00434FE0 manipulator.
- `src/lib` keeps its own copy of some functions (lib cannot include game headers): 0044C470, 006E6370, 006E63AD, 006E6433, 006FE90F,
  00700AA9, 00700AF9, 00700B9F, 00700C05, 00700D4C, 00700E17 (and the CRT / STLport ones such as 006E848B, 00404D70, 00405950); those
  addresses stay listed as duplicates.
- Where a class member forwarded the same address (`World::GetPlayer`, `Ostream` members, `CStringArray` members, ...) it is a one-line
  wrapper over the shared forwarder; a rewrite that is also declared in a header (e.g. `FatalError` in app.cpp) gets a different shared name
  (`FatalErrorOriginal`).

State: `docs/originals-report.md` groups every address forwarded from more than one file into already rewritten (to be called
directly), library code and not yet ported, and lists the "differ" group (declarations that disagree), including the pending `ui_*` sites
(grep `src/game/ui_`). Batches are 50 addresses taken by site count from the report. Next step after the forwarders: call the rewritten
ones directly.
