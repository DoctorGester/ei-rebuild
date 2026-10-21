# Standalone exe: the address image, the library ports, UTF-8

The product is the standalone image: `ei_standalone.exe` lays out game.exe's data without its code and runs our
code from `ei_hook.dll`. game.exe runs only as the original game (the harness's references). This file holds how
the image works, where the library code lives and what it must keep, the UTF-8 plan and what the hook retirement
left. How changes are checked: docs/notes/standalone-verification.md. Per-ticket history: `git log --grep=<ID>`.

## 1. What ties the code to the exe (measured 2026-10-15)

| Tie | Count |
|---|---|
| Unported game functions | 0 (S2 did the last 200, all under 5 bytes; 00656D69 is not a function) |
| Forwarders (`EI_ORIGINAL`) | 284 sites under src/ after C13d (3,817 addresses before C13, 294 sites before C13d): Rebirth-patched callees, the skip list's type-recovery rows, vector recorders and harness / replay entries (docs/notes/cleanup-tooling.md); forwarders to library code without a registration are 0 since S12g / S6e (operator new / delete 007005AF / 007005D8 are the image's allocator) |
| Code pointers in the exe's data | 4,524 to rewritten code (vtables, callback tables), 391 to 359 library functions |
| Game data addresses | 3,601 distinct in 524 files: .rdata 912, import table 465, initialised .data 1,812, .bss 410 |
| Static initialisers | 748 in the .data list 00783004-00783BB0, all generated (S3); 190 exit functions generated (S3a, S16a) |
| Runtime uses outside src/lib | `mfc::` 3,481, `stlp::` 1,009, `gamecrt::` 1,191 (865 new / delete), `gameapi::` 536 |

`tools/dashboard_data.py` measures the current numbers (the dashboard's standalone panel). C13 made 1,001 forwarders
direct calls with `tools/direct_calls.py` (`--report -v --cast --library` lists the rest and why each stays; the
skip list config/direct_calls_skip.csv; check_cleanup ratchets the count per file). C13a recovered the real types
of about 360 more (per-file shims replaced by the real classes: the script interpreter's world, brain, AI map, lever,
stats and mode calls, the options' registry key, the window procedure's input; float bits passed with std::bit_cast);
C13a1 replaced the rest of the per-file shims (the UnitController overlay by CUnitServer, the script interpreter's
call-only shims, the game glue objects, UiInput by UiContainerInput, the global map's and the stats panel's views, the
registries by CFileRegistry, ui_gdi.h's GDI views by mfc::impl's classes, the sprite views by CUiMesh) and made 336
forwarder sites direct calls (2,352 to 2,014). `direct_calls.py --loose` also converts forwarders whose types differ
in ways a call site can spell (a cast written at the call; `--exclude` leaves files out, `--cast-sites` names calls that
need a static_cast). The game glue (00578xxx) ignores `this`: its world_glue.h functions are called without the dummy
this / EDX. 534 "types differ" (431 under --loose) and 102 skip-list rows are left for C13a2. S18 (after C13a2) removes
the rest: generated definitions for the data addresses, vtables as real virtual classes, imports linked directly,
plain new / delete; then the image and the read of game.exe go.

**Finding game.exe (S18e3).** Every tool that reads game.exe (rdata_literals, exe_strings, gen_/check_static_init,
gen_/check_short_functions, gen_image_cells, gen_iat, check_signatures, check_stolen, check_fp_fold, find_vtables,
find_dead_code, standalone_gaps, and CMake's `EI_GAME_EXE`) goes through `tools/exe_path.py`: `--exe` (or `EI_EXE`), else
`run_all.run_dir(root)`'s `vanilla\game.exe`, then `rebirth\game.exe` (a worktree without a run\ junction uses the main
checkout's); nothing found is one error naming the paths looked for. Nothing reads F:\games any more.

String constants of the exe's data need no generated definition: run\vanilla's and run\rebirth's game.exe differ in
one .text byte (file offset 0x56BB6) and nothing else, and the image runs neither one's code, so a string the code
passed by address (`game::Text(0x...)`, a table of name addresses) is written as a literal with the same bytes. C11
batch 15 did that for the options' value names, the console's commands and texts, the menu rectangles, the material
names, the camp and item-info texts and a few logs; batch 16 the `reinterpret_cast<const char*>(0x...)` helpers
(`game_strings.h` and per-file ones), the `constexpr std::uintptr_t kXxx` string constants (now `constexpr const char*`),
their tables (game flow's zone names, the hero part names, the attribute and skill names, the option groups, the mercenary
zones), options_defaults' value names and the lexer's comment tokens. The strings were read from both exes by script
(`tools/exe_strings.py`; `--wrap X` takes `X(0x...)` and `X(std::uintptr_t(0x...))`, `--before RE` a parameter position,
`--skip` leaves addresses), not typed. Keep a string by address only where code compares the pointer itself
(`game::kEmptyTextAddress` and the per-file `EmptyText()` copies of 0079B298), where the game writes into the string
(the window title 0079BB24, the digit at 007930F6, the pointer cells LoadingMovieName 00791EE0), and in the CRT
(`crt_locale` compares its locale names by pointer; `_iob` is not a string). A pointer-sized cell read as
`const char* const*` holds the address of the text, not the text: read the cell's pointer first.

## 2. The image

### How it runs

- **Host and image** (S1, src/standalone/host.cpp, src/hook/standalone.cpp). `ei_standalone.exe` is an ordinary exe
  (S18k: default base, relocatable) that reads no game.exe. **The guard**: first thing in main it reserves every free
  part of game.exe's range 0x400000-0x7D7000 (game_data.h's `kImageBegin`-`kImageEnd`) with no access, so nothing is
  allocated there later and a stray game address faults and is named: the host's `OnGameRange` (`ei_standalone: read
  of game.exe's headers|code|data|.rsrc at ... by ...`, or `no code at ... before ei_hook.dll's missing-code report`),
  then ei_hook.dll's OnOldCode / OnOldData with the detailed reports. What was there before main stays: EiStandaloneMain logs
  each open part and the total (`game.exe's range 00400000-007D7000 guarded|NOT all guarded (N byte(s) open; M taken
  before the host's main; the host at ...)`, the host's export `EiStandaloneRangeTaken`), informational, not a check
  failure. Measured on the suite (2026-10-21): about half the runs 0 open, the rest 30 KB-1.9 MB of private memory
  (heap / stack segments the loader placed there by ASLR's choice, always within what was taken before main); the host
  itself lands outside the range (00980000). The seam itself never hands out such
  an address: in the image `GameAddress` of a game address outside the moved range (headers, code, .rsrc) is
  `StrayGameAddress`'s report (`the data seam was asked for game.exe's code at ...`); a game address is itself only in
  game.exe, an address outside game.exe's range (a resolved pointer, null) is itself in both. `ei::Standalone()` is
  "the main module exports `EiStandaloneHost`" (the host's marker; game.exe exports nothing).
  **Resources** (S18j): game.exe's are the host's own: `tools/gen_resources.py` writes every leaf of game.exe's .rsrc
  tree (two cursors and MFC's cursor group, five bitmaps, the icon group 105 and its three icons, two dialogs, twelve
  string blocks) as a .res at build time (build/<cfg>/image/game_resources.res, from `EI_GAME_EXE`, not committed)
  that the linker builds in. WinMain's hInstance and GetModuleHandleA(NULL) are the host, so the resource calls,
  DirectInput and MFC's null resource handle need no wrapper; GetModuleFileNameA(NULL) names ei_standalone.exe (the
  message box title; app_dxversion's exe folder, which only tells dgVoodoo beside it from the system's DirectDraw;
  GetCommandLineA's first token too).
  Then it loads ei_hook.dll, which keeps .rdata (with the import slots) and .data in its own storage
  and resolves game.exe's imports as it initialises (S18f; since S18k DDraw, mss32 and binkw32 from the working
  directory, the game folder, else System32, so Rebirth's dinput proxy and addon.dll stay out; "Own data" below);
  then `InstallHooks` takes the registrations as the code for their addresses ("Code", below). Nothing runs in or
  jumps through game.exe's code. `ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game args>]`, run from the game
  folder; without `--run` it builds the image, checks the code table and the cells and exits.
- **What comes from game.exe at build time** (S18k: nothing at run time): the sections' bytes and the dispatch-only
  functions' bytes (`tools/gen_image_cells.py --bytes / --dispatch`, build/<cfg>/image/, which also checks the 1.06
  timestamp), the resources (gen_resources.py); the section places are constants (game_data.h, image_code.cpp,
  hook.cpp's `kCodeBegin`-`kCodeEnd`, the registrations' bounds). InstallHooks reads game.exe's file only in game.exe
  itself (the recorders' stolen bytes). `EI_REPLACE_DISPATCH` thunks are compared with dispatch_bytes.inc (8
  dispatch-only functions; 3 registered).
- **Start** (EiStandaloneMain): the C initialiser `__initmbctable` (00783BC0), then the 748 static initialisers in
  list order (`StaticInitialisers(Run)`), then WinMain; its result goes to the CRT's exit as the CRT did.
- **Missing code.** Reaching an address without code (CodeAddress's stub, or a jump into the guarded .text) logs
  `FAILED: standalone: no code at <address> (in the game function at <start> | library code), [esp] <caller>` and
  ends the run; `tools/resolve_crash.py <run folder>` names it. A read or write of .text is reported as such
  (`... of game.exe's code at ...`). The handler goes in before InstallHooks
  (S16c), so a startup action's hit is reported too; a hit during ei_hook.dll's static constructors prints
  `ei_standalone: no code at ... before ei_hook.dll's missing-code report` and exits 3.
  ei_launch reports a game that ended by itself while its report still says RUNNING (`FAILED: the game ended ...`).
- **What the image could still reach without code**: `python tools/standalone_gaps.py` lists forwarder targets, code
  addresses written in src/ and function starts the exe's data points at, each without a registration. The rule
  (S16d): the image runs none of game.exe's code, so a table entry or vtable slot is reached only when our code calls
  through it. `config/standalone_unreachable.csv` holds each decision with its reason (CObject::GetRuntimeClass
  slots, `_purecall`, MFC classes never made or never called virtually, CRT tables); `--listed` prints them, `--check`
  exits 1 on an open entry or a config line that covers nothing. 0 open since S16d.
