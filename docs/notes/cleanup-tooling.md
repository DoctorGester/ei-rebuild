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

How `check_returns.py` follows EAX, what it cannot see (vtable and other indirect calls) and its allow list are in its docstring.

## `progress.py` cleanup artefacts

Counted over `src/*.cpp` and `*.h` with comments blanked first (`tools/srcscan.py`, shared with `gen_originals.py`): EI_ORIGINAL forwarders
(sites and distinct addresses), forwarded from several files, naked non-forwarders, files with `#pragma float_control`, unknown_XXX fields,
`At<>` / `Field<>` uses, files defining their own `At()`, `ei::VCall` uses, game data addresses (distinct literals
`0x0073xxxx`-`0x008Fxxxx`), `__fastcall Fn(T* self, void*)` shims, struct names defined in several files, and the rest listed in its output.
`gen_originals.py` lists every address with an `EI_ORIGINAL` forwarder in more than one file (file, line, declared signature), tagged by how far
the files agree and sorted into already rewritten (a direct call replaces the forwarders), library code, and not yet ported. "Rewritten" means
an `EI_REPLACE(0x...)` / `EI_REPLACE_METHOD(0x...)` with a literal address, or a `UI_INPUT_HANDLER` / `UI_WHEEL_HANDLER` (`ui_hud.cpp`).

## Shared forwarder headers (cleanup ticket C3)

One declaration per forwarded address in `src/game/originals_<area>.h` (`core`, `engine`, `script`, `render`, `units`, `world`, `ai`, `ui`,
`gameplay`, `effects` by address range; `library` for STLport / MFC / CRT code), each `__declspec(naked) inline ... EI_ORIGINAL(addr)` with a
comment giving RET n and what EAX / ST0 holds, and forward declarations of the types it names. The signature is decided from the game.exe
disassembly (RET n, the code before each RET, callers' use of EAX / ST0 / AL through the `check_returns` logic over all call sites, including
those inside replaced functions). Other sites keep their local name and signature as a one-line inline wrapper over the shared forwarder.
Rewritten targets stay forwarders (called through the original address) so the hook and its verify wrapper still see the call.

Conventions (keep following them):

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
