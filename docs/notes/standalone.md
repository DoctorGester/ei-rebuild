# Standalone exe: the address image, the library ports, UTF-8

The product is the standalone image: `ei_standalone.exe` lays out game.exe's data without its code and runs our
code from `ei_hook.dll`. game.exe runs only as the original game (the harness's references). This file holds how
the image works, where the library code lives and what it must keep, the UTF-8 plan and game.exe's role. How
changes are checked: docs/notes/standalone-verification.md. Per-ticket history: `git log --grep=<ID>`.

## 1. What ties the code to the exe

The list, each item with the ticket that removes it: docs/standalone-cleanup.md. The counts: `python tools/progress.py`
and `tools/dashboard_data.py` (the dashboard's standalone panel). The game glue (00578xxx) ignores
`this`: its world_glue.h functions are called without the dummy this / EDX.

**Finding game.exe (S18e3).** Every tool that reads game.exe (rdata_literals, exe_strings, gen_static_init,
gen_short_functions, gen_image_cells, gen_iat, check_signatures, check_stolen, find_vtables,
find_dead_code, standalone_gaps; the build reads none since C25c) goes through `tools/exe_path.py`: `--exe` (or `EI_EXE`), else
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
`--skip` leaves addresses), not typed. The empty string 0079B298 is the named `game::kEmptyText` (C18; its readers
copy or compare the text, none compares the pointer). Keep a string by address only where the game writes into the string
(the window title 0079BB24, the digit at 007930F6, the pointer cells LoadingMovieName 00791EE0), and in the CRT
(`_iob` is not a string). A pointer-sized cell read as
`const char* const*` holds the address of the text, not the text: read the cell's pointer first.

## 2. The image

### How it runs

- **Host and image** (S1, src/standalone/host.cpp, src/hook/standalone.cpp). `ei_standalone.exe` is an ordinary exe
  (S18k: relocatable; since 2026-10-21 temporarily a fixed preferred base 0x01000000 without ASLR, so it never
  loads inside game.exe's range) that reads no game.exe. **The guard**: first thing in main it reserves every free
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
  **Resources** (S18j, C23): the host has none of game.exe's. Its one resource is a VERSIONINFO of its own
  (`src/standalone/host.rc`, committed, linked by CMake's RC language); the game's strings are literals in our code
  (the window title "Evil Islands" and class "EIGAME" in app_main.cpp; MFC's strings in `mfc_resource.cpp`'s table,
  only the ids our code can reach); the bitmaps, dialogs, cursors and icons were never loaded, and the window class has
  no icon (C24). WinMain's hInstance and GetModuleHandleA(NULL) are the host, so DirectInput needs no wrapper;
  GetModuleFileNameA(NULL) names ei_standalone.exe (the
  message box title; app_dxversion's exe folder, which only tells dgVoodoo beside it from the system's DirectDraw;
  GetCommandLineA's first token too).
  Then it loads ei_hook.dll, which keeps .rdata (with the import slots) and .data in its own storage
  and resolves game.exe's imports as it initialises (S18f; since S18k DDraw, mss32 and binkw32 from the working
  directory, the game folder, else System32, so Rebirth's dinput proxy and addon.dll stay out; "Own data" below);
  then `InstallHooks` takes the registrations as the code for their addresses ("Code", below). Nothing runs in or
  jumps through game.exe's code. `ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game args>]`, run from the game
  folder; without `--run` it builds the image, checks the code table and the cells and exits.
- **What comes from game.exe at build time** (S18k: nothing at run time): the sections' bytes
  (`tools/gen_image_cells.py --bytes`, build/<cfg>/image/, which also checks the 1.06 timestamp; C25 measured which
  of them the image reads: "The build without game.exe" below); the section places are constants (game_data.h, image_code.cpp, hook.cpp's `kCodeBegin`-`kCodeEnd`, the registrations'
  bounds). InstallHooks reads game.exe's file only in game.exe itself (the recorders' stolen bytes). The 8
  dispatch-only functions (`MOV EAX,[ECX]; JMP [EAX+n]`) need no bytes since C25: the four with a port are
  EI_REWRITTEN (their callers call the virtual), `EI_REPLACE_DISPATCH` and its byte check are gone.
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
  zero but the committed cells, from 0073B000 up to the last nonzero dword (C25c: no game.exe; `tools/gen_image_cells.py
  --bytes`, a CMake custom command) into build/<cfg>/image/image_data_bytes.inc, not committed;
  image_data.cpp copies them into the storage, the rest is zero. **Every 1.06 game.exe has the same .rdata,
  .data and .rsrc** (checked 2026-10-21 on run\vanilla, run\rebirth and the five copies under F:\games: vanilla's
  and Rebirth's differ in one .text byte only, S18e3).
  **The cells**: 1243 (src/standalone/image_cells.inc, written by `tools/gen_image_cells.py`, which build.cmd
  runs with `--check`): the aligned dwords of .rdata (past the import table) and .data whose value lies in the
  range, less text and the cells inside named ranges (C18, below); 610 point into .data, 633 into .rdata. Text that reads as an address is of two kinds:
  string tails (the dword's bytes printable up to a zero and the four bytes before printable too) and short words
  ("Any", "May", "sky", "say", "day": `xxy\0` reads 0079xxxx) whose target would be mid-text (the byte before it
  printable ASCII; a string pointer points at a start). Into .rdata the filters are not enough (`?t\0` reads
  0074xxxx: "Nov", "Thu", "pow", "Div", "net" pass them and are the tool's TEXT list; real pointers such as
  `8]t\0` read as text too, so no stronger text rule is safe): every cell into .rdata is either in a structure
  the tool walks (STRUCTURES, counted per kind: the C++ EH tables, FuncInfo (magic 19930520) -> unwind map 2963,
  -> try block map 378, try block -> handler array 380, handler -> the caught type's type descriptor 26 (these four
  are counted but no cells, C20b, below), ThrowInfo -> catchable type array 10, array -> catchable
  types 41; RTTI, a vtable's slot -1 -> complete object locator 53, locator -> class hierarchy 53, hierarchy ->
  base class array 53, array -> base class descriptors 128) or in a REVIEWED_RDATA group: CCmdTarget's maps and CWnd's / CWinApp's message
  maps (11), the type descriptors' vtable (type_info's, 74, in .data), and the CRT's string tables in .data (the
  locale category names, __lc_time_c, "(null)", setlocale's language / country tables
  (142), the run-time error messages). Cells into .data are in REVIEWED_DATA groups (cell range, count, what it
  is; a new cell or a changed count fails): in .rdata MFC's CCmdTarget maps (3), the message map entry 00746C80, RTTI locators and base class
  descriptors (117) and EH catchable types (12) to their type descriptors (in .data); in .data the
  string-pointer tables (the script's keyword table (442) and comment tokens) and the CRT / MFC cells (the lock table,
  `_iob`, `_pctype`, the small-block heap's self-linked region list, `_tzname`, `__lconv_c`, CString's empty
  string and `_afxDataNil`). Every cell lies inside the range and is fixed up in its new place. game.exe's own
  code points at .rdata too (FuncInfo addresses in its EH prologues, string immediates), but none of it runs in
  the image. **Left out of the cells** (C20b, `LEFT_STRUCTURES`; the code-cell side is C20a below): the C++ EH
  FuncInfos' pointers (unwind map 2963, try block map 378, handler arrays 380) and the handlers' caught-type
  descriptors (26 of the 38 cells into the type descriptors; the 12 of the catchable types stay) are 3,747 cells
  `MoveData` no longer fixes up, 5517 -> 1770: only a frame handler on game.exe's own frames reads them (its
  FuncInfo comes from the function's prologue, and our code's are the compiler's); nothing in src/ or tests/
  names the tables (address literals checked), the harness's crash report reads only ThrowInfo, named_data.py
  names none of them. They keep game.exe's values, so a stray read of one lands in the guarded old range
  (`OnOldData`). ThrowInfo -> catchable type array and the array's entries stay: `CxxThrowException` and our
  frame handler read them.
  **The guard**: nothing at the old range (the host's reservation, "Host and image"): a use that missed the seam ends the run with
  `standalone: read|write of game.exe's .rdata|.data at <data> by <code>: ...` (in the import table:
  `... of game.exe's own import slot <slot> (<dll>!<function>) by <code>`) in ei_hook.log, on stderr and in the
  harness's report (`OnOldData`; resolve the code address with the map, as for an INT3 hit). ei_vectors.exe's
  `GameAddress` stays the address.
  **Addresses read back from objects** (a vtable, a string pointer) are the storage's in the image: the harness
  prints them as the original's with `ei::OriginalAddress` (game_data.h; DescribeTopWindow, harness_digest's
  missile vtable, ui_census's window labels and its game-vtable test).
- **Code** (S18i, src/standalone/image_code.cpp): `ei::CodeAddress(address)` (ei/hook.h; `ei::Code<F>`) is what
  runs for game.exe's function at an address: in game.exe the address; in the image the registration (InstallHooks
  hands its table over), else a stub (16 bytes, CALL NoCodeEntry plus the address) that reports `no code at`. The
  harness changes nothing by address in the image (C29: named seams, `IsaacRandom::SetSeedHook` / `SetDrawObserver`,
  `gamecrt::SetRandObserver`). Who asks: the static initialisers' calls (static_init.h), the code literals of src/ (handler tables, stats, callbacks, factories,
  the WndProc and new handlers) and the CRT's takers of function pointers (atexit, at the registration; qsort, the eh vector
  iterators, ostream's manipulators), which convert at the call. Never at namespace-scope static init: the table
  is not in yet (the stub would stick). **Code cells**: `tools/gen_image_cells.py` also writes
  image_code_cells.inc (7,928 cells of .rdata and .data holding a function start: vtable slots, ThrowInfo
  destructors / copy constructors, MFC CreateObject, the static initialiser list, and reviewed tables); `FixCodeCells`
  points each at CodeAddress of its value after InstallHooks (`... code cell(s) of game.exe's data point at our
  code, ... at stubs`); a slot the harness took before that (the console streambuf's sync) is fixed with it
  (`FixUpCodePointer`). Left as game.exe's bytes, no code cells: the CRT's SEH scope tables and the C++ EH
  FuncInfos' unwind actions and catch handlers (13,511 funclet cells, C20a; `LEFT_KINDS`; the tables' pointer cells likewise, C20b, "The cells" above): only a frame handler
  on game.exe's own frames reads them (its FuncInfo comes from the function's prologue, and our code's are the
  compiler's); none had a registration, so all sat at stubs that end the run, and no golden reached one. A stray
  call through one lands in the guarded code section (`no code at`). The ThrowInfo cells stay: our frame handler
  reads game.exe's throw information when our code throws with it (`CxxThrowException`). The
  launch-free check: every registration is its address's code (`CheckCode`); `CheckGuard` logs what of game.exe's
  range is open. Stack scans of the harness and the profiler skip the game's code range in the image.
- **Own import table** (S18f, src/standalone/image_imports.cpp): the import address table (game_data.h's
  `kImportsBegin`-`kImportsEnd`, 0073B000-0073B820, .rdata's first 0x820 bytes) moves with .data at the same shift:
  the storage starts at 0073B000 (low 16 bits 0xB000; the .rdata between the table and .data has its place there,
  unused until S18h), so `GameAddress` maps two ranges with one `g_dataShift`. `tools/gen_iat.py` writes the table
  as a struct (src/lib/import_table.h: `ei::ImportTable`, one member per slot named after its import, `_AIL_startup@0`
  -> `AIL_startup`, ordinals by the export they name, `WSOCK32_end`-style members for the DLLs' zero terminators;
  `ei::Imports()` is it through the seam), the image's list (src/standalone/image_imports.inc, 504 imports:
  ImportTable member, DLL, function or ordinal) and gameapi.h, all committed; build.cmd runs it with `--check`.
  Right after the move (same init_seg(compiler) object, before the harness's static
  constructor takes its slots) `ResolveImageImports` loads each DLL (the working directory, else System32;
  dinput always the system's; game.exe's folder came first until S18k) and fills our slots;
  GetCommandLineA gets its wrapper there (`ImageRoute`, hook/standalone_main.cpp; the resource, module and DirectInput
  wrappers went with S18j). Our code names no slot by address (C18): it calls through `ei::Imports().X` (gameapi.h's
  routed imports, Miles, Bink, Winsock, DirectInput, the static initialisers' `Import(&ei::ImportTable::Sleep, ...)`)
  or the Windows function itself (below); the harness's `PatchSlot` / `WatchWrites` still take slots by address
  through the seam (tests/, in game.exe the slot itself). `iat` is sealed in config/data_seam.csv (76 raw uses -> 0;
  data_seam.py's helpers are now transitive: TraceSlot -> PatchSlot -> GamePointer).
  **Which calls read the slot** (C18, gen_iat.py's `routed()`): in the image a slot holds the export unless the image
  routes it (ImageRoute: GetCommandLineA; with EI_UTF8 the `EI_UTF8_ROUTE` list of game/system_text.cpp), the
  harness takes it (any import slot address tests/ names: window focus, cursor, files and saves, profile, registry,
  clocks, heap, Winsock, Miles) or our code takes it by address (`&gameapi::X`: hook/paths.cpp's RegQueryValueExA in
  game.exe). Those gameapi.h functions are `inline auto& X = ei::Imports().X;` (67, with gen_iat's KEEP:
  GetVersionExA, deprecated in the SDK: a direct call warns); the other 344 are `using ::X;`, the Windows function through ei_hook.dll's own imports
  (`#pragma comment(lib)` for the seven DLLs). A harness route on a new slot makes `gen_iat.py --check` (build.cmd; it reads the committed import files) fail until
  gen_iat.py is rerun (that one needs game.exe), which moves that function back to the slot. In game.exe our code does not run (S21), except
  hook/paths.cpp and the harness, which keep the slots they patch. Rebirth's only import slot patch (MoviesPatch,
  BinkOpen 0073B720) is ours behind its flag in the image (mod_folders.cpp); Miles, Bink, Winsock, DirectInput,
  DirectDraw and DirectSound stay table calls: the table loads them from the game folder (Miles, Bink, dgVoodoo's
  DDraw) or the system (dinput), which ei_hook.dll's own imports would not. The table is still needed as it is:
  the harness patches it and those families call through it; the slots of the direct functions are only resolved
  (and could go once the harness names its routes by import).
  **game.exe's own table** is not there, as the rest of the old .rdata (S18h, S18k) (the read of an old slot names its
  import, above; S18f had an INT3 trap per old slot instead); a slot of ours whose import did not resolve leads to a
  trap (`OnTrap`), and the launch-free check fails. **Direct callers**: none run: the image has no .text (S18i,
  S18k; S18f-S18h checked it held only INT3 and our jumps), so no `CALL [slot]` / `JMP [slot]` of the
  original exists. No pointer
  cell of .rdata or .data holds a slot's address (a scan of game.exe's aligned dwords), and our code reaches slots
  only through `ei::Imports()` and the harness through literal addresses, which the sealed check covers. Not routed to our table: ui_census's game.exe
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
  original's FMUL on the main thread rounds to 24, and a literal has no volatile load to stop it: check_fp_fold (removed in C25d) flagged the
  doubles that come out of it; its list of new float constants was reviewed then. The pi * (1/180) of two doubles is `ei::kRadiansPerDegree` (src/game/angles.h, float, checked against exact
  arithmetic); a chain rounds after each product (`static_cast<float>(kRadiansPerDegree * 90.0)`).
  **The static initialisers' folds** (S18e1): gen_static_init writes a float fold's .rdata operands as the same tagged literals
  (`d.StoreFloat(0x0079B1B8, 3.1415927f /*0073B848*/ * 0.5f /*0073B86C*/)`; 243 operands; the one operand in .data, an earlier
  fold's result 007C896C, stays `d.Float`), so the compiler folds them. The original computes them at run time at 53 bits:
  VC6's `_FPinit` (006E72BC, through the pointer 007975A0 that `_cinit` 006E8D7A calls) runs `_controlfp(_PC_53, _MCW_PC)`
  (006EEF7C) before `_initterm`, so they are not at the main thread's later 24 bits. check_static_init (removed in C25d) evaluated each fold of
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
| S18j | game.exe's resources built into ei_standalone.exe (a .res from the build; C23 replaced it with a VERSIONINFO of the host's own), the module handle the host's, no header copy (the page guarded) | game.exe loaded as a data file, the resource / module / DirectInput wrappers, the headers at 0x400000 |
| S18k | an ordinary host (no fixed base, no range in its image; free parts of the range reserved as the guard), no read of game.exe (dispatch bytes from the build, imports from the game folder), stray game addresses reported by the seam | the address image (dashboard "image ties" 5 -> 0) |

Not on this path: named, typed globals in place of a section's bytes (C18, below) and vtables as real virtual
classes (C19), plain new / delete and standard containers (C14), Win32 calls linked directly.

### The build without game.exe (C25)

The build reads no game.exe (C25c). The image's .rdata / .data bytes (`gen_image_cells.py --bytes`, CMake) are zero
but the committed pointer and code cells (image_cells.inc, image_code_cells.inc); everything else our code reads
there is a named global of ours (C18) or a literal. The resources are the host's own (C23). How it got there: C25a's
trace of the reads and zero-filled trial, its kept list (`config/image_kept.csv`) and its static proof that every
address src/ names held only zeros and cells; C25c named the last rows (out_of_range is a class of ours the compiler
throws, stlp_string.cpp; `__lc_category` and its static "C" are crt_locale.cpp's) and dropped that machinery
(EI_IMAGE_BYTES_FROM_EXE, the trial, EI_IMAGE_TRACE / image_trace.py, the kept list, the proof, EI_NO_GAME_EXE).

C25d deleted tools\check_against_exe.cmd and the modes only it ran (check_fp_fold, rdata_literals --check, named_data's
comparison with game.exe's bytes and cells, check_short_functions, check_static_init, gen_image_cells --check's scan,
gen_iat --check's read of the import directory): each had proved its generated file or tagged definition when it was
made, and the generators still read game.exe when rerun. What needs no game.exe stays in build.cmd, from the committed files:
`gen_iat.py --check` (import_table.h, image_imports.inc and gameapi.h regenerated from the committed table and the routes:
a hand edit or a new harness route fails), `named_data.py --check` (definitions well formed, csv current, no address
literal inside a named, retired or unread range, global_objects.csv) and `gen_image_cells.py --check` (retired vtables
against vtables.csv and the sources, the committed cells' targets). The generated static_init.cpp / static_exit.cpp,
image_cells.inc / image_code_cells.inc and short_functions.cpp are only as fresh as their generator's last run: rerun
it (reads game.exe) after a change that registers or ports what it decodes.
The harness still runs the original game.exe for goldens and vectors.

What C25a named or dropped of its list, still the code's form:
- **Written globals at addon sites**: WideScreen's layout widths 00742B08 (800) / 007428AC (695) are
  `widescreen::g_layoutWidth` / `g_panelRight` (ui_widescreen.cpp, tagged; Startup writes them as addon did; the clock and
  the quick items read them). named_data.py's `NAMED_ADDON_SITES` lets a named range hold such a site, be written though in
  .rdata, and be named by tests/verify/addon_sites.inc (game.exe's address, for harness_addon.cpp in game.exe).
- **A ported static initialiser**: 007043FA (`RegisterWindowMessageA("commctrl_DragListMsg")` into 007CAA64) is
  mfc_window.cpp's RegisterDragListMessage (5 bytes, a jump to its body: rows in extra_function_sizes.csv /
  function_sizes.inc); static_init.cpp calls it.
- **Named**: ostream's virtual base table `kOstreamVirtualBases` {0, 8} (crt_iostream.cpp), OnNewParty's next record id
  `g_nextRecordObjectId` (player_server_handlers.cpp), and C25b's pause key `g_pauseMessageKey` 007930EC (ui.h; the
  message table and SetPauseTextDigit share it, so a resume shows "reserved_03" as with EI_Plugin).
- **MFC's empty string**: `_afxInitData` {-1, 0, 0} and its character are mfc_string.cpp's `g_nilString`; DataNil() /
  PchNil() read `g_dataNil` (ours; nothing of ours makes a string in game.exe's runs, H30). game.exe's cells 00796E48 /
  00796E4C stay in image_cells.inc, unread.
- **Unread tables, no definition**: the 72 `.SEC` copies and the dead code's two tables (C18o's
  header_constant_copies.cpp / dead_code_tables.cpp) are gone; their cells are gen_image_cells.py's `UNREAD` groups
  (counted, left out of the cells like LEFT_STRUCTURES): zero in the image.

### Named data (C18)

The code still names game.exe's data by address (progress.py's "game data addresses", 3,028 when C18 began, 3,015
after its first batch, 3,000 after the second, 2,982 after C18c with C19 batch 2 and C21 batch 2, 2,952 after C18d,
2,095 after C18j's unread objects and 2,068 after its particle and weather names, 1,859 after the app* objects of C18f, 1,798 after C18h's small areas, 1,733
after C18e's runtime classes and throws, 1,328 after C18m's reviewed unread objects, 1,276 after C18i's net and CRT state, 732 -> 728 after C18o's game tables,
which took the pointer cells from 1,522 to 1,243, 243 -> 234 after C18q's script keyword and CRT tables, pointer cells
1,243 -> 711; 247 -> 130 after the remaining objects batch, 2026-10-22; 121 -> 90 after C18s's console streams, 90 -> 56 after C18t's singletons with C25a's rows, 53 -> 41 after C18u's generated names, 41 -> 6 after C18r's MFC and CRT objects). C18 turns them into named C++
globals, one kind and one owner area per batch; C19 does the
vtables. `python tools/named_data.py --split [KIND ...]` counts what is left by kind (import slot, vtable,
eh/rtti/runtime class, rdata constant, data object, bss object, other) and by owner area (the one hand-written file
that names the address, "several files", or "generated static init / exit only"), and lists the addresses of the
kinds given.
- **Blocks**: an address that looks like a lone object can sit inside a larger one the code reaches by its base: C18f's
  0079BCF0 and 0079BEC8 are GameOptions members (0079BC70 +0x80, +0x258), read as `options::TheOptions()->...`, not named.
  A `char` buffer is `char g_x[256] = /*addr*/ {""};` (the tool packs char arrays as names), a function pointer
  `void* g_hook = /*addr*/ {nullptr};` cast at the call.
- **Global objects (C18n)**: `--check` refuses a named range inside a singleton the code reaches by field offset
  (config/global_objects.csv: address, size, name, class; TheServerWorld 007AFF60 +0x12E7C, TheClientWorld, TheGameFlow,
  TheInterface, TheCursor, TheCamera, TheSceneRenderer, ...) and says which object and field: C18i's 007C2CC4 is
  `TheServerWorld + 0x12D64 (CWorldServer::sessionKind_)`; the world also holds TheMissionScript 007C27D4 (commandsA_),
  007C29CC (commandsB_), TheSectorMap 007B01F4 (sectorMap_), the AI map 007B00D0 and the diplomacy 007C2BC4. Naming the
  whole object is allowed. `python tools/named_data.py --owner BEGIN [END]` asks the same of any address. The CSV's sizes
  come from `static_assert(sizeof(T) == N)` where the sources have one (checked against it); CWorldServer 0x12E7C,
  CCamera 0x308 and CSceneRenderer 0x5C have none and were read off the compiler. A new GamePointer<T>(addr) accessor in
  game_singletons.h must lie in a listed object. Before naming a lone address, `--owner` it.
- **C18h (the small areas)**: three kinds of reads went away. (1) Members of blocks read by their base: the logic
  settings 007C53F8 (`GameLogicSettings()`: guard / follow radii, the hit point curve's hpVal, skillVal, lost money /
  experience, durability, absorption / attack / defence per difficulty; 25 literals in unit*, combat*, player_server_handlers)
  and the options 0079BC70 (`options::TheOptions()->...`: video / audio, texture quality, mipmapping, reverse stereo, flora
  light, the two LOD distances, the camera's terrain area, keyboard layout, the objects' lighting coefficient, the hero and
  quest lights). Check a new candidate against these two structs first (an offset from 007C53F8 / 0079BC70 is a member).
  (2) Struct tables with string pointers: TYPES has `OptionDefinition` / `ActionDefinition` (ui_options.cpp), `ScriptParticleType`
  (script_effects.cpp), `AmbientSoundClass` (terrain_ambience.cpp, written by CountAmbientVariants: not const). (3) Lone
  variables, guards and scratch arrays (lightning's noise curves, terrain's animation speeds and light-block lists, the
  mouse drag, shot counter, party id counter, ...); a header's global is `inline T g_x = /*addr*/ {};`.
  A table that follows another (the key actions 00787070 right after the 88 key names from 007857B0, whose code took the actions'
  address as the names' end) is one extent to the code that indexes across: the check cannot see an address literal at a
  start. The first run of the image crashed in key_bindings reading zeroed names; name the whole run of tables, and take an
  end as begin + count.
  Left for later: objects a static initialiser stores into (floats such as 007C7D20 are the C18j `constexpr float` form),
  members of the camera 0079B2A8 (0079B2B0, 0079B2C4, 0079B388, 0079B3C4), the scene renderer 007AF060 (007AF064), the client
  and server world blocks (007AF604, 007AFCA4, 007AFFAC, 007AFFC4), the mission script (007C2CD0), the Esc menu's rectangles 007C80A8 (inside the menus' block 007C8048 that LoadMenuRects fills by its base: a first try named them zero and the Esc menu's buttons went dead), the CWordArray 0079B668
  (0079B66C), the singleton pointers of game_singletons.h, the loading movie 007C5520 (static initialisers store into it).
- **The harness in game.exe**: the original suite (`--no-hooks`) runs game.exe's own code, which never touches our
  named globals. A harness step there must read game.exe's copy (`ei::OriginalGameData<T>(address)`, tests/ only)
  or call game.exe's function by address instead of ours (the zone step's LeaveToZone, modfile's text folder, the
  digest's random; 4cbf5d5c). Run the original suite after naming data the harness reads.
- **The form**: a typed definition whose initialiser carries the original's address as a tag right after the `=`:
  `constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};`. The code names the global
  (`kSlotXs[i]`), never the address. It lives in the file that reads it (its anonymous namespace), or as
  `inline constexpr` in the subsystem's header when several files read it (ui_camp.h's `kBodySlotX`). A single float,
  double or integer is no table: it is rdata_literals' tagged literal (`5.0f /*007402EC*/`, S18e), even when the code
  copied its bytes with memcpy. A table of string pointers is `const char* const kX[] = /*addr*/ {"a", "b"};`, one
  string pointer `constexpr const char* kX = /*addr*/ "text";` (a per-file copy of a header's `const char* const`
  the original read through memory: terrain_sector's kSectorExtension); a struct with pointer fields takes string
  literals, names and nullptr in its initialiser (material.cpp's kMaterialKindFiles).
- **The check** (`python tools/named_data.py --check`, build.cmd; no game.exe since C25d): the initialiser's numbers,
  packed by the declared element type (`TYPES`: the scalars, GUID as `IHH` and 8 `B`; nested braces are flattened, a `[n]`
  bound must match the count), lie in .rdata past the import table, or .data and its zero-filled tail;
  config/named_data.csv lists exactly the tagged ranges (address, size, name, file; `--update` writes it); and
  nothing else reaches a range: no address literal of src/ or tests/ (.cpp, .h, .inc, comments blanked) inside it, no
  addon.dll site, no two ranges overlapping. That the numbers are game.exe's bytes and that no cell of its data points
  into the range was proved when each range was made (`--emit` prints the definition from the bytes and packs it back).
- **Pointer elements** (C18c): a type with pointers (TYPES `P`: `char*`, `MaterialKindFile` `iP`) packs no address.
  Each pointer element names what game.exe's pointer in its place points at, and the check compares that: a string
  literal (adjacent ones joined, escapes decoded) the zero-terminated text at the target (game.exe's string literals,
  in .data); the name of another named range its address; the name of a function of ours (`&` optional) the
  address of its registration (`EI_REPLACE*`: the code game.exe's pointer called, which the image's code cell pointed
  at); nullptr (or 0) a zero. The packed dword is then game.exe's pointer, so the byte comparison holds, and its
  offset is a pointer element: the only place a cell may sit inside the range. gen_image_cells leaves the cells inside
  named ranges out of image_cells.inc / image_code_cells.inc (the bytes are zero, nothing fixes them up) and its
  reviewed groups' counts drop with them; the strings they pointed at stay in the image for their other readers.
  A pointer to game.exe's data that is not ours (another record, a type descriptor) has no form: the target is
  only known at run time (GamePointer), so a table that holds one stays data until its targets are named too.
- **The bytes**: gen_image_cells.py --bytes writes the csv's ranges zero (CMake's custom command depends on the
  csv), so the image's copy of the sections no longer carries them; what the check proves unreachable is also
  unread at run time.
- **The scripted part (C18o)**: `python tools/named_data.py --emit ADDRESS TYPE COUNT NAME` prints the tagged
  definition from game.exe's bytes (as its static initialisers leave them) by TYPES' packing in reverse: strings and
  `char[N]` fields (TYPES code `'Ns'`, e.g. `UiKeyName` `('i', 'P', '64s')`) as literals, pointers to a named range's
  start by name, to code by our registration's function, zero as nullptr; COUNT `4x10` gives rows. What it cannot
  name prints as `/*?ADDRESS*/ nullptr` with a line on stderr (a pointer into another record, a number that is no
  pointer: the range starts or ends elsewhere), and it packs its own text back as --check does. Paste, adjust the
  declaration (drop `constexpr` for a table the code writes; `extern` in the header when several files read it), run
  `--update`, then `--rewrite NAME [--apply]`: the `GamePointer<T>(0x...)` / `GameData<T>(0x...)` reads of an
  element's start with T the element type become `NAME` / `(NAME + k)` / `NAME[k]`; DataId constants, other types and
  field addresses are listed for a person. Then drop the emptied REVIEWED_DATA / REVIEWED_RDATA groups of
  gen_image_cells.py (their counts fail otherwise) and regenerate image_cells.inc. **Unread copies**: a table only
  dead code read (the units' model folders 0078F7EC, read by the uncalled 00532650, and 007932F4) and the per-file
  copies of a header constant nothing of ours reads (72 copies of ".SEC"; a port names the copy it reads in its own
  file, ai_map_load.cpp's 0073FD1C / 0073FD20) were `[[maybe_unused]]` definitions until C25a dropped them (their
  cells are gen_image_cells' UNREAD groups); the CRT's math function names 00797CC0 (crt_x87.cpp) stay named, its
  readers have no registration. Left: the CRT / MFC tables a static initialiser or the
  CRT's start-up fills (C18q's list below).
- **The script keyword table and the CRT's tables (C18q)**: `kScriptKeywords` (script.h, script_parse.cpp; 0078BCA0,
  225 keywords and the null entry, TYPES `ScriptKeyword` 'PiPP') and the lexer's comment markers 0078BC8C..94
  (`scriptparse::kCommentOpen` / `kCommentClose` / `kLineComment`, script_parse.h; the quote 0078BC98 is the literal
  `kQuote`). **Rebirth**: only the image runs our code, and addon.dll never loads there; `KeywordTable()` gives
  `scriptaddon::Table()` (script_addon.cpp: built once from kScriptKeywords and addon's 69 records, as addon's
  10022290 does) when the AddNewScriptCommands flag is on, else kScriptKeywords. game.exe (--no-hooks, the recorders)
  runs its own code on its own table or addon's; nothing of ours reads game.exe's copy. The script vectors' fixture
  "@ScriptKeywords" keeps its format; the replay compares the recorded table and markers with ours instead of
  writing them into the stand-in. The CRT's locale data (getqloc's tables, `_ctype` / `_pctype`, `__lc_category`) is
  unread since C26 (setlocale and _setmbcp are the host CRT's; named_data still takes `NAME + k`, a named range's
  element k, as a pointer element). `_iob` is not read: our streams are the host's FILE*, and the game's stdin / stdout / stderr are
  `libcrt::GameStream(0..2)`, three 0x20-byte stand-ins in crt_format.cpp that only HostFile compares (the vector
  recorder reads game.exe's `_pctype` through OriginalGameData; the replay sets the classes with SetReplayLocale).
  config/image_kept.csv (C25) lost the rows these names replace: the keyword table and the strings only its cells
  reached (0078BCA0, 0078CAC0, 0078B820 / 864 / 8A4, 0078BBD0, 00787B5C / 98 / B4), and the CRT's locale rows
  (C18q, C26).
  **Left in the CRT**, by reason:
  - unread by our code (the host CRT or a literal does the work; C25 zero-fills): the locale categories
    `__lc_category` 00797888 with the static "C" 00797778, getqloc's tables 00798914-00798BE8, `__rgcode_page_info`
    00797680-00797778, `_ctype` / `_pctype` / `_pwctype` 00797DD8-00797FE0 and the rest of the locale and multibyte
    state (C26: setlocale / _setmbcp are the host's), `__lc_time_curr` 00797A58 and
    `__lc_time_c` 00797A60 (strftime / the time functions are the host's, mfc_time.cpp), printf's "(null)" 0079804C /
    00798050 (literals in crt_format.cpp), `_iob` 00798060 (its two cells to `_bufin`), getqloc's locale info table
    00798470 (27 rows of 0x2C, 54 cells; 006F5231 is its GetLocaleInfoA stand-in off NT, ours calls GetLocaleInfoA),
    the run-time error messages 0079AC30-0079ACBC (`_NMSG_WRITE`, not ported), `_tzname` 0079AD4C / 0079AD50 ("PST",
    "PDT"; the host's tzset), `__lconv_c` 0079AE20-0079AE44 and `__lconv` 0079AE50 (localeconv is not ported; the
    decimal point is `g_decimalPoint`).
  - the CRT's start-up state, unported: the lock table 00797C04-00797C44 (`_mtinitlocks` fills it; our CRT takes no
    locks), the small-block heap's region list 00798BF0-00798BFC and 0079AC10 (`__sbh_*`; on NT 5+ `__heap_select`
    picks the system heap, which is the path crt.cpp ports).
- **A batch** (the method; batch 1 did the interface's .rdata tables, 13 addresses: ui_hero_preview's four clear
  rectangle tables, ui_item_view's school glow colours and its pi / 5.0 floats, ui_briefing_people's speaker places,
  ui_hud_slide's part positions, the camp body slots of ui_container_draws / ui_hero_panels):
  1. `python tools/named_data.py --split "<kind>"`: take the addresses of one kind whose owner is your area.
  2. Per address: read its uses for the extent (the count the code indexes, the original's bound, the next tagged
     neighbour) and the element type; write the definition with the tag (the values from game.exe:
     `gen_image_cells.image(exe_path.find_exe())` in a `python -c`, or or `named_data.py --emit`, which prints the
     definition from game.exe's bytes).
  3. Replace every read (`GamePointer<const T>(0x...)`, `GameData<const T>(0x...)`, the per-file accessor
     returning the pointer) with the name; drop the accessor and the comments that repeated the address or the
     values.
  4. `python tools/named_data.py --update`, then `--check`, `data_seam.py --check`,
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
    C18c did the string pointers (random 0073D080, world_server_life 0073F918, terrain_sector 00744164,
    terrain_landscape 007441D4, sound_music 00744090 / 00744094) and material.cpp's kind / file table 00743BF8
    (8 `{kind, "x.mat"}` rows). Keep the original's bound on an index the data
    gives (ui_item_view's `school < 8` is signed, as the original's compare: a negative school would read before our
    array where the original read the neighbouring .rdata; no database has one).
  - **Pointers to game.exe's records** (C18e): MFC's 41 CRuntimeClass records are named as one piece
    (lib/mfc_runtime_class.h: the 36 in .rdata as `inline constexpr CRuntimeClass kClassCObject = /*00745D20*/ {...}`;
    the 5 serializable ones in .data, which AfxClassInit links into the module state's list: CStringArray / CWordArray /
    CDWordArray / CByteArray in mfc_coll.cpp, CDib in game/dib.cpp, `constinit CRuntimeClass g_classCDib`; TYPES has
    `CRuntimeClass`, 'PiIPPP'). Each record names its base's record and its create function. **All or none**: a
    base pointer is a named range, game.exe's data holds no pointer into one, and every record derives from CObject
    (whose record the 5 .data ones point at, as do all the others), so the first name needed the whole tree, and each
    create function it points at had to be ours (the check compares the registration): the ten that were unported
    (CDC, CGdiObject, CMenu, CTempMenu, CWnd, CTempWnd, the four arrays) are small ports registered at their
    addresses with a row in config/extra_function_sizes.csv and src/hook/function_sizes.inc (nothing calls them in
    the game: `unreached` in the coverage); the 5 initialisers that called AfxClassInit with a record's address
    (`push offset; call 00700674`, the list holds the first instruction of each, a jump to the body) are ported on the
    name and registered at the list's address (`d.Cdecl(address)` in static_init.cpp). Do not run
    gen_function_sizes.py for new rows: it rewrites every size from the Ghidra database, which has moved from the
    committed file; add the rows by hand in order.
    config/runtime_classes.csv (the cell tool's list of the records) went with them: their cells are inside named
    ranges.
    **The throws**: mfc_exception's file / archive / not supported / memory / resource exceptions and the device
    layer's GameError are this compiler's `throw` of a pointer to a class of the game's name (lib/mfc_try.h:
    CObject, CException, CSimpleException, CFileException, CArchiveException, CNotSupportedException,
    CMemoryException, CResourceException; dx_errors.h: CGameException, CDXException), cast from the game's object.
    The compiler's ThrowInfo lists the pointer to the class and each base like the original's six records
    (0077F440, 0077F2B0, 0077EE80, 0077EE38, 0077EC28, 00759908), which nothing reads now: they and their catchable
    types stay in the image as dead data with their cells (ThrowInfo -> array 10 and array -> types 41 are counted
    over all of game.exe). stlp_string's out_of_range (thrown by value, 0074C640) is a class of ours since C25c,
    thrown by `throw` (std::exception, its 256-byte name, logic_error): its records are dead data too.
  - .data and .bss objects (C18d did particle*: 25 .data tables for 29 addresses, the bag's ball sizes 007A9BFC and the draw
    arrays' guard 007A9BF4). The form is the same; **const follows our code, not the section**: a .data table no
    code of ours writes is `constexpr` like an .rdata one (most are: the original's arrays lacked const;
    particle_callbacks' alpha tables), an object the code writes is plain (`float g_bagSizes[12] = /*007A9BFC*/ {};`,
    `g_` prefix). The check fills elements the initialiser leaves out with zeros, as C++ does (`{}`: the
    zero-filled tail's objects, which take all of it; a table's trailing zeros are left out), and refuses a
    non-const range in .rdata.
    Before converting, read `python tools/named_data.py --refs BEGIN END` (the original's absolute operands into
    the range, by function and our registration): the starts the code indexes from (a read backwards from an end
    or one entry on is the same table: kRiseFallAlpha 0078A914 serves 0078A914 / 0078A918 / 0078A960, kClayReach
    0078A6F4 / 0078A72C), the extent a loop's end compare gives (EmitBag's 007A9C2C: 12 sizes), and every function
    that reaches it; our port of each must use the name. Some hits are no operand (a call's rel32: read the bytes).
    The extent is otherwise the next referenced start. It shows no access through a pointer: an object inside a
    larger one that the code reaches by its base (the camera 0079B2DC, written by the briefing and view screens
    through the camera block; the world's wind 007AF5DC, inside the world's block) stays at its address until
    its whole enclosing object is named.
    Every reader must use the name: the generated static initialisers and exit functions (below), the harness
    (tests/verify runs in game.exe too, where a name is our copy and not the original's object: it keeps the
    address there), the vectors' stand-ins (tests/vectors fill recorded values by address), and game.exe's data
    pointing at it (a cell keeps it at its address). The check's literal and cell scans cover src/, tests/ and
    the cells; config/ and the vector data name none of these. Saves and messages carry values, not addresses:
    the goldens catch a changed value.
  - C18g named the single-owner world* and ai* objects (1,875 -> 1,829 game data addresses): the area-list tables, the
    mark table (00791164: 5 rows of 16; the open-cell mark is column 10, so 0079118C is inside it), occluder costs
    and damping, steep offsets, transparency, the order angle limit, normal.dat's table, the three SpiralCache
    function statics with their guards, function-static guards, the decal builder, stamps and the guard key, and the
    arrays with a plain `free` exit function (world_sync's four, world_tick's two, the object sets' scratch, the
    decals' part points: `constinit lib::AlignedArray<T> g_x = /*addr*/ {}` and the exit function ported beside it,
    EI_REPLACE at its address; the code that registered the generated ExitXXXX registers the port). **Left**: the
    tables the path vectors fill by address (00791108 / 0079112C / 00791978 and its middle 00791988, 0079169C,
    the spiral tree 007C2FDC-007C2FF0, 007C3038 / 007C3058: C18i's kind), members of larger objects (the options
    object 0079BC70-0079BEDC: 0079BC98, 0079BD24-0079BD58, 0079BE54, 0079BECC-0079BED4; the client world 007AF5B8
    + 0x7E4: 007AF5F0, 007AF63C, 007AF758 / 75C; the server world 007AFF60: 007AFF98, 007AFFC8, 007B007C; the
    scene renderer 007AF060: 007AF07C; the interface 007C7D50: 007C7D84, 007C7DD8), the map name lists
    00790270-007902C0 (pointers into strings in the same region), objects with a constructor or a destructor that
    is not a plain free (ai_region_reach's three point arrays, the water patch 007C2F64, LastResources, the script
    stream, ai_events' pool, the shape cache, the smoothing pool), the alarm log pointer 007C2EB8 (a static
    initialiser stores another object's address) and 007C2F8C (ln 0.6: a call's result).
  - **Vector-filled tables and the net / CRT state** (C18i). net_coder's code tables (g_offsetBits 0079B6B0,
    g_offsetCodes 0079B738, g_lengthBits 0079B7C0, g_lengthCodes 0079B848) and kCoderBitMasks 0078DB28: the
    original indexes the length tables and the masks from one entry before their start (0079B7BC / 0079B844 by
    length 1..0x22, 0078DB24 by bit count; 0078DB24 itself is the tail of the string "sky00"), so ours take
    `length - 1` / `count - 1`. **An object's start is where the next one ends**, not the base the code indexes
    from: read the bytes and the neighbours before trusting a `--refs` start. The coder vectors' "@CoderTables"
    keeps its format (the recorder reads game.exe's copies at the indexed bases through OriginalGameData; the
    replay compares from entry 1 and fills ours). A bound must be decimal: `[0x22]` read as no bound (one element,
    zero bytes) and named nothing; named_data now refuses it. The network globals: net_classes.h's g_gamePort
    00783E30 (8888; the harness writes its free port to ours in the image, game.exe's in --no-hooks), g_netLog,
    g_currentSession, g_currentConnection, g_gameClient / g_gameServer 0079B5D8 / 0079B5DC (counted pointers: their
    static initialisers 00412230 / 00412280 and exit functions 00412250 / 004122A0 are ported in net_session.cpp,
    rows in config/extra_function_sizes.csv; GameServer() / GameClient() stay as accessors of the names), the pump
    time, the player / variable serials, CollectChanges's vector and guard 0079B8D0 / 0079B8DC (its exit function
    00434980 beside it), the select timeout and net_delta.h's keep / read state. **The "campaign mode" 007C2CC4 is not
    a global**: it is the server world's `sessionKind_` (007AFF60 + 0x12D64), which StartZone writes; naming it apart
    left UpdateWeather and the rest reading the world's own copy (the main menu never came up in the image), so its
    readers take `TheServerWorld()->sessionKind_`, and 007C2CC4 stays with the world. The CRT's locale and multibyte state is the host
    CRT's since C26 (lib/crt_locale_data.cpp reads it), crt_file's modes (zero constants) and crt_iostream's shared lock and ios count. **Left**: 00783E34 / 00783E38
    (the send rates: 00783E34 is an addon.dll site, Rebirth's SetDefaultRate, which the image does not reproduce
    yet), the growing buffers 0079B8E0 / 0079B900 (game.exe's vtable: C19), 007B00BC (inside the server world
    007AFF60). The CRT's allocator state is named since C18r (below).
  - **The static initialisers' objects** (C18j). Most game data addresses left were named only by the generated
    static_init.cpp / static_exit.cpp. Two rules in tools/gen_static_init.py (each is a decision of the generator):
    - **Named ranges**: a store, float fold or fill into a range of config/named_data.csv is left out of
      static_init.cpp (the block keeps its `d.Begin`, its comment says "n left out"); the definition's initialiser
      takes its place. the definition is game.exe's bytes *as the initialisers leave
      them* (`--emit` packs it back and compares) (gen_static_init.stored_values: the stores in list order, a fold computed as the original does, x87
      order at 53 bits, rounded to a float; a call's stored result is unknown and refused), so `constexpr float
      kCellSize = /*007AEDA8*/ 0.5f;` (a single number takes this form: one .data object that an initialiser sets,
      not an .rdata literal) is checked against 1.0f / 2.0f. Only constant initialisation: a C++ dynamic
      initialiser or constructor runs at our CRT's start, before every game initialiser, not at the list's point.
      What must run in list order (a builder call, a constructor that draws random numbers) keeps its call, and the
      call's address argument fails named_data's literal check: port that initialiser by hand on the names and
      register it at its address (EI_REPLACE; a row in config/extra_function_sizes.csv and function_sizes.inc when
      Ghidra has no function there); the generator then writes one `d.Cdecl(address)` at its place in the list
      (weather_noise.cpp's BuildWeatherNoise, 004E3B20, builds the five sway curves; 004E3AD0's fills of their
      segments are left out, the definitions' `{}` zeros take their place). An exit function that destroys a
      named object is the same: its address fails the check until it is ported on the name (the destructor rule,
      below).
      C18j named particle_callbacks' kSpinAngle 007A9BF8 (2 pi; the product with the satellite's rate is
      `static_cast<float>` so the compiler's fold keeps the original's 24-bit FMUL), particle_create's
      kIdentityRotation 007A9C30, face_normal's kDegenerateNormal 007A9C98, weather_particles' five scalars
      007AED98-007AEDA8 and its seed tables 0078DEA4 / 0078DF24, and weather_noise's five sway curves
      007ABB98-007AED98 (one type, WeatherNoiseCurve; the draw evaluates a segment through MotionCubic).
      C18l named the camera's pi / 2, zero vector and identity rotation (camera.h: 0079B2A4, 0079B5C8, 0079B5B8),
      the scene loader's zero vector 0079B9B0 (scene_loader.h), the angle constants and unit vectors of ground_print
      (007C8998 / 007C899C), sound_manager (007C8808), terrain_* (007C8908, 007C8930, 007C8964, the wave scales
      007C8968-007C8970, 007C8978, 007C8988), player_client_handlers' fixed view rotation 007C8388, and the game's
      ISAAC generator 007A9D40 (a mutable zero-start global, `IsaacRandom g_random = /*007A9D40*/ {}`; the stores
      zero its first dword). A product or quotient of two named constants that the original computes at run time
      is `static_cast<float>` (check_fp_fold: sound_manager's 100 / (pi / 2)). Left as they are (then): 007AF3E8 and
      007C53C4 store the address of the log stream 007C5810 (named since C18s), 007C2F24 is a
      function-local guard (run-time state), 0079B2A0 (2 pi) and the folds that only game.exe's own code reads (the
      per-window pi / 2 and 1 / 1 copies in 007C7B70-007C8844) are no hand code's; sky_object 007AA890 /
      007AA8A0, the unit objects 007AF280 / 007AF284, the animation / object3d / map_object lifetime objects
      (007AF0E0-007AF110, 007AF1C0-007AF238) and torch_spec 007AF2D0 wait for C19 batch 4's conversions.
      gen_static_init reads a parenthesised negative literal in a fold; named_data takes a negative single number.
    - **Objects with exit functions** (C18k, the destructor rule): an initialiser that registers an exit function
      (`stores + atexit`, `constructor + atexit`) and the exit function are ported on the name; the game's atexit
      list keeps its order and its entries.
      - The definition is constant initialisation with no C++ destructor (`constinit`): a destructor of ours would
        run at our CRT's exit, not at the original's place in the game's atexit list, and could free into the game's
        heap after the game's CRT has gone. A container the stores zero is defined zero:
        `constinit lib::AlignedArray<TLVertex> g_vertices = /*007AEE00*/ {};`, `constinit stlp::vector<stlp::string>
        g_questFiles = /*007AFDD0*/ {};` (named_data's TYPES names a template, `lib::AlignedArray` / `stlp::vector`;
        a pointer type it does not list is a pointer element: `CDirectInputMouse* g_mouse = /*0079BA08*/ {nullptr};`).
      - The exit function: ported on the name and registered at its address (EI_REPLACE; function_sizes.inc already
        has it, gen_static_init wrote it there); the initialiser's `d.AtExit(address)` stays at its place in the list
        and gen_static_init leaves the registered function out of static_exit.cpp. It goes next to the definition
        (device_buffers.cpp's FreeVertices, world_briefing.cpp's DestroyQuestFiles, camera_object.cpp's DestroyCamera).
      - A constructor whose result is no constant (CString's empty string: a copy of 00796E48, a run-time pointer;
        `CString(const char*)` allocates) runs in list order: the initialiser is ported on the name and registered at
        its address (a row in config/extra_function_sizes.csv and function_sizes.inc: Ghidra has no function there),
        so the generator writes `d.Cdecl(address)` in its place. The object is `constinit lib::StaticObject<T>`
        (lib/static_object.h: zero storage that our CRT neither constructs nor destroys); the initialiser calls
        `Construct(...)` and registers the exit function by its address (`GameAtExit(ei::Code<...>(exit))`), which
        calls `Destroy()`. A guarded exit function's guard byte is named with it (game_folders.cpp's 007AF438).
        named_data compares such a definition with game.exe's bytes (zero): a registered initialiser's stores are
        not applied (decode_all emits only its call).
      - C18k4 named whole class objects the same way: a class with game.exe's vtable stays with C19, a class
        without one is `constinit lib::StaticObject<Class> g_x = /*ADDR*/ {};` and the initialiser calls the class's
        own `Construct(...)`. named_data needs the class's size (STATIC_OBJECT_BYTES in tools/named_data.py maps the
        T of a StaticObject<T> to it; a template deeper than one level gets an alias, `using IntSet = ...`). Every
        access goes through the name or a member (an accessor such as `ThePartyManager()`); a raw address inside the
        range is refused. Named: ui_objectives' last quest string 007C8178, bink_video's loading holder 007C551C, the
        protection checks / driver / deferred-unit lists (static_lists.cpp), the spell / item / quest / print / speech
        databases (database_globals.cpp), the lever table, the unit and skill databases (unit_db_globals.cpp), the
        party manager 007AA5A0, saved parties 007C82A0, the spell footprint set 007C8478, the party id set 007C8370
        and the unread set 007AB8D8. C18k5 (1,717 -> 1,651 game data addresses) named the last four without a vtable: the options
        0079BC70 (options_global.cpp holds the definition so the vector replay links it; options_construct.cpp the
        initialiser), GameFlow 007AFE20 (world_game.cpp), CCursor 007C7BD8 (ui_cursor_setup.cpp) and CInterface3D 007C7D50
        (ui_small_members.cpp). Their raw reads became members: the options' 80 reads (camera speeds, shadows, prints,
        volumes, difficulty, paths, master server ...), the mouse position and drag motion (CCursor::position_ /
        motion_: 007C7C50 and 007C7C60 were lone ints before C18k5 named the cursor), the interface's window stack
        (007C7D84 was misnamed PeerList), close-all and sessions flags. Accessors of the objects whose header only
        forward-declares them (TheGameFlow, TheInterface, TheCursor) are out-of-line functions. **The harness** reads
        them through tests/verify/harness_objects.h (HarnessOptions / HarnessFlow / HarnessInterface / HarnessCursor: ours
        in the image, `ei::OriginalGameData` at the address in --no-hooks runs); the stats recorder reads the
        difficulty from game.exe's options the same way. Left: string_map 0079B930, CMapDatabase 0079BF00, the renderer
        007AF060 and the other vtable-storing objects (C19).
      - gen_static_init still decodes a hand-ported initialiser (SHADOW) for unread_objects only: its instructions,
        the cells it stores and its exit function's instructions stay the initialisers' own (without that, two unread
        objects above the folder strings, 007AF43C and 007AF468, lost their proof).
      - An unread object whose initialiser also registers a `free` exit function (an aligned array nothing else
        touches: its block is never allocated, the exit function does nothing) has its stores, its atexit and the exit
        function left out (UNREAD_EXITS: 007A9CA8, 007AEDF0).
      - C18k named the device's mesh work arrays (device_buffers.cpp, 007AEDE0-007AEE30), link_mesh_draw's moved
        packets 007AEED0, renderer_lines' line vertices and projected points (007A9CE0, 007A9CB8), the quest files
        and folders (world_briefing.cpp, 007AFDD0 / 007AFDC0) and the zone table (world_game.cpp, 007AFDE0), the
        stock generation's scratch vectors (item_stock.cpp, 007C8640-007C8680), the command line's CStrings and the
        mouse pointer (app_main.cpp, 0079BA08-0079BA1C; the movie folder's default ".\capture\" is our literal),
        the script's quest name 007AA61C and the folder strings (game_folders.cpp, 007AF450-007AF460).
      - C18k1 / C18k2 (1,505 -> 1,461 game data addresses) named the aligned arrays whose exit function is a plain
        free: map_object_shadow's points / pixels / quads (007AF1F8 / 007AF1E0 / 007AF208, the guard 007AF1F0 with
        them), figure_mesh's offset vertices 007C2E18, terrain_sector's projection buffers 007C88F8 / 007C88E8 (their
        guard byte 007C88C9 stays at its address: bit 4 is the outcodes' destructor 006BFAE0, which the generated
        static_exit.cpp still runs until the CByteArray 007C88D0 is named, C19), and the function-local statics of
        renderer_strip (007AF000-007AF040), renderer_sprites (007AEF90, 007AEFA0, 007AEFB8-007AEFE8),
        figure_rasterise 007C2E00, the two part draws 007C2E48 / 007C2E60, lightning_draw 0079C1D0, particle_render
        007A9BD0, effects_weather_draw 007ABA30, link_mesh_draw 007AEEB8 and terrain_animation 007C8938. The hand code's
        GameAtExit keeps passing the exit function's address (or the port itself). Not named, because unported code
        reaches them: camera 0079B1E0 / 0079B1F8 / 0079B210 (00402310), terrain 007C8890 / 007C88A8 / 007C88B8
        (006BFF40) and 007C8950 (006C8990); their `free` exit functions stay generated until those functions are ported.
      - C18k3 (1,190 -> 1,180 game data addresses after the draw lists) named the frame's eleven draw lists
        (world_client_view.cpp, 007AF508-007AF5A8: plain `stlp::vector<CEngineObject*>` 0x10 apart, the word after
        each no one's; the last one ends where the deferred unit list 007AF5B4 starts, so there is no 16-byte
        struct; their stores stay generated, the exit functions 0056A510-0056A830 are ported; world.h's
        FrameObjects picks one from a table of the names; 0056A840, which clears them all, has no caller) and the
        CArrays whose tables C19 compiled: the sector spiral's lengths, radius tree and offsets (sector_spiral.cpp,
        007C2FC0 / 007C2FD8 / 007C2FF8; a file of its own so the path vectors' replay links them) and the path
        smoother's node pool and 81 transition tables (ai_path_smooth.cpp, 007C3038 / 007C3058; the tables are
        `lib::StaticObject<SmoothTable[81]>`, built in order like the `eh vector constructor iterator` and destroyed
        last first like its destructor iterator). Their initialisers (005BF810, 005BF840, 005BF8B0, 005C41C0,
        005C4460) have rows in extra_function_sizes.csv / function_sizes.inc. The path vectors keep their format:
        the record reads game.exe's copies through `ei::OriginalGameData` (a type alias where the type has a
        template argument: the literal check's pattern takes no nested `<>`), the replay fills ours by the
        record's order.
      - Not this rule (C19): an object that carries game.exe's vtable: the CArrays 007A9CC8 / 007AA608, the save stream
        (CMemoryStream 007AFDF8). The console's ostreams and their log pointers are named since C18s (below).
    - **Unread objects** (config/unread_data.csv, written by gen_static_init.py): an object nothing but its
      initialiser reaches needs no name. `unread_objects` takes each run of contiguous dword stores of one
      initialiser that stores constants only (kinds stores, float fold, copy) into .data and keeps it when no
      instruction of game.exe's code has an absolute operand inside it but the initialisers' and their exit
      functions' own (the interpreter records the instructions it decodes), no address literal of src/ or tests/
      (but the generated files) names it, no initialiser call, atexit or exit function takes it, no initialiser
      reads it (a copy's source), no named range holds it, and no address referenced elsewhere and stored by no
      initialiser lies less than 0x100 below it (it could be a field of that object). Its stores are left out like a
      named range's, and named_data.py --check gives it the same proof (no literal inside it). 347
      objects, 998 store statements: the per-file copies of header constants (zero vectors, identity rotations
      {1, 0, 0, 0}, floats: 1 / 1 and pi's folds) that the original's code never reads either, and the neighbours of
      other objects below. **C18m (reviewed)**: 48 groups lay within 0x100 above a referenced object; each was read
      against that object (named_data.py --refs; its extent from the ported class's size, its constructor or the code's
      accesses) and cannot be one of its fields: `REVIEWED_NEAR` in gen_static_init.py (group -> the object below and
      why; most are a group right after the end of a database, a stream, a hash table or a list head). Reads inside
      config/dead_code.csv do not count as the original's reads, and `REVIEWED_READ` lists five groups whose readers
      are ported with a literal or are no operand (loop bounds ending at the group, a call displacement that reads as an
      address). An entry that no longer fits (the object below got a name, the group got a reader) fails the
      generator: drop it. The rest of the generated-only addresses are objects the original reads (read by a ported
      function that uses the value or another name; stored by an initialiser that also calls or registers an exit
      function; or stored by calls): name them by area as above.
    The verify build has no run where the original's initialisers and ours meet (since S21e game.exe runs only the
    original), so the comparison is the build's: exact for stores and folds of literals; a definition that a
    call fills (the sway curves) is compared at its zero start, and the goldens cover what the call builds.
  - **The remaining objects** (C18, 2026-10-22; 247 -> 130):
    - **Singletons' fields read as members**: what was left of the client / server worlds', the scene renderer's and
      the camera's fields (managers, tick and its fraction, terrain, scripts, diplomacy, flags, wind, platforms) is
      read through the object (`TheClientWorld()->manager3C_`); the accessors that need world.h are out of line in
      world_singletons.cpp (game_singletons.h declares them; replay_path.cpp defines CurrentWorldTick for the path
      vectors). TheMatrixDevice was the scene pipeline (ScenePipeline()), 007AFDB0 the origin (worlddata::kOrigin),
      007C5488 / 007C548C the logic settings' considerModifier / considerLogBase. Check a lone address with
      `named_data.py --owner` and against the logic settings (007C53F8) and the options (0079BC70) first.
    - **Whole objects with a hand-ported initialiser**: the logic settings (g_logicSettings, its initialiser 005CDFB0;
      TYPES `LogicSettings`; the stats recorder reads game.exe's copy), and the constructed statics as
      `lib::StaticObject<T>` (zero storage, STATIC_OBJECT_BYTES) whose initialiser and exit function are ported on the
      name and registered at their addresses, each with a row in extra_function_sizes.csv / function_sizes.inc: the
      particles' draw order 007A9C50, the unit shape cache 007C2F70 (defined in ai_movement.cpp, which the path
      vectors link), the AI events' pool 007C43A8 (and the free list 007C53F0, ai_grids.cpp), the terrain's projection
      outcodes 007C88D0, the sector spiral's builder 005BF8E0. A function-local static's exit function is ported
      beside it and the port registers its own (`GameAtExit(DestroyX)`): the glow's and the particles' sprite arrays,
      ai_region_reach's point arrays, Aggression's candidate list, the water patch, UseResources' CString.
    - **gen_static_init**: REVIEWED_SPLIT splits one initialiser's stores where they are two objects (005229C0: the hit
      log pointer 007AF284 is read, the ln folds above it are not); RETIRED_STATICS lists functions whose port builds
      none of the original's function-local statics, so their runtime atexit sites never run (006BFF40, the terrain
      draw: its tile arrays 007C8848..007C88C8); a fold of ln in an unread object is `????????` in unread_data.csv.
      Winsock's exit function 0047C330 is ported without the wait (only dead code counts the threads 007A9774).
    - **Addon sites**: named_data.py's NAMED_ADDON_SITES lets a named range hold the site of a Rebirth patch disabled
      here (SetDefaultRate's 00783E34: net::g_remoteRate, a plain int a flag can write); addon_sites.inc still names
      it for the harness in game.exe.
    - **The console's streams and command maps (C18s, 121 -> 90)**: console_streams.cpp names the eight streambufs
      (`lib::StaticObject<ConsoleStreambuf>`, 0x454 bytes, 0x458 apart), the eight ostreams (`console::OStreamObject`:
      the ostream and its ios, the virtual base, 0x58 bytes; trivial, so a plain `constinit` aggregate, TYPES
      'OStreamObject') and the two command maps (`lib::StaticObject<CommandMap>`), with their 18 initialisers
      (005DC050 / 005DC120, 005DC1E0-005DC3F0, 005DC420-005DC5E0) and 18 exit functions ported on the names and
      registered (rows in extra_function_sizes.csv / function_sizes.inc). The log pointers are `constexpr` pointers whose
      initialiser names the stream (`{&console::g_aiLogOut}`: named_data takes a qualified name of a range): unit_hits'
      kHitLog 007AF284, ai_alarm's kAlarmLog 007C2EB8, combat's kCombatLog 007AF3E8, unit_ai's kAiLog 007C53C4 and
      kModeLog 007C54E8; a file's own ostream view (AiLogStream, UnitLogStream, GameOstream, net::LogStream) reads the
      stream through `console::StreamAs<View>`.
    - **The singletons and the small leftovers (C18t, 90 -> 61; 56 with C25a)**: the worlds (g_serverWorld 007AFF60, g_clientWorld
      007AF5B8, world_construct.cpp), the camera 0079B2A8, the scene renderer 007AF060 and the device lock 007AEE78
      (renderer_lifetime.cpp), the map database 0079BF00, the debug panel 0079B930 (string_map.cpp), the class registry
      0079B270 (chunk_archive.cpp), clip's work words 0079B668, the network buffers 0079B8E0 / 0079B900, the save stream
      007AFDF8 and the received messages 007C83D0 (with its destroyed flag 007C83C8) are `lib::StaticObject` with their
      initialisers and exit functions ported next to them (atexit_400.cpp is gone); game_singletons.h's accessors are
      out of line. Plain globals: the main window 0079BC28 (app::g_mainWindow; render_game.h's Addresses hold
      pointers to it and the renderer), the stage blocks 007AEDD0 (g_stageBlocks, keyed by StageBlock), the loading
      movie's 007C5520 / 007C554C-54, the network buffer flags 0079B8FC, the damping scale 007C2F8C (its initialiser
      005B2FB0, ln 0.6 on the x87, ported in ai_refresh.cpp). The harness reads game.exe's copies through
      harness_objects.h (`ei::Standalone() ? ours : &ei::OriginalGameData<T>(address)`), and in a --no-hooks run calls
      game.exe's own PickAt2 / UnitAt / GameSendLeave04 / GameSetConstructionVariable (EI_ORIGINAL), which read them.
    - **Names in the generated code (C18u, 53 -> 41)**: gen_static_init.py's GENERATED_NAMES maps an address to a
      name and its header, and the generator writes the name where the original's code has the address: a guard's
      flag byte (`d.Once(name, bit)`), the object a call takes as `this` (`d.Thiscall(fn, &name)`) and where a call's
      result goes (`d.Store(name, d.Stdcall(...))`, kept in list order; named_data.py compares that definition at its
      zero start instead of refusing the unknown store). static_init.h's visitors take a `std::uint8_t&` guard, a
      `void*` object and a `T&` destination beside the addresses. Each entry must start a range of named_data.csv (the
      generator says so otherwise). Named this way: the function-local statics' guard 007CAEE4
      (`lib::g_emptyStaticGuard`, static_object.h: 175 generated tests and CSysInfo's) and the 11 MFC globals only the
      initialisers and exit functions reach (lib/mfc_static_objects.{h,cpp}): the exit guards of the temporary objects'
      fixed allocators and of the four static windows (007C8BB8, 007C9138, 007CAAA0, 007CAA24), _simpleUserException
      007C8BC0 (`lib::StaticObject<CSimpleException>`), the thread / process locals 007C90BC, 007C90C0, 007CAA84,
      007CAA88, 007CAA8C (nothing fills them in the image) and _afxDBCS 007CAA98 (its one reader in game.exe,
      _AfxCbtFilterHook 00704BE6, is not ported). C18r added the objects hand code reads too, whose initialisers are
      plain library calls: the temporary objects' fixed allocators (mfc_gdi.h: g_tempDCAlloc 007C8D18,
      g_tempGdiObjectAlloc 007C8CF0; mfc_window.h: g_tempWndAlloc 007CA940, g_tempMenuAlloc 007CAAA8) and the four
      static windows (g_wndTop 007CAA28, g_wndBottom 007CA9E8, g_wndTopMost 007CA9A8, g_wndNoTopMost 007CA968).
    - **The MFC and CRT objects (C18r, 41 -> 6)**: the CRT's allocator state is named in crt.cpp: STLport's node
      allocator (`g_nodeFreeLists` 0079B224, 16 lists by 8-byte class, `g_nodeHeapSize` 0079B264, `g_chunkStart` /
      `g_chunkEnd` 0079B268 / 6C; the ports that inlined a block's return call NodeDeallocate) and `_pnhHeap`
      (`g_heapNewHandler` 007CAAD8); `_afxNewHandler` is mfc_handle_map's `g_afxNewHandler` 00796E14, initialised
      `{DefaultNewHandler}` (a pointer element naming our registration at 0070058F). MFC (hand-ported initialisers and exit functions, rows in extra_function_sizes.csv /
      function_sizes.inc: each initialiser is the compiler's `call A; jmp B`, 10 bytes): the four string allocators
      (mfc_string.h `g_stringAlloc64` .. `512`, 007C8B80 .. 007C8B08), the
      resource / memory / not-supported exceptions (mfc_exception.cpp, 007C8C58 / 007C8DE8 / 007C8D50) and afxData
      (mfc_state.cpp `g_afxData` 007C90C8, `lib::StaticObject<AuxData>`); plain globals: the global locks
      (`g_afxLockInit` 007C8E80, `g_afxLockInitLock` 007C8EC8, `g_afxResourceLocks` 007C8EE0, `g_afxCriticalWin32s` /
      `g_afxCriticalInit` 007C9078 / 7C), `g_afxThreadData` 007C9084 and the halftone brush 007C8D40 (_afxWingdixTerm
      007C8D44 kept only atexit's answer, which nothing reads: dropped). The DragList message id `g_dragListMessage`
      007CAA64 is pointed at by CWnd's message map entry 00746C80, which nothing in the image reads
      (config/standalone_unreachable.csv).
      printf's power tables and the x87 functions' constants were already ours (crt_format.cpp, crt_x87.cpp).
    - **The empty string (C18, 6 -> 5)**: 0079B298 is game_strings.h's `kEmptyText` (`char[1]`, `game::EmptyText()`
      returns it). Its readers copy it (CString / STLport assignments) or compare its text (mbscmp, memcmp); none compares
      the pointer. The only data cells that point at it are the units' model folders 0078F7EC / 0078F824, a table only
      the dead 00532650 reads (gen_image_cells' UNREAD: zero in the image), so an UNREAD cell may
      point into a named range. The seams that took an address and had no address caller left went with it:
      `game::Text`, the camera's and the transform maths' `GameFloat(std::uintptr_t)`, the `FreeArray` / `ConstructList`
      overloads by vtable address and `CArray::Construct(std::uintptr_t)` (a vtable is passed as the class's `Vtable()`).
    - **Left**: the section bounds in game_data.h and image_data.cpp (0073B000, 0073B820, 00783000, 007CD000, 007D7000):
      the address image's layout, not data. They go when the image no longer keeps game.exe's sections at their
      addresses (the generated static initialisers, image cells and import slots still reach the range by address).
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
   7,258 less the retired slots) and writes their bytes zero in the image; named_data.py --check proves no address inside them is named (src/, tests/), and gen_image_cells.py --check that
   each row is a vtable of config/vtables.csv with its slot count and a class of the file it names. A vtable two
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
- **Batch 3's families**: CTexture (00743D24), CMappedResource / CMMIOResource (007432E0 with its tail 007432FC,
  00743300), CSoundManager (007440F0), the map objects (torch 0073E834 + 0073E850, lever 0073E740, trap 0073E548, magic
  trap 0073E63C + 0073E6E8), the torch's spec and its client half (0073D558, 0073E954 + 0073E98C) and the managers
  CTextureManager (00743CB0), CSpellManager (00743A14), CFigureManager (0073FB88): 17 rows, 21,439 -> 21,037 code cells.
  - **Bases still built by constructor bodies** (CObject3D, CMapObject, CBaseManager, CResourceManager): CUnit, the
    plain map objects and the other managers keep `Construct` / `DestructBody` (they store game.exe's vtable). The
    class also gets protected C++ constructors and a virtual destructor that share the bodies' work
    (`SetUpMapObject` / `TearDownMapObject`, `SetUpBaseManager` / `TearDownBaseManager`, ...), and every slot is
    defined (the bodies registered by member pointer get forwarding definitions), so its compiled vtable is a
    replica of game.exe's: a derived class's base destructors run with it, and a virtual call there (CMapObject's
    `DestroyMapObject`, CBaseManager's `Destroy`) reaches the same function as in the original. Members whose
    default constructor only nulls (stlp's vector and hash_map) are fine; base_manager.cpp asserts the rest are trivial.
  - **Slots with other types in the original**: slot 41 (`ApplyScience(int, int)`) gets the key record's address
    from combat_actions.cpp; the trap's and lever's overrides cast it back to `const LockKey*`. CTorchObjectSpecSlots
    (the setters 13-18 reached through the vtable) became CTorchObjectSpec's own virtuals.
  - **The tails go with their table**: gen_image_cells.py's check accepts a retired row that no code stores when it
    starts where a retired row it may follow ends (007432FC after 007432E0's 7 slots; 0073E850, 0073E6E8, 0073E98C).
  - **addon.dll sites**: CMappedResource's table holds addon.dll's ResOverride site 007432F4. addon.dll runs in game.exe
    only (harness_addon.cpp), so named_data.py exempts the sites in `RETIRED_ADDON_SITES` from the "no addon site
    inside a retired vtable" rule and does not count tests/verify/addon_sites.inc as naming an address inside one.
- **Batch 4's families**: every class derived from CBaseManager / CResourceManager (CMaterialManager 00743C38,
  CItemManager 0073F25C, CServerSpecificManager 0073D4F4, CClientSpecificManager 0073BECC + its tail 0073BF08,
  CLightFactory 00743B50, CPlayerManager 0073F328, CEffectManager 0073F2C4, CParticleManager 0073F644,
  CLightningManager 0073F5B8, CMapObjectManager 0073DCD8, the visibility fog's ClientArrayManager 0073F554) and the
  plain map objects (CTree 0073E178, CBuilding 0073E26C, CStone 0073E360, CStuffObject 0073E454): 19 rows,
  7,526 -> 7,004 code cells.
  - **Managers the original builds inline** (the item, player, effect, particle, lightning and fog managers: the base
    constructor, then members and the vtable in the caller) get a public C++ constructor and a `Construct()` that
    constructs in place (unregistered when the original has no function of its own), for `ConstructOn` and the
    callers (`Make<CBaseManager>(size, ConstructOn<CItemManager, &CItemManager::Construct>, nullptr)`).
  - **Slots a manager inherits as a thunk** (slot 3 of the item, material, map object, player and effect managers:
    `thunk_FUN_00674f40` / `thunk_FUN_004031e0`) and slot 13's copies of CBaseManager::Init at their own address
    (00562D70 lightning, 00562DA0 fog) are the base's virtual in the compiled table; their registrations stay.
  - **Two classes for one table**: world.h's ClientListManager was CLightningManager (the client world's +0x1A0, the
    same 0xCC bytes; its list is the new bolts); it is gone and the world holds a CLightningManager*. The map object
    manager was a plain struct reached through casts; it is now a CResourceManager (slot 14's body is NewObjectImpl,
    CreateObject is slot 23's override).
  - **A template keyed by its vtable** (PlainMapObject<kTreeVtable>) is keyed by the class id; gen_image_cells.py's
    check wants the csv's class to be one the file declares (`PlainMapObject`, the instance in the note).
  - **Left in game.exe's tables**: the bases' own 0073B870 (CBaseManager) and 00743338 (CResourceManager), stored only
    by the registered bodies ConstructBase / DestructBody / Construct / Destruct, which no code of ours calls any more
    (they go with their registrations, C20). CSkyObject and CUnit: batch 5.
- **Batch 5's families**: CSkyObject (0073D5B8), CUnit (0073EA8C + its tail 0073EAA0) and CFakeUnit (0073BFE8): 4
  rows, 7,004 -> 6,855 code cells.
  - **CSkyObject** derives from CPlainFigureMesh (batch 6; the mesh's layout plus the haze array; game.exe's 0x44
    table is the 0x42 mesh's 0073C1C4 but for slots 0, 11 and 12). CFigureMesh's protected constructor and destructor
    share ConstructBase's and DestructBase's work (`SetUpFigure` / `TearDownFigure`). CTransformObject's slots 13-17
    are defined (core_transform.cpp) for the compiled tables. SetPose takes the quaternion and offset by reference.
  - **CUnit**: every slot the unit changes is declared, the bodies registered at their addresses under other names
    (CreateUnit, Tick, LoadUnit, StandPoint, the net-state functions, ...) are called by the slots; slot 56's result
    (ObjectDeletedImpl has none) is 0, which nothing reads. The C++ constructor and destructor are 0052E110's and
    0052E310's work; the destructor calls slot 5's body (ReleaseUnit) directly, as the original calls it through the
    table's memory (that call is why find_vtables split the tail 0073EAA0 off). The members' CArray tables (carried
    0073EB84, the motion's 0073EB90 / 0073EB9C and their bases 0073EBA8 / 0073EBB4) are compiled collections since
    batch 8.
  - **CFakeUnit** (0073BFE8; scene::FakeUnitScene, scene_loader.h) is a CUnit with the skin and the scene's arrays
    (4 bytes at +0x4DC: the skin is 8-aligned in the original), overriding 2, 3, 11, 12 and adding slot 62
    (LoadScene). The skin (SceneSkin) got a C++ constructor and destructor (its name is a member; it is freed
    after the bone block now, a free order only). No scenario makes a fake unit (class 0x39; coverage: dead), so
    only the builds check it. CUnit's slot bodies stay registered at their addresses until C20.
- **Batch 6's families**: the figure classes 0x41 (CSharedFigureMesh, 0073FBF0), 0x42 (CPlainFigureMesh, 0073C1C4: the
  factory's attached parts and the scene loader's figure records 00453240) and 0x43 (CUiMesh, "CI3DFigure", 0073FC48),
  and the scene's meshes (scene::SceneMesh, "CFakeFigure", 0073C274): 4 rows, 6,855 -> 6,767 code cells. Next: the
  factory's other classes and link_figure.h's (batch 7), the containers by template (CArray<FaceGroup> 0073C2BC, the
  unit's member tables 0073EB84 / 90 / 9C / A8 / B4), network messages, interface (needs the harness id); the bases'
  own tables (0073B870, 00743338, 0073C224) with C20.
  - **CFigureMesh** is the abstract base ("CFigure"): it declares slots 18-21 (CopyFigure, LoadFromMemory, LoadFig,
    LoadOld; 20 / 21 pure) and overrides 2, 3, 7, 11, 12, 15, 16 as game.exe's base table 0073C224 has them (19 is
    LoadNothing there). CPlainFigureMesh adds 22 / 23 (MirrorX / MirrorY) and overrides 3, 17, 19-21; CSharedFigureMesh
    overrides 3 and 19-21 (game.exe's 0x41 table has 22 slots: no mirror); CUiMesh overrides 2, 3, 11, 12 of
    CPlainFigureMesh, CSkyObject 11 and 12. The slot bodies keep their registered names (CreateMesh, ClearMesh,
    SetPose, ...); where a virtual took the name the body is `...Impl` (CopyFigureImpl, LoadFromMemoryImpl, LoadFigImpl,
    LoadOldImpl, MirrorXImpl, MirrorYImpl). CFigureObjectVirtuals / FigureSlots are gone.
  - **0073C224 stays** (as CBaseManager's 0073B870): only ConstructBase / DestructBase store it, which no code of ours
    calls; nothing ever has the base as its final class (game.exe's other two stores, 00476FB0 / 004A0350, are dead code).
  - **Sizes**: CFigureMesh is class 0x41's 0x1E0 bytes; the four arrays only class 0x42 and its derived classes have
    (corners, words, indices, texture coordinates) and the bodies that use them are CPlainFigureMesh's (batch 7). The
    factory allocates `sizeof(T)`, static_asserted against the game's size for the class.
  - **Value-initialisation**: the factory's `new (o) T()` zero-fills a class whose default constructor is not
    user-provided before running it; every class it constructs declares one (CUiMesh's is empty), so the memory is
    left as operator new gave it, as in the original.
  - **ei_vectors** links record_arrays.cpp but not the figure or scene files: the element constructors that need the
    classes' vtables (InitFigurePart, InitSceneMesh) are defined next to the classes and stubbed in
    tests/vectors/replay_unlinked.cpp.
  - **SceneMesh** (no scenario makes a fake unit's scene: builds only) overrides 2 (1), 3, 7, 11, 12, 16, 17 (empty).
  - Registrations whose only reader was a retired table's code cell became EI_REWRITTEN
    (`registration_readers.py --drop --only`), batch 5's sky ones with them.
- **Batch 7's families**: the figure factory's other classes AnimationTrack (0x300B, 0073D9D8), FigureBone (0x300C,
  0073DA0C) and CFigureAnimationState (0x300D, 0073DA4C; also the map object factory's class 0x300D), the figure mesh
  drawer (FigureMeshDrawer, class 0x36 "CFigureClientSpecific", 0073FB10) over its base (FigureDrawerBase, 0073FB44)
  and the figure's mesh (CLinkMesh, 0073D790) over its base (CLinkMeshBase, 0073D7CC): 7 rows, 6,767 -> 6,672 code
  cells; the 37 registrations only those cells read are EI_REWRITTEN. Before them, class 0x41's size (above).
  - **Engine objects again** (as batch 2's): C++ constructor and virtual destructor sharing the registered bodies'
    work, `operator delete` into the game's heap, the changed slots overridden (2 / 3 call the registered reset
    bodies CreateTrack, DestroyBone, ...; 11 / 12 return the small_slots_500.cpp / world_class_slots.cpp constants).
    Their lists' tables (the bone's child list and the state's lists, 0073DA40 / 0073DA80) went in batch 8.
  - **A base with only pure slots** (0073FB44, 0073D7CC: every slot but 0 is `_purecall`) is an abstract C++ class
    with a protected constructor; its registered destructor bodies (005A70C0 / 005A70A0, 004F2D40 / 004F2E00) call its
    destructor qualified. CLinkMeshBase declares the 14 slots pure (their comments live there); CLinkMesh overrides
    each with a call of its `...Impl` body. The counted root under CLinkMeshBase (0073C368, one slot, no fields) is not
    a class of ours: other classes' destructors still store it (`kRootVtableAddress`).
  - **Counted deletes**: the figure's last reference goes with `delete mesh` (the virtual deleting destructor), where
    the code called slot 0 with 1 by name (`DeleteThis(1)`).
  - **CObject3D (0073DDD4) waits for C20** with the bases' own tables: only CObject3D::Construct / DestructBody store
    it, reached from CMapObject's (0073DAA0, the same case) and CObject3D::DeleteObject3D, none of which our code
    calls; every class under it has a C++ constructor. Its row in config/vtables.csv is 5 slots: find_vtables split the
    table at 0073DDE8 (006E0437 reads that address), and gen_image_cells.py lists the other 22 slots as a reviewed
    range (`CODE_REVIEWED`), so retiring it also needs the row widened to 27 and that range dropped. (Batch 22 retired
    it with CMapObject's, and moved the range to NOT_CODE instead of widening the row.)
- **Batch 8's families**: the containers by template: CArray<FaceGroup> (0073C2BC, record_arrays.h's
  FaceGroupArrayClass), the unit's member arrays (carried 0073EB84; the motion's waypoints 0073EB90 over 0073EBB4 and
  points 0073EB9C over 0073EBA8) and the lists 0073DA40 (a bone's children, an animation state's bones), 0073DA80 (an
  animation state's queue), 0073DE4C (a 3D object's effect sources): 9 rows, 6,659 -> 6,632 code cells; the 16 slot
  registrations only those cells read (the scalar deleting destructors and the Serialize bodies) are EI_REWRITTEN.
  - **Compiled collections** (mfc_slots.h's `CompiledCollection<Self, Destroy, Store>`): the collection templates keep
    the vtable a plain field, and an instantiation's Class tag names the table its constructor and destructor store
    (`Class::Vtable()`). A tag that derives from CompiledCollection is a C++ class with CObject's three slots over
    the header's layout: slot 0 GetRuntimeClass (CObject's record, as 007005E3), slot 1 the virtual destructor
    calling the instantiation's own destructor on the header (`ArrayHeader<Tag>::Destruct`, `PtrList::Destruct<Tag>`,
    `scene::FaceGroupArrayDestruct`: the table written, the elements destroyed, the storage freed; the scalar
    deleting destructor frees into the game's heap), slot 2 Serialize (`SerializeCArray<Abi>`, `SerializeCList<Abi>`).
    `Vtable()` is the table a Tag's constructor stores (`CompiledVtable<T>`: one built in a buffer and read back).
    Nothing of ours is a Tag object: our code calls the header's functions by name, and the slots are what a dispatch
    through the table reaches (an element's slot 1 in DestructElements, an archive's slot 2).
  - **Derived instantiations** (the motion's 0073EB9C / 0073EB90, which the unit's constructor stores, over
    0073EBA8 / 0073EBB4, which the destructors and 00556D70 store): the derived tag (MotionPointsClass,
    MotionWaypointArrayClass) derives from the base's and only names its own table; its destructor ends in the
    base's, which stores the base's table, as game.exe's 00532E30 / 00532E50 do.
  - **Slot 0** is `ObjectRuntimeClass()` (mfc_coll.cpp: ei_vectors links it, not mfc_slots.cpp).
  - CUnit's carried array's table field is `void*` (unit.h); the code sets every one of these tables through its tag.
  - The other ~150 container tables can go the same way, a tag at a time: replace the tag's `Vtable()` by deriving
    from CompiledCollection with the tag's destructor and Serialize (mfc_slots.cpp's ScalarDelete / Serialize
    registrations name both), retire the row, then drop the registrations only the retired cells read.
- **Batch 9's families** (2026-10-21): 52 more container tags, 6,632 -> 6,476 code cells, 97 slot registrations
  EI_REWRITTEN: in mfc_slots.h the trap arrays (circles, points, and the map trap record's areas / targets derived from
  them), the script command's argument and binding arrays (ArgObjectsBase / ArgGroupsBase / BindingsBase with the
  derived ArgObjects / ArgGroups), the scene loader's arrays and maps (Vector3, NodeItems A-C, TextureLink,
  MeshCorner, FigureRegistry, CornerMap A-C), the map object, world and unit arrays (Effect, EffectSound, Frame, Id,
  SkinLayer, Countdown, PartyUnit, SpiralCell), the path smoothing, UI and terrain builder arrays (SmoothEdge / Copy /
  Step, ObjectiveExit, ItemStrip, QuickEntry, TokenPtr, LinePtr, TerrainArray A-C) and the pointer lists
  (ClientObject, Experience, Attention, Player, Texture, Sound); in record_arrays.h CArray<SceneNode / DeltaField /
  Behaviour / SectorVertex>, the command's nested calls (SubCallBase with the derived SubCallArray), the binding arrays
  (BindingArrayClass over BindingsBaseClass) and the terrain's tile lists.
  - **A derived tag** is the tag whose table's slots call another tag's destructor (ScalarDelete<ArrayHeader<Base>>)
    and Serialize (the same address as the base's): `struct Derived final : Base` with its own `Vtable()`
    (`CompiledVtable<Derived>()`); the base is not final and is defined before it.
  - **Where the uses were**: every `GamePointer<void>(0x...)` / `DataId(0x...)` / `kXxxVtable` constant for a retired
    table became the tag's `Vtable()` (helpers that took an address got a `void*` overload or parameter); tests/vectors'
    replay_script_load.cpp builds its command arrays with SubCallArrayClass (the loader's empty arrays, 0073D4A8).
    Aliases a tag names (Plain36 / Plain56, AttentionList) moved above the tags.
  - **Skipped** (not the pattern, or a header the tag cannot reach): SectorOccluderArrayClass 0073FAE0 (shares slots
    with the sector's own table 0073FADC, which gen_image_cells.py refuses to retire apart); Array0498Class /
    ParticleDepthArrayClass 0073D024, SectorCellArrayClass / SpiralOffsetArrayClass 0073FDEC and SmoothRowArrayClass /
    SmoothNodeArrayClass 0073FE34 (static_init.cpp stores the table in generated static initialisers: 007A9C50,
    007C2FF8, 007C3038); the tags of mfc_array.h and mfc_list.h (the aliases there name them: they would move into
    mfc_slots.h); the owned-element arrays whose Serialize alias lives in a game header (record_arrays.h, game_arrays.h:
    FakeUnit*, NamedRecord, the global map's) or later in mfc_slots.h; game_slots.h's and game_lists.h's script lists
    (SerializeCListThrough over aliases of game_slots.h); synonyms of the same address in two headers (game_arrays.h's).
- **Batch 10's families** (2026-10-21): 51 more container tags, 6,476 -> 6,323 code cells, 93 slot registrations
  EI_REWRITTEN: the owned-element arrays of mfc_slots.h (FakeUnitBoneArrays, SceneObject, TerrainByteArrays,
  AlignedAt4 / AlignedAt8, TextureName, MemoryRecord, the global map's Point / Path / Zone, Patrol with the derived
  BehaviourPatrols, Allod, ScriptArea: their element-policy aliases moved above the tags); the owned-element arrays whose
  Serialize alias lived in a game header (record_arrays.h: FakeUnitPart / SceneRecord / Record9C and Definition;
  game_arrays.h: PointLight, Group, the object pointer base ObjectPtrArrayBase); game_slots.h's script lists and arrays
  (ItemObject, ClientObject2, ObjectRef, Victim, RefListB, OwnedRef, SharedObject, ArmourSpell, Wave, the script's
  ParticleSource / FxSource / Lightning / Decl / Definition / Run / Group / Token / Define lists, MemoryObject,
  ClientObjectPtr, EffectObject and CommandArray); and mfc_array.h's model and animation file arrays (RotKey, PosKey,
  MorphHead, Normal, FilePoint, FileUv, FileFace, Index) moved into mfc_slots.h.
  - **Duplicates made one**: game_arrays.h / game_slots.h / mfc::impl each had a tag for 0073D3A0 (PointLight) and 0073D3B8
    (Definition) and game_lists.h another for the script lists (TempObject / TempGroup / Instance names), the owned and
    shared lists and the token list; each address has one tag now (game_slots.h's `gameslots::`, record_arrays.h's
    `recordarrays::`, game_arrays.h's `gamearrays::`). The raw constants (script.h's `scriptvtable::kXxxList` and
    kGroupsBase, world.h's ObjectsBaseVtable, worlddata::kClientListVtable, combat_lifetime / world_destructors /
    world_construct / sound_manager_lifetime / map_object_lifetime's `kXxxVtable`, scene_loader.h's `scenedata::` array
    accessors) became `Tag::Vtable()`. The sound cache's WaveListClass registrations moved to game_slots.cpp: its
    Serialize names WaveRecord::Assign (sound_manager.cpp), which tests/vectors does not link.
  - **Skipped, for a later batch**: the tables static_init.cpp stores in generated initialisers (0073D024, 0073FDEC,
    0073FE34, plus FloatArrayClass 0073D3E8 and the Objective / NamedRecord array 0073D46C, 007AA608); the sector occluder
    array 0073FAE0; RefListAClass 0073FA4C (kept as the old tag); mfc_array.h's remaining tags (IntArray, UnitPtr,
    Float, ArgFloat, ObjectRef, ScriptGroup, ScriptFloats, ScriptArea, ScriptIfThen, PartySlot, AiLook, Sprite, BoneLink,
    Waypoint, Exit, MapRecordItems, Point, AttackOption, AnimationSteps(Copy), ClientMap, ObjectPtr, Sector, CellRow,
    AreaList, StringArray, ArrayBase, AiLookRecord) and mfc_list.h's (PtrList, ItemList, TempList, CellList, LootList,
    PlatformList, Client / Client2 / Client3, NodeList, BlockCostList, AiMapList, WaypointList, StaticList, DwordMap,
    ScriptCommands): the shared base tags (mfc::PtrListVtable / DWordArrayVtable) and the element aliases that name them
    in mfc_array.h need their dependencies reordered first.
- **Batch 11's families** (2026-10-21): 43 more container tags, 6,323 -> 6,194 code cells (6,218 after the 35 library tags,
  then the 8 game ones), 76 slot registrations EI_REWRITTEN (62 + 14): the rest of mfc_array.h's and mfc_list.h's tags.
  - **Library tags** (35, defined in mfc_slots.h after the element policies and the base tags PtrList / IntArray, which
    the batch moved ahead of the users): IntArray, UnitPtrArray, ArgFloatArray, ScriptFloats, ScriptArea with the derived
    ScriptAreaParts, ScriptIfThenArray, PartySlotArray, AiLookArray with AiLookRecordArray, SpriteArray, BoneLinkArray,
    ArrayBase with WaypointArray, PointArray, AttackOptionArray, AnimationSteps with AnimationStepsCopy, CellRowArray,
    StringArray, and the lists PtrList, ItemList, TempList, CellList, LootList, PlatformList, ClientList / 2 / 3, NodeList,
    BlockCostList, AiMapList, WaypointList, StaticList, plus DwordMap. mfc_array.h and mfc_list.h only declare them
    (the element policies and aliases name them), so a translation unit that calls `Tag::Vtable()` or instantiates an
    alias over them includes lib/mfc_slots.h (MSVC instantiates the alias' element functions where the alias is
    named: an incomplete tag fails there). Removed: lib/mfc.h's PtrListVtable / DWordArrayVtable, combat_plan.h's
    kAnimationStepsVtable / kAnimationStepsBaseVtable, script.h's kFloatsBase / kStringsBase.
  - **Game tags** (8, mfc::impl classes defined in the game header that has their elements): game_arrays.h (ExitArray with
    the derived MapRecordItems, ObjectRefArray, ScriptGroup, the derived ObjectPtrArray), game_slots.h (the derived
    ScriptCommands), record_arrays.h (ClientMapArray, SectorArray). The element aliases that name them moved with them
    (MapRecords, ScriptGroups, SharedObject*, CommandBlock*): a lib header cannot name a tag it does not define. The lib
    registrations over them (Construct of the arrays, ConstructElements / NewNode of the aliases, ExitTargets::Destruct)
    moved to game_arrays.cpp, record_arrays.cpp (ClientMap) and game_slots.cpp. The Sector array's Construct is in
    sector_map.cpp: its table's destructor is CSectorMap::DestructSectors, which tests/vectors does not link.
    gameslots::GroupDestruct became gamearrays::DestructGroupBody (inline, so the vectors build links the tags).
    Raw constants gone: flowdata::kExitArrayVtable / kExitArrayBaseVtable, world_construct.cpp's kZoneExitArrayVtable,
    world_destructors.cpp's kWaterLevelsVtable, sector_map.h's kSectorArrayVtable, world.h's ObjectsVtable, script.h's
    kRefs / kRefsBase and the literals in script_load.cpp, script_runtime_helpers.cpp and script_vars_append.cpp.
  - **Left raw** (batch 12 took them): AreaList 0073F9E0, FloatArray 0073D3E8, the script variables' derived tables and
    the batch 10 skips.
- **Batch 12's families** (2026-10-21): the container tables earlier batches left for tool or initialiser reasons: 12
  rows, 6,194 -> 6,158 code cells, 24 slot registrations EI_REWRITTEN.
  - **One-slot tables** (find_vtables.py): a table ends after its first slot when the next address is one the code
    stores (`mov r/m32, imm32` / `mov r32, imm32`: a vtable of its own; an address the code only calls through, `call
    [v]`, still splits only after the second slot). CSectorMap's 0073F9DC and the sector's 0073FADC (each only a
    deleting destructor) were 4 slots, overlapping the area list's 0073F9E0 and the occluder array's 0073FAE0; 19 other
    rows got their one slot too (0073CAB0, 0073D760 / 64 / 70 / 74, 0073FD58, 00740F28, 00742F20 / 24, 00743320,
    00744054-00744064, 00744264, and the retired 0073CA08, 0073CA0C, 007432FC). Only the slot counts of
    config/vtables.csv were taken from the rerun: a full rerun also rewrites installed_by from today's functions.csv and
    adds five library rows (0074676C, 0074718C, 007474B8, 00747E2C / 30), left for the library batch. The union of
    slots is the same, so no code cell moved. AreaListClass and SectorOccluderArrayClass are compiled tags in
    mfc_slots.h (sector_map.h's kSectorOccluderArrayVtable gone).
  - **Tables the static initialisers stored** (ParticleDepth 0073D024 / 007A9C50, Objective 0073D46C / 007AA608, SpiralOffset
    0073FDEC / 007C2FF8, CArray<float> 0073D3E8 / the radius tree's keys 007C2FD8, SmoothNode 0073FE34 / 007C3038): the
    initialisers inlined the arrays' constructors (00498E60, 004C9D90, 005C18C0, 005BF890, 005C44B0); game_arrays.cpp
    registers those constructors (`ArrayHeader<Tag>::Construct`), so gen_static_init.py writes `d.Thiscall(ctor, object)`
    in place of the stores of game.exe's table (no change to the generator: a registered callee is called, not inlined).
    The four game tags are game_arrays.h's (mfc_slots.h's raw duplicates Array0498Class, SectorCellArrayClass,
    SmoothRowArrayClass and NamedRecordArrayClass are gone, their slot registrations moved to game_arrays.cpp);
    FloatArrayClass is mfc_slots.h's (the raw constants of sector_map, terrain_landscape and map_object_lifetime gone).
    Two unread objects right after two of the arrays (007A9C64, 007C3010) are REVIEWED_NEAR entries: with the stores
    gone the array below is no longer an initialiser-stored object, and its maxSize / growBy field is the referenced
    address below them.
  - **The script variables' tables**: kFloats 0073D2E0 is mfc_slots.h's ScriptFloatValuesClass (derived from
    ScriptFloatsClass, whose destructor its slot calls), kIfThen 0073D430 ScriptIfThenPairsClass (derived from
    ScriptIfThenArrayClass); kStrings 0073D2D4 and kGroups 0073D304 are game_arrays.h's ScriptStringValuesClass /
    ScriptGroupValuesClass over the destructors their slots call (ScriptNamesDestruct 004BC810, ScriptGroupArrayDestruct
    004BCA20). script.h's scriptvtable::kFloats / kStrings / kGroups / kIfThen, game_lists.cpp's DefinitionIfThenClass
    and the literals in script_load.cpp / script_parse_load.cpp are gone. RefListAClass 0073FA4C (the world server's
    new connections, AcceptNewPlayers) is game_slots.h's compiled tag (game_flow_data.h's kSessionListVtable gone).
- **Batch 13's families** (2026-10-21): the one-slot rows batch 12 freed and the CScript members' tables: 26 rows,
  6,158 -> 6,126 code cells, 27 slot registrations EI_REWRITTEN.
  - **Compiled tags for any class** (src/lib/compiled_vtable.h): `ei::CompiledVtable<T>()` (moved out of mfc_slots.h,
    which re-exports it), `ei::CompiledTag<Self>` (the `Vtable()` accessor) and `ei::CompiledDeleter<Self, &C::Delete>`
    (a one-slot table whose slot 0 calls the class's deleting destructor member). A class that keeps `void* vtable`
    as a field stores `Tag::Vtable()` where it stored game.exe's address; the original's dispatch through the field
    (`Release`, `Delete`, `Blit`, `Kind`) reaches the tag's slot, which calls the same body.
  - **The rows**: the thrown error object (CDXException / CGameException, 0073CAB4 / 0073CAB0; dx_errors.h; real
    classes since C28j); the renderer's (real classes since C28j: renderer_base.h's CRendererBase 0073D768,
    texture.h's CDXDirectDraw 0073D770, CTextureDevice 0073D760 over it and CDXSurface 0073D774, model.h's
    CTransformPipeline 0073D778, whose MatrixStack members construct and free themselves); 0073D764 with no class (only the dead 004E72A0 / 004E76B0 / 004E7700 store it);
    UnitShapeCache 0073FD58 (a real virtual class: CreateShape calls CreateUnitShape, Construct constructs in place);
    CUiSurface 00740F28 (a real class since C28m: slot 0 Blit; CUiScrollingSurface 00742FD8 over it, built with
    `new`, freed with `delete`) and the scrolling text's tokens (UiTextToken
    00742F24, UiFormatToken 00742F28, UiWordToken 00742F20; ui.h; real classes since C28m); CResourceBase / CResourceStream
    (00743324 / 00743320: real virtual classes since C15 batch 11); the item records (item_db.h, 00744054-00744068: real
    classes since C28i, ItemRecordHead abstract with Kind as slot 0 (its 00744060's slot is the compiler CRT's
    `_purecall` as game.exe's 006E73EB, reached only by a bug), constructors and copy constructors per kind); prints.db's PrintRecord /
    PrintRecord2 (00744264 / 00744268: real virtual classes, `Vtable()` the compiler's, Serialize defined).
  - **Two game tables for one instantiation**: the CScript constructor's 0073D394 / 0073D358 / 0073D334 do what
    0073D3A0 / 0073D3B8 / 0073D3C4 do (MSVC6 emitted the instantiations twice); the constructor stores the compiled
    PointLightArrayClass / DefinitionArrayClass / ScriptAreaArrayClass that the destructors already used.
  - **Not vtables**: the five rows a full find_vtables rerun adds (0074676C, 0074718C, 007474B8, 00747E2C / 30) are
    string bytes ("%lc", "Unregister", ...) whose first dword happens to fall in .text: never retire them (zeroing
    them breaks the strings); a rerun's rows need that check before they are copied into config/vtables.csv.
- **Batch 14's families** (2026-10-21): the network area's tables and the items': 42 rows, 6,126 -> 5,666 code cells,
  220 slot registrations EI_REWRITTEN.
  - **Messages and server entries** (net_messages.h, net_classes.h; 20 rows 0073BB28-0073BDD0): real C++ classes since
    C28k (a constructor, Clone's copy constructor, OnShutdown destroying and rebuilding in place, a destructor). The
    message registry keys on the object's class (TypeRegistry::KeyOf: its vtable).
  - **Sessions** (net_classes.h): SessionBase (15 slots), ServerBase / ClientBase (17) and the final ServerSession /
    ClientSession, real C++ classes since C28k. **Connection, replica entry** (net_connection.h) and the **links**
    (net_link.h: NetLink and its Loopback / Replay / Framed / Driver links): real C++ classes since C28k (FileStream
    since earlier, DriverSession since C28g).
  - **Items** (item_class.h over engine_object_class.h; real classes since C28a): EngineObjectClass declares CEngineObject's 13 slots with the
    base's bodies (Release keeps the hook's EAX: this, or the manager's slot 24 result); ItemClass adds CItem's 14-21
    (17-20 return a CString by value: the caller's hidden result, as game.exe's); one tag per item class (the
    scroll's slots 9, 10, 13 and 14 are CItemQuick's bodies) and the item constructor's.
  - **Trap: a removed `kVtableAddress` falls back to the base's.** Removing a class's constant leaves an unqualified
    `kVtableAddress` in its members compiling against the base class's (CEngineObject's 0073B8E0, CRefObject's): the
    first build of the item tags stored CEngineObject's table from party_lifetime.cpp's CItemConstructor::Construct
    and every run hit `_purecall` at startup. Before building, declare a poison member (`static const struct
    Poison kVtableAddress;`) in the classes and build once to list every leftover use.
  - **Left**: GameBuffer's 0073CA48 / 0073CA64 / 0073D624 (net_coder.h): tests/vectors/record_coder.cpp compares a
    buffer's table with game.exe's address under game.exe, which named_data's check forbids for a retired row; it
    needs another recognition first.
- **Batch 15's families** (2026-10-22): the spells', GameBuffer's, the world's and the units' tables: 39 rows,
  5,666 -> 5,175 code cells, 194 slot registrations EI_REWRITTEN, 1,138 -> 1,108 game data addresses.
  - **Spells and light** (spell_class.h; real classes since C28b): CSpellClass, CSpellContainerClass, CSpellConstructorClass, and CLightClass
    over the new `TransformObjectClass<Self, T>` (engine_object_class.h: CTransformObject's slots 13-17 with the base's
    bodies). 0074399C stays: the harness names it.
  - **GameBuffer** (net_coder.h): the abstract GameBuffer (0073CA48: 2 Commit returns 0, 5 its deleting destructor)
    with GrowingBuffer (0073D624) and BufferView (0073CA64) over it, real C++ classes since C28k (locals and members
    are built and destroyed by C++; MasterLink and Query became real classes with them).
    tests/vectors/record_coder.cpp recognises game.exe's buffers by `&ei::OriginalGameData<const
    std::uint8_t>(0x0073D624 / 0x0073CA64)`, which named_data allows in tests.
  - **World effects** (world_effect.h; real classes since C28f): CEffect declares 18-23 over the transform's; the point
    light's own radius is slot 24 (LightRadius; its slot 19 is the base's), the missile's MoveParts slot 24. 007435D8 is a
    byte match inside an FDIV operand of 005E1680: retired as the particle table's tail.
  - **Small ones**: the unit shape (ai_unit_shape.cpp) and the item link (item_link.cpp) (real classes since C28e);
    the terrain (terrain.h, slot 14 its own CombineRotation; tail 0073F6E8 a byte match in 006E81C2), the text manager
    (item_db_text.h), the player sources' base 0073F38C (slots 2-4 and 6 pure), the server's counted helper 0073F240,
    the client's source 0073F628 and the network session 0073F3A8 (world.h, net_link.h) are real classes since C28g;
    the bridge manager 0073F4FC (PlatformSet, world.h; since C28g). Generated EI_REPLACE_SHORT rows (short_functions.cpp) are never dropped.
  - **Trap: `python tools/find_vtables.py --help` runs the tool** and rewrites config/vtables.csv; restore it with git.
  - **Left in world* / unit***: the map object companions 0073DBEC / 0073DD6C (irregular slots), the worlds 0073F18C /
    0073F41C / 0073F928 (42-43 slots), the unit client's 0073ECD8 / 0073EDBC / 0073F0C4, the AI modes (the harness
    digest names them), and world_data's unit spec tables 0073C6E0 / 0073FA58 (map spec, C21).
- **Batch 16's families** (2026-10-22): the worlds', the unit controllers' and the map object companions' tables: 9
  rows (retired 346 -> 355), 5,175 -> 4,825 code cells.
  - **Worlds** (world.h; real classes since C28g): CWorld declares slots 25-41 (13-24 are CManagerContext's; 27-30
    pure), CWorldClient (0073F41C) adds slot 42 MoveObject2, CWorldServer (0073F928). Not final: the original calls
    the slots through the table, and a devirtualised call let the client's slot 29 inline slot 27's float body
    (effects_bow's unit height moved). The height slots return double (ST0); the server's slot 27 and the client's
    call the float bodies GridHeight / ClampedGroundHeight, and slot 6 calls Update / Segment, which keep their names
    for the main loop and the harness (by address).
  - **Unit controllers** (real classes since C28h): CUnitController (unit.h, 0073EDBC) declares slots 13-53 with
    the base's bodies (16, 17, 24-27, 36, 37 and 50 pure); CUnitServer (combat.h, 0073EF38) and CUnitClientSpecific
    (unit_client_models.h, 0073ECD8) derive from it. Slots whose original is a plain RET are void (CUnit passes EAX
    of 23 / 44 / 49 / 53 on; nothing was seen to use it, so CUnit's wrappers return 0). The bodies registered at
    their addresses (the server's MotionSlot / EmptySlot17, the client's ClientControllerPath and the shared empty
    ones) stay beside the overrides.
  - **Map object companions** (real classes since C28h): CMapObjectCompanion (map_object.h, 0073DC54) declares
    slots 13-19 (17 and 18 pure); CMapObjectClientSpecific (0073DBEC) and CMapObjectServer (0073DD6C, slot 20
    TakeHit) derive from it (unit.h). Slot 13 takes a bool (the client's body reads the low byte); slot 19 is void
    (the objects' slot 47 passes EAX on, which nothing reads).
  - **check_fp_fold** (removed in C25d): once CWorld::SetTimeSpeed lost its slot registration the compiler inlined it into the
    server's (0058EEE0); its 1/15 is 0055FB50's own double.
  - **Left in world* / unit***: world_data's unit spec tables 0073C6E0 / 0073FA58 (C21). (The tamed mode 0073F0C4
    went with the other AI modes; real classes since C28h.)
- **Batch 17's families** (2026-10-22): combat_lifetime's containers, the AI modes, the 3D object halves and a dozen
  small ones: 33 rows (retired 355 -> 388), 4,825 -> 4,548 code cells, 1,099 -> 1,065 game data addresses.
  - **Second tables**: the brain constructor stores 0073F04C / 0073F070 / 0073F140, tables with the same slots as the
    container tags' 0073F034 / ... (twins the compiler emitted per instantiation site); one tag serves both.
    The shadow object's one-slot 0073DF68 is WoundsMaterial's (map_object_shadow.h): a real final class over
    CDeviceMaterial, whose slot 0 is now a pure `DeletingDestructor`.
  - **AI modes** (real classes since C28h, unit_ai.h): AIMode (the base 0073D15C) and AIActionMode (0073D22C) over
    CRefObject, one class per mode (guard, sentry, follow, suspection, aggression, corpse watcher, fear, patrol,
    player, tamed: TamedMode derives from PlayerMode). AIMode's constructor clears the count and flags (the
    original's creators did). Slot 2's original body leaves EAX unset where ours returns 0. The patrol points' CArray
    (unit_ai_mode_lifetime.cpp) is a CompiledCollection.
  - **The digest's mode id**: harness_digest.cpp's `ModeKinds()` lists each mode's game.exe table
    (`&ei::OriginalGameData<const std::uint8_t>(address)`) in type order; in the image `ModeId` takes the kind from
    `GetType()` (Follow's 10 the last), so `ai.mode` prints game.exe's address in both runs and no golden changed.
  - **3D object halves** (real classes since C28h): CObject3DSpecific (map_object_figures.h, 0073DEFC; slots 13-17
    pure), CObject3DServer (0073DFBC: 14 / 16 / 17 null slots) and CObject3DClientSpecific (map_object_shadow.h,
    0073DE90).
  - **Small ones**: the theme array 007440CC and the map database's maps 0073C3E8 (CompiledCollection; game_slots.h's
    MapRecordArrayClass over BuiltZoneRecords), the mouse 0073C350 (`ei::CompiledDeleter`), the debug panel 0073BFA4,
    the class template 0073D0C4, the party record 0073C360 (real CRefCounted classes since C28i; ClassTemplate keeps
    a `Construct` that runs its constructor in place for 004A6580's registration, which the harness's zeroing patch
    calls) and CDib 00744288 (a real class since C28i), the AI map's unit layers
    0073FD7C / 0073FD74 / 0073FD6C, a sector 0073FADC and the sector map 0073F9DC (real classes since C28h: CUnitLayer,
    CSmallUnitLayer, CLargeUnitLayer, Sector and CSectorMap, members of CAIMap, whose constructor and destructor
    are its own since then).
  - **Left**: 236 rows of config/vtables.csv (3,702 slots); the counted root 0073BB68 (some 100 storing functions),
    the render helper 0073D880, the cloud noise base 0073D030 with ui_global_map's 007420D4, the map spec chunk
    readers 0073C710 / 18 / 20 (map_spec_unit.cpp, next to C21's unit spec tables), the UI's 007403F8 / 00740FD4 /
    007418E0 / 00742464, and the MFC library kind.
- **Batch 18's families** (2026-10-22): the two counted roots, the render helper, the cloud noise, the item turn,
  four input sides, the figure loader, seven engine objects, the party and the player connections: 29 rows (retired
  388 -> 417), 4,548 -> 4,229 code cells, 109 registrations EI_REWRITTEN.
  - **Roots**: `CRefCounted::RootVtable()` (core_streams.cpp's CountedRootClass: the deleting destructor, the empty
    Serialize) in place of 0073BB68, which every counted class's destructor leaves (net messages, sessions, links,
    AI modes, the party records, the spell effect, ...); `PlainRootVtable()` (core_refcount.h, PlainRootClass) in place
    of 0073C368, the renderer's, the figures' and the mouse's root.
  - **Small tags**: the renderer's helper CRenderHelper (0073D840) and its preset CRenderPreset (0073D880), real
    classes since C28j (their fieldless abstract bases 0073D860 / 0073D888 are not modelled); the cloud
    noise's 0073D030 and 007420D4 (CCloudNoise and CGlobalMapClouds, real classes since C28f); the item
    turn's ItemTurn (007403F8, a real CRefObject since C28m); the figure loader CLinkFigureLoader (0073D808), the wounds material (0073DF68) and
    the DirectInput mouse (0073C350), real classes since C28j (the loader's abstract base 0073D824 is not modelled).
  - **Input sides** (ui_input_class.h): `InputSideClass<Self, Delete>` declares an input side's 13 slots with the base
    input side's bodies (1-6 and 9-11 empty, returning 0 where the original leaves EAX; 7 / 8 the double click as a
    press; 12 the wheel ignored); slot 0 is `Delete`, the window's deleting destructor through the side (the window
    member pointer's class gives the -0x94) or UiInputSide's own for an extra side. The container's
    ContainerInputClass (00740FD4 with its tail 00740FE8), the HUD top panel's TopPanelInputClass (00742464 with
    0074247C), the battle camera's CameraInputClass (00740208) and the shared InertInputClass (00741154:
    `InertInputVtable()`). The window tables themselves stay game.exe's: the census (ui_census.cpp's
    `InGameVtables`) and the window slot checks' stand-in tables need a window's vtable to be game data, so the
    windows' main tables (007418E0 and the other 26-slot ones) wait for a harness id as the AI modes' (batch 17).
  - **Engine objects** (engine_object_class.h): CHoover (0073C434) is a real class since C28e, CLightning (0073C550)
    and CCamera (0073B9E0) since C28f, CSysInfo (0073D644; its global a lib::StaticObject) since C28i; PlayerClass
    (00743104).
    The classes' `DeleteScalar` became `DeletingDestruct` (EngineObjectClass's slot 0 names it). CParticleSource
    (0073CFD0) and CRainSnow (0073F4C8 with its tail 0073F4E8; its slots 11 / 12, "CWeather" and 0x1D, are dead
    code's) are real classes since C28f.
  - **The party** (party_lifetime.cpp): PartyBaseClass (0074306C) and PartyClass (00743000) over a
    PartyContextClass that declares the manager context's slots 13-26 as the worlds' (world.h) with the base's
    bodies (every getter null; 24 the party's copy of an item); the party overrides Create / Destroy, its class name
    and id and the item and spell getters. Its unit records' CArray is PartyMemberArrayClass (00742FF4), a
    CompiledCollection whose Serialize goes through a mfc::impl::CArchive member (mfc.h's CArray takes mfc.h's
    CArchive).
  - **The player connections** (player_messages.h, player_client.h): CPlayerConnection (007431E0; slots 13 / 23
    pure, the others empty), CPlayerClient (00743180), CPlayerServer (00743280) and the world player CPlayer
    (00743104) are real C++ classes since C28k; the sides' slots call their named bodies.
  - **UI_INPUT_HANDLER registrations** are not taken by `registration_readers.py --drop` (its default kinds): the top
    panel's four became EI_REWRITTEN of the window members by hand.
  - **Left**: the windows' main tables (harness id first), the scene renderer 0073D93C and its base 0073D8A0 (26 / 25
    slots, the placeholder slots need their real signatures), CRefObject's 0073BB58 (net_connection.cpp stores it:
    after C21), the map spec chunk readers (C21), the streams (the harness names their Write slots), the MFC library
    kind.
- **Batch 19's families** (2026-10-22): CRefObject, the scene renderer and 42 window tables: 45 rows (retired
  417 -> 462), 4,229 -> 3,393 code cells, about 145 registrations EI_REWRITTEN.
  - **CRefObject** (core_streams.cpp's RefObjectBaseClass, `CRefObject::Vtable()`) in place of 0073BB58: the deleting
    destructor, the empty Serialize, slots 2 / 3 `_purecall`. The net files store it (net_connection.cpp's one line).
  - **The scene renderer** is a real virtual class (model.h): `CVisualizator` declares game.exe's 0073D8A0 (slot 0 and
    4-6, 8-24 pure, 1 the virtual destructor, 2 / 3 / 7 the base's bodies) and `CSceneRenderer` over it overrides every
    slot and adds SetView (25) in place of 0073D93C. The registered constructor and destructor bodies are
    `new (this) CSceneRenderer()` and `this->CSceneRenderer::~CSceneRenderer()`; the slot bodies other code registers
    go through `__fastcall` shims (rule 4). `CTextManager::CreateImpl`'s unread registration is EI_REWRITTEN.
  - **Windows** (ui_window_class.h): `UiWindowClass<Self, Delete>` declares a window's 25 slots with the plain
    window's bodies (CTransformObject's 0-17 with the interface objects' 2 / 11 / 12, 18 Build empty, 19 ShowSurfaces,
    20 DrawBase, 21 UpdateRegionTips, 22 RedrawBase, 23 RedrawTips, 24 Update empty); slot 0 is `Delete`. It does not
    derive from TransformObjectClass: that one's slot 0 names the class's DeletingDestruct, which every instantiation
    then needs. ScrollbarClass adds 24-26, PanelClass casts to a panel whose slot 0 is a base's destructor, and
    ScrollbarInputClass / PanelInputClass give the input sides' handlers. The plain window (`CUiWindow::PlainVtable()` /
    `PlainInputVtable()`, not `Vtable()`: a derived window would inherit it, batch 14's trap) and the base input side
    (`BaseInputVtable()`, ui_input_class.h); the scroll bar, slider, scrolling text, spin, edit field and tooltip; the
    view screen's unit pick; the chat overlay, the briefing's chat window, the item screens' icon window and the
    scrolling RichEdit surface (2 slots); the HUD's top panel, clock, movement panel, unit panel, map, weapon bar,
    quick items, spell and action columns and portraits. The HUD's Rebirth-patched slot bodies (the movement panel's
    setup, the map's and the unit panel's update, the unit panel's draw) are the vanilla bodies, which reproduce
    WideScreen behind its flag as the image runs them (no addon.dll in the image). The `UI_INPUT_HANDLER` macros of
    ui_hud.cpp became EI_REWRITTEN lines.
  - **The harness id**: ui_census.cpp's `WindowKinds` lists each compiled window table with game.exe's address of its
    kind (`ei::OriginalGameData`), as harness_digest.cpp's ModeKinds does for the AI modes; `RealVtable` names a
    compiled window by that address and no longer looks for stand-in copies in game data (the window slot checks'
    stand-ins are gone; slot_recorder.h has no user left). `ReadSurface` takes the compiled surface tables.
    kKnownWindows / WindowReady name screens (containers), none of them compiled yet.
  - **Left**: the containers and screens (00741008 and the 42 / 55-slot tables: they need a ContainerClass over
    UiWindowClass's 25 slots, and kKnownWindows / WindowReady / CSaveLoadWindow / COptionsWindow's kVtableAddress a
    mapping like WindowKinds), the camp's panels (007418E0, 00741504-00741C60, the item info panel 007404A8 /
    007404DC: named in ui_camp_create.cpp, C15's area), the console 00740034, the map spec chunk readers (C21), the
    streams and the MFC library kind (batch 20 took the containers, screens, console, streams and part of the
    library kind).
- **Batch 20's families** (2026-10-22): the containers and screens, the console, the streams and part of the library
  kind: 62 rows (retired 462 -> 524), 3,393 -> 2,154 code cells, game data addresses 994 -> 936.
  - **Containers and screens** (ui_container_class.h, ui_screen_tables.cpp): `ContainerClass<Self, Delete, Screen>`
    over UiWindowClass declares the container's 42 slots (19-24 the container's bodies, 25 / 26 `_purecall`, 27-41
    the interface's screen news with game.exe's shared RET stubs' answers), `ItemScreenClass` adds the item screens'
    42-54 (CItemScreen's defaults) and `ContainerInputClass` forwards the input side to UiContainerInput. `Screen`
    is the class the slots call: the window class, a class derived from it, or a screen kept as a struct around its
    container (CEscMenu, CCampPages, CMpHeroScreen; reached by reinterpret_cast, `ScreenAs`). UiWindowClass's
    window slots use CUiWindow when Delete's class is no window (CMpHeroScreen). 39 tables: the container
    00741008 (its input side 00740FD4, batch 19's, moved here), and each screen's main and input tables (battle, main menu, briefing, view, island map,
    titles, help, objectives, Esc menu, save / load, options, message box, difficulty, master request box,
    connection screen, plain item screen, camp, exchange, hero screen). Accessors are `X::Vtable()` /
    `InputVtable()`, the bases' `CUiContainer::ContainerVtable()` and `CItemScreen::ItemScreenVtable()` (batch 14's
    trap). The island map's slot 40 has its own CGlobalMap::SaveState (it was registered as the briefing's), and
    the objectives window's slot 26 is CObjectivesWindow::RestoreAlphaRef.
  - **The harness id** (tests/verify/harness_windows.cpp, from ui_census.cpp): `RealWindowVtable` maps each compiled
    window table to game.exe's address of its kind. kKnownWindows became `KnownWindows()` of game.exe's main tables
    (`ei::OriginalGameData`); WindowReady, DescribeTopWindow, waitwindow (a hex table is game.exe's address; the top
    window's table goes through RealWindowVtable, then OriginalAddress), the camp's put-on and checkplayers compare
    game.exe's addresses in both runs. CSaveLoadWindow / COptionsWindow lost kVtableAddress.
  - **The console** (console.cpp): ConsoleStreambufClass in place of 00740034 (1 / 7 the console's sync and
    overflow, 8 EOF, the rest old iostream's streambuf defaults). Only ConsoleStreambuf changed (no ConsoleWindow
    edits). The image's harness reads each line through `console::g_consoleLineObserver` (set under
    `ei::Standalone()`); game.exe's run still takes game.exe's sync slot.
  - **The streams** (core_streams.h; real classes since C28e): CStream (0073BDB0, 1-6 pure), CMemoryStream (0073BD90
    with its tail 0073BD98; 0073C960 is the original's second copy of its table, stored by 0066C4F0) and FileStream
    (net_link.h, 0073C8D8). Locals are plain, members of hand-built owners use placement new and the destructor;
    ChunkArchive and DeltaField have real constructors and destructors over them.
  - **The counted root** (core_refcount.h): CRefCounted / CRefObject have C++ constructors (the count left to the
    creator) and destructors and the game heap's `operator new` / `delete`; CItemLink (item.h) and CUnitShape
    (ai_unit_layer.h) derive from them as real classes. The root's and the counted value's compiled tables
    (RootVtable, CRefObject::Vtable, PlainRootVtable) stay until the network (C28k) and the other derived families
    are real classes.
    The save trace reads the image's memory stream writes through `g_memoryStreamWriteObserver` (called by slot 2
    with its return address); game.exe's run still takes the Write slots.
  - **The library kind**: MFC's CStringArray, CWordArray, CDWordArray, CByteArray, CUIntArray (mfc_coll.cpp's
    ArrayTable) and CMapPtrToPtr (mfc_handle_map.cpp), slot 0 returning our CRuntimeClass record; the simple
    exceptions (CResourceException, CUserException, CMemoryException, CNotSupportedException) and
    CArchiveException (mfc_exception.cpp); the old iostream's streambuf, ios and ostream-for-ios tables
    (crt_iostream.cpp; ostream's virtual base table is data and stays); std::exception (crt_except.cpp). CPtrArray,
    CPtrList and ostream_withassign are retired with no class of ours (file `-`): only game.exe's constructors and
    destructors store them, and no code of ours calls those.
  - **Left**: the camp's panels (ui_camp_create.cpp, C15's area), the map spec chunk readers (C21), stlp_string's
    out_of_range tables 0073B994 / 0073BE8C (batch 21), the CFile family (CFile 00745DF4, CFileException 00745E4C, CStdioFile 007464F0, CMemFile 00746650,
    CFileFind 00745EE8), the GDI and DC classes (mfc_gdi: CDC and its three subclasses, CTempDC, CPen, CRgn,
    CTempGdiObject; CGdiObject 007463D4 and CBrush 007463C4 are stored by game code too), CHandleMap 0074685C (its
    deleting destructor needs the handle map's destructor body), the thread, window, menu and application classes
    and the module / thread state records (mfc_thread, mfc_window, mfc_state), CCmdUI / CTestCmdUI,
    CCommandLineInfo, CRecentFileList, 00746E60 and type_info 007472B0 (the RTTI type descriptors point at it).
- **Batch 21's families** (2026-10-22): the rest of the MFC library kind and stlp's out_of_range: 36 rows (retired
  524 -> 560), 2,154 -> 1,696 code cells, game data addresses 936 -> 918 (no library vtable is named in src/ any more).
  - **Unported slots**: a slot of a compiled table whose game.exe body is not ported (the image never reaches it) is
    `ei::UnportedSlot(<game.exe's address>)` (compiled_vtable.h): reached anyway, it reports "no code at <address>"
    through `ei::ReportNoCode` (now declared in ei/hook.h), as the old table's slot did. config/standalone_unreachable.csv
    lists those addresses (`address` lines: standalone_gaps.py counts them as code literals) with the reason; the
    retired tables' `vtable` lines are gone (a retired table's slots are no data reference, so the line covered nothing).
    Port only what the image reaches (or what a ported body calls: CDC::Detach's slot 5); the rest stays unported.
  - **mfc_gdi**: GdiObjectTable (CGdiObject, CBrush, CPen, CTempGdiObject: 0 the class's record, 1 the deleting
    destructor, CTempGdiObject's back to its fixed allocator) and DCTable (CDC, CTempDC: 29 slots; 3-6, 10, 12, 26, the
    destructor, Detach ported, the rest unported). Since C28n real classes over CObject (mfc_gdi.h; CTempGdiObject and
    CTempDC in mfc_gdi.cpp), the interface's pens, brushes and fonts plain locals or `new`. CClientDC,
    CWindowDC, CPaintDC and CRgn are retired with no class of ours.
  - **CHandleMap** (mfc_handle_map.cpp): HandleMapTable (one slot) over the ported destructor, DeleteTemp and
    CMapPtrToPtr::GetNextAssoc (the image's maps are never destroyed; DeleteTemp deletes each temporary object
    through its slot 1, the CTempDC / CTempGdiObject destructors above).
  - **Windows and threads**: mfc_window.h's CmdTargetTable declares CCmdTarget's 20 slots (3-19 unported; 4 and 10
    the class's own); WndTable (CWnd, CTempWnd: 46 slots, only the destructors ported), MenuTable (CMenu,
    CTempMenu: the destructors unported, they need the menu handle map) and WinThreadTable (31 slots: the destructor
    and Delete ported; WinThread::Delete now deletes through DeletingDestructor). Retired with no class of ours:
    CWinApp, CCommandLineInfo, CRecentFileList, CCmdUI, CTestCmdUI, _AFX_WIN_STATE and the six state records
    (_AFX_CTL3D_STATE / _THREAD, CThreadData, _AFX_THREAD_STATE, AFX_MODULE_STATE, AFX_MODULE_THREAD_STATE,
    _AFX_BASE_MODULE_STATE).
  - **The CFile family** (mfc_file.cpp): FileTable (CFile; CStdioFile's StdioFileTable over it with 16 = its
    LockRange body and 21-23 its own), FileFindTable and mfc_exception.cpp's FileExceptionTable; every slot was
    registered already. CMemFile retired with no class of ours. Overloaded slots (CFileFind's CTime / FILETIME
    getters, CStdioFile's two ReadStrings) get distinct names: MSVC groups a name's overloads in the table.
  - **stlp's out_of_range** (stlp_string.cpp): since C25c real classes (NamedException : std::exception, LogicError,
    OutOfRange) the compiler throws; game.exe's ThrowInfo 0074C640 and its records are dead data.
  - **Left**: the camp's panels (C15), the map spec chunk readers (C21), the game's CFont table 0073BFB4 (string_map,
    the interface and the console name it: C21's area), 00746E60 (one slot, a false row?) and type_info 007472B0.
- **Batch 22's families** (2026-10-22): the bases' own tables, the zone records, the item panels, CFont and the map
  objects: 44 rows (retired 560 -> 604), 1,696 -> 974 code cells, game data addresses 918 -> 874.
  - **The bases' own tables** (engine_object_class.h, gone in C28p: CEngineObject, CTransformObject and CObject3D
    are real classes whose destructors the compiler builds): EngineObjectBaseClass (CEngineObject's 0073B8E0), `ManagerClass`
    over EngineObjectClass (CBaseManager's slots 2 / 3 / 9 / 10 and 13-24) with BaseManagerClass (0073B870) and
    resource_manager.cpp's ResourceManagerClass (00743338), and TransformBaseClass (CTransformObject's 0073BA38). The
    constants became `X::BaseVtable()`; figure_mesh_life.cpp's FigureBaseClass (0073C224, CFigureMesh's slots 18 / 19
    over TransformObjectClass; gone in C28h with ConstructBase / DestructBase) and spell_lifetime.cpp's SpellEffectClass (0074399C, a one-slot deleter plus an empty
    Serialize; the harness's IsSpellEffect compares the image's table with `CSpellEffect::Vtable()`). CTransformObject
    gained `BaseVtable()`, which hides CEngineObject's in its derived classes: every unqualified `BaseVtable()` under it
    was checked to be its own class's (CEffect, CFigureMesh). named_data's check found ui_window.cpp's
    CUiWindow::DestroyBase storing 0073BA38 by address.
  - **Zone records** (map_spec.h): 0073C5C4-0073C7B8 and the quick item effect's twin 0073FA58 (ObjectSpec's). The unit
    record's chunk readers (map_spec_unit.cpp) for 0073C710 / 18 / 20. world_data.h's kUnitSpecVtable /
    kQuickEffectSpecVtable went. Since C28i the records are real classes on Chunk (Spec, ObjectSpec and EffectSpec with
    the records on them, their own operator new on the game's heap; the twin is a plain ObjectSpec) and the chunk
    readers TargetChunk's; map_spec_class.h's tags went.
  - **Item panels** (ui_item_panel_tables.cpp): `ItemPanelClass` over PanelClass (slot 25 the item under a point,
    CUiWindow::NoItemAt by default) and `ItemPanelInputClass` (2 CUiItemPanel::OnLeftDown) for the blank panel, the
    description panel, the statistics column, the equipment panel (with its tail 007418E8), the quick strip, the trade
    panel and grid, the spell and item makers and the repair grid: 21 rows. Accessors are `X::Vtable()` /
    `InputVtable()`, `CUiItemPanel::BlankVtable()` and `CConstructorPage::ItemMakerVtable()` (CSpellMakerPage derives
    from it). harness_windows.cpp maps the ten main tables. The item strips' tables (00741A1C, 0074197C, 00741BF4 and
    their input sides) stay: CUiItemStrip and NewStrip are C21's.
  - **CFont** (mfc_gdi.cpp's FontTable over GdiObjectTable with kClassCFont): the debug panel, the interface and the
    console store `CFont::Vtable()`.
  - **Map objects** (map_object_tables.cpp, gone in C28p): `Object3DClass` over TransformObjectClass (CObject3D's 27 slots) and
    MapObjectBaseClass over it (CMapObject's 61; gone in C28h: CMapObject's own table), each slot the class's C++
    virtual called qualified. CObject3D's row is
    five slots (find_vtables split it at 0073DDE8): its slots 5-26 moved from gen_image_cells.py's CODE_REVIEWED to
    NOT_CODE, with a `data` line in config/standalone_unreachable.csv (nothing stores the table).
  - **Left**: the item strips (C21), 00746E60 (a false row: the "ime" string 00704BE6 hands lstrcmpiA; retiring it
    would zero the text, so it needs find_vtables to drop the row), type_info 007472B0 (named_data refuses it: the
    exe's RTTI descriptors point at it), and the one-slot .data rows 0073B984, 0073D6B4, 0074E824, 00753B04, 00758904,
    00758B0C, 00758B14 (not looked at).
- **Batch 23's families** (2026-10-22): the item strips, CVisibilityFog, stlp's length_error and type_info, and six
  false rows dropped: every row of config/vtables.csv is retired now (613 rows; retired 604 -> 613, 974 -> 832 code
  cells, game data addresses 874 -> 867).
  - **Item strips** (ui_item_panel_tables.cpp): `ItemStripClass` over ItemPanelClass<.., CUiItemStrip> (24 Update,
    26 RebuildList) and `ItemStripInputClass` (2 / 3 / 4 the strip's buttons and drag, 12 the wheel) for the plain
    strip 00741A1C / 007419E8 (slot 26 is game.exe's _purecall 006E73EB: `ei::UnportedSlot`), the forwarding strip
    0074197C / 00741948 (0 ForwardingStripDelete, 20 Draw, 25 ItemAt, input 2 OnLButtonDownForward) and the figure
    strips 00741BF4 / 00741C60 (20 DrawFigures, 25 FigureItemAt, 26 CountFigures, input 2 OnFigureClick). Accessors
    `CUiItemStrip::PlainVtable()` / `ForwardingVtable()` / `FigureVtable()` and their `...InputVtable()`;
    harness_windows.cpp maps the three window tables.
  - **CVisibilityFog** (effects.h, 0073D6B4; a real class since C28f): 2 / 3 the cells cleared, 6 UpdateCells, 11 / 12.
  - **Retired with no class of ours**: 0073B984 is stlp's length_error (stored only by game.exe's string growth
    0040C2A0, 0040C6E0, 00421800, 004269A0, 0042B700; our ports never throw it). The ~140 functions that name the
    address compare a hash table's prime pointer with the end of __stl_prime_list (0073B914 + 28 dwords), which is
    the same address: no read of the table. type_info 007472B0 (game.exe's CRT): the 74 RTTI type descriptors' vtable
    field points at it; nothing in the image calls a descriptor's virtual (the frame handlers compare descriptors by
    address and name; none is destroyed), so those pointer cells may point at the
    zero table.
  - **False rows** (find_vtables.py now skips a candidate whose slot 0 is an EH funclet, functions.csv kind `eh`, or
    config/dead_code.csv's "not a function start"): 00746E60 (the text "ime" 00704BE6 hands lstrcmpiA; now a
    NOT_CODE cell of gen_image_cells.py, the MFC message map group split around it) and the EH unwind entries
    0074E824, 00753B04, 00758904, 00758B0C, 00758B14 (now eh-unwind cells, left). Only those rows were taken out of
    config/vtables.csv: a full rerun of find_vtables.py also rewrites installed_by lists from today's functions.csv
    and finds five new one-slot rows (0074676C, 0074718C, 007474B8, 00747E2C, 00747E30: text or data, unchecked) and
    a second slot for 00746768; review those before regenerating the csv.
- **Tails in config/vtables.csv**: find_vtables.py splits a table at every address the code holds as an operand, so
  some rows are the tail of the row before them: split off at a devirtualised slot call (`call [0073BD98]` is
  CMemoryStream's slot 2, 0073BD90's 8 slots listed as 2 + 6) or at a byte match inside an instruction. A tail is never
  stored; retiring it zeroes live slots (the first try of batch 2 retired 0073BD98: every run crashed calling a null
  slot at startup). gen_image_cells.py's check refuses a retired row that no code of game.exe stores
  (`stored_vtable`: `mov r/m32, imm32` or `mov r32, imm32`) or that shares a slot with a row still in use (rows that overlap
  where find_vtables.py could not tell a table's end). A family's retirement takes its tails with it (0073E954's 0073E98C).
- **Retired with no class of ours** (batch 2; file `-` in the csv, the note names the storing functions): 0073B98C
  (an exception class, 0040C440 / 0040C500), 0073BFAC (CDebugPanel's base), 0073D860 / 0073D888 (the render helper's
  base and local), 0074019C (CUiMesh::SetVertexCount's local), 00740244 (the battle camera's input side, before
  InputSideDestroy's 007401D0) and the dead code's 0073C474, 0073CA00 / 08 / 0C / 10, 0073F7C4 / 0073F7F8: 135 code
  cells. Not retired from the "no store in src/" row: the tails 0073BD98 (CMemoryStream), 0073E98C (0073E954),
  0073EAA0, 007435B4, 00743618, 00743678, 007436D8, 00743738, 00743AA8, 00743ADC (devirtualised slot calls of the
  tables before them), 0073BF08, 0073E6E8, 0073E850, 0073F4E8, 00740FE8, 007418E8, 0074247C, 007435D8 (byte matches:
  inside the table before them), 0073D764 (shared slots with 0073D760 / 0073D768 until batch 12
  gave each one slot: a candidate now); 0073B984 and the EH unwind entries went in batch 23. The library kind's unstored MFC
  vtables (CClientDC, CWinApp, ...; config/standalone_unreachable.csv) go with the library batch.
- **Ids for later families**: the harness names objects by game.exe's vtable (window labels in goldens: the census's
  `W00740688#3`, `waitwindow`, the digest's AI modes). A converted class needs an id that both game.exe runs (golden
  recording) and the image give the same: e.g. the harness maps a vtable to a name (`kKnownWindows`' names) through
  game.exe's address in game.exe and through the class's compiler vtable (read from a constructed object, or
  `typeid` once no game.exe table is left in that hierarchy) in the image; when the label text changes, re-record with
  `--no-hooks --golden --fresh`. Slots the harness patches (`PatchSlot`, harness_savetrace) need a hook in our code
  instead.

### Real classes (C28)

C19 left most families on compiled tag classes: the class keeps game.exe's layout and its constructor body stores a
tag's compiled table (`ei::SetVtable(this, X::Vtable())`, engine_object_class.h, compiled_vtable.h, mfc_slots.h),
whose slots call `...Impl` bodies or `__fastcall ...Hook` shims. C28 makes each family ordinary C++ (child tickets
C28a-p, one family per commit; the order is in C28's ticket note). How a family converts (C28a, the items):
- The constructor body becomes the C++ constructor (no SetVtable), Destruct / DestructBody the virtual destructor,
  DeletingDestruct the compiler's scalar deleting destructor. The destructor calls its own Destroy qualified
  (`CItem::Destroy()`), as the original's does.
- The slot bodies are the virtual overrides (the `Impl` split folded into the virtual of the same name, base calls
  qualified); class names and ids are `GetClassName` / `GetClassId` overrides (`#undef GetClassName` where windows.h
  is in). The helper's `Vtable()`, `Construct` / `DestructBody` / `DeletingDestruct` and the layout asserts go.
- Allocation: `new T()`. CEngineObject has the game heap's `operator new` (noexcept: null without memory, then no
  constructor runs, which the original's callers test) and `operator delete`, plus the placement pair; a derived
  class declares no `operator delete` of its own (it would hide the placement delete: warning C4291). An object built
  in storage someone else allocates is `new (place) X()` and `place->~X()`; one embedded in an owner whose own
  Construct / Destruct bodies build it is a `lib::InPlace<X>` member (PartyStorage, party_storage.h); a local is a
  plain `X x;`.
- A constructor or destructor whose address is still called (an EI_ORIGINAL forwarder, C29) keeps a registered
  `__fastcall` shim that calls it (party_lifetime.cpp's DestroyPartyInPlace at 006611D0).
- The function-to-original map: virtuals are `EI_REWRITTEN(addr, X::Virtual)`; constructors and destructors, which
  have no address to take, are `EI_REWRITTEN_SPECIAL(addr, X::X)` / `(addr, X::~X)` (include/ei/hook.h). The
  original's scalar deleting destructor is tagged with the destructor's name too: check_signatures accepts a
  destructor tag whose original pops 4 more (the flags argument).
- game.exe's tables stay in config/retired_vtables.csv; their rows name the real class and its header.

The window base (C28l, ui.h): `UiInputSide` (007401D0: the input slots 1-12 as virtuals, `handled_`) and
`CUiWindow : CUiWindowBase, UiInputSide` (CUiWindowBase is CTransformObject plus the 0x40 bytes before +0x94, so
the input side lands at +0x94 and the compiler's adjustor thunks are game.exe's SUB ECX,0x94; JMP) with slots 18-24
as virtuals; `CUiContainer` adds 25-41 (25 / 26 `_purecall`) and its input dispatch as the input-side overrides
(screens call the defaults qualified, `CUiContainer::LeftDown(...)`). Real classes too: the inert extra side
(`UiInertInput`, 00741154), the battle camera's (`CCameraInput`), the view screen's unit pick (`UnitPickInput`) and
the item screens' icon window (`CItemIconWindow`). Since C28m the screens (CBattleScreen ... CMpHeroScreen,
the item screens over `CItemScreen`'s slots 42-54) and the item panels (`CUiItemPanel`'s slot 25 `ItemAt`; the
strips `CUiItemStrip` (26 `RebuildList`, pure), `CForwardingStrip` and `CFigureStrip`) are ordinary classes built with
`new X(...)`, their slots the overrides (an input slot that runs a named handler is a one-line override,
`int LeftDown(...) override { return OnLButtonDown(...), 0; }`). Member pointers of
the windows are two words: `EI_REWRITTEN`'s record points at a constant holding `&fn`, and `ei::MemberAddress`
takes the code word (adjustment 0). The harness names a window by its class's
`typeid` (harness_windows.cpp's `WindowKind`).

### What the image needs in place of game.exe's module (S14, S16, S19)

- **Resources** (S18j, C23): none of game.exe's ("How it runs"); the game's module is the host's. MFC's module state has
  null instance and resource handles in the original too (the game never runs AfxWinInit); `AfxLoadString` answers from
  a table in code. Checked by `render_cursor_present`'s `windowtitle` (the window's title "Evil Islands" and class
  "EIGAME"; its `cursorcheck` frames show the game's cursor, which is game data, not a resource).
- **game.exe at run time**: none since S18k ("What comes from game.exe at build time", above). The harness's run_all
  hands ei_launch none (S18k1).
- **Command line**: `ei_standalone ... -- <game args>` is WinMain's line and GetCommandLineA's after `"<game.exe>"`.
- **Code bytes read as data**: none left (R9b3). app_frame's cursor lock (004580D7) calls our rewrite directly; the
  harness reaches it and WinMain's camera (00457595), server update (00457314) and client segment (00457368) calls
  through hook points (`ei::mainloop::g_harnessHooks`, ei/main_loop.h; tests/verify/harness_hooks.h). ClassifyToken's
  keyword table (the immediate at 004CF69B, which Rebirth rewrote) is `KeywordTable()`: kScriptKeywords (0078BCA0's
  bytes, C18q) or the add-on's, which script_addon.cpp builds from it under the flag.
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

### Settings file (ST1, 2026-10-22)

The image keeps the settings game.exe keeps under HKEY_CURRENT_USER in **ei_settings.ini next to
ei_standalone.exe** and never writes the registry (src/game/settings_file.{h,cpp}). game.exe (the original game,
the `--no-hooks` suite) keeps the registry.
- **Format**: a section per key, its path below HKEY_CURRENT_USER (`[Software\Nival Interactive\EvilIslands\General
  Settings]`), a line per value in reg export's syntax: `"FullScreen"=dword:00000001`, `"VideoResolution"="1920x1080"`
  (`\\` and `\"` escaped), `"Brightness"=float:0.5` (a 4-byte REG_BINARY float, written only when the text reads back
  to the same bits), `hex:..` (REG_BINARY), `hex(<type>):..` (anything else, or a string the quoted form cannot
  hold); `@` is a key's default value, `;` starts a comment. Every value round-trips bit for bit: the bytes are what
  the game's ANSI registry call gives (UTF-8 with EI_UTF8). Names and paths compare without case.
- **Import**: when the file is missing, the first use copies `Software\Nival Interactive\EvilIslands` and Rebirth's
  `Software\Gipat.Ru\EI_Starter` (every subkey; read only, through text::sys as the game's reads) and writes the
  file. Nothing is merged later: the file is the settings from then on.
- **Who uses it**: CWinRegistry (win_registry.cpp: the options loader and writer; `Open` makes the file's key,
  `Close` writes the file when a value changed, atomically through `ei_settings.ini.tmp`) and the direct readers
  through `settings_file::ReadValue` (console_addon's AutoExec lines, mod_folders, script_addon_commands' AddonPath,
  ui_widescreen, world_client_draw's draw radii, directdraw_holder's `Standalone` "PerfOverlay": the performance
  overlay, renderer.md section 15, not read in the harness build).
- **Read filters** change a value as it is read, as the registry hooks did on game.exe's import slot: EI_GAME_ROOT's
  Work path / CD-ROM Path (hook/paths.cpp) and the harness's `discprompt` (harness_startup.cpp).
- **Harness runs** (tests/verify/harness_settings.h): the harness writes `<run folder>\ei_settings.ini` at start
  from config/harness/registry.reg (its HKEY_CURRENT_USER keys) with the scenario's starter lines (resolution,
  widescreen, fullscreen, fpscursor, music, addonpath, CtfMon) in place, and pins it: read only (the game's writes
  are dropped, as the registry's were), no import, a read of a value the file lacks fails the run unless the
  snapshot marks it absent (`"name"=-`). So a standalone run never depends on the machine's registry, and its
  folder shows the settings it played with. The advapi32 hooks stay for anything else that reads the registry.
- **Playing while the suites run (ST1b)**: WinMain's one-instance check (FindWindowA on the class atom and the title,
  app_main.cpp RegisterWindowClass) used to find a hidden harness game's window and end a play launch silently
  (exit -1). Harness runs answer the check themselves (harness.cpp NoOtherGameWindow: no other window, so two harness
  runs never meet, and a harness run never meets the user's), and now give the game window the title "<title>
  (harness)" (CreateWithoutActivating; nothing reads the title, goldens do not record it), so the play launch's real
  check matches nothing. This covers game.exe's harness runs too (the same hook). When the check does end a game it
  logs `app: another game window exists (window .. of process ..)` in ei_hook.log, and ei_launch (no harness) says
  so on stderr for exit code -1.
- **Checks (ST1a)**: the product's two file paths, which a read-only pinned run does not reach, are checked without
  the user's registry.
  - *Format and import, no game*: src/game/settings_document.cpp holds `Document` (Text / Parse), `JoinPath` and the
    import's walk (`ImportTrees` over a `RegistrySource`: the registry's in settings_file.cpp, `DocumentSource` over a
    Document for tests). tests/vectors/replay_settings.cpp runs after the vector replays in `ei_vectors.exe`
    (build.cmd fails when a check does): dwords (and a REG_DWORD that is not four bytes), escaped strings and the ones
    that go to `hex(1)`, floats (+-0, denormals, NaN, infinities, a sweep of 20,000 random bit patterns plus every
    exponent: the bits always read back, numbers as `float:`, NaN / infinity as `hex:`), binaries, `hex(7)` and the
    other types, `@` against a value named "@", names without case, keys that merge without case, comments / BOM / line
    breaks / continuation lines, bad lines (reported and skipped), and the import against a made-up tree and against
    config/harness/registry.reg itself (its HKEY_CURRENT_USER keys parsed as the stand-in registry: the two trees only,
    no parents, no sibling keys, every value bit for bit).
  - *The game's save and the import in a run*: `settingsfile writable` (settings_save.txt) leaves the run's
    ei_settings.ini writable with a probe key of odd values; the game's exit save (WinMain's SaveToRegistry, `Flush` at
    `CWinRegistry::Close`, `.tmp` then moved) rewrites it with water waves switched on in memory. `settingsfile import`
    (settings_import.txt) starts with no file and imports the snapshot through `settings_file::SetImportSource` (a
    `DocumentSource`, never HKCU), so the first-use import, `Write` and the game reading the result run for real.
    `ei_launch` compares the file after the game ended with the scenario's `# settings-file: settings/<name>.ini`
    (tests/scenarios/settings/; line breaks apart), fails the run on a difference or a left `.tmp`, and skips the check
    on the Rebirth install unless `<name>.rebirth.ini` exists. To change an expected file: read the difference in the
    failure lines, then copy the run's ei_settings.ini over it. Both scenarios are `# no original:` (game.exe uses the
    registry).
  - Known limits: a value name with a line break or other control character cannot be written (the registry allows
    it; the game has none); a line the parser does not understand is dropped on the next save (logged as
    "settings: ... not understood"); the real registry source (`Registry` in settings_file.cpp) is the one part of the
    import no test runs, as it reads HKCU.

### Generated code

- **Short functions** (S2): the 200 game functions under five bytes are in src/game/short_functions.cpp, generated by
  `tools/gen_short_functions.py` from their bytes (12 kinds: `Ret`, `Ret4/8/12`, `ReturnZero`, `ReturnMinusOne`,
  `ReturnTrue`, `ReturnFalse`, `ReturnThis`, `LoadDword<n>`, `MemberAddress<n>`, `LoadFloat<n>`), each compiling to
  the original's bytes, registered with `EI_REPLACE_SHORT`. check_short_functions (removed in C25d) checked each
  kind against game.exe and our code. A few were added by hand (particle element callbacks 00485530,
  00492FB0-004965D0: `EI_REPLACE_SHORT(Ret4)`).
- **Static initialisers** (S3): src/standalone/static_init.cpp by `tools/gen_static_init.py`. A small symbolic
  interpreter decodes each initialiser into stores, copies, fills, float folds (`Log` for FLDLN2 / FYL2X; .rdata
  operands as tagged literals, S18e1), calls (cdecl, stdcall, thiscall, through an import slot), `atexit` and the
  once-only guard bit; a single-use decodable
  callee is inlined, everything else is called at its address. One template, `StaticInitialisers(Do&)`
  (static_init.h), with visitors `Run` (the image; calls go to `ei::Code` of the address) and `Targets` (logs call
  targets without code).
  When a helper gets registered later the output changes: rerun the generator (check_static_init, removed in
  C25d, re-decoded game.exe and compared).
- **Exit functions** (S3a, S16a): src/standalone/static_exit.cpp, same tool: 130 whole-function patterns
  (`EXIT_PATTERNS`: `mov ecx, obj; jmp dtor`, flag-guarded, operator delete of a counted buffer, a counted
  `Release`, DeleteCriticalSection, the eh vector destructor iterator, the WSACleanup wait, `ret`) plus 60 run-time
  atexit registrations of function-local statics (PUSH fn, CALL 006E727B within 14 instructions). Their sizes go
  into config/extra_function_sizes.csv and function_sizes.inc (the generator writes them, the check compares).
- **Compiler jump thunks** (functions.csv kind "thunk") are registered with their target's replacement.
- **Dispatch-only functions** (`MOV EAX,[ECX]; JMP [EAX+n]`: 00675D40, 00469E00, 00469DE0, 00552F30 ported) are
  `EI_REWRITTEN`: the callers call the virtual. A registration of one would be a virtual-call thunk, which
  InstallHooks refuses (C25 dropped `EI_REPLACE_DISPATCH`, whose check compared game.exe's bytes).

### Registration rules

- `EI_REPLACE` / `_METHOD` / `_SHORT` / `_STANDALONE` all go in (the last name is historical: it was
  "image only" while game.exe ran our code). `tools/check_registrations.py` (build.cmd) fails on two registrations
  of one address; it reads the built DLL's `.eireg` section (every macro leaves a `RegRecord`).
- A registration stays only while something reaches its address (C20c): `tools/registration_readers.py` prints the
  split by reader (code cells by kind, code literals, static init, named data, harness, vectors, scenarios / goldens,
  the short-function and exit-function generators; not an EI_ORIGINAL forwarder: those are tests/' only since C29 and
  run only in game.exe, on the original's code, C29a), `--unread` / `--files` what nothing reads,
  `--drop` writes `EI_REWRITTEN` in their place (with `--exclude` / `--exclude-name` for files another worker holds,
  `--only FILE` for a list `--list` printed). Code literals include static_init.cpp's six-digit ones (0x5C4210). A
  dropped function is called by name; one whose registration was the only instantiation of a template needs an
  explicit instantiation (AckTable<ExtraAcks>::RecordAt). Counts (progress.py's "EI_REPLACE registrations"): 9,347
  before C20c, 5,262 after it, 3,527 after C20d (1,735 that only check_fp_fold read became EI_REWRITTEN; 20 in
  CSkyObject / CUnit left for C19 batch 5). C20c deleted its ~4,000 lines outright, so those functions are missing
  from the map (C20d1).
- **The function-to-original map** (C20d): the registrations plus `EI_REWRITTEN(0x..., Fn);` lines (include/ei/hook.h).
  EI_REWRITTEN registers nothing: it leaves `{address, &Fn, file, line}` in the DLL's `.eiorg` section, which nothing
  reads at run time, so the compiler checks the name and the function's own copy stays in the image as with a
  registration. check_registrations reads `.eiorg` (one EI_REWRITTEN per address, none also registered). The tools
  read the map from the sources: check_signatures.registrations() (kind 'replace'; check_signatures), coverage.py, direct_calls.py, save_trace.py, ui_census_report.py, gen_originals.py,
  progress.py's / dashboard_data.py's rewritten counts, check_cleanup (its address is no raw literal), and
  gen_short_functions.ported_elsewhere (a rewrite: no generated short function, and gen_static_init neither inlines
  nor decodes it). standalone_gaps and registration_readers skip the lines (no reader, no reached literal).
  Switching kept every map the same (C20d's comparison: same entries in all of them; check_fp_fold on main's DLL with
  the new sources: the same counts). Converting the 1,735 changed some inlining (removing the registrars' static
  constructors let MSVC inline a few converted functions into their callers): release check_fp_fold went from 1,260
  functions with constants / 4,752 matched / 196 helpers / 40 unattached / 417 uncalled to 1,276 / 4,752 / 200 / 44 /
  418, 0 problems in both.
- standalone_gaps reads .rsrc too: dialog text can spell a function start (007D4030, " L" = 004C0020), listed in
  config/standalone_unreachable.csv when the function's registration goes.
- Library functions need a row in config/extra_function_sizes.csv even when Ghidra has a function (the installer
  skips an unknown start); some Ghidra sizes run past the RET (0070CAD2, 006E6B7D: decoded sizes).
- config/library.csv names are often wrong for STLport / MFC rows (destructors named clear, AddTail named a
  constructor, 006EC8A8 `_mbsupr` lowers, 005E0550 is CTime::operator-). Read the code; the csv is add-only. Unquoted
  commas in names broke rows once (0055C730, 00561B00: now quoted).
- Library bodies live in src/lib; game-element instantiations in src/game (game_arrays, game_lists, game_slots,
  game_hashes, record_*, library_*). Callers call them by name (libcrt::Fopen ...; no forwarders since C29), and
  C14a-d replace the containers with standard ones.

### The CRT (src/lib/crt*.cpp, namespace `libcrt`; S4, S4a-S4d, S16, S16a, SV7c2)

- **Locale** (C26): the host CRT's. The text manager's Create 004DED20 calls the host's setlocale(LC_CTYPE, the
  "Font CharSet" option, "english" by default -> "English_United States.1252") and `_setmbcp(_MB_CP_LOCALE)`; the
  host's start-up did __initmbctable's `_setmbcp(_MB_CP_ANSI)` (with EI_UTF8 the standalone's start-up sets
  `_MB_CP_SBCS`: no lead bytes). What still reads the state directly (crt_format.cpp's _output, CaseFoldTable,
  mfc_resource.cpp) goes through crt_locale_data.h (`_pctype`, the LC_CTYPE locale name as an LCID, `__lc_codepage`,
  `_mbctype`, `_mbcasemap`). Both suites kept every golden the same with the switch (VC6 and the host build
  LC_CTYPE's tables from the same GetStringTypeW / LCMapStringW calls; the C tables match for 0..255).
- **Strings and characters** (crt.cpp, C26): the host CRT's: strncmp, strncpy, _stricmp, _strnicmp, _memicmp,
  _mbscmp, _mbsicmp, _mbsinc, _ismbblead, isleadbyte, tolower, toupper, isalpha / isdigit / isspace / isprint,
  _mbsncmp, _mbclen and _mbslwr behind their libcrt names (the EI_UTF8 branches and ei_vectors' recorded answers need
  the names); memmove, memcpy, memset, strstr, wcslen, _mbschr, _mbsrchr, _mbsstr, _mbspbrk, atoi, _itoa and
  _splitpath called by their own names. Both suites goldens the same, though the host's compares return a difference
  rather than VC6's -1 / 1, atoi saturates where VC6's wraps, and a negative char's class is 0 rather than the words
  before `_pctype`.
- **Heap** (crt.cpp): malloc 006EABD1 and free 006EA8AD are the host CRT's (C26: both suites goldens the same, the
  image suite twice; until then a VC6-style Win32 heap of their own, `_crtheap` 007CAF08, sizes rounded up to 16,
  through the image's import slots, SV7c2). realloc, calloc, _msize and _strdup had no callers. The harness's
  repeatable heap, jitter, fill and trace (tests/verify/harness_heap.h) act on game.exe only now: the image's blocks
  are the host heap's (its low-fragmentation front end), so a golden of the image already holds under other
  addresses in every run. operator new runs MFC's handler loop
  (`_afxNewHandler` 00796E14, initial value `AfxNewHandler` 0070058F: throws the static CMemoryException); the
  STLport node allocator 00405950 / 00404D70 with refill 004060E0 and chunk_alloc 00406B70 is ported exactly (the
  pool is the exe's data). Nothing frees across heaps: our code takes game memory only through libcrt; the host CRT
  keeps its own (std containers, FILE buffers).
- **atexit / exit**: the host CRT's (C26: image goldens the same): atexit 006E727B / _onexit 006E71FD register with
  `_crt_atexit` (the function converted to its code when registered), exit 006E8DA7 is std::exit, which runs that
  table before the host's terminators. rand / srand: the seed is a thread_local in the image
  (`libcrt::RandState()`, which the harness's digest and randtrace read; the image has no _getptd 006F00D1).
- **Formatting** (crt_format.cpp): sprintf 006E8715, vsprintf 006ECC47, fprintf 006EAEB0, printf 006E955F are VC6's
  `_output` (the `__lookuptable` state machine copied from 007476D0, `%I64`, `%Z`, "(null)"). Floats are not "17
  correct digits": `$I10_OUTPUT` builds them from a 12-byte long double (96-bit `__ld12mul` with truncated partial
  products, the exe's power-of-ten tables 0079AE60 / 0079AFC0), then `_fptostr` rounds half up; three exponent digits;
  "1#INF" / "1#IND" / "1#QNAN" / "1#SNAN" ("%.2f" of infinity is "1.#J"); -0 prints as 0. The decimal point is the
  host's LC_NUMERIC one ("."). C26 tried the host's printf: the crt vectors differ (3 of 258 records: "%g" / "%.6g"
  of FLT_MAX "3.40282e+38" for VC6's "3.40282e+038", infinity "inf" for "1.#INF") and so do six image goldens
  (combat, combat_more, sound, unit_ai_cast, unit_ai_corpse, unit_ai_heal: the console's "skill=3.40282e+038"
  attack line), so _output stays. atof and sscanf are the host CRT's (C26: goldens and the atof vectors the
  same); callers use std::atof / sscanf_s.
- **Files** (crt_file.cpp): _open is VC6's _sopen through the game's import slots (so the harness's save / install
  redirection applies), the handle wrapped as a host descriptor; fopen parses the mode as `_openfile` and wraps with
  `_fdopen`, so the game's CrtFile* is the host's FILE*; the stream and descriptor functions are the host's (a bad
  descriptor returns the error instead of the host's invalid-parameter abort); `&_iob[0..2]` map to the host's std
  streams; _access and _findfirst go through the game's slots (the game never closes the find handle). fflush
  006ED534 is the host's. Fact from S4b1: a truncating open of a just-written file, then a write past its end, can
  read the gap back as old bytes on this machine under load (plain CreateFileA too).
- **x87 entries** (crt_x87.cpp, naked, `__asm` exempt in check_cleanup): _ftol 006E853C (decodes the 80-bit value
  with integers; the integer indefinite for NaN / infinity / |x| >= 2^63), _CIacos / _CIasin / _CIpow and
  the stack forms 006E7334 / 006E83D4, _hypot 006FD026. The CRT's control word keeps the caller's
  precision and rounds to nearest, so the port computes at 53 bits and rounds each step to the caller's precision;
  pow splits y log2(x) with fma errors, ties-to-even, exp2, ldexp. C26 tried the host CRT for the maths: acos / asin /
  pow / _hypot (with fmod, modf and _controlfp) changed 14 image goldens (#vitals of the hero in effects_bow,
  net_pair_quest, rebirth_fixes_ui, sound, spells_hero, ui_camp_make, ui_gallery_camp, ui_gallery_deals, units, whose
  console prints "44.8744" for "44.8745"; #unit of the base's units in terrain_water_waves, ui_base_dialogue,
  ui_base_topic, ui_gallery_base, zone_base_bz1g), so those stay ported; fmod (_CIfmod 006E95AA, fmod 006E95A0),
  modf 006EAD0B, floor and ceil alone kept every golden and are the host's (std::fmod and so on). _controlfp stays
  the port (SV10k below: the UCRT's differs in the control word and costs time, though the goldens were the same);
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
- **Exceptions** (crt_except.cpp): _CxxThrowException 006E7819 raises 0xE06D7363 with the thrower's ThrowInfo (only
  stlp's out_of_range uses it now; the pointer throws are the compiler's, "Named data (C18)"); MSVC 6 std::exception (vtable 00747290, "Unknown exception").
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
  reads its allocator argument (callers pass a real one); `ThrowOutOfRange` throws an out_of_range of ours
  (std::exception, the 256-byte name, logic_error; C25c).
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
- A system call does not trap on an inaccessible page, it fails: C25's image trace saw WriteFile fail (error 1784) on
  a save's one-byte .data constant. Page protection tricks must open a buffer's pages around the Win32 calls that
  take it.
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

### U1: the switch and the asset boundary (2026-10-21)

- **Switch**: CMake option `EI_UTF8` (off by default) and cache string `EI_ASSET_CODEPAGE` (1251), both compile
  definitions; `build.cmd <config> utf8` builds it into `build\<config>-utf8`. Code tests `text::kUtf8` with
  `if constexpr`; with the switch off nothing changes a byte (the gallery's frames and goldens are the same as
  main's).
- **Helpers** (src/game/asset_text.h): `text::FromAssetText` (asset bytes to UTF-8; a `std::string` overload and an
  in-place `CString*` one), `text::ToAssetText` (back to the asset code page, false for ASCII or text that is not
  valid UTF-8), `text::CharacterCount` (a length that means time: one per character, a colour escape `\007C`+3
  bytes by its bytes). U1's ANSI boundary helper `ForAnsiCall` (UTF-8 back into the asset code page at GDI's
  ANSI calls) went with U3.
- **Sites converted**: the text database (`TextSources::FindText`, which `CTextManager::Open` reads through; the
  copies are cached per data address and dropped when their archive closes); `CTextManager::ListKeys` (key names)
  and the key passed to the lookup (one "pers" key holds Cyrillic); CFileRegistry's `GetString`,
  `GetStringBuffer`, `GetStringArray`; Chunk's `ReadString` / `ReadEncodedString` (.mob); credits.scr
  (`CCreditsScreen::Build`). The archives also hold binary files: `GameFlow::RegisterQuests` reads a zone's
  quest.reg from the text cursor, so it opens it with `CTextManager::OpenData` (bytes as they are), and
  `TextSources::Find` (004DEBC0) stays the raw lookup.
- (U1's 8-bit save name editor, a `ToAssetText` / `FromAssetText` pair around it, went with U4; the text draw
  and extent are wide since U3.)
- **Behaviour by length**: a briefing phrase lasts 0.05 s per character (`CBriefingScreen::NextPhrase`): counted
  with `CharacterCount`.
- **Check** (repeat for U2-U5): build `build.cmd verify` and `build.cmd verify utf8`; run the gallery on each,
  `python tools/work.py test <ID> ui_gallery_menus ui_gallery_game ui_gallery_camp ui_gallery_mp ui_gallery_base
  ui_gallery_deals render_titles_present --golden --jobs 4`, the second time with
  `EI_BUILD_DIR=<worktree>\build\verify-utf8`; copy the first run's `build\verify\runs\<scenario>-standalone\frames`
  away and compare each scenario with `python tools/compare_frames.py <off frames> <on frames> --tolerance 0
  --max-fraction 0`. U1's result: 51 of the 54 frames identical (menus, in-game HUD and dialogue, camp, base,
  trade, titles and credits). The 3 others were ui_gallery_mp's briefing, chat_typed and chat_sent: the base
  screen's player list (the face and "На базе", top right) was missing. That was not the text but the heap's
  layout (the UTF-8 copies only moved it; so did leaked allocations or a log line): the server's update lists the
  player record and the client's binding in the order of its entries' addresses, and a player read before the
  binding was listed as nobody (UB1, fixed: network.md "Original bugs kept (rest)", address_order.h). Goldens with the switch on differ only in ui_gallery_game's
  saves (the save title and the top panel text are UTF-8 now), which is allowed.
- **Found for U2-U5**: `GetStringBuffer` cuts UTF-8 by bytes at the
  buffer size (U5); `CharUpperA` in script_addon_strings upper-cases bytes (U5); network player names pass an `isprint` filter byte by byte (multiplayer, U5); the script string
  length (script_addon_strings) counts bytes (U5); the CRT's LC_CTYPE is "english" (1252) from the Font CharSet
  option, so `tolower`/`MakeLower` and the `_ismbc*` classes treat UTF-8 bytes as 1252 letters (U5: a UTF-8-aware
  case table); the system ANSI code page here is 1252, so ANSI file and window calls of Cyrillic needed U2's wide
  calls.

### U2: the system boundary (2026-10-21)

- **Where**: src/game/system_text.h. With EI_UTF8 the image's import slot of each ANSI call that takes or gives
  text holds its UTF-8 version (`text::utf8::<Name>A`, same signature; `text::SystemRoute`, asked by
  `ImageRoute` in hook/standalone_main.cpp when image_imports.cpp resolves the slots). So every call through
  `gameapi::` passes and receives UTF-8, and with it `CFile`, `CFileFind`, `CStdioFile` and the CRT's `fopen` /
  `_findfirst` / `_access` (they call through the slots). The harness takes the same slots afterwards and wraps
  the UTF-8 versions, so its saves redirect, overlay and registry snapshot work unchanged (they work on the
  bytes; advapi32's and kernel32's wide exports are taken too: harness_settings.h, harness_install.h). Without
  EI_UTF8 no slot is routed.
- **Routed**: files (CreateFile, FindFirstFile / FindNextFile, DeleteFile, MoveFile, CopyFile, Create- /
  RemoveDirectory, Get- / SetFileAttributes, GetCurrentDirectory, GetFullPathName, GetModuleFileName,
  GetDiskFreeSpace, GetVolumeInformation, LoadLibrary, LoadCursorFromFile, mmioOpen), the registry (RegOpenKeyEx,
  RegCreateKeyEx, RegQueryValueEx and RegSetValueEx: string values as UTF-8, their sizes in UTF-8 bytes),
  CreateWindowEx (a non-ASCII title is set as UTF-16 with `DefWindowProcW(WM_SETTEXT)`, past what
  CreateWindowExA would convert in the ANSI code page), FindWindow, MessageBox, GetKeyNameText, LoadString (the
  window title and MFC's strings, UTF-16 in the resources).
- **Direct calls** (not through the slots): `text::sys::<Name>A` is the UTF-8 version with EI_UTF8, the ANSI call
  without (mod_folders, script_addon_commands, console_addon, world_client_draw, world_client_maps). The
  console's clipboard and addon's AutoExec lines convert with UTF-8 instead of the ANSI code page;
  `Fullpath` (mfc_file.cpp) takes the current folder through the slot.
- **Input that is not UTF-8** is taken in the ANSI code page, as the ANSI call took it; output is always UTF-8.
- `ShowMessageBox` passes its text as it is.
- **Not routed**: the profile calls (`CIniFile`: GetPrivateProfile*, WritePrivateProfileString; the values
  round-trip as bytes and the config\*.ini names are ASCII; a Cyrillic install folder would need them wide,
  with the ini files' code page decided), OutputDebugString, the loader's own calls (hook/, standalone/:
  ei_hook.dll's folder, EI_GAME_ROOT through `RedirectGamePaths`). (CharUpperA / CharLowerA are routed since U5: the case table.)
- **Save names**: saves are folders `saves\save<n>` with the title inside info.sav, so a save's title never
  reaches a file name; the multiplayer heroes are `mp\<n>.mp`. Cyrillic file names are checked at every
  UTF-8 harness start instead (tests/verify/harness_utf8.cpp): a folder and a file with Cyrillic names made,
  listed, read back and removed through the game's slots under the saves redirect, the names checked on disk
  as UTF-16; a failure fails the run.
- **Check**: the frame comparison above. U2's result: as U1's, 51 of the 54 frames identical and ui_gallery_mp's
  briefing, chat_typed and chat_sent differing by the same 27249 pixels (the heap-layout bug, UB1); goldens with
  the switch on differ only in ui_gallery_game's saves, as U1's. The image's log names the routes: "504 resolved
  (33 routed)" with EI_UTF8 since U4 (32 here and the command line), "(1 routed)" without.

### U4: typing (2026-10-21)

- **The window is a Unicode one** with EI_UTF8 (system_text.h): the image's slot of `RegisterClassExA` registers
  the class wide (`text::utf8::RegisterClassExA`), and the slots of `DefWindowProcA`, `GetMessageA`,
  `PeekMessageA` and `DispatchMessageA` take the wide calls themselves (the same arguments). Retrieval and
  dispatch must both be wide: a message taken with an A call is converted to the ANSI code page whatever the
  window. So WM_CHAR carries a UTF-16 unit; `DispatchInput` (00457970, app.cpp) passes the interface a code
  point (`TypedCharacter` joins a surrogate pair; a lone half is dropped). The harness's own window subclass
  (SetWindowLongPtrA, AlwaysActiveWindowProc) is unaffected: input never goes through the procedure, and
  CallWindowProcA converts what is sent.
- **The editors** (asset_text.h, "Typing"): `text::Typed(head, character)` appends the character's UTF-8 bytes,
  `TypedLength` is how many, `NextCharacter` / `PreviousCharacter` step the caret, `CharacterStart` snaps a byte
  offset to its character, `CharacterCount` limits the length. The caret stays a byte offset (every draw and
  `Left` / `Mid` take it as one) at a character's start. Without EI_UTF8 each helper is the original's byte step
  and `Typed` is `mfc::operator+(head, char)`, the same allocations in the same order. Sites: the message box's
  edit field (0065C610, 0065C930: its letter class for a code point past ASCII is `IsCharAlphaW`), the console
  line (005E4180, 005E44A0; Rebirth's paste puts the caret at `CharacterStart` of its own sum), the chat line
  (005E3230, 005E3550), the save window's name (006548A0, 00655050). U1's `ToAssetText` / `FromAssetText` pair
  around the save name is gone, and with it the in-place `ToAssetText(CString*)`.
- **Check**: tests/scenarios/utf8_typing.txt (no original run: the original has no Unicode typing) types
  Cyrillic and a character past U+FFFF into the console line with the harness's `typeunicode` (each UTF-16 unit
  posted with PostMessageW) and checks the line's bytes and the caret with `consoleline` (harness_utf8.h) after
  typing, Left, Backspace, Delete, Home / Right, End and Esc. Without EI_UTF8 both steps are skipped. And the
  frame comparison above: U4's result as U1's and U2's (51 of 54 frames identical, ui_gallery_mp's three by
  the same 27249 pixels, UB1; goldens with the switch on differ only in ui_gallery_game's saves).
- **Rule from a real bug**: a route must be a function of ours (`text::utf8::DefWindowProcA` calling
  `DefWindowProcW`), never `&::DefWindowProcW`: the routes table is read during ei_hook.dll's static
  initialisation, when an entry holding an import's address is not set yet (the first try routed none of
  them and typed Cyrillic came out as "???").

### U3: drawing and measuring (2026-10-21)

- **Wide text calls** (system_text.h): with EI_UTF8 the image's slots of `DrawTextA`, `GetTextExtentPoint32A` and
  `CreateFontIndirectA` take `text::utf8::DrawTextA` / `GetTextExtentPoint32A` (the text, UTF-8 or else ANSI-code-page
  bytes, to UTF-16; the lengths stay byte counts) and `CreateFontIndirectA` (the LOGFONT and its face name to
  `CreateFontIndirectW`). Every interface text draw (MFC's virtual `CDC::DrawText`, slot 26: the
  surfaces' `DrawTextIn` 005FDE20, the console's lines and input, the debug panel's map), the interface's extent
  (`CInterface3D::TextExtent` 005F8340, what the editors' width limits use through
  `InterfaceTextExtentOriginal`) and the console's `PromptWidth` go through them; no call site changed. The
  harness's font slot wrapper (`HarnessCreateFontIndirect`, no anti-aliasing) wraps the UTF-8 version, as its
  file redirects do. `text::ForAnsiCall` is gone.
- **The "charset option"**: the fonts never took a charset from an option. MFC's `CreatePointFont` asks for
  `DEFAULT_CHARSET`, which the font mapper realises in the system's ANSI code page; the ANSI text calls read bytes
  in the code page of the selected font's charset. So the original (and the build without EI_UTF8) draws the
  assets' cp1251 bytes as Cyrillic only on a system whose ANSI code page is 1251; on this machine (1252) every
  frame shows them as Latin-1 letters ("Ãðàôèêà" for "Графика"). With the wide calls the charset no longer
  decides the glyph and `DEFAULT_CHARSET` stays. The "Font CharSet" option (+0x224, "english") only sets the CRT's
  LC_CTYPE (`CTextManager::Create` 004DED20) (with EI_UTF8 not since U5): its 1252 tables upper / lower-case UTF-8 lead
  bytes (`tolower` of 0xD0 is 0xF0) but also let `isprint` pass them (the network name filter).
- **Check**: the frame comparison above with `EI_FONT_CHARSET=204` in the environment of the run without EI_UTF8
  (harness.cpp: the harness's font slot sets that charset, as a system with the assets' code page realises
  `DEFAULT_CHARSET`, so the ANSI calls draw the assets' Cyrillic); the UTF-8 run without it. U3's result: 54 of 54
  frames identical (the same glyphs from the wide calls and the 1251 ANSI calls). Without the variable the
  build without EI_UTF8 draws Latin-1 letters where the UTF-8 build draws Cyrillic, so most frames differ there.
  Goldens with the switch on differ only in ui_gallery_game's saves, as before. utf8_typing types "λ" (outside
  cp1251) into the frozen menu's console and requires its frame to differ from the line "?" (`utf8 diffcapture`;
  the "utf8 <step>" prefix runs a step in the UTF-8 build only): 58 pixels at 4K, the glyph. How "😀" draws (Times
  New Roman has no emoji; whether DrawTextW falls back to another font) is not checked.
- **Still ANSI**: the titles' RichEdit (`SendMessageA`, RTF carries its own code page).

### U5: the audit (2026-10-21)

Every site that treats text as bytes, decided as an identifier (left: ASCII names, keys, file names, numbers, or
bytes compared and copied whole) or display text (fixed for UTF-8 with EI_UTF8; without it nothing changes).

- **Case** (src/lib/text_case.h): a small table, ASCII, Latin-1 and the rest of code page 1252's letters, Cyrillic
  U+0400-U+045F and Ґ ґ (every letter of 1251); each pair is two UTF-8 bytes both ways, so a case change never
  changes a length. With EI_UTF8 the CRT no longer takes case or classes from the locale: `_stricmp`,
  `_strnicmp`, `_memicmp` and `_mbsicmp` compare the table's lower-cased bytes (`text::CompareIgnoringCase`;
  "Р" D0 A0 and "Ѡ" D1 A0 share a trail, so every byte is folded, never only differing ones), `_mbslwr` (and so
  `CString::MakeLower`) lowers by it, `tolower` / `toupper` change ASCII only, and `isalpha`, `isdigit`,
  `isspace`, `isprint`, `_ismbcdigit` and `_ismbcspace` give a byte past ASCII no class (code page 1252's
  `isspace(0xA0)`, the trail of "Р", would split words). There are no double-byte lead bytes (`_ismbblead`,
  `isleadbyte`, `_mbsinc`, `_mbclen`, `_splitpath`), whatever the system's ANSI code page. The "Font CharSet"
  option is not used: `CTextManager::Create` (004DED20) leaves LC_CTYPE "C". `CharUpperA` / `CharLowerA` are
  routed (their slots, `text::sys`) to the table. Sites: about 100 case-insensitive compares (map, zone,
  allod, material, model, spell, skill, item, keyword and file names, "none", the briefing's person and topic
  keys, the save folders) left as identifiers: they reach the CRT's compares, which use the table. 19
  `MakeLower` (keys and file names) left. 5 `CharUpperA` / `CharLowerA`: mod_folders' 2 path keys and MFC's
  `CFileFind` path left (now UTF-8-safe through the routes), script `StrLower` / `StrUpper` fixed. 6 per-byte
  folds (the .reg and archive name hashes, the map name hash, the archive wildcard, the keyword tables through
  `CaseFoldTable`) left: their names are ASCII (the .reg tables' slots were laid out by the original's tools).
  8 class tests left (drive letters, digits in scripts and party files, the edit field's ASCII classes); the
  network's player name filter (`IsPrintableText` 0047ADA0) fixed: by character, each a valid UTF-8
  sequence of a printable code point (the original passed UTF-8 bytes only through the 1252 table's
  `isprint` and the bytes before it for a negative char).
- **Buffers**: about 95 fixed `char` buffers. Left: paths and file names (MAX_PATH, the figure, link and
  terrain names, archive patterns), numbers and ids, model / texture / sound names read from the network
  (`DeltaField::ReadText` empties one too long), error texts (`wvsprintfA` stops at 1 KB), the profile calls'
  buffers (ANSI, U2), the host password (`strncpy` to 20 bytes on both sides, compared as bytes). Fixed:
  `CFileRegistry::GetStringBuffer` cuts at a character's end (`text::FittingBytes`; the asset's 16-bit length no
  longer wraps); the server list's 90-byte name buffer (`text::CopyFitting`); the message box's edit text
  (50 characters, up to 200 bytes) into the connection screen's 0x80-byte box buffer (`CMessageBox::CopyEditText`,
  cut at a character's end); `DrawTextA`'s UTF-8 route with `DT_MODIFYSTRING` (the server list's ellipsis)
  gives `DrawTextW` the four characters of room it writes past the text (it wrote past the `std::wstring`). The
  chat line also stays within 200 bytes (`CChatOverlay::OnChar`): a client's update carries 220 bytes
  (`Connection::updateBudget_`) and an entry past that is never counted, so a longer SayMessage would never
  arrive. Save names are CStrings (no buffer).
- **Byte indexing**: about 37 sites. Left: ASCII prefixes, markers and separators (`Left(1)`, `Left(2)`, `Mid` past
  "pers_", "constr", '|', "^<digit>", the text splitters and tokenizers that stop at ASCII bytes; a UTF-8
  byte never equals one), the editors' carets (U4). Fixed: script `StrLen` and `StrMid` count characters
  (`text::CharacterCount`, `text::CharacterBytes`); the new game's name cut to 10 characters, not bytes
  (`CMpConnectScreen`).
- **Console padding**: none. Nothing pads text to a width (no `%-20s`, no stream width); the console tables are
  literal ASCII.
- **Word wrap**: the chat overlay's `AddLine` (005E2A30) takes a character as its whole UTF-8 sequence
  (`text::CharacterLength`), so a word wider than a line breaks between characters and every piece is measured
  as text. `CUiSurface::DrawWrappedText`, the console tokenizer and the text reader walk bytes but break only at
  ASCII spaces and newlines: left.
- **Check**: tests/scenarios/utf8_typing.txt's "utf8 utf8text" (Cyrillic compares in either case, the "Р" / "Ѡ"
  pair, `MakeLower`, `CharUpperA`, `tolower` and `isspace` of UTF-8 bytes, the name filter on a Cyrillic name, a
  cut sequence and a control character) and "utf8 chatwrap" (a long Cyrillic line with a 150-letter word added to
  the chat overlay: more than one line, each valid UTF-8, joined back to the text), and the frame comparison
  above.

## 5. game.exe's role

game.exe runs only as the original game, for the harness's references; none of our code runs in it.
- ei_launch sets `EI_NO_HOOKS=1` for game.exe (`--no-hooks` is accepted and changes nothing; the image never gets it). Its
  only hooks are the vector recorders with `EI_VECTORS=1` (refused when addon.dll is loaded: recordings are the vanilla
  code's) and the harness's own patches, by address (that side of the harness keeps game.exe's addresses).
- **Builds**: `release` is the product (the image); `verify` (CMake option EI_HARNESS) is the image plus the harness
  (tests/verify) and the recorders (tests/vectors).
- What runs against it: the original's harness (goldens, vectors, widescreen references, the reference cache);
  how verification works: docs/notes/standalone-verification.md section 1.
- **Scenarios**: a line `# no original: <install>... <reason>` leaves a scenario's `--no-hooks` runs out of plans
  (renderer-only scenarios, rebirth_flags on the vanilla data, render_present_fullscreen).
- EI_PROFILE=1 samples the image: EiStandaloneMain calls InstallProfiler before WinMain, and profile_report names the
  image's functions `ours:`.
- **The user's real mouse is kept out of both games** (tests/verify/harness_input.cpp / .h): the game window's subclass
  (AlwaysActiveWindowProc) answers WM_NCHITTEST with HTNOWHERE, so the real cursor over it makes only non-client messages;
  the harness's posted messages never hit-test. DirectInputCreateEx's import slot (0073B044) is routed at the game's
  CreateWindowExA (routing it at the harness's start came before the image's own routing and broke DirectInput): the
  game's GetDeviceState for GUID_SysMouse is wrapped and the state zeroed after each read. The report's `MOUSE:` line
  counts reads, real moves left out and hit tests. Without it the original's goldens did not repeat: dgVoodoo's window
  covers the desktop and is not cloaked, so the resting cursor sent WM_MOUSEMOVE to it and scrolled the camera.

## 6. The image's source files

Moved from the files' headers.

### host.cpp

ei_standalone: runs the rebuilt game without game.exe (docs/notes/standalone.md section 2). The host is an ordinary
exe: it neither reads game.exe nor lays out anything at game.exe's addresses. The game's hInstance is this module,
whose one resource is its version information (host.rc; the game's strings are literals in our code, no icon yet);
ei_hook.dll keeps game.exe's .rdata and .data in its own storage with bytes from the build
(src/standalone/image_data.cpp), resolves game.exe's imports into a table of its own as it initialises
(image_imports.cpp), takes the registrations as game.exe's code (InstallHooks, image_code.cpp), points the code
cells of game.exe's data at them and runs WinMain. Function pointers in the data and vtable slots reach our code
that way. The guard: first thing, the host reserves what of game.exe's range (0x400000 up to its SizeOfImage) is
free, with no access, so nothing is allocated there later and a stray game address (a jump into game.exe's code
that went around CodeAddress, a read of its data that missed the seam) faults and is named instead of reaching
other memory. What the loader had already put there (this exe, a heap) stays as it is; ei_hook.dll logs how much is
guarded.

### image_code.cpp

game.exe's code in the standalone image (docs/notes/standalone.md, "Own data"). Nothing of game.exe's code is at
its addresses (the host keeps what of game.exe's range is free reserved with no access): nothing runs there and
nothing jumps through it. What runs for an address of game.exe's code is CodeAddress's: the replacement registered
for it (InstallHooks hands the table over: SetCodeTable), else a stub that reports there is no code at the address.
The code cells of game.exe's data (vtable slots, callback tables, the static initialiser list:
image_code_cells.inc, tools/gen_image_cells.py) are pointed at it once the table is in (FixCodeCells), and the
static initialisers, the code literals and the harness ask CodeAddress. In game.exe (the original's runs) every
address is its own code.

### image_data.cpp

game.exe's .rdata and .data in ei_hook.dll's own storage (docs/notes/standalone.md, "Own data"). In the standalone
image both sections (kMovedBegin-kMovedEnd: .rdata with the import address table at its start, .data with its
zero-filled tail, the .bss) are kept in g_dataStorage instead of at their addresses: the data seam (game_data.h)
adds g_dataShift to each of their addresses, the pointer cells of game.exe's .rdata and .data that point into the
range (image_cells.inc, tools/gen_image_cells.py: vtables' RTTI, the C++ exception tables, MFC's runtime classes
and maps, string pointers) get the same shift. Nothing is at the old range (the host keeps what of game.exe's range
is free reserved), so a use that missed the seam faults and is named with its data address and code address
(OnOldData); a game address outside the range asked of the seam is named too (StrayGameAddress). The initial bytes
come from game.exe at build time (image_data_bytes.inc in the build folder, gen_image_cells.py --bytes). In
game.exe (the original's runs) none of it happens: the shift stays 0. The import slots at the storage's start are
ours: zero from the build, resolved right after the move (image_imports.cpp). The move runs before every other
static constructor of ei_hook.dll (init_seg(compiler)): the harness and the game's globals bind references to game
data at static initialisation, and the harness takes import slots there.

### image_imports.cpp

game.exe's imports in ei_hook.dll's storage (docs/notes/standalone.md, "Own data"). The image's import slots are a
table of ours: every slot of game.exe's import address table (image_imports.inc, written by tools/gen_iat.py from
game.exe's import directory: ImportTable member, DLL, function or ordinal) is resolved here into the storage at the
data seam's shift (game_data.h: kImportsBegin-kImportsEnd move with .rdata), so everything that calls through a
slot (lib/import_table.h's Imports(): gameapi.h's routed imports, Miles, Bink, Winsock, the static initialisers'
Import; the harness's slot routes by address) reaches our table. The image's command-line import gets its wrapper
in place of the export (ImageRoute, hook/standalone_main.cpp); the resource and module imports take the host's
module as it is. Nothing is at game.exe's own table, as at the rest of the old .rdata (image_data.cpp): the image
runs none of game.exe's code, so a call through an old slot is a use that missed the seam, reported with the slot's
import and its code address (OnOldData, DescribeImportSlot). A slot of ours whose import did not resolve leads to a
trap until the run ends (OnTrap; the launch-free check fails on it). Runs from image_data.cpp's mover, before every
other static constructor of ei_hook.dll: the harness takes import slots in its own. In game.exe (the original's
runs) nothing happens: the loader filled its table.

### lib/mfc_file.h

MFC 4.2's CFile, CStdioFile and CFileFind (filecore.cpp, filetxt.cpp, filefind.cpp) and the path helpers they use,
written out for the standalone exe (docs/notes/standalone.md section 2). Same layout as the copy linked into
game.exe; the tables are compiled (mfc_file.cpp's FileTable, StdioFileTable and FileFindTable) in place, with the
slots in game.exe's order. Every body is registered at its library address (mfc_file.cpp). CFile works on Win32
handles through the game's import table. CStdioFile's stream is the host CRT's FILE (crt.h: the game's FILE* is the
host's).

### lib/mfc_handle_map.h

MFC 4.2's handle maps (winhand.cpp) and what they are built from: CMapPtrToPtr (map_pp.cpp),
CRuntimeClass::CreateObject and the new handler MFC swaps around its allocations (docs/notes/standalone.md section
2). A handle map finds the C++ object of a Windows handle: the permanent map holds the objects that attached their
handle (a CPen, a CFont), the temporary map the objects FromHandle made for the others (a CTempDC for a surface's
DC, a CTempGdiObject for a stock pen). The game has no CWinApp, so nothing ever empties the temporary maps (no
OnIdle). Every body is registered at its library address (mfc_handle_map.cpp); the maps live in MFC's module thread
state, which the image replaces with its own (mfc_gdi.cpp).

### lib/mfc_runtime_class.h

MFC 4.2's CRuntimeClass records of the game's copy, as named data: the 41 the exe holds, 36 here (const, in .rdata)
and the 5 serializable ones (CByteArray, CDWordArray, CStringArray, CWordArray in mfc_coll.cpp, CDib in
game/dib.cpp) in .data, where the static initialisers link them into the module state's class list (AfxClassInit
writes m_pNextClass). Each record names its base's record and its create function (ours, registered at the game's
address: the pointers are compared by tools/named_data.py, "Pointer elements"). The hierarchy is one piece: a
record's base is a named range, and game.exe's data holds no pointer into a named range, so CObject's record (the
root) could be named only once every record that points at it was. Only the exceptions', files' and GDI
temporaries' records are read (GetRuntimeClass, the handle maps); the rest are what their base pointers and create
functions reach.

### lib/mfc_state.h

MFC 4.2's thread and process locals (afxtls.cpp) and its system metrics record AUX_DATA (afxData, auxdata.cpp), as
far as the exe's static initialisers and exit functions reach them (docs/notes/standalone.md). The thread and
process locals are globals MFC fills on first use (_afxThreadState, _afxBaseModuleState and the others); the exit
functions free them through the destructors here. afxData is built by a static initialiser and its menu-dot bitmap
deleted at exit. The header also holds what the other static initialisers call: AfxClassInit (the runtime class
list of the module state), MFC's global locks, _afxDBCS's initialiser and wingdix.cpp's halftone brush (an atexit
that deletes it). The image has a stand-in module state with the members used so far (ImageModuleState). Every body
is registered at its library address (mfc_state.cpp); in the image nothing fills the locals (the handle maps are
thread_local, mfc_gdi.cpp), so their destructors find an empty slot or object.

### lib/mfc_slots.h: the collections' compiler-made vtables

---- Compiler-made vtables ------------------------------------------------------------------- The collection
templates keep the vtable as a plain field (mfc.h): an instantiation's Class tag names the table its constructor
and destructor store (`Class::Vtable()`). A tag that is a CompiledCollection is a C++ class whose vtable the
compiler makes in place of game.exe's table (docs/notes/standalone.md, "Compiler-made vtables"): CObject's three
slots (GetRuntimeClass, the destructor `Destroy`, Serialize (`Store`)) over the header's layout. Our code calls the
header's functions by name; the slots are what a dispatch through the table reaches. A tag derived from another is
the derived instantiation's (its destructor ends in the base's, which stores the base's table).

### lib/mfc_try.h: why TRY / CATCH works across the two C++ runtimes

A rewrite writes exactly that. What makes it work across the two C++ runtimes:
- a throw is `throw` of a pointer to one of the classes below: this compiler makes the throw information, which
  lists the pointer to the class and to each base like the game's did (".PAVCFileException@@", ".PAVCException@@",
  ".PAVCObject@@", ".PAX");
- a handler matches a thrown object when the type descriptors are the same object or carry the same decorated name.
  So the classes are in the global namespace under the game's names and have its bases; nothing else of them is
  used (they are never made here: the objects are the game's MFC objects, thrown through a cast pointer);
- the unwind runs the frames between the throw and the handler through their frame handlers (RtlUnwind), then ours.

### lib/stlp_deque.h

STLport's deque<T> as compiled into game.exe (stlp.h includes it; docs/notes/standalone.md, section 2). 0x28 bytes:
the start and finish iterators (cur, first, last, node), the map of node pointers and its size; the allocator
proxies are empty bytes inside the map words (the constructor writes them and then zeroes the words). Nodes hold
512 / sizeof(T) elements (one for larger T). Only trivially destructible T: the game's instantiations destroy
nothing. Instantiations: the camera's view stack (0x24-byte entries, 14 per 0x1F8-byte node; camera.h keeps its own
layout) and the material's (0x10-byte entries, 32 per 0x200-byte node; material.h). The members are registered at
the instantiations' addresses in stlp_deque.cpp through the DequeAbi shims.

### lib/stlp_hash.h

STLport's hashtable (hash_map / hash_set) as compiled into game.exe (docs/notes/standalone.md, section 2); the
library body the instantiations' own addresses are registered to (stlp_hash.cpp). Layout (0x14 bytes): the hasher,
key_equal and key extractor (empty, one byte each, then a pad byte), the bucket vector (vector<Node*>,
stlp_vector.h) and the element count, which STLport keeps in the node allocator's proxy. A node is the next pointer
followed by the value; its size picks the pool's free list, so each instantiation's node type comes from its
node-creation code. Order: bucket counts are STLport's primes (NextPrime, stlp.cpp: the first of the 28 primes not
below the hint, else the last), a key lands in bucket hash(key) % buckets, a new node goes to the head of its
bucket, and iteration walks the buckets in order and each chain from its head. A rehash (resize) moves each old
bucket's chain, head first, to the head of its new bucket.

### lib/stlp_tree.h

STLport's red-black tree (_Rb_tree, map, multimap, set) as compiled into game.exe (stlp.h includes it;
docs/notes/standalone.md, section 2). The STLport of the exe is SGI's 3.x tree: _M_insert(x, y, v) without the
later hint argument, the hint insert falling back to insert_unique(v), erase(key) through equal_range. A tree is
0xC bytes: the header node (from the node pool; its parent is the root, its left / right the leftmost / rightmost
node, red), the element count and the empty compare object (one byte). Nodes are color (a bool: red 0, black 1, in
a dword whose other bytes are left as the allocation had them), parent, left, right, then the value. The node-only
algorithms (_Rb_global<bool>: increment, decrement, the rotations and both rebalances) are one body each in
stlp_tree.cpp, registered at their addresses.