- **Suite.** `run_all` / `work.py test` run the image by default: install `standalone` (the vanilla data) and
  `standalone-rebirth` (the Rebirth data, the flags of `config/ei_rebirth_standalone.ini`, which are the 41 a Rebirth
  launch with addon turns on; refresh it when addon.dll or EI_Plugin.ini change). Always our renderer (no display
  mode change). run_all passes ei_launch no game.exe for them (S18k1; the run\<install>-clean folders of SV10f are gone):
  `--game` is the install whose data they play, and ei_launch --standalone sets EI_GAME_ROOT from it; `--exe` only
  places the shared host copy of a launch without a scenario (launch-vanilla.cmd). run\<install> keeps dgVoodoo for
  the original's runs.

### Own data (S18)

The image ran on game.exe's sections at their addresses. S18 moves them out one section at a time,
each step green on the goldens and removing a measured tie:
- **The seam** (S18a, src/game/game_data.h): code names game data by the original's address and reaches it
  only through `ei::GameAddress(a)`, `GameData<T>(a)` or `GamePointer<T>(a)`; `DataId(a)` marks an address
  kept in a table, resolved by its reader. In game.exe (the original's runs: the harness and the vector
  recorders share this code) it is the address; in the image a moved section's addresses map to our
  storage, so the layout inside a section (arrays running into neighbours) stays. The generated static
  initialisers resolve through their visitor (`d.At` for an argument that is a data address).
- **The check** (`python tools/data_seam.py`, build.cmd): per section the uses through the seam, through a
  helper whose every definition hands its address to the seam (per-file `GameFloat(std::uintptr_t)`), a
  constant whose every use does, the generated ones, and raw. config/data_seam.csv holds the raw count per
  file and section (only down; `--update`) and the sealed sections (none raw). `--list <section>` shows the
  raw uses, `--apply <section>` rewrites the casts it can (`*reinterpret_cast<T*>(0x...)`, the helpers'
  casts, constants used in casts).
- **Moving a section**: our storage (the low 16 bits of each address kept), GameAddress's shift set before
  any static constructor, the pointer cells of the remaining image data that point into it fixed up (no
  relocations in game.exe: a scan of aligned dwords, string tails left out), and nothing at the old range
  (the host's reservation since S18k; PAGE_NOACCESS before), so a use that missed the seam faults with its address. A moved section's initial bytes
  are generated from game.exe at build time (not committed).
- **.data sealed** (S18c): every use of 00783000-0079BF70 goes through the seam (tables of ids kept as `DataId`,
  read with `GamePointer` / `GameData`; the addon-site table's .data entries are ids too, generated by
  tools/rebirth/make_addon_sites.py); `data` is sealed in config/data_seam.csv, so a new raw use fails build.cmd.
- **.data and .rdata moved** (S18b the .bss, S18d the whole .data, S18h .rdata; src/standalone/image_data.cpp): the
  moved range is game_data.h's `kMovedBegin`-`kMovedEnd`, 0073B000-007CD000: .rdata (the import address table at its
  start, S18f) and .data (its initialised bytes and the zero-filled tail) as one range with one shift,
  `ei::g_dataShift` (0 in game.exe); `GameAddress` is one range check. Both ends are page boundaries: .text ends
  at 0073A670 inside the page below 0073B000, .rsrc starts at 007CD000, so the guard is exactly the sections' pages
  and no strip is left unguarded (S18b's .bss-only move left 0079BF70-0079BFFF to the seal, as that page also held
  .data's initialised end). Nothing runs across either end, and nothing inside the range can run across a
  boundary any more (the key bindings' action table 0079BF70, 0x200 bytes, runs across the file's raw end
  0079C000: S18b's move from 0079C000 split it and the guard caught LoadKeyBindings' memset; .rdata's end 007828B2
  and .data's start have one shift since S18h). `g_dataStorage` in ei_hook.dll is used from the address whose low
  16 bits are 0xB000;
  the shift is set by an `init_seg(compiler)` object, before every other static constructor of the DLL (the
  harness's and the game globals' `GameData` references bind at static init). Nothing reads or writes the range
  before it: the host lays out only the headers, the loader and the DLL's CRT start-up touch no game data;
  the log line counts the old range's nonzero bytes (0).
  **The bytes**: the host copies neither section (it skips RVAs 0033B000 and 00383000); the build writes
  their file bytes from 0073B000 up to .data's last nonzero dword, the import table's 0x820 bytes zero (our slots;
  ResolveImageImports fills them), .rdata's tail past 007828B2 zero, the ranges our named globals hold zero
  (config/named_data.csv, "Named data (C18)" below) (`tools/gen_image_cells.py --bytes`, a CMake
  custom command from `EI_GAME_EXE`; empty by default: `tools/exe_path.py` finds it) into
  build/<cfg>/image/image_data_bytes.inc, not committed (user 2026-10-21: the interim before named globals);
  image_data.cpp copies them into the storage, the rest is zero. **Every 1.06 game.exe has the same .rdata,
  .data and .rsrc** (checked 2026-10-21 on run\vanilla, run\rebirth and the five copies under F:\games: vanilla's
  and Rebirth's differ in one .text byte only, S18e3).
  **The cells**: 5531 (src/standalone/image_cells.inc, written by `tools/gen_image_cells.py`, which build.cmd
  runs with `--check`): the aligned dwords of .rdata (past the import table) and .data whose value lies in the
  range, less text; 1071 point into .data, 4460 into .rdata. Text that reads as an address is of two kinds:
  string tails (the dword's bytes printable up to a zero and the four bytes before printable too) and short words
  ("Any", "May", "sky", "say", "day": `xxy\0` reads 0079xxxx) whose target would be mid-text (the byte before it
  printable ASCII; a string pointer points at a start). Into .rdata the filters are not enough (`?t\0` reads
  0074xxxx: "Nov", "Thu", "pow", "Div", "net" pass them and are the tool's TEXT list; real pointers such as
  `8]t\0` read as text too, so no stronger text rule is safe): every cell into .rdata is either in a structure
  the tool walks (STRUCTURES, counted per kind: the C++ EH tables, FuncInfo (magic 19930520) -> unwind map 2963,
  -> try block map 378, try block -> handler array 380, ThrowInfo -> catchable type array 10, array -> catchable
  types 41; RTTI, a vtable's slot -1 -> complete object locator 53, locator -> class hierarchy 53, hierarchy ->
  base class array 53, array -> base class descriptors 128; MFC's CRuntimeClass from config/runtime_classes.csv,
  class name 40 and base class 40) or in a REVIEWED_RDATA group: CCmdTarget's maps and CWnd's / CWinApp's message
  maps (11), the type descriptors' vtable (type_info's, 74, in .data), and the CRT's string tables in .data (the
  locale category names, __lc_time_c, the math functions' names, "(null)", setlocale's language / country tables
  (142), the run-time error messages). Cells into .data are in REVIEWED_DATA groups (cell range, count, what it
  is; a new cell or a changed count fails): in .rdata the per-file `const char* const` copies of ".SEC" (74) and a few other
  string constants, MFC's CCmdTarget maps (3), the message map entry 00746C80, RTTI locators and base class
  descriptors (117) and EH handler / catchable types (38) to their type descriptors (in .data); in .data the
  string-pointer tables (the key bindings' command table, the action definitions, the script's particle types
  and keyword table (442), skills, log channels, ambient sounds, ...) and the CRT / MFC cells (the lock table,
  `_iob`, `_pctype`, the small-block heap's self-linked region list, `_tzname`, `__lconv_c`, CString's empty
  string and `_afxDataNil`). Every cell lies inside the range and is fixed up in its new place. game.exe's own
  code points at .rdata too (FuncInfo addresses in its EH prologues, string immediates), but none of it runs in
  the image.
  **The guard**: nothing at the old range (the host's reservation, "Host and image"): a use that missed the seam ends the run with
  `standalone: read|write of game.exe's .rdata|.data at <data> by <code>: ...` (in the import table:
  `... of game.exe's own import slot <slot> (<dll>!<function>) by <code>`) in ei_hook.log, on stderr and in the
  harness's report (`OnOldData`; resolve the code address with the map, as for an INT3 hit). ei_vectors.exe's
  `GameAddress` stays the address.
  **Addresses read back from objects** (a vtable, a string pointer) are the storage's in the image: the harness
  prints them as the original's with `ei::OriginalAddress` (game_data.h; DescribeTopWindow, harness_digest's
  missile vtable, ui_census's window labels and its game-vtable test).
- **Code** (S18i, src/standalone/image_code.cpp): `ei::CodeAddress(address)` (ei/hook.h; `ei::Code<F>`) is what
  runs for game.exe's function at an address: in game.exe the address; in the image the harness's replacement
  (`RedirectCode`: FixedIsaacSeed, the clock's rand), else the registration (InstallHooks hands its table over),
  else a stub (16 bytes, CALL NoCodeEntry plus the address) that reports `no code at`. Who asks: the forwarders
  (S18i1: `EI_ORIGINAL` keeps a `CodeLink` per forwarder, {code, address}; bound, it jumps through it directly;
  unbound, it pushes the link and jumps to `ei_code_bind`, which keeps EAX / ECX / EDX and asks `BindCode`: a link
  binds once its code is fixed, the registration or the address itself in game.exe, never a redirection or a stub;
  `RedirectCode` unbinds them all; the harness step `codebinding off` too, for an A/B), the static
  initialisers' calls (static_init.h), the code literals of src/ (handler tables, stats, callbacks, factories,
  the WndProc and new handlers) and the CRT's takers of function pointers (exit's onexit list, qsort, the eh vector
  iterators, ostream's manipulators), which convert at the call. Never at namespace-scope static init: the table
  is not in yet (the stub would stick). **Code cells**: `tools/gen_image_cells.py` also writes
  image_code_cells.inc (21,636 cells of .rdata and .data holding a function start: vtable slots, EH unwind /
  handler / throw / copy entries, MFC CreateObject, the static initialiser list, and reviewed tables); `FixCodeCells`
  points each at CodeAddress of its value after InstallHooks (`... code cell(s) of game.exe's data point at our
  code, ... at stubs`); a slot the harness took before that (the console streambuf's sync) is fixed with it
  (`FixUpCodePointer`). The CRT's SEH scope tables are left: only game.exe's own frames read them. The
  launch-free check: every registration is its address's code (`CheckCode`); `CheckGuard` logs what of game.exe's
  range is open. Stack scans of the harness and the profiler skip the game's code range in the image.
- **Own import table** (S18f, src/standalone/image_imports.cpp): the import address table (game_data.h's
  `kImportsBegin`-`kImportsEnd`, 0073B000-0073B820, .rdata's first 0x820 bytes) moves with .data at the same shift:
  the storage starts at 0073B000 (low 16 bits 0xB000; the .rdata between the table and .data has its place there,
  unused until S18h), so `GameAddress` maps two ranges with one `g_dataShift`. `tools/gen_iat.py` writes the table
  (src/standalone/image_imports.inc, committed, 504 slots: slot, DLL, function or ordinal; build.cmd runs it with
  `--check`) and gameapi.h. Right after the move (same init_seg(compiler) object, before the harness's static
  constructor takes its slots) `ResolveImageImports` loads each DLL (the working directory, else System32;
  dinput always the system's; game.exe's folder came first until S18k) and fills our slots;
  GetCommandLineA gets its wrapper there (`ImageRoute`, hook/standalone.cpp; the resource, module and DirectInput
  wrappers went with S18j). Everything that calls through a slot by its address reaches our table through the seam:
  gameapi.h, the static initialisers' `Import`, Bink's slots, the harness's `PatchSlot` / `WatchWrites` (in game.exe
  all of them are the slot itself). `iat` is sealed in config/data_seam.csv (76 raw uses -> 0; data_seam.py's
  helpers are now transitive: TraceSlot -> PatchSlot -> GamePointer).
  **game.exe's own table** is not there, as the rest of the old .rdata (S18h, S18k) (the read of an old slot names its
  import, above; S18f had an INT3 trap per old slot instead); a slot of ours whose import did not resolve leads to a
  trap (`OnTrap`), and the launch-free check fails. **Direct callers**: none run: the image has no .text (S18i,
  S18k; S18f-S18h checked it held only INT3 and our jumps), so no `CALL [slot]` / `JMP [slot]` of the
  original exists. No pointer
  cell of .rdata or .data holds a slot's address (a scan of game.exe's aligned dwords), and our code reaches slots
  only through literal addresses, which the sealed check covers. Not routed to our table: ui_census's game.exe
  import hooks (`PatchModule(GetModuleHandleA(nullptr))` is the host in the image, as before S18f).

- **.rdata constants as literals** (S18e, `tools/rdata_literals.py`): a float, double or integer cell that no code writes is
  the value in the source, tagged with its address: `0.5f /*0073B86C*/`, `3.141592653589793 /*0073CAE8*/`, a negative one in
  parentheses, a reference result (effects take the constant's address) as `kFloatConstant<0.5f /*0073B86C*/>`. The type is the
  spelling: `f` float (dword), no suffix double (qword), an integer the dword; `/*addr f*/` a float widened to double. `--apply`
  rewrites the reads (`GameFloat(0x..)` helpers found from their definitions, `GameData<const float>(0x..)`, a constant that holds
  the address) and drops the helpers and address constants nothing uses any more; `--check` (build.cmd) compares every tagged
  literal with the bytes of game.exe (Python's float parsing; `--asserts FILE` writes static_asserts on the same literals for the
  compiler, `cl /std:c++20 /c`: all 984 held). What stays data: the cells addon.dll writes (tests/verify/addon_sites.inc: 007428AC,
  00742B08, 007432F4, 0073B720; found by reading config/rebirth_patches.csv's generated site list; vanilla's and Rebirth's .rdata
  are the same bytes, and the section is read-only, so those are the only writers), the import table and tables / vtables / GUIDs (S18g). SV10j's files (unit_ai*, combat_*) followed in S18e2
  (13 reads; a limit * limit product is written `static_cast<float>(0.2f * 0.2f)` so it keeps the FMUL's 24 bits). **Folds**: the compiler folds a product of constants at 53 bits where the
  original's FMUL on the main thread rounds to 24, and a literal has no volatile load to stop it: `tools/check_fp_fold.py` flags the
  doubles that come out of it and its `-v` list of new float constants was compared with main's (all of this ticket's new entries
  reviewed). The pi * (1/180) of two doubles is `ei::kRadiansPerDegree` (src/game/angles.h, float, checked against exact
  arithmetic); a chain rounds after each product (`static_cast<float>(kRadiansPerDegree * 90.0)`).
  **The static initialisers' folds** (S18e1): gen_static_init writes a float fold's .rdata operands as the same tagged literals
  (`d.StoreFloat(0x0079B1B8, 3.1415927f /*0073B848*/ * 0.5f /*0073B86C*/)`; 243 operands; the one operand in .data, an earlier
  fold's result 007C896C, stays `d.Float`), so the compiler folds them. The original computes them at run time at 53 bits:
  VC6's `_FPinit` (006E72BC, through the pointer 007975A0 that `_cinit` 006E8D7A calls) runs `_controlfp(_PC_53, _MCW_PC)`
  (006EEF7C) before `_initterm`, so they are not at the main thread's later 24 bits. check_static_init evaluates each fold of
  literals in x87 order at 53 bits and in C++ precedence at 53 bits and in float, and fails when the three floats differ: all 118
  agree (single operations of floats round the same through 53 bits; the three chains give 40 and 30 exactly). The `d.Log` of a
  literal is still x87::flog at run time (FYL2X ignores the precision control). The six `d.Dword` reads of .rdata (pointer
  cells 0073D720, 0073D728) stay.
- **.rdata sealed** (S18g; `rdata` sealed in config/data_seam.csv, raw 365 -> 0): every use of 0073B820-00783000 reaches the
  seam. **Vtables** (until C19 makes a family's vtables the compiler's: "Compiler-made vtables" below):
  `ei::SetVtable(object, std::uintptr_t)` (include/ei/vcall.h) resolves its argument with `GameAddress`, so
  the per-class `kVtableAddress` constants and the `SetVtable(this, 0x...)` literals are ids / helper arguments; the
  `const void*` overload takes a pointer already resolved (`X::Vtable()` accessors returning `GamePointer<void>`). A file's own
  vtable helper (`SetVtable(CUnitShape*, std::uintptr_t)`, `ConstructList`, `FreeArray`, `DestroyArray`, `InitList`,
  `SetVtables`, `NewPlainPanel`, ...) stores `ei::GamePointer<void>(id)` or forwards `GamePointer<const void>(id)` to the
  `const void*` overload (forwarding the id to `ei::SetVtable` again reads to data_seam.py as recursion, no helper). A comparison
  with an object's vtable compares with `ei::GameAddress(X::kVtableAddress)` (harness_digest's CSpellEffect, record_coder's
  buffers, the harness's save / options windows). A vtable that is a template argument (map_object_kinds.h's `PlainMapObject`) or
  a table entry (the harness's `kKnownWindows`, the effects' `Vtable()` accessors returning `std::uintptr_t`) is `ei::DataId`,
  resolved by its reader (`SetVtable`, `WindowVtables`). Resolving twice is harmless (the storage lies outside game.exe's
  ranges, so `GameAddress` of a resolved address is that address) but not the convention. **Other tables**: unit_hits' armour
  rows through `ArmourTable` (GamePointer), ui_widescreen's two layout floats (addon.dll writes them) through Read/WriteFloat,
  the addon-site table's .rdata entries as `DataId` (make_addon_sites.py, from 0073B000 to .data's end). cover.cpp's code bound
  is the last byte of .text (0073AFFF), not the start of .rdata. **data_seam.py**: an alias constant
  (`constexpr std::uintptr_t kStreamVtable = CStream::kVtableAddress;`) passes for the constant it names when all of its own
  mentions reach the seam. A helper's parameter that shares a member's name (`list.vtable = GamePointer<void>(vtable)`) is not
  seen as routed (the member counts as a use): name the parameter `vtableAddress`. Lambdas are no helpers (pass
  `GamePointer<void>(0x...)` to them).
| Sub-ticket | Step | Tie it removes |
|---|---|---|
| S18a | the seam, the check, .bss sealed, the casts of the other sections | raw uses: .bss 883 -> 0, .data 495 -> 64, .rdata 2028 -> 381, IAT 563 -> 76 |
| S18b | .bss moves to ei_hook.dll's storage, 16 cells fixed up, old range guarded | the image's .bss |
| S18c | .data sealed | raw .data uses |
| S18d | .data moves whole with .bss (one range, one shift): bytes from the build, 1071 cells, guarded | the host's copy of .data |
| S18e | .rdata constants (floats, doubles, integers no code writes) as literals | .rdata uses 2288 -> 1295 (helper 968 -> 88, seam 603 -> 507, raw 381 -> 367) |
| S18e1 | the static initialisers' float fold operands as literals | .rdata uses 1541 -> 1298 (generated 255 -> 12) |
| S18f | own import table: generated slots resolved by the DLL into the storage (one shift with .data); gameapi.h and the harness's routes through it; old slots trap; iat sealed | the host's import resolution, the IAT |
| S18g | .rdata sealed: vtables (SetVtable through the seam), tables, the harness's vtable tables | raw .rdata uses 365 -> 0 |
| S18h | .rdata moves into the storage with .data (one range, one shift): bytes from the build, 5531 cells (4460 into .rdata: walked EH / RTTI / MFC structures, reviewed string tables), guarded, old import slots under the guard | the host's copy of .rdata |
| S18i | code pointers in our sections fixed up to our functions (21,636 code cells; a stub reports an unregistered one), static init calls, forwarders and code literals through CodeAddress, .text guarded | the jumps in .text |
| S18j | game.exe's resources built into ei_standalone.exe (a .res from the build), the module handle the host's, no header copy (the page guarded) | game.exe loaded as a data file, the resource / module / DirectInput wrappers, the headers at 0x400000 |
| S18k | an ordinary host (no fixed base, no range in its image; free parts of the range reserved as the guard), no read of game.exe (dispatch bytes from the build, imports from the game folder), stray game addresses reported by the seam | the address image (dashboard "image ties" 5 -> 0) |

Not on this path: named, typed globals in place of a section's bytes (C18, below) and vtables as real virtual
classes (C19), plain new / delete and standard containers (C14), Win32 calls linked directly.

### Named data (C18)

The code still names game.exe's data by address (progress.py's "game data addresses", 3,028 when C18 began, 3,015
after its first batch, 3,000 after the second). C18 turns them into named C++ globals, one kind and one owner area per batch; C19 does the
vtables. `python tools/named_data.py --split [KIND ...]` counts what is left by kind (import slot, vtable,
eh/rtti/runtime class, rdata constant, data object, bss object, other) and by owner area (the one hand-written file
that names the address, "several files", or "generated static init / exit only"), and lists the addresses of the
kinds given.
- **The form**: a typed definition whose initialiser carries the original's address as a tag right after the `=`:
  `constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};`. The code names the global
  (`kSlotXs[i]`), never the address. It lives in the file that reads it (its anonymous namespace), or as
  `inline constexpr` in the subsystem's header when several files read it (ui_camp.h's `kBodySlotX`). A single float,
  double or integer is no table: it is rdata_literals' tagged literal (`5.0f /*007402EC*/`, S18e), even when the code
  copied its bytes with memcpy.
- **The check** (`python tools/named_data.py --check`, build.cmd): the initialiser's numbers, packed by the declared
  element type (`TYPES`: the scalars, GUID as `IHH8B`; nested braces are flattened, a `[n]` bound must match the
  count), are game.exe's bytes at the tag (.rdata past the import table, or .data and its zero-filled tail);
  config/named_data.csv lists exactly the tagged ranges (address, size, name, file; `--update` writes it); and
  nothing else reaches a range: no address literal of src/ or tests/ (.cpp, .h, .inc, comments blanked) inside it,
  no pointer cell of game.exe's data inside it or pointing into it (image_cells.inc), no code cell inside it
  (image_code_cells.inc), no addon.dll site, no two ranges overlapping. A wrong value fails with its first
  differing byte.
- **The bytes**: gen_image_cells.py --bytes writes the csv's ranges zero (CMake's custom command depends on the
  csv), so the image's copy of the sections no longer carries them; what the check proves unreachable is also
  unread at run time.
- **A batch** (the method; batch 1 did the interface's .rdata tables, 13 addresses: ui_hero_preview's four clear
  rectangle tables, ui_item_view's school glow colours and its pi / 5.0 floats, ui_briefing_people's speaker places,
  ui_hud_slide's part positions, the camp body slots of ui_container_draws / ui_hero_panels):
  1. `python tools/named_data.py --split "<kind>"`: take the addresses of one kind whose owner is your area.
  2. Per address: read its uses for the extent (the count the code indexes, the original's bound, the next tagged
     neighbour) and the element type; write the definition with the tag (the values from game.exe:
     `gen_image_cells.image(exe_path.find_exe())` in a `python -c`, or write zeros and let --check name the first
     difference).
  3. Replace every read (`GamePointer<const T>(0x...)`, `GameData<const T>(0x...)`, the per-file accessor
     returning the pointer) with the name; drop the accessor and the comments that repeated the address or the
     values.
  4. `python tools/named_data.py --update`, then `--check`, `data_seam.py --check`, `rdata_literals.py --check`,
     `check_cleanup.py` (`--update` when its counts went down), `build.cmd verify` and `release`, and
     `python tools/work.py test <ID> --golden --jobs 4`.
- **What the other kinds need** (later batches):
  - .rdata tables of numbers or GUIDs (app_dxversion, input_mouse, device.h: the DirectX interface ids): as batch 1.
    Batch 2 (C18b) did unit* (the armour tables, one definition for the overlapping 0073E09C/A0/A4 read as four
    entries per hit location), effects.h's sphere size curves (read by effects.cpp and particle_callbacks.cpp),
    figure_models' hash prime list, link_mesh_draw's RECT, and the scalars net_connection / net_lobby /
    hoover_loot as rdata literals. A table of names is a `char name[rows][cols][n]` of string literals (TYPES
    has no entry: `char` packs each literal into the last bound's chars, zero padded; kArmourPartNames).
    A vector recorder or replayer that read a named table by address reads the name instead (unitdb::kSlotWeights).
    What is left of the ticket's list holds string pointers (random 0073D080, world_server_life 0073F918,
    terrain_sector 00744164, terrain_landscape 007441D4): C18c. Keep the original's bound on an index the data
    gives (ui_item_view's `school < 8` is signed, as the original's compare: a negative school would read before our
    array where the original read the neighbouring .rdata; no database has one).
  - Tables that hold pointers (material.h 00743BF8, sound_music 00744090, the CRuntimeClass and EH records of
    src/lib): the check refuses them (a pointer cell inside the range); they need pointers to our own objects, which
    the packing does not compare yet.
  - .data and .bss objects (written at run time): the same form without constexpr (`int g_x = /*0079xxxx*/ {5};`,
    `{}` for the zero-filled tail). Before converting, every reader must use the name: the generated static
    initialisers and exit functions (static_init.cpp / static_exit.cpp store into or destroy it: the definition's
    initialiser or constructor takes the store's place, in the list's order when it has a constructor), the
    harness (tests/verify runs in game.exe too, where a name is our copy and not the original's object: it keeps
    the address there), the vectors' stand-ins (tests/vectors fill recorded values by address), and game.exe's
    data pointing at it (a cell keeps it at its address). Saves and messages carry values, not addresses: the
    goldens catch a changed value.
  - Import slots (gameapi.h 406, sound* 36, crt* 20, bink 10): not data to name; they go with direct Win32 calls.

### Compiler-made vtables (C19)

Most objects still get game.exe's vtables: the constructors we wrote out store the address (`ei::SetVtable`), and the
image's copy of each table holds code cells that image_code.cpp points at our registered functions. C19 makes the
classes real C++ virtual classes, one family per batch, so the compiler builds their vtables and game.exe's tables
are left to nobody. The split (batch 1, tools: `find_vtables.py`'s config/vtables.csv, 619 vtables, 7,258 slot code
cells; who names each address in src/ and tests/, comments blanked):

| Family | Vtables | Slots | What ties it |
|---|---|---|---|
| container instantiations (mfc_array / mfc_slots / mfc_list, record_arrays, class_templates) | 159 | 475 | one table per CArray / CList instantiation: a template parameter of the shared templates |
| world, units, items, spells, effects, AI, combat, party | 101 | 1,358 | |
| interface (ui*, console) | 88 | 1,574 | slots called by index (`vtable_[n]`: 74 sites, ui.h 28) |
| library (MFC, CRT; RTTI type_info) | 54 | 522 | MFC's own classes; with C14 |
| network (net*, link*, player_sync, server*) | 45 | 422 | message classes (net_messages.cpp: 117 stores) |
| no store in src/ | 39 | 421 | only game.exe's code installed them: 13 retired (batch 2, below); the rest are tails or EH data |
| render, device, textures, figures, scene, sound | 39 | 516 | |
| map objects (map_object*, object3d*, trap, magic trap, lever, torch, sky, lightning) | 39 | 947 | |
| named by the harness | 35 | 940 | the window labels (`kKnownWindows`, `WindowReady`, the census's `W<vtable>#n`), the digest's AI modes and CSpellEffect, harness_savetrace's stream Write slots, the console streambuf's sync slot (`PatchSlot`) |
| streams, files, resources, core | 17 | 136 | |
| src/lib only | 3 | 7 | |

Nothing else depends on a vtable's address: saves and network messages carry values (no vtable comparison in src/),
game.exe's game classes have no RTTI (only MFC's have type descriptors; nothing reads them), find_vtables reads RTTI
names only for the csv. addon.dll writes one slot: ResOverride's 007432E0 slot 5 (mod_folders reproduces it).

**The method** (batch 1: the ground prints, ground_print.{h,cpp}, 0073F754 / 0073F760 / 0073F76C / 0073F778):
1. Pick a family whose vtables only its own files name (`python tools/named_data.py --split vtable` lists each
   vtable address with the files that name it; tests/ too: grep the harness), with no harness use (or replace
   that first, below).
2. The classes: drop `__declspec(novtable)` and the `kVtableAddress` constants; the virtuals stay declared in
   game.exe's slot order (every slot, so slot indexes keep their meaning) and hold the bodies (`TickImpl` becomes
   `Tick`); a derived slot calls the base's qualified (`GroundPrint::Tick()`).
3. The constructors become C++ constructors (the shared part a protected one, the copy the lists make a copy
   constructor); callers construct in place (`new (storage) BloodPrint(...)`, `new (to) BloodPrint(from)` in the list
   policies). Members must be trivially default-constructible (or initialised in the list as the original does):
   a constructor default-initialises what it does not name. The explicit `Destruct` / deleting destructors can stay.
4. The registrations stay (C20 retires them): virtual slots and constructors through `__fastcall` shims that call
   without dispatch (`self->GroundPrint::Tick()`, `new (self) GroundPrint(...)`; rewrite guide rule 4).
5. List the family's game.exe vtables in config/retired_vtables.csv (vtable, slots, class, declaring file):
   gen_image_cells.py moves their slots out of the code cells (group `vtable-retired`; the `vtable` group's count is
   7,258 less the retired slots) and writes their bytes zero in the image; named_data.py --check proves nothing names an
   address inside them (src/, tests/), no pointer cell points into them and no code cell is left in them. A vtable two
   classes shared (0073F778, the copy constructor's) goes when both do.
6. `python tools/gen_image_cells.py`, the checks, both builds, `work.py test <ID> --golden --jobs 4`.
- **Engine objects** (batch 2: CMusicSystem, CMaterial): CEngineObject's default slots are defined
  (engine_object.cpp: the destructor clears the owner, CopyFrom is CopyFromImpl, 5 / 9 / 10 empty, 6 returns 0), so a
  derived class with a C++ constructor gets the compiler's vtable; the constructor calls ClearFields. Slot 0 is the
  virtual destructor: the compiler's scalar deleting destructor runs the destructor and the class's
  `operator delete` (declare one that calls gamecrt::operator_delete: the global one is not the game's heap). The
  destructor body leaves out the members' destructors (C++ runs them after it). The registrations: Construct is
  `new (this) X()`, the destructor bodies call `this->X::~X()` (qualified: no dispatch), slots 11 / 12 overrides
  with shims calling them qualified. windows.h's GetClassName macro renames slot 11 in a file that includes it in
  between: engine_object.h, the class's header and its .cpp `#undef GetClassName`.
- **Batch 2's families**: the chunks (Chunk, RootChunk; 0073BE94 / 0073BE9C and the .mob root's 0073FAD4), the
  buffered streams (BufferedStream, BufferedFile over GameBuffer's slots; 0073F8C8 / 0073F8A4), CMusicSystem
  (00744098) and CMaterial (00743BB4). Left for later batches, with what each needs: CSoundManager (007440F0; its
  members include a CMappedResource, a polymorphic class whose own virtuals and destructor would have to be defined,
  and a dozen other member types to check for default constructors), CTexture (00743D24; its name_ is a CString C15
  retypes), CSkyObject (0073D5B8; abstract in C++ and sharing 0073C224 with CFigureMesh), the map objects (torch
  0073E834 with its tail 0073E850, lever 0073E740, trap 0073E548, magic trap 0073E63C: CMapObject's 61 slots need
  definitions first), the torch spec (0073E954 with its tail 0073E98C), the texture and material managers
  (CResourceManager's 26 slots), CArray<MusicTheme> (007440CC, with the container instantiations).
- **Tails in config/vtables.csv**: find_vtables.py splits a table at every address the code holds as an operand, so
  some rows are the tail of the row before them: split off at a devirtualised slot call (`call [0073BD98]` is
  CMemoryStream's slot 2, 0073BD90's 8 slots listed as 2 + 6) or at a byte match inside an instruction. A tail is never
  stored; retiring it zeroes live slots (the first try of batch 2 retired 0073BD98: every run crashed calling a null
  slot at startup). gen_image_cells.py's check refuses a retired row that no code of game.exe stores
  (`stored_vtable`: `mov r/m32, imm32` or `mov r32, imm32`) or that shares a slot with a row still in use (the overlapping
  rows 0073D760 / 0073D764 / 0073D768). A family's retirement takes its tails with it (0073E954's 0073E98C).
- **Retired with no class of ours** (batch 2; file `-` in the csv, the note names the storing functions): 0073B98C
  (an exception class, 0040C440 / 0040C500), 0073BFAC (CDebugPanel's base), 0073D860 / 0073D888 (the render helper's
  base and local), 0074019C (CUiMesh::SetVertexCount's local), 00740244 (the battle camera's input side, before
  InputSideDestroy's 007401D0) and the dead code's 0073C474, 0073CA00 / 08 / 0C / 10, 0073F7C4 / 0073F7F8: 135 code
  cells. Not retired from the "no store in src/" row: the tails 0073BD98 (CMemoryStream), 0073E98C (0073E954),
  0073EAA0, 007435B4, 00743618, 00743678, 007436D8, 00743738, 00743AA8, 00743ADC (devirtualised slot calls of the
  tables before them), 0073BF08, 0073E6E8, 0073E850, 0073F4E8, 00740FE8, 007418E8, 0074247C, 007435D8 (byte matches:
  inside the table before them), 0073D764 (shares slots with 0073D760 / 0073D768), 0073B984 (an exception base: six stores
  but some 140 compares with its address in game.exe's code; kept for the library batch) and the five one-slot rows 0074E824,
  00753B04, 00758904, 00758B0C, 00758B14 (EH unwind map entries, not vtables). The library kind's unstored MFC
  vtables (CClientDC, CWinApp, ...; config/standalone_unreachable.csv) go with the library batch.
- **Ids for later families**: the harness names objects by game.exe's vtable (window labels in goldens: the census's
  `W00740688#3`, `waitwindow`, the digest's AI modes). A converted class needs an id that both game.exe runs (golden
  recording) and the image give the same: e.g. the harness maps a vtable to a name (`kKnownWindows`' names) through
  game.exe's address in game.exe and through the class's compiler vtable (read from a constructed object, or
  `typeid` once no game.exe table is left in that hierarchy) in the image; when the label text changes, re-record with
  `--no-hooks --golden --fresh`. Slots the harness patches (`PatchSlot`, harness_savetrace) need a hook in our code
  instead.

### What the image needs in place of game.exe's module (S14, S16, S19)

- **Resources** (S18j): built into the host from game.exe ("How it runs"); the game's module is the host's.
  MFC's module state has null instance and resource handles in the original too (the game never runs AfxWinInit),
  so LoadStringA(NULL) loads from the process's own module in both. Checked by `render_cursor_present`'s
  `windowicon menu_icon` (the window class's icons are the module's icon 105, drawn to frames\menu_icon.bmp; its
  `cursorcheck` frames show the game's cursor, which is game data, not a resource).
- **game.exe at run time**: none since S18k ("What comes from game.exe at build time", above). The harness's run_all
  hands ei_launch none (S18k1).
- **Command line**: `ei_standalone ... -- <game args>` is WinMain's line and GetCommandLineA's after `"<game.exe>"`.
- **Code bytes read as data**: none left (R9b3). app_frame's cursor lock (004580D7) calls our rewrite directly; the
  harness reaches it and WinMain's camera (00457595), server update (00457314) and client segment (00457368) calls
  through hook points (`ei::mainloop::g_harnessHooks`, ei/main_loop.h; tests/verify/harness_hooks.h). ClassifyToken's
  keyword table (the immediate at 004CF69B, which Rebirth rewrote) is the vanilla table 0078BCA0 or the add-on's.
  `AddonPanelOffset` returns none.
- **DirectInput**: DirectInputCreateEx refuses an instance that is not a loaded module, so the image's import goes to
  a wrapper that passes the host's module. c_dfDIMouse (00745D00) points into game.exe's code section, so
  `CDirectInputMouse::MouseDataFormat` is our copy of the seven objects. Other dinput.lib data in .text would need
  the same.
- **Rebirth**: InstallHooks calls `rebirth_hook::SettingsOnly`: flags from ei_rebirth.ini and EI_REBIRTH,
  EI_Plugin's player count from `[Rebirth] NPlayers` (default 7). Miles's LoadLibraryA import refuses EI_Plugin.asi
  (mp3dec.asi loads). `starter::RebirthInstall()` is true for addon.dll or the image with a Rebirth flag on (the
  digest's and census's install lines follow it). GameMain reads addon's registry key with `NivalRegistry`
  (`Software\Gipat.ru\EI_Starter\EvilIslands`; with the vanilla key every Rebirth run differed from record 0). What
  addon and EI_Plugin patch is reproduced behind flags (docs/notes/rebirth.md): the multiplayer patches and Pause /
  Briefings / NewMaster's game-side parts (S19a), NewMaster's second master server opt-in (`SecondMaster=` in
  ei_rebirth.ini, empty in every harness run; S19c).
- **Mod folders** (S19b, src/game/mod_folders.{h,cpp}): Maps, ResOverride, CameraPatch, StreamPatch and MoviesPatch
  as addon.dll does them (rebirth.md, "Resources and paths"), active only in the image with the flag on. Sites:
  GetFile / ListFiles (plain bodies `GetFilePlain` / `ListFilesPlain`), OpenResource / Destroy,
  CFigureManager::CreateWithArchives, CLinkFigureLoader::Construct, CTextureManager / CMaterialManager::Create, the
  nine maps-folder users, CCamera::LoadPath, SwitchStream, the three BinkOpen calls. Checked by
  `rebirth_mod_folders` (a mod written by `tools/make_test_mod.py`, harness steps `addonpath` and `modfile`). Not
  covered by a run: StreamPatch, MoviesPatch, the extra / ignored archives, Maps' zone-table and quest sites (S19d).
- **Renderer**: the image always draws with the new renderer; there is no DirectDraw fallback (R11a, renderer.md
  section 1). EI_RENDER's `new,present` (run_all still sets it) changes nothing.
- **disabled.txt** never applied to the image and is gone (S21e).

### Generated code

- **Short functions** (S2): the 200 game functions under five bytes are in src/game/short_functions.cpp, generated by
  `tools/gen_short_functions.py` from their bytes (12 kinds: `Ret`, `Ret4/8/12`, `ReturnZero`, `ReturnMinusOne`,
  `ReturnTrue`, `ReturnFalse`, `ReturnThis`, `LoadDword<n>`, `MemberAddress<n>`, `LoadFloat<n>`), each compiling to
  the original's bytes, registered with `EI_REPLACE_SHORT`. `tools/check_short_functions.py` (build.cmd) checks each
  kind against game.exe and our code. A few were added by hand (particle element callbacks 00485530,
  00492FB0-004965D0: `EI_REPLACE_SHORT(Ret4)`).
- **Static initialisers** (S3): src/standalone/static_init.cpp by `tools/gen_static_init.py`. A small symbolic
  interpreter decodes each initialiser into stores, copies, fills, float folds (`Log` for FLDLN2 / FYL2X; .rdata
  operands as tagged literals, S18e1), calls (cdecl, stdcall, thiscall, through an import slot), `atexit` and the
  once-only guard bit; a single-use decodable
  callee is inlined, everything else is called at its address. One template, `StaticInitialisers(Do&)`
  (static_init.h), with visitors `Run` (the image; calls go to `ei::Code` of the address) and `Targets` (logs call
  targets without code).
  `tools/check_static_init.py` (build.cmd) re-decodes game.exe: when a helper gets registered later the output
  changes, so rerun the generator.
- **Exit functions** (S3a, S16a): src/standalone/static_exit.cpp, same tool: 130 whole-function patterns
  (`EXIT_PATTERNS`: `mov ecx, obj; jmp dtor`, flag-guarded, operator delete of a counted buffer, a counted
  `Release`, DeleteCriticalSection, the eh vector destructor iterator, the WSACleanup wait, `ret`) plus 60 run-time
  atexit registrations of function-local statics (PUSH fn, CALL 006E727B within 14 instructions). Their sizes go
  into config/extra_function_sizes.csv and function_sizes.inc (the generator writes them, the check compares).
- **Compiler jump thunks** (functions.csv kind "thunk") are registered with their target's replacement.
- **Dispatch-only functions** (`MOV EAX,[ECX]; JMP [EAX+n]`: 00675D40, 00469E00, 00469DE0) use `EI_REPLACE_DISPATCH`;
  the installer checks that our bytes are the same dispatch.

### Registration rules

- `EI_REPLACE` / `_METHOD` / `_DISPATCH` / `_SHORT` / `_STANDALONE` all go in (the last name is historical: it was
  "image only" while game.exe ran our code). `tools/check_registrations.py` (build.cmd) fails on two registrations
  of one address; it reads the built DLL's `.eireg` section (every macro leaves a `RegRecord`).
- Library functions need a row in config/extra_function_sizes.csv even when Ghidra has a function (the installer
  skips an unknown start); some Ghidra sizes run past the RET (0070CAD2, 006E6B7D: decoded sizes).
- config/library.csv names are often wrong for STLport / MFC rows (destructors named clear, AddTail named a
  constructor, 006EC8A8 `_mbsupr` lowers, 005E0550 is CTime::operator-). Read the code; the csv is add-only. Unquoted
  commas in names broke rows once (0055C730, 00561B00: now quoted).
- Library bodies live in src/lib, layout- and allocator-compatible with the exe; game-element instantiations in
  src/game (game_arrays, game_lists, game_slots, game_hashes, record_*, library_*). C13 made the matching CRT / MFC
  call sites direct (libcrt::Fopen ...); the rest still go through forwarders (C13a2).

### The CRT (src/lib/crt*.cpp, namespace `libcrt`; S4, S4a-S4d, S16, S16a, SV7c2)

- **State** (S4, S4c) is read where the exe keeps it: the game runs under a real LC_CTYPE locale (`__lc_handle` 007CABF0
  nonzero after the text manager's Create 004DED20 calls setlocale(LC_CTYPE, the "Font CharSet" option, "english"
  by default -> "English_United States.1252") and `_setmbcp(_MB_CP_LOCALE)`); tolower / toupper take `_tolower_lk`'s
  LCMapStringW path, compares fold through a table rebuilt on a locale change, classes come from `_pctype`, lead bytes
  from `_mbctype`. Single-byte paths only (`__mb_cur_max` 1). Until the game sets the locale it is the C locale.
  Our setlocale / _setmbcp (crt_locale.cpp) is getqloc.c's name search, `__init_ctype` (GetStringTypeW, called
  directly) and _setmbcp with setSBUpLow; no locks, no LC_ALL.
- **Heap** (crt.cpp): malloc 006EABD1, free 006EA8AD, realloc 006ED69A, calloc 006F0317 (count * size unchecked),
  _msize 006EEC7B, _strdup 006EEC50 use a Win32 heap of their own, `_crtheap` 007CAF08, made with `HeapCreate(0,
  0x1000, 0)` at the first allocation, sizes rounded up to 16, through the image's import slots (gameapi.h), so the
  harness's heap patches act on the image as on game.exe (SV7c2). operator new runs MFC's handler loop
  (`_afxNewHandler` 00796E14, initial value `AfxNewHandler` 0070058F: throws the static CMemoryException); the
  STLport node allocator 00405950 / 00404D70 with refill 004060E0 and chunk_alloc 00406B70 is ported exactly (the
  pool is the exe's data). Nothing frees across heaps: our code takes game memory only through libcrt; the host CRT
  keeps its own (std containers, FILE buffers).
- **atexit / exit**: our list, then the host CRT's exit. rand / srand: the seed is a thread_local in the image
  (`libcrt::RandState()`, which the harness's digest and randtrace read; the image has no _getptd 006F00D1).
- **Formatting** (crt_format.cpp): sprintf 006E8715, vsprintf 006ECC47, fprintf 006EAEB0, printf 006E955F are VC6's
  `_output` (the `__lookuptable` state machine copied from 007476D0, `%I64`, `%Z`, "(null)"). Floats are not "17
  correct digits": `$I10_OUTPUT` builds them from a 12-byte long double (96-bit `__ld12mul` with truncated partial
  products, the exe's power-of-ten tables 0079AE60 / 0079AFC0), then `_fptostr` rounds half up; three exponent digits;
  "1#INF" / "1#IND" / "1#QNAN" / "1#SNAN" ("%.2f" of infinity is "1.#J"); -0 prints as 0. atof 006E911A is
  `__strgtold12` (24 digits, the 24th bumped when more follow and it is >= 5; exponents beyond 5200 saturate; 'd'
  accepted) then `_ld12tod` (ties and some values above round down). sscanf 006EAEEC is `_input` for integers, %n,
  %c / %s / %[ (floats and wide characters end the scan; a format that starts with %n reads a stale stack byte in the
  original). The decimal point comes from 00797FE8.
- **Files** (crt_file.cpp): _open is VC6's _sopen through the game's import slots (so the harness's save / install
  redirection applies), the handle wrapped as a host descriptor; fopen parses the mode as `_openfile` and wraps with
  `_fdopen`, so the game's CrtFile* is the host's FILE*; the stream and descriptor functions are the host's (a bad
  descriptor returns the error instead of the host's invalid-parameter abort); `&_iob[0..2]` map to the host's std
  streams; _access and _findfirst go through the game's slots (the game never closes the find handle). fflush
  006ED534 is the host's. Fact from S4b1: a truncating open of a just-written file, then a write past its end, can
  read the gap back as old bytes on this machine under load (plain CreateFileA too).
- **x87 entries** (crt_x87.cpp, naked, `__asm` exempt in check_cleanup): _ftol 006E853C (decodes the 80-bit value
  with integers; the integer indefinite for NaN / infinity / |x| >= 2^63), _CIacos / _CIasin / _CIpow / _CIfmod and
  their stack forms 006E7334 / 006E83D4 / 006E95A0, _hypot 006FD026. The CRT's control word keeps the caller's
  precision and rounds to nearest, so the port computes at 53 bits and rounds each step to the caller's precision;
  pow splits y log2(x) with fma errors, ties-to-even, exp2, ldexp; fmod is exact (keeps the CRT loader's NaN payload);
  `_hypothlp` runs at 64 bits with every step stored to a double and a denormal result cut off. errno / _matherr are
  not reproduced. The control word (SV10i): FNSTCW, and FLDCW only when the wanted word differs (x87::ControlWord /
  SetControlWord), put back only then; frexp / ldexp of normal doubles by the bits (std::frexp / std::ldexp save, set
  and restore the word on every call: the UCRT's `_ctrlfp`, 9% of the town's main thread through Vector3::Length).
  The FPU keeps bits 0-5 and 8-12 (bit 6 reads 1, 7 and 13-15 read 0), the same bits __control87_2's abstract form
  round-trips, so the words are the same. Checked old against new (build/SV10i/equiv.cpp, not checked in: main's
  file and the new one compiled side by side) on 50M calls of Hypot, CiPow, CiAcos, CiAsin and StackFmod under every
  precision, rounding and infinity control: results, control word after, x87 status flags and errno all the same;
  a one-line mutation (Hypot at 53 bits) differs. No vectors of this file exist (the crt family is formatting).
- **FISTP for std::lrint (SV10j)**: `x87::RoundToInt` (x87.h; FLD float / double, FISTP dword: the original's own
  rounding, e.g. 004BD339 and 00553420) in the 25 places that called std::lrint (the AI's cells, the scripts' FX and particle
  ids and delays, combat and armour slots, the tornado, money and level). The UCRT's lrintf / lrint read the x87 and
  SSE control words on every call (acrt_fenv_get_control). Checked old against new (build/SV10j/equiv.cpp, not checked
  in: std::lrint and RoundToInt side by side, cw 007F / 027F / 037F with every rounding): every float bit pattern under
  every rounding and precision with MXCSR at its default, and 50M doubles (random bits, boundaries, halves, specials).
  At round-to-nearest (the game's: the main thread's word is 007F) every in-range float gives the same value, the same
  x87 status flags (PE), errno and control word at 24, 53 and 64 bits; at chop too. Differences, all where FISTP is the
  original: NaN, infinities and values rounding outside the int range (1,644,167,166 float patterns) give FISTP's
  0x80000000 with IE, where lrint gives 0 with errno EDOM (float 2^31 alone gives 0x80000000 without errno); at round
  down / up lrint follows MXCSR's rounding, not the x87 one (equal when both are set alike); lrint sets MXCSR's PE flag;
  lrint(double) at 24 bits computes at the FPU's precision and misrounds values near +-2^31 (1.6% of the doubles there;
  no game call passes a double). Mutations (the double overload through a float; the float one truncating) differ in
  61,547 / 786,725 cases at 027F. No call site depends on lrint's 0: Usable / the armour layers test > 0, the tornado clamps
  below 0, the armour's int16 store keeps 0; the script ids (a NaN id now takes the next negative id, as 004BD347 does),
  delays, steps and AI cells follow the original. Goldens the same.
- **_controlfp without the UCRT (SV10k)**: `libcrt::Controlfp` 006EBBB4 is the original's own steps: FNSTCW
  (`x87::ControlWord`), 006EBC2A (hardware word to the CRT's abstract one: _EM_* in bits 0-4 in the reverse order of
  the hardware's masks plus denormal in 19, _RC_* in 8-9, _PC_* in 16-17 with 64 bits = 0, _IC in 18), the merge
  `(abstract & ~mask) | (value & mask)` (returned as is, junk bits of `value` included), 006EBCBC (abstract back to
  hardware) and FLDCW (`x87::SetControlWord`), the last only when a bit the FPU keeps (0x1F3F) would change. No MXCSR
  effect, as VC6's. The UCRT's `__control87_2` (0.2-0.4% of the town's / fight's main thread, two calls per lit sector
  from `SectorData::Light`) is gone from the profile. Checked old against new against the original's own bytes
  (build/SV10k/equiv.cpp, not checked in: game.exe 006EBBB4..006EBD45 copied into executable memory and called, the
  UCRT version and the port side by side, built with the image's flags): return value, control word, MXCSR and x87 status
  after, over every 16-bit start word x 90 masks x 52 values (307M calls), every FPU-kept start word x every abstract
  word x 8 masks, the callers' sequences (0,0) / (0x300,0x300) / (previous,~0) from every start word, and 2M random
  calls: the port equals the original in all. The UCRT version (sampled every 64th call) differed from it in the
  control word in 437k of 4.9M calls, never in the return value or MXCSR (probably start words whose reserved bits the
  round trip changes, which the UCRT version skipped setting: the original always loads the converted word).
  Mutations (_PC_53 mapped to 24 bits; the FLDCW test ignoring bit 3, the overflow mask) differ in 69M / 2.9M cases.
- **Exceptions** (crt_except.cpp): _CxxThrowException 006E7819 raises 0xE06D7363 with the thrower's ThrowInfo, so
  the exe's catch tables and ours match; MSVC 6 std::exception (vtable 00747290, "Unknown exception").
- **iostreams** (crt_iostream.{h,cpp}): MSVC 6's old streambuf / ios / ostream layouts, the inserters (numbers
  through the game's sprintf with VC6's formats: "%%%s.%.0ug", precision capped at 15), endl 004BA9F0, the streams'
  constructors and destructors. The eight ostreams 007C55A8-007C5810 all write to console streambufs (console.h).
- Not ported (no callers): acos / asin / fmod's plain forms, streambuf() 006FCA77, ios(streambuf*), ostream(),
  ostream_withassign. The C initialisers `__onexitinit`, `__initstdio` and VC6's unhandled-exception filter are not
  run: the image's atexit, streams and crash filter are ours / the host's.

### MFC (src/lib/mfc_*.{h,cpp}; S5-S8b, S3d, S3f, S16)

The exe links VC6's MFC 6.0; WinMain does not go through CWinApp (CWinApp's constructor 0070D654 has no caller).
- **CString** (mfc_string, `mfc::impl::CString`, `CFixedAlloc`, the five string pools 007C8B08..007C8B80 and
  007C8D18). 00700EFE is MakeLower (it calls `_mbslwr`); 00700EEC is the real MakeUpper, unreached.
- **Collections**: mfc_coll (CStringArray, one `PlainArray` for CWordArray / CDWordArray / CByteArray / CUIntArray;
  ini_file's "DWordArray" 6FF933 is CUIntArray). CArray (mfc_array, `ArrayAbi<T, Policy>`): the element policy is what
  the instantiation's ConstructElements does after MFC's memset (nothing, an inlined constructor, or
  `EachElements<T, Init, Destroy>`). CList / CMap / CPlex (mfc_list: `ListAbi`, `MapAbi`, HashKey `key >> 4`); node
  sizes from each NewNode's CPlex::Create. FreeDataChain returns 0: the original leaves the caller's EAX, which
  AIPath::Reset 005C7D50 returns.
- **Slots only the exe's vtables reach** (mfc_slots, game_slots; S6a, S6c, S6d): scalar deleting destructors
  (`ScalarDelete<&Destruct>`), Serialize as afxtempl's with raw-byte SerializeElements (`SerializeCArray / CList /
  CMap<Abi>`, `SerializeCListThrough<ListTemporary<...>>` for lists whose loader goes through a temporary). The
  element policy must be the one the instantiation's inlined SetSize / NewNode uses, which is not always its
  destructor's (00450570). Quirks kept: a loaded list's elements borrow the source's references; a wave record's
  load frees the source's name buffer; the token and wave loads leak each element's temporary block.
- **Files, archives, exceptions** (S8, S8b): mfc_exception (the four throws through 006E7819; the exception link is a
  thread_local at 00702107; the four static exceptions; CSimpleException 0x98 bytes; ReportError / GetErrorMessage),
  mfc_file (CFile, CStdioFile on the host CRT, CFileFind, the name getters via comdlg32's GetFileTitleA), mfc_time
  (host `_mktime32` / `_localtime32_s` / `strftime`, VC6's time() from GetLocalTime), mfc_archive (CArchive and the
  CString operators), mfc_resource (AfxLoadString through LoadStringA on the module state's null resource handle,
  AfxFormatStrings: "%0" reads the pointer before the array, as the original).
- **Windows, GDI, state**: AfxMessageBox has no owner and the module's file name as title (the image shows
  ei_standalone.exe). CDC (0x10 bytes), CPen, CFont, CGdiObject; handle maps (mfc_handle_map: CMapPtrToPtr, whose
  association has no hash field; CHandleMap::FromHandle) are thread_local per thread and never freed; with no
  CWinApp the temporary maps are never emptied (no OnIdle), in the original too. mfc_state: CThreadSlotData,
  CThreadLocal / CProcessLocal destructors (nothing fills them in the image), AUX_DATA afxData 007C90C8, MFC's global
  locks (17 lock types: an 18th overwrote _afxThreadData), _AfxInitDBCS, the halftone brush term (registered twice:
  runs on a null brush the second time). `ImageModuleState` at AfxGetModuleState 0070CE8C: one module state, only
  the class list. mfc_thread: AfxBeginThread / AfxEndThread on host _beginthreadex. CWnd::DestroyWindow 007051A1 is
  a forwarder that no game window reaches (no game window is a CWnd).
- Not ported, unreached: CString Delete / Insert / Replace / MakeReverse / FreeExtra / Span* / AnsiToOem / wide
  constructors / FormatMessage; CPtrList, CPtrArray (S5a).

### STLport (src/lib/stlp_*.{h,cpp}; S9-S12g)

One body per member over an element policy, `*Abi<T, Policy>` shims with each instantiation's calling convention.
- **Node allocator rule**: the node size picks the free list; a wrong element size corrupts another size's pool
  (005392C0 holds 20 bytes in a 32-byte node). Read sizes from each `_M_create_node` / `_M_new_node` / clear's free
  list.
- **list** (stlp_list, stlp_list_refs: Share + Destroy, counted + release, held engine objects, CString); game records
  in record_lists, the connection's in library_net_lists. Where the original copies uninitialised stack bytes into a
  default element, ours copies zeros (every caller overwrites them).
- **vector** (S10, S10a; stlp_vector, stlp_vector_refs; record_vectors with `records::Memberwise` in record_members.h).
  `records::IsPlain<T>` decides plain data: never CString, stlp::string, stlp::vector or arrays of them (an array of
  vectors is trivially copyable to the compiler, so a default record's lists were garbage once: S10b / S10d).
- **hashtable** (stlp_hash: dword keys, `StringKeyTraits`, `CStringFloatTraits`, `StringListTraits`,
  `DwordObjectTraits`; game_hashes): default 100 -> 193 buckets; node sizes 12 bytes (most), 8 (the dword sets),
  20 (string keys, on the 24-byte list), 32 (find_or_insert 004E2E90).
- **deque** (stlp_deque: camera view stack 0x24 in 0x1F8-byte nodes, material entries 0x10 in 0x200).
- **tree** (stlp_tree, stlp_tree_abi `TreeAbi`, stlp_tree_cstring; net_connection_trees, library_trees): SGI 3.x's tree
  (`_M_insert(x, y, v)`, RET 0x10, the hint insert falls back to `insert_unique(v)`); the node colour is a byte;
  `stlp::CStringLess` is `_mbscmp < 0`; the sent-ids map compares signed. operator[] of records and attached ids
  inserts a partly uninitialised value in the original.
- **string** (stlp_string, `StringAbi`): append's capacity follows size + max(size, n) + 1; the from-text constructor
  reads its allocator argument (callers pass a real one); `ThrowOutOfRange` builds MSVC 6's out_of_range
  (__Named_exception vtable 0073B994, out_of_range 0073BE8C, throw info 0074C640).
- **pair** (stlp_pair), **sort** (stlp_algo.h `stlp::Sort<T, Less, Ops>`: STLport's introsort with median of three by
  reference, unguarded partitions and inserts, heap sort at depth 0, element policies with `Held<T, Ops>`
  temporaries; registered sorts in src/game/stl_sorts.cpp). Comparator facts: the save list sorts by `!(a < b)`
  (newest first; equal times compare true both ways), the objectives' quest sort looks names up in the quest map
  (adding missing ones), the picking hits use `DepthLess`.
- Unregistered twins with the same instructions (no code of ours calls them): 00429DB0, 004FAF10, 0042B3C0,
  0042B3F0, 00404D50, 005E1560, 00457940, 004FC0C0, 004217C0; "unreviewed family" rows 005F8E90, 005F92A0,
  005F94B0, 005FB810.

### Compiler-written helpers of game types (S3b, S3c, S7a, S13, S13a, S13b)

The global constructors only static initialisers call (the six databases, logic settings 005CDFC0, SavedParties,
both worlds: database_construct.cpp, world_construct.cpp), the exit callees (database_destruct.cpp, the worlds'
and Terrain / Sector / particle manager destructors in world_destructors.cpp), record destructors / copies /
assignments (record_lifetime.h: `DestroyVector`, `AsCString`, `LeadingStringDelete`), the database resets
(database_reset.cpp) and the script lists' by-value copies (game_lists). Bodies that are library rows are called
through the library's address or inlined from lib/, never registered on the game side. Quirks kept: the link queue's
stream copy 00479410 reads the new block's uninitialised bytes into the source; 004CC0C0 copies a command's float
arguments as dwords (ours through the FPU: only a signalling NaN differs).

### Rules from real bugs

- A NaN in a sort key makes a comparator irreflexive and STLport's unguarded scans run off the array: the draw list
  sort crashed in UnitOrderLess::Less (S17a: a shadow-only object's never-written view depth); CObject3D::Construct
  now zeroes it. Any comparator over a never-set float needs the same care.
- A stolen-byte count must cover whole instructions (`MOV [ESP+0],0` is eight bytes); check_stolen.py checks the
  recorders' counts.
- Leftover memory is not preserved (user 2026-10-15): where the original reads never-set bytes, ours writes a
  canonical value (zero) and the harness patches the original the same way (standalone-verification.md section 5).

### Open

S2a (short getters as named members), S5a (unreached MFC pieces), S11b, S16e (`find_vtables.py` should take only
function starts as slots; the dashboard's library count includes library.csv's "not a function" rows), S19d. The console's `disconnect` (005DFC10 -> GameFlow 00581620 ->
CWorldClient::DisconnectLocalPlayer 00566E00, S16c) cannot be a scenario: in net_lobby's lobby the client's next
segment then ends the game in its "packet parsing error" box, the original's too. Multiplayer client paths and many container members are reached by no
scenario (tests/coverage/coverage.csv).

## 3. Runtime: MFC, STLport, CRT later

MFC in use: containers (`CArray` 470 qualified uses, lists 218, `CMap` 4), `CString` 2,546, files and archives 91, a
little GDI, `AfxMessageBox`, the TRY / CATCH link. After C13 / C15 (C14): `CArray` to `std::vector`, lists to
`std::list`, `stlp::string` and `CString` to one string type, `stlp::vector / list / map` to their `std` twins; the
framework pieces stay small classes of our own (linking real MFC would tie the build to Windows, platform-layer.md).

What must survive:
- **Hash map order** (39 `stlp::hash_map` uses): iteration follows STLport's bucket counts and insertion rule. A loop
  that feeds game logic, saves or network messages keeps our copy of the container.
- **Sort order**: STLport's `sort` leaves equal elements in another order than MSVC's (the objectives list shows it).
  Keep the original algorithm at those sites.
- **CRT behaviour**: `_ftol`, `rand` (MSVC6's generator and seed), sprintf's float digits (section 2), the pooled
  small-block allocator only where code depends on reuse order (none known).
- **Formats**: the archive encoding of strings and arrays in saves and network messages is a pair of read / write
  functions, independent of the container type.
- **Struct sizes**: a `CString` is 4 bytes, a `std::string` 24; every raw offset left breaks when a member changes
  size, so this waits for the member cleanup.
- **x87 maths**: the exe stays a 32-bit MSVC build with the same float settings until the float checks are replaced.

## 4. UTF-8 instead of cp1251

Goal: every string inside the game is UTF-8; original assets stay as they are and are converted when read. Saves and
multiplayer need not match the vanilla game (user, 2026-10-13). Behind one compile-time switch; the check is that
frame captures of a scenario are the same with the switch off and on.

How text moves today: assets hold cp1251 bytes; the interface draws through GDI's ANSI calls with a font whose
charset comes from the "Font CharSet" option; typed characters arrive as WM_CHAR in the ANSI code page; file names
(save names typed by the player) go to the ANSI file calls.

| Ticket | Work | Size |
|---|---|---|
| U1 Asset boundary | One `FromAssetText(bytes, codepage)` and calls to it where display text is read: the text database (`TextSource`, texts.res), .reg string values (menus.reg, quest.reg, smessbase.reg), strings in .mob files (unit names, dialogue, script literals), credits.scr. The source code page is a setting (1251). Identifiers and file names inside assets are ASCII. RTF (titles) carries its own code page. | 1 |
| U2 System boundary | Wide versions of the file, registry, window title, message box, clipboard and key name calls, converting at the call. 19 direct ANSI call sites outside src/lib, plus `gameapi.h`, `CFile` and `CFileFind`. | 1 |
| U3 Drawing and measuring | The interface's text draw and extent (005F8340 and the draw routines), the console and the debug panel call the wide GDI functions; the charset option is dropped. Falls away if the native UI's font rasteriser comes first. | 1 |
| U4 Typing | Unicode WM_CHAR; the edit field, console line, chat line and name entry move the caret, delete and limit length by character, not byte. | 1 |
| U5 Audit | Identifier (leave) or display text (fix) per site: 99 case-insensitive compares and case changes (Cyrillic needs a small case table once the CRT's code page is gone), 95 fixed `char` buffers, per-byte indexing of text (about 37 sites), `%-20s` padding in console tables, word wrap inside a long word. | 1-2 |

Byte-equality compares, hashing, copying and formatting work unchanged on UTF-8.

## 5. The hook retirement (S21-S21e, 2026-10-16)

User 2026-10-15: the standalone image is the only product, checked by the goldens and the function vectors.
- **Gone**: hooked runs of game.exe (`--hooked`, `--native`, `--dgvoodoo`, the hooked side of `--widescreen` and
  compare.py, `EI_COVER=hooks`, `EI_PLAIN`); the verify wrappers (640 tests/verify/verify_*.cpp, verify.cpp,
  verified.txt, disabled.txt, the start-up checks; the last commit with them is S21d's parent); InstallHooks'
  game.exe mode (trampolines for rewrites, Rebirth detection, rebirth_sites.inc, DumpPatchedFunctions, FollowAddon,
  `EI_RECORD_OURS`); check_returns and check_patched_calls; the "call Rebirth-patched callees through their
  addresses" rule (rewrite guide rule 9: Rebirth's patches are flags now).
- **game.exe** always runs the original: ei_launch sets `EI_NO_HOOKS=1` for it (`--no-hooks` is accepted and changes
  nothing; the image never gets it). Its only hooks are the vector recorders with `EI_VECTORS=1` (refused when
  addon.dll is loaded: recordings are the vanilla code's) and the harness's own patches.
- **Builds**: `release` is the product (the image); `verify` (CMake option EI_HARNESS) is the image plus the harness
  (tests/verify) and the recorders (tests/vectors).
- **What stays**: the original's harness (goldens, vectors, widescreen references, the reference cache), the image's
  harness, golden.py / savefile.py, ei_vectors.exe, the frame tools, the Rebirth flags, and the build checks that
  hold for the image (check_registrations, check_short_functions, check_static_init, check_fp_fold, check_thunks,
  check_x87, check_forwarders, check_cleanup, check_signatures, check_stolen for the recorders).
- **Scenarios** (S21a): a line `# no original: <install>... <reason>` leaves a scenario's `--no-hooks` runs out of
  plans (renderer-only scenarios, rebirth_flags on the vanilla data, render_present_fullscreen). render_movie and
  render_terrain were dropped (their goldens equalled render_movie_present's / render_scene's).
- How verification works now: docs/notes/standalone-verification.md section 1.
- EI_PROFILE=1 samples the image (S21f): EiStandaloneMain calls InstallProfiler before WinMain; none of the samples is
  game.exe's code (it has none), and profile_report names the image's functions `ours:`.
- S21g: `harness_heap.cpp`'s order observer and heap::Aside are gone. S21h: the EI_REBIRTH_IMPLEMENTS declarations are gone
  (they disagreed with config/rebirth_patches.csv: 23 declarations named patches the csv does not have, the Eip* plugin features and three WideScreen
  functions, and 37 "implemented" csv rows had none; the csv's status column is the record).
- Left: 81 src/ comments about calls through an address "so the verify wrapper sees it" (C13 / C13a1 made most such
  calls direct; the rest are C13a2's), tests/coverage/reach.txt (the last hook build's reach, kept).

**S21c1 (2026-10-16): the original's goldens repeat again; the cause was the user's real mouse.** Two fresh
`--clock virtual,randtrace` passes of the six scenarios: 1 and 2 goldens differed (zone_gz15h_tornado V / R), each
time first in two client units' flag 0x400 (CWorldClient's "model kept loaded" mark, CollectVisible: which models were
in view), then the camera in the saves (focus x 109.999 against 111.684) and the particles' draws (CParticleSource::Tick,
one draw apart after a sound started in one run only), and from there every random draw. All on the client side, in
view of the camera: the camera had moved. The harness answers GetCursorPos with its own mouse, but the original's
window under dgVoodoo covers the desktop (client area at 0,0, 3840x2160) and is not cloaked (WB12 cloaks the image's
only), so the real cursor, wherever it rests, is over the topmost game window: Windows sends that window WM_MOUSEMOVE
with the cursor's coordinates when the cursor moves and when a starting game's window appears under it (logged: the
cursor at (0, 1823), the left edge, which scrolls the camera). The input dispatcher (00457970) takes it as the harness's
own. The game also polls a background DirectInput mouse (0065EAD0 non-exclusive, background; 005E52E0 sums its motion
for right-button turning), which reads the hardware whatever window is in front. Which run got the stray move
depended on where the user's cursor was and which window was on top. Why it showed from about 04:18 on is not proven;
likely the image's windows (the default suite since S21, cloaked since WB12) stopped lying over the original's, so
the original's own windows now take the stray moves. WB14 (loopback binds; 0 binds in single-player runs) and SV7b6a /
SV7c2 were not involved: with the mouse left out and all of them in, the goldens repeat.
- tests/verify/harness_input.cpp / .h (both the original and the image): the game window's subclass
  (AlwaysActiveWindowProc) answers WM_NCHITTEST with HTNOWHERE, so the real cursor over it makes only non-client
  messages (to DefWindowProc); the harness's posted messages never hit-test. DirectInputCreateEx's import slot
  (0073B044) is routed: the game's CreateDeviceEx for GUID_SysMouse gets its device's GetDeviceState wrapped, the
  state zeroed after each read (no motion, no buttons; counted when the hardware had any). The report's `MOUSE:` line:
  reads, real moves left out, hit tests over the window.
- Runs (`work.py test S21c1 ... --jobs 4`): before the fix, two passes of the six (randtrace) gave 1 and 2 differing
  goldens. With it: the six scenarios 12 the same (3 runs with the real cursor over the window, left out). `--no-hooks
  --golden --fresh`, every golden scenario: pass 1, 136 runs ALL PASS, 126 goldens the same, 0 differ (32 runs with
  hit tests left out, 1 to 11 each; no DirectInput motion: the user's mouse stood still); pass 2 the same (126 the
  same, 0 differ). The first build routed the DirectInput slot at the harness's start, which in the image came before
  the host's own routing of it (RouteImageCalls: "not found to route", then E_INVALIDARG and the game's "Can't create
  direct input object" box in every image run); it is now routed at the game's CreateWindowExA. Final build: `--golden`
  (the image) 186 runs ALL PASS, 174 goldens the same, 0 differ; `--no-hooks --golden --fresh` twice: 136 runs ALL PASS,
  126 the same, 0 differ each time (about 30 runs a pass with the real cursor over a window, left out). After merging
  main (S21d): `--golden` 186 runs ALL PASS, 175 the same, 0 differ; `--no-hooks --golden --fresh` 136 runs ALL PASS,
  126 the same, 0 differ.
