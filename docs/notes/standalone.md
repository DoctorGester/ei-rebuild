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
gen_image_cells, gen_iat, check_signatures, check_stolen, find_vtables,
find_dead_code; the build reads none since C25c) goes through `tools/exe_path.py`: `--exe` (or `EI_EXE`), else
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
  (S18k: relocatable, ASLR as any exe; it may load anywhere, game.exe's range included) that reads no game.exe and
  reserves nothing of game.exe's range (C48 removed the reservation, its guard log and the fixed base: with no raw game
  address left, the guard was a safety net only). A stray game address faults when the memory is free and is named by
  ei_hook.dll: OnOldCode (the code section) and OnOldData (.rdata / .data) with the detailed reports. The seam itself
  never hands out such an address: in the image `GameAddress` of a game address outside the moved range (headers, code,
  .rsrc) is `StrayGameAddress`'s report (`the data seam was asked for game.exe's code at ...`); a game address is
  itself only in game.exe, an address outside game.exe's range (a resolved pointer, null) is itself in both. A raw
  access to the headers or .rsrc has no report of its own (only the seam names those): it ends in the unhandled-exception
  log line. Where the loader put memory in the range (the host, a heap, a DLL), an access finds that memory and
  does not fault. `ei::Standalone()` is
  "the main module exports `EiStandaloneHost`" (the host's marker; game.exe exports nothing).
  **Resources** (S18j, C23): the host has none of game.exe's. Its one resource is a VERSIONINFO of its own
  (`src/standalone/host.rc`, committed, linked by CMake's RC language); the game's strings are literals in our code
  (the window title "Evil Islands" and class "EIGAME" in app_main.cpp); the bitmaps, dialogs, cursors and icons were never loaded, and the window class has
  no icon (C24). WinMain's hInstance and GetModuleHandleA(NULL) are the host, so DirectInput needs no wrapper;
  GetModuleFileNameA(NULL) names ei_standalone.exe (the
  message box title; GetCommandLineA's first token too).
  Then it loads ei_hook.dll, which keeps .rdata (with the import slots) and .data in its own storage
  and resolves game.exe's imports as it initialises (S18f; since S18k DDraw, mss32 and binkw32 from the working
  directory, the game folder, else System32, so Rebirth's dinput proxy and addon.dll stay out; "Own data" below);
  then the start-up actions run (`RunStartupActions`: the Rebirth flags, WideScreen's resolution), the static
  initialisers and WinMain (`app::GameMain`, called by name: "Code", below). Nothing runs in or jumps through
  game.exe's code. `ei_standalone [--dll <ei_hook.dll>] [--run] [-- <game args>]`, run from the game folder; without
  `--run` it builds the image, checks the routes and the imports and exits.
- **What comes from game.exe at build time** (S18k: nothing at run time): the sections' bytes
  (`tools/gen_image_cells.py --bytes`, build/<cfg>/image/, which also checks the 1.06 timestamp; C25 measured which
  of them the image reads: "The build without game.exe" below); the section places are constants (game_data.h, hook.h's `kGameTextBegin`-`kGameTextEnd`). InstallHooks reads
  game.exe's file only in game.exe itself (the recorders' stolen bytes). The 8
  dispatch-only functions (`MOV EAX,[ECX]; JMP [EAX+n]`) need no bytes since C25: their callers call the
  virtual, `EI_REPLACE_DISPATCH` and its byte check are gone.
- **Start** (EiStandaloneMain): the C initialiser `__initmbctable` (00783BC0), then the 748 static initialisers in
  list order (`RunStaticInitialisers`), then WinMain; its result goes to the CRT's exit as the CRT did.
- **Missing code.** Reaching an address without code (a jump into the guarded .text) logs `FAILED: standalone: no code at <address>, [esp] <caller>` and ends the run;
  `tools/resolve_crash.py <run folder>` names it. A read or write of .text is reported as such
  (`... of game.exe's code at ...`). The handler goes in before the start-up actions
  (S16c), so a startup action's hit is reported too; a hit during ei_hook.dll's static constructors prints
  `ei_standalone: no code at ... before ei_hook.dll's missing-code report` and exits 3.
  ei_launch reports a game that ended by itself while its report still says RUNNING (`FAILED: the game ended ...`).
- **What the image could still reach without code**: nothing by address (our code calls by name; game.exe's data
  holds no code pointer in the image since C20).
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
  initialisers name every object they touch (plain calls, no data address; gen_static_init stops at a statement that
  would have one).
- **The check** (`python tools/data_seam.py`, build.cmd): per section the uses through the seam, through a
  helper whose every definition hands its address to the seam (per-file `GameFloat(std::uintptr_t)`), a
  constant whose every use does, and raw. config/data_seam.csv holds the raw count per
  file and section (only down; `--update`) and the sealed sections (none raw). `--list <section>` shows the
  raw uses, `--apply <section>` rewrites the casts it can (`*reinterpret_cast<T*>(0x...)`, the helpers'
  casts, constants used in casts).
- **Moving a section**: our storage (the low 16 bits of each address kept), GameAddress's shift set before
  any static constructor, the pointer cells of the remaining image data that point into it fixed up (no
  relocations in game.exe: a scan of aligned dwords, string tails left out), and nothing at the old range
  (free memory faults; PAGE_NOACCESS before S18k, the host's reservation until C48), so a use that missed the seam faults with its address. A moved section's initial bytes
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
  the old range holds nothing of ours.
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
  names none of them. They keep game.exe's values, so a stray read of one lands in the old range
  (`OnOldData`). ThrowInfo -> catchable type array and the array's entries stay: `CxxThrowException` and our
  frame handler read them.
  **The guard**: nothing at the old range (free memory, "Host and image"): a use that missed the seam ends the run with
  `standalone: read|write of game.exe's .rdata|.data at <data> by <code>: ...` (in the import table:
  `... of game.exe's own import slot <slot> (<dll>!<function>) by <code>`) in ei_hook.log, on stderr and in the
  harness's report (`OnOldData`; resolve the code address with the map, as for an INT3 hit). ei_vectors.exe's
  `GameAddress` stays the address.
  **Addresses read back from objects** (a vtable, a string pointer) are the storage's in the image: the harness
  prints them as the original's with `ei::OriginalAddress` (game_data.h; DescribeTopWindow, harness_digest's
  missile vtable, ui_census's window labels and its game-vtable test).
- **Code** (C20 batch 6): nothing of ours stands at a game.exe code address. Our code calls every function by
  name: the static initialisers (static_init.h), the handler tables, callbacks and factories (`&Fn`), WinMain
  (`app::GameMain`); the CRT's takers of function pointers (atexit, qsort, the eh vector iterators, ostream's
  manipulators) call what they are given. Until batch 6 `ei::CodeAddress` (src/standalone/image_code.cpp) gave the
  registration for an address, else a stub reporting `no code at`. The harness changes nothing by address in the
  image (C29: named seams, `IsaacRandom::SetSeedHook` / `SetDrawObserver`, `libcrt::SetRandObserver`). **No code cells** (C20 batch 1): game.exe's data keeps no code pointer
  in the image; every dword of .rdata / .data that pointed into its code section is zero there. Until then
  `image_code_cells.inc` listed the ones the image fixed up to the registration of their value
  (21,636 at S18i; 825 at the end: the static initialiser list 748, the original's MFC message maps 35, the CRT's function
  pointers 35, ThrowInfo's destructors / copy constructors 7), and C20 batch 1 measured that nothing of ours read
  any of them: the static initialisers are generated code (static_init.cpp calls each one's body or its
  registration, never through the list), CWnd::GetMessageMap is never called, the CRT tables' readers are
  game.exe's own CRT (the host CRT's in the image, C26), and no code throws with game.exe's ThrowInfo
  (libcrt::CxxThrowException has no caller; our throws use the compiler's). The vtable slots went with the compiler-made vtables, the EH
  funclets with C20a. A stray call through one would be a call of null. The launch-free check (no `--run`): the routes and imports are checked. Stack scans of the harness and the profiler skip the game's code range in the image.
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
  routed imports and every import of src/lib/import_prototypes.h, the static initialisers'
  `Import(&ei::ImportTable::Sleep, ...)`) or the Windows function itself (below). Every member is typed (C18p): the
  SDK's `decltype(&::X)`, or for what the SDK lacks the `ei::import_fn::X` of import_prototypes.h (Miles with typed
  handles `miles::Sample*` / `Sequence*` / `DigitalDriver*` / `MidiDriver*`, Bink, Winsock 1.1 with SOCKET as
  `std::uint32_t`, DirectInput, DirectDraw, DirectSound's enumerator, the common controls, comdlg32, the print
  spooler, GetVersion, the Interlocked pair); gen_iat.py fails on an import with neither. The harness names its
  slots by member too: tests/verify/harness_imports.h's `PatchImport(&ImportTable::X, hook, original)` (clock,
  heap, Winsock, Miles, window, files, profile, registry), `TraceSlot<T>(&ImportTable::X)`, `crash::WatchWrites()`;
  only addon_sites.inc's MoviesPatch site (BinkOpen's slot, a Rebirth patch site) is an address. `iat` is sealed in config/data_seam.csv (76 raw uses -> 0;
  data_seam.py's helpers are now transitive: TraceSlot -> PatchSlot -> GamePointer).
  **Which calls read the slot** (C18, gen_iat.py's `routed()`): in the image a slot holds the export unless the image
  routes it (ImageRoute: GetCommandLineA; the `TEXT_ROUTE` list of game/system_text.cpp), the
  harness takes it (any `ImportTable::X` tests/ names: window focus, cursor, files and saves, profile, registry,
  clocks, heap, Winsock, Miles) or our code takes it by address (`&gameapi::X`: hook/paths.cpp's RegQueryValueExA in
  game.exe). Those gameapi.h functions are `inline auto& X = ei::Imports().X;` (68, with gen_iat's KEEP:
  GetVersionExA, deprecated in the SDK: a direct call warns), and so are the 91 of import_prototypes.h (no import
  library; not the Interlocked pair, which our code takes as the compiler's intrinsics); the other 343 are `using ::X;`, the Windows function through ei_hook.dll's own imports
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
  only through `ei::Imports()`, and the harness too (harness_imports.h), which the sealed check covers. Not routed to our table: ui_census's game.exe
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
  seam. **Vtables**: no class of ours stores game.exe's table ("Compiler-made vtables" below). **Other tables**: unit_hits' armour
  rows through `ArmourTable` (GamePointer), ui_widescreen's two layout floats (addon.dll writes them) through Read/WriteFloat,
  the addon-site table's .rdata entries as `DataId` (make_addon_sites.py, from 0073B000 to .data's end). cover.cpp's code bound
  is the last byte of .text (0073AFFF), not the start of .rdata. **data_seam.py**: an alias constant passes for the constant it names when all of its own
  mentions reach the seam.

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
| S18k | an ordinary host (no fixed base, no range in its image; C48: nothing reserved), no read of game.exe (dispatch bytes from the build, imports from the game folder), stray game addresses reported by the seam | the address image (dashboard "image ties" 5 -> 0) |

Not on this path: named, typed globals in place of a section's bytes (C18, below) and vtables as real virtual
classes (C19, C28), plain new / delete and standard containers (C14), Win32 calls linked directly.

### The build without game.exe (C25)

The build reads no game.exe (C25c). The image's .rdata / .data bytes (`gen_image_cells.py --bytes`, CMake) are zero
but the committed pointer cells (image_cells.inc); everything else our code reads
there is a named global of ours (C18) or a literal. The resources are the host's own (C23). How it got there: C25a's
trace of the reads and zero-filled trial, its kept list (`config/image_kept.csv`) and its static proof that every
address src/ names held only zeros and cells; C25c named the last rows (`__lc_category` and its static "C" are crt_locale.cpp's) and dropped that machinery
(EI_IMAGE_BYTES_FROM_EXE, the trial, EI_IMAGE_TRACE / image_trace.py, the kept list, the proof, EI_NO_GAME_EXE).
The code cells went in C20 batch 1 ("How it runs", Code): the bytes are zero but the pointer cells.

C25d deleted tools\check_against_exe.cmd and the modes only it ran (check_fp_fold, rdata_literals --check, named_data's
comparison with game.exe's bytes and cells, check_short_functions, check_static_init, gen_image_cells --check's scan,
gen_iat --check's read of the import directory): each had proved its generated file or tagged definition when it was
made, and the generators still read game.exe when rerun. What needs no game.exe stays in build.cmd, from the committed files:
`gen_iat.py --check` (import_table.h, image_imports.inc and gameapi.h regenerated from the committed table and the routes:
a hand edit or a new harness route fails), `named_data.py --check` (definitions well formed, csv current, no address
literal inside a named, retired or unread range, global_objects.csv) and `gen_image_cells.py --check` (retired vtables
against vtables.csv and the sources, the committed cells' targets). The generated static_init.cpp / static_exit.cpp,
image_cells.inc are only as fresh as their generator's last run: rerun
it (reads game.exe) after a change that registers or ports what it decodes.
The harness still runs the original game.exe for goldens and vectors.

What C25a named or dropped of its list, still the code's form:
- **Written globals at addon sites**: WideScreen's layout widths 00742B08 (800) / 007428AC (695) are
  `widescreen::g_layoutWidth` / `g_panelRight` (ui_widescreen.cpp, tagged; Startup writes them as addon did; the clock and
  the quick items read them). named_data.py's `NAMED_ADDON_SITES` lets a named range hold such a site, be written though in
  .rdata, and be named by tests/verify/addon_sites.inc (game.exe's address, for harness_addon.cpp in game.exe).
- **The DragList message id**: 007043FA (`RegisterWindowMessageA("commctrl_DragListMsg")` into 007CAA64, read only by
  CWnd's message map) is gen_static_init.py's UNUSED_INITIALISERS since C14zi.
- **Named**: OnNewParty's next record id
  `g_nextRecordObjectId` (player_server_handlers.cpp), and C25b's pause key `g_pauseMessageKey` 007930EC (ui.h; the
  message table and SetPauseTextDigit share it, so a resume shows "reserved_03" as with EI_Plugin).
- **MFC's empty string**: game.exe's cells
  00796E48 / 00796E4C (`_afxInitData` and its character) stay in image_cells.inc, unread.
- **Unread tables, no definition**: the 72 `.SEC` copies and the dead code's two tables (C18o's
  header_constant_copies.cpp / dead_code_tables.cpp) are gone; their cells are gen_image_cells.py's `UNREAD` groups
  (counted, left out of the cells like LEFT_STRUCTURES): zero in the image.

### Named data (C18)

The code still names game.exe's data by address (progress.py's "game data addresses", 3,028 when C18 began, 3,015
after its first batch, 3,000 after the second, 2,982 after C18c with the first vtable batch and C21 batch 2, 2,952 after C18d,
2,095 after C18j's unread objects and 2,068 after its particle and weather names, 1,859 after the app* objects of C18f, 1,798 after C18h's small areas, 1,733
after C18e's runtime classes and throws, 1,328 after C18m's reviewed unread objects, 1,276 after C18i's net and CRT state, 732 -> 728 after C18o's game tables,
which took the pointer cells from 1,522 to 1,243, 243 -> 234 after C18q's script keyword and CRT tables, pointer cells
1,243 -> 711; 247 -> 130 after the remaining objects batch, 2026-10-22; 121 -> 90 after C18s's console streams, 90 -> 56 after C18t's singletons with C25a's rows, 53 -> 41 after C18u's generated names, 41 -> 6 after C18r's MFC and CRT objects). C18 turns them into named C++
globals, one kind and one owner area per batch; the vtables are the compiler's. `python tools/named_data.py --split [KIND ...]` counts what is left by kind (import slot, vtable,
eh/rtti/runtime class, rdata constant, data object, bss object, other) and by owner area (the one hand-written file
that names the address, "several files", or "generated static init / exit only"), and lists the addresses of the
kinds given.
- **Blocks**: an address that looks like a lone object can sit inside a larger one the code reaches by its base: C18f's
  0079BCF0 and 0079BEC8 are GameOptions members (0079BC70 +0x80, +0x258), read as `options::g_options->...`, not named.
  A `char` buffer is `char g_x[256] = /*addr*/ {""};` (the tool packs char arrays as names), a function pointer
  `void* g_hook = /*addr*/ {nullptr};` cast at the call.
- **Global objects (C18n)**: `--check` refuses a named range inside a singleton the code reaches by field offset
  (config/global_objects.csv: address, size, name, class; g_serverWorld 007AFF60 +0x12E7C, g_clientWorld, g_gameFlow,
  g_interface, g_cursor, g_camera, g_sceneRenderer, ...) and says which object and field: C18i's 007C2CC4 is
  `g_serverWorld + 0x12D64 (CWorldServer::sessionKind_)`; the world also holds TheMissionScript 007C27D4 (commandsA_),
  007C29CC (commandsB_), TheSectorMap 007B01F4 (sectorMap_), the AI map 007B00D0 and the diplomacy 007C2BC4. Naming the
  whole object is allowed. `python tools/named_data.py --owner BEGIN [END]` asks the same of any address. The CSV's sizes
  come from `static_assert(sizeof(T) == N)` where the sources have one (checked against it); CWorldServer 0x12E7C,
  CCamera 0x308 and CSceneRenderer 0x5C have none and were read off the compiler. A new GamePointer<T>(addr) accessor in
  game_singletons.h must lie in a listed object. Before naming a lone address, `--owner` it.
- **C18h (the small areas)**: three kinds of reads went away. (1) Members of blocks read by their base: the logic
  settings 007C53F8 (`GameLogicSettings()`: guard / follow radii, the hit point curve's hpVal, skillVal, lost money /
  experience, durability, absorption / attack / defence per difficulty; 25 literals in unit*, combat*, player_server_handlers)
  and the options 0079BC70 (`options::g_options->...`: video / audio, texture quality, mipmapping, reverse stereo, flora
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
  in .data); the name of another named range its address; the name of a function of ours (`&` optional; any other
  name, the compiler checks it) some code of game.exe (the function it rewrites; since C20l no table holds its address); nullptr (or 0) a zero. The packed dword is then game.exe's pointer, so the byte comparison holds, and its
  offset is a pointer element: the only place a cell may sit inside the range. gen_image_cells leaves the cells inside
  named ranges out of image_cells.inc (the bytes are zero, nothing fixes them up) and its
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
  cells are gen_image_cells' UNREAD groups); so are the CRT's math function names 00797CC0 (C35h: only its error
  reporting, not ported, read them). Left: the CRT / MFC tables a static initialiser or the
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
    `__lc_time_c` 00797A60 (strftime / the time functions are the host's, lib/calendar.cpp), printf's "(null)" 0079804C /
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
  - .rdata tables of numbers or GUIDs (input_mouse: the DirectInput interface ids): as batch 1.
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
  - **MFC's CRuntimeClass records** (C18e named them; none since C14zi): every record's cells (class name, base
    record) are gen_image_cells.py's UNREAD, the classes being gone; the initialisers that linked a record into the
    module state's class list (AfxClassInit 00700674) are gen_static_init.py's UNUSED_INITIALISERS. Do not run
    gen_function_sizes.py for new rows of config/extra_function_sizes.csv: it rewrites every size from the Ghidra
    database, which has moved from the committed file; add the rows by hand in order.
    **The throws**: the device layer's GameError is this compiler's `throw` of a pointer to a class of the game's name
    (dx_errors.h: CGameException, CDXException). A failed file call throws `lib::FileError` (a std::runtime_error, by
    value) where MFC threw a CFileException*; no MFC exception class is left (C14zi).
    The compiler's ThrowInfo lists the pointer to the class and each base like the original's six records
    (0077F440, 0077F2B0, 0077EE80, 0077EE38, 0077EC28, 00759908), which nothing reads now: they and their catchable
    types stay in the image as dead data with their cells (ThrowInfo -> array 10 and array -> types 41 are counted
    over all of game.exe). STLport's out_of_range (thrown by value, 0074C640) is never thrown by our code (C14a removed the last
    throw): its records are dead data too.
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
    readers take `g_serverWorld->sessionKind_`, and 007C2CC4 stays with the world. The CRT's locale and multibyte state is the host
    CRT's "C" locale since C26 (nothing reads it since C35b), the old iostream's shared lock and ios count and ostream's virtual base table (C35d: console::Stream has none;
    crt_file's zero mode constants went with C35a). **Left**: 00783E34 / 00783E38
    (the send rates: 00783E34 is an addon.dll site, Rebirth's SetDefaultRate, which the image does not reproduce
    yet), the growing buffers 0079B8E0 / 0079B900 (game.exe's vtable: C19), 007B00BC (inside the server world
    007AFF60). The CRT's allocator state is named since C18r (below).
  - **The static initialisers' objects** (C18j). Most game data addresses left were named only by the generated
    static_init.cpp / static_exit.cpp. Two rules in tools/gen_static_init.py (each is a decision of the generator):
    - **Named ranges**: a store, float fold or fill into a range of config/named_data.csv is left out of
      static_init.cpp (its comment says "n left out"); the definition's initialiser
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
      kIdentityRotation 007A9C30, weather_particles' five scalars
      007AED98-007AEDA8 and its seed tables 0078DEA4 / 0078DF24, and weather_noise's five sway curves
      007ABB98-007AED98 (one type, WeatherNoiseCurve; the draw evaluates a segment through MotionCubic).
      C18l named the camera's pi / 2, zero vector and identity rotation (camera.h: 0079B2A4, 0079B5C8, 0079B5B8),
      the angle constants and unit vectors of ground_print
      (007C8998 / 007C899C), sound_manager (007C8808), terrain_* (007C8908, 007C8930, 007C8964, the wave scales
      007C8968-007C8970, 007C8978, 007C8988), player_client_handlers' fixed view rotation 007C8388, and the game's
      ISAAC generator 007A9D40 (a mutable zero-start global, `IsaacRandom g_random = /*007A9D40*/ {}`; the stores
      zero its first dword). A product or quotient of two named constants that the original computes at run time
      is `static_cast<float>` (check_fp_fold: sound_manager's 100 / (pi / 2)). Left as they are (then): 007AF3E8 and
      007C53C4 store the address of the log stream 007C5810 (named since C18s), 007C2F24 is a
      function-local guard (run-time state), 0079B2A0 (2 pi) and the folds that only game.exe's own code reads (the
      per-window pi / 2 and 1 / 1 copies in 007C7B70-007C8844) are no hand code's; sky_object 007AA890 /
      007AA8A0, the unit objects 007AF280 / 007AF284, the animation / object3d / map_object lifetime objects
      (007AF0E0-007AF110, 007AF1C0-007AF238) and torch_spec 007AF2D0 wait for their classes' conversions.
      gen_static_init reads a parenthesised negative literal in a fold; named_data takes a negative single number.
    - **Objects with exit functions** (C18k, the destructor rule): an initialiser that registers an exit function
      (`stores + atexit`, `constructor + atexit`) and the exit function are ported on the name; the game's atexit
      list keeps its order and its entries.
      - The definition is constant initialisation with no C++ destructor (`constinit`): a destructor of ours would
        run at our CRT's exit, not at the original's place in the game's atexit list, and could free into the game's
        heap after the game's CRT has gone. A container the stores zero is defined zero:
        `constinit lib::AlignedArray<TLVertex> g_vertices = /*007AEE00*/ {};`, `constinit std::vector<std::string>
        g_questFiles = /*007AFDD0*/ {};` (named_data's TYPES names a template, `lib::AlignedArray` / `std::vector`;
        a pointer type it does not list is a pointer element: `CDirectInputMouse* g_mouse = /*0079BA08*/ {nullptr};`).
      - The exit function: ported on the name, outside any anonymous namespace, with a row in
        config/static_init_ports.csv (function_sizes.inc already has it, gen_static_init wrote it there); the initialiser's atexit stays at its
        place in the list as `std::atexit(Name)` and gen_static_init leaves the ported function out of static_exit.cpp. It goes next to the definition
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
        access goes through the name or a member (`g_partyManager->`); a raw address inside the
        range is refused. Named: ui_objectives' last quest string 007C8178, the
        protection checks / driver / deferred-unit lists (static_lists.cpp), the spell / item / quest / print / speech
        databases (database_globals.cpp), the lever table, the unit and skill databases (unit_db_globals.cpp), the
        party manager 007AA5A0, saved parties 007C82A0, the spell footprint set 007C8478, the party id set 007C8370
        and the unread set 007AB8D8. C18k5 (1,717 -> 1,651 game data addresses) named the last four without a vtable: the options
        0079BC70 (options_global.cpp holds the definition so the vector replay links it; options_construct.cpp the
        initialiser), GameFlow 007AFE20 (world_game.cpp), CCursor 007C7BD8 (ui_cursor_setup.cpp) and CInterface3D 007C7D50
        (ui_small_members.cpp). Their raw reads became members: the options' 80 reads (camera speeds, shadows, prints,
        volumes, difficulty, paths, master server ...), the mouse position and drag motion (CCursor::position_ /
        motion_: 007C7C50 and 007C7C60 were lone ints before C18k5 named the cursor), the interface's window stack
        (007C7D84 was misnamed PeerList), close-all and sessions flags. The objects whose header only forward-declares
        their class (g_gameFlow, g_interface, g_cursor) are declared extern in game_singletons.h. **The harness** reads
        them through tests/verify/harness_objects.h (HarnessOptions / HarnessFlow / HarnessInterface / HarnessCursor: ours
        in the image, `ei::OriginalGameData` at the address in --no-hooks runs); the stats recorder reads the
        difficulty from game.exe's options the same way. Left: CMapDatabase 0079BF00, the renderer
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
        (world_client_view.cpp, 007AF508-007AF5A8: plain `std::vector<CEngineObject*>` 0x10 apart, the word after
        each no one's; the last one ends where the deferred unit list 007AF5B4 starts, so there is no 16-byte
        struct; their stores stay generated, the exit functions 0056A510-0056A830 were ported (plain globals since C50b); world.h's
        FrameObjects picks one from a table of the names; 0056A840, which clears them all, has no caller) and the
        CArrays whose tables C19 compiled: the sector spiral's lengths, radius tree and offsets (sector_spiral.cpp, a
        file of its own so the path vectors' replay links them) and the path smoother's node pool and 81 transition
        tables (ai_path_smooth.cpp). Since C50a they are plain globals of ours (the C++ runtime constructs and
        destroys them; no address): their initialisers (005BF810, 005BF840, 005BF8B0, 005C41C0, 005C4460) are
        gen_static_init's UNUSED_INITIALISERS; the builders 005BF8E0 / 005C42B0 still run at their places in the
        list. The path vectors keep their format:
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
      read through the object (`g_clientWorld->figureManager_`); the accessors that need world.h are out of line in
      world_singletons.cpp (game_singletons.h declares them; replay_path.cpp defines CurrentWorldTick for the path
      vectors). TheMatrixDevice was the scene pipeline (ScenePipeline()), 007AFDB0 the origin (worlddata::kOrigin),
      007C5488 / 007C548C the logic settings' considerModifier / considerLogBase. Check a lone address with
      `named_data.py --owner` and against the logic settings (007C53F8) and the options (0079BC70) first.
    - **Whole objects with a hand-ported initialiser**: the logic settings (g_logicSettings, its initialiser 005CDFB0;
      TYPES `LogicSettings`; the stats recorder reads game.exe's copy), and the constructed statics as
      `lib::StaticObject<T>` (zero storage, STATIC_OBJECT_BYTES) whose initialiser and exit function are ported on the
      name and registered at their addresses, each with a row in extra_function_sizes.csv / function_sizes.inc: the
      terrain's projection outcodes 007C88D0, the sector spiral's builder 005BF8E0. A function-local static's exit
      function is ported beside it and the port registers its own (`GameAtExit(DestroyX)`).
    - **Plain globals and locals instead (C50, C50a, C50b)**: an object of ours that needs no game order is a plain
      global (constructed and destroyed by the C++ runtime), a function-local static or a local, with no address: the
      unit shape cache (ai_movement.cpp), the AI events' pool (ai_events.cpp; the free list 007C53F0 stays named), the
      spells' footprint set, the sector spiral's tables and the smoother's pool and tables; the frame's draw lists and
      deferred units (world_client_view.cpp), the decal builder, the draw work arrays (particles, glow, fog, shadow,
      draw order), the lever table, the quest's objectives and name, the received messages' stream, the client's
      party id set, the zone table and the quest files / folders. Their initialisers are UNUSED_INITIALISERS (the exit
      functions go with them); a function whose port builds none of the original's function-local statics is in
      RETIRED_STATICS (Aggression's candidates, LabelRegions' flood lists, ReachesRegion's block list,
      StampWaterPatch's patch, SyncPlayer's diff arrays, UpdateUnits' id lists, the fog's dead cells, the draws' work
      arrays, UseResources' string). The spiral caches of PlanLongPath, FindFreeCellNear and WakeUnitsAround are
      function-local statics of ours (constant-initialised, no guard); so is UseResources' string. The objects the
      interface's files spell with `->` (g_gameFlow, g_savedParties, g_partyManager, g_saveStream) stay
      `lib::StaticObject` until those files are converted, but their classes construct and destroy themselves.
    - **gen_static_init**: REVIEWED_SPLIT splits one initialiser's stores where they are two objects (005229C0: the hit
      log pointer 007AF284 is read, the ln folds above it are not); RETIRED_STATICS lists functions whose port builds
      none of the original's function-local statics, so their runtime atexit sites never run (006BFF40, the terrain
      draw: its tile arrays 007C8848..007C88C8); a fold of ln in an unread object is `????????` in unread_data.csv.
      Winsock's exit function 0047C330 is ported without the wait (only dead code counts the threads 007A9774).
    - **Addon sites**: named_data.py's NAMED_ADDON_SITES lets a named range hold the site of a Rebirth patch disabled
      here (SetDefaultRate's 00783E34: net::g_remoteRate, a plain int a flag can write); addon_sites.inc still names
      it for the harness in game.exe.
    - **The console's streams and command maps (C18s, 121 -> 90)**: console_streams.cpp names the eight streambufs
      (`lib::StaticObject<ConsoleStreambuf>`, 0x454 bytes, 0x458 apart), the eight ostreams (`console::Stream` since C35d,
      a plain `constinit` object naming its streambuf; TYPES 'Stream' keeps the original's 0x58 bytes) and the two command maps (`lib::StaticObject<CommandMap>`), with their 18 initialisers
      (005DC050 / 005DC120, 005DC1E0-005DC3F0, 005DC420-005DC5E0) and 18 exit functions ported on the names and
      registered (rows in extra_function_sizes.csv / function_sizes.inc). The log pointers are `constexpr` pointers whose
      initialiser names the stream (`{&console::g_aiLogOut}`: named_data takes a qualified name of a range): unit_hits'
      kHitLog 007AF284, ai_alarm's kAlarmLog 007C2EB8, combat's kCombatLog 007AF3E8, unit_ai's kAiLog 007C53C4 and
      kModeLog 007C54E8; the files write to them as `console::Stream&`.
    - **The singletons and the small leftovers (C18t, 90 -> 61; 56 with C25a)**: the worlds (g_serverWorld 007AFF60, g_clientWorld
      007AF5B8, world_construct.cpp), the camera 0079B2A8, the scene renderer 007AF060 and the device lock 007AEE78
      (renderer_lifetime.cpp), the map database 0079BF00, the class registry
      0079B270 (chunk_archive.cpp), clip's work words 0079B668, the network buffers 0079B8E0 / 0079B900, the save stream
      007AFDF8 and the received messages 007C83D0 (with its destroyed flag 007C83C8) are `lib::StaticObject` with their
      initialisers and exit functions ported next to them (atexit_400.cpp is gone); game_singletons.h's accessors are
      out of line. Plain globals: the main window 0079BC28 (app::g_mainWindow; render_game.h's Addresses hold
      pointers to it and the renderer), the loading
      movie's 007C5520 / 007C554C-54, the network buffer flags 0079B8FC, the damping scale 007C2F8C (its initialiser
      005B2FB0, ln 0.6 on the x87, ported in ai_refresh.cpp). The harness reads game.exe's copies through
      harness_objects.h (`ei::Standalone() ? ours : &ei::OriginalGameData<T>(address)`), and in a --no-hooks run calls
      game.exe's own PickAt2 / UnitAt / GameSendLeave04 / GameSetConstructionVariable (EI_ORIGINAL), which read them.
    - **Names in the generated code (C18u, 53 -> 41)**: gen_static_init.py's GENERATED_NAMES maps an address to a
      name and its header, and the generator writes the name where the original's code has the address: a guard's
      flag byte (`Once(name, bit)`), the object a call takes as `this` (`Call(&Class::Method, &name)`) and where a
      call's result goes (a store of the result, kept in list order; named_data.py compares that definition at its
      zero start instead of refusing the unknown store). Only the guard is generated now (static_init.h's `Once` takes
      a `std::uint8_t&`; the generator stops at an operation static_init.h lacks). Each entry must start a range of named_data.csv (the
      generator says so otherwise). Named this way: the function-local statics' guard 007CAEE4
      (`lib::g_emptyStaticGuard`, static_object.h: 175 generated tests and CSysInfo's). MFC's globals that only the
      initialisers and exit functions reached (_simpleUserException 007C8BC0, the thread / process locals 007C90BC,
      007C90C0, 007CAA84, 007CAA88, 007CAA8C, _afxDBCS 007CAA98) went in C14zi: their initialisers (and the locals'
      atexits) are UNUSED_INITIALISERS. C18r had added the objects hand code reads too, whose initialisers are
      plain library calls: the temporary objects' fixed allocators (g_tempDCAlloc 007C8D18 and g_tempGdiObjectAlloc
      007C8CF0 went in C14ze with the GDI classes, guard byte 007C8BB8 too; the temporary windows' 007CA940 and menus' 007CAAA8 went in C20o: their classes were
      never made, tools/gen_static_init.py's UNUSED_INITIALISERS leaves both initialisers and exit functions out). MFC's
      four static windows (wndTop 007CAA28, wndBottom 007CA9E8, wndTopMost 007CA9A8, wndNoTopMost 007CA968, guard byte
      007CAA24) were CWnds nothing read: C14zh deleted the class and listed their four initialisers (00704410,
      0070444E, 0070448C, 007044CA) in UNUSED_INITIALISERS.
    - **The MFC and CRT objects (C18r, 41 -> 6)**: the CRT's allocator state (STLport's node allocator, free lists 0079B224, has no port: our objects are the host's `new`); `_pnhHeap` (007CAAD8) is gone
      (nothing called the handler it held, C35e); `_afxNewHandler` 00796E14 went
      with MFC's new handler loop (C14ze, C35g: our allocations are the host's `new`). MFC's (hand-ported until C14zi: the resource / memory / not-supported exceptions 007C8C58 /
      007C8DE8 / 007C8D50, afxData 007C90C8, the global locks 007C8E80..007C907C, _afxThreadData 007C9084, the halftone
      brush 007C8D40, the DragList message id 007CAA64) are gone: their initialisers are UNUSED_INITIALISERS (each the
      compiler's `call A; jmp B`, 10 bytes: rows in extra_function_sizes.csv).
      printf's power tables and the x87 functions' constants were already ours (crt_format.cpp, crt_x87.cpp).
    - **The empty string (C18, 6 -> 5)**: 0079B298 is game_strings.h's `kEmptyText` (`char[1]`, `game::EmptyText()`
      returns it). Its readers copy it (CString / STLport assignments) or compare its text (mbscmp, memcmp); none compares
      the pointer. The only data cells that point at it are the units' model folders 0078F7EC / 0078F824, a table only
      the dead 00532650 reads (gen_image_cells' UNREAD: zero in the image), so an UNREAD cell may
      point into a named range. The seams that took an address and had no address caller left went with it:
      `game::Text` and the camera's and the transform maths' `GameFloat(std::uintptr_t)`.
    - **Left**: the section bounds in game_data.h and image_data.cpp (0073B000, 0073B820, 00783000, 007CD000, 007D7000):
      the address image's layout, not data. They go when the image no longer keeps game.exe's sections at their
      addresses (the generated static initialisers, image cells and import slots still reach the range by address).
  - Import slots (gameapi.h 406, sound* 36, crt* 20, bink 10): not data to name; they go with direct Win32 calls.

### Compiler-made vtables (C19)

Every class of ours with virtual functions is a real C++ class: the compiler builds its vtable and no code of ours
stores game.exe's table address. game.exe's tables stay in the image as data of the original (generated from game.exe
at build, "Own data"). config/vtables.csv (tools/find_vtables.py; 608 rows: vtable address, slot count, RTTI name, kind,
slot 0, class, file, note) lists them, and every row is retired: its class and declaring file (`-` for a table with no
class of ours) are in the csv, gen_image_cells.py moves the table's slots out of the code cells (group
`vtable-retired`) and writes their bytes zero in the image, `python tools/named_data.py --check` proves no address
inside a retired table is named in src/ or tests/, and `python tools/gen_image_cells.py --check` that each row is a
vtable of the csv with its slot count and a class of the file it names (a find_vtables.py rerun's new row needs a
decision). `python tools/named_data.py --split vtable` lists each table with the files that name it.

- **Not vtables**: strings whose dword falls in .text: "%ld" 0074676C, "dde" 0074718C, "a/p" 007474B8 (each a false
  Ghidra function, library.csv's "not a function" rows 00646C25 / 00656464 / 00702F61), "OCP" / "ACP" 00747E2C / 30
  (inside a function), "ime" 00746E60 (dead_code.csv; a NOT_CODE cell of gen_image_cells.py), and the EH unwind entries
  (kind `eh`: 0074E824, 00753B04, 00758904, 00758B0C, 00758B14). Never retire them (zeroing them breaks the strings).
  find_vtables.py takes a dword as a slot only when it is a game, library or thunk function start less those rows
  (and skips a candidate whose slot 0 is an EH funclet), so a full rerun reproduces config/vtables.csv.
  `python tools/find_vtables.py --help` runs the tool and rewrites the csv: restore it with git.
- **Tails in config/vtables.csv**: find_vtables.py splits a table at every address the code holds as an operand, so
  some rows are the tail of the row before them: split off at a devirtualised slot call (`call [0073BD98]` is
  CMemoryStream's slot 2, 0073BD90's 8 slots listed as 2 + 6) or at a byte match inside an instruction (0073E850,
  0073E6E8, 007435D8, ...). A tail is never stored; retiring it zeroes live slots (retiring 0073BD98 alone made every
  run crash calling a null slot at startup). gen_image_cells.py's check refuses a retired row that no code of game.exe
  stores (`stored_vtable`: `mov r/m32, imm32` or `mov r32, imm32`) or that shares a slot with a row still in use. A
  family's retirement takes its tails with it (0073E954's 0073E98C). A table ends after its first slot when the next
  address is one the code stores (a vtable of its own); an address the code only calls through splits only after the
  second slot.
- **Tables with no class of ours** (file `-`): only game.exe's own constructors and destructors store them, and no code
  of ours calls those: the exception classes (CResourceException, CUserException, CMemoryException, CArchiveException,
  CFileException, std::exception, STLport's length_error 0073B984 and out_of_range), the MFC application, window,
  thread, GDI, DC, handle-map and state classes (CWinApp, CWnd, CCmdTarget, CDC, CGdiObject, CHandleMap, _AFX_*_STATE,
  ...), the old iostream classes, 0073B98C, 0073BFAC, 0073D860 / 0073D888, 0074019C and 00740244 (small bases and
  locals of game.exe's inlined code) and the dead code's tables. Our code does the same work with other classes: the
  game's files are `lib::File`, `lib::TextFile` and `lib::FileFind` (src/lib/file_io.h; a failed call throws
  `lib::FileError`), its fonts, pens and brushes `lib/gdi.h`'s, its threads `lib::StartThread`. 0073B984's ~140 readers
  compare a hash table's prime pointer with the end of `__stl_prime_list` (0073B914 + 28 dwords), the same address: no
  read of the table. type_info 007472B0 (game.exe's CRT): the 74 RTTI type descriptors' vtable field points at it;
  nothing in the image calls a descriptor's virtual (the frame handlers compare descriptors by address and name), so
  those cells may point at the zero table. Code that reaches game.exe's unported function reports "no code at
  <address>" through `ei::ReportNoCode` (ei/hook.h).
- **Ids for the harness**: the harness names objects by game.exe's vtable (window labels in goldens: the census's
  `W00740688#3`, `waitwindow`, the digest's AI modes). A converted class needs an id that both game.exe runs (golden
  recording) and the image give the same: the harness maps a class to game.exe's address (`ei::OriginalGameData`):
  harness_digest.cpp's `ModeKinds()` for the AI modes, `WindowKind` (a window by its class's `typeid`) with
  `KnownWindows()` / `WindowReady` naming screens by game.exe's main table. When the label text changes, re-record with
  `--no-hooks --golden --fresh`. Slots the harness patches (`PatchSlot`, harness_savetrace) need a hook in our code
  instead (the console's line observer `console::g_consoleLineObserver`, the memory stream's
  `g_memoryStreamWriteObserver`).
- **Slot conventions**: a class declares every slot of game.exe's table in its order (slot indexes keep their meaning
  for the harness and for callers that pass a slot number), the slots game.exe fills with `_purecall` are pure, and a
  slot whose original leaves EAX unset returns 0 or is void only where nothing reads the result. A base is not made
  final where the original calls through the table (a devirtualised call let the client world's slot 29 inline slot
  27's float body: the effects' bow height moved).
- **Traps**: a class constant left in a derived class compiles against the base class's (the item tags once stored
  CEngineObject's table, every run hit `_purecall` at startup): remove a constant by declaring a poison member first.
  Member pointers of classes with several bases are two words: `ei::MemberAddress` takes the code word (adjustment 0).

### Real classes (C28)

Classes are ordinary C++: the constructor body is the C++ constructor, `Destruct` / `DestructBody` the virtual
destructor, the slot bodies the virtual overrides (base calls qualified), class names and ids `ClassName` /
`GetClassId` overrides. How a family converts (C28a, the items):
- The destructor calls its own Destroy qualified (`CItem::Destroy()`), as the original's does.
- Allocation: `new T()`. CEngineObject has the game heap's `operator new` (noexcept: null without memory, then no
  constructor runs, which the original's callers test) and `operator delete`, plus the placement pair; a derived
  class declares no `operator delete` of its own (it would hide the placement delete: warning C4291). An object built
  in storage someone else allocates is `new (place) X()` and `place->~X()`; one embedded in an owner whose own
  Construct / Destruct bodies build it is a `lib::InPlace<X>` member (PartyStorage, party_storage.h); a local is a
  plain `X x;`.
- A constructor or destructor whose address is still called (an EI_ORIGINAL forwarder, C29) keeps a registered
  `__fastcall` shim that calls it (party_lifetime.cpp's DestroyPartyInPlace at 006611D0).
- check_signatures accepts a destructor named by an EI_RECORD whose original pops 4 more (the scalar deleting
  destructor's flags argument).
- Members must be trivially default-constructible or initialised in the constructor's list as the original does: a
  constructor default-initialises what it does not name.

The window base (ui.h): `UiInputSide` (007401D0: the input slots 1-12 as virtuals, `handled_`) and
`CUiWindow : CUiWindowBase, UiInputSide` (CUiWindowBase is CTransformObject plus the 0x40 bytes before +0x94, so
the input side lands at +0x94 and the compiler's adjustor thunks are game.exe's SUB ECX,0x94; JMP) with slots 18-24
as virtuals; `CUiContainer` adds 25-41 (25 / 26 `_purecall`) and its input dispatch as the input-side overrides
(screens call the defaults qualified, `CUiContainer::LeftDown(...)`). The screens (CBattleScreen ... CMpHeroScreen,
the item screens over `CItemScreen`'s slots 42-54) and the item panels (`CUiItemPanel`'s slot 25 `ItemAt`; the
strips `CUiItemStrip` (26 `RebuildList`, pure), `CForwardingStrip` and `CFigureStrip`) are ordinary classes built with
`new X(...)`, their slots the overrides (an input slot that runs a named handler is a one-line override,
`int LeftDown(...) override { return OnLButtonDown(...), 0; }`).

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
- **Rebirth**: RunStartupActions calls `rebirth_hook::SettingsOnly`: flags from ei_rebirth.ini and EI_REBIRTH,
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
  the game's ANSI registry call gives (UTF-8). Names and paths compare without case.
- **Import**: when the file is missing, the first use copies `Software\Nival Interactive\EvilIslands` and Rebirth's
  `Software\Gipat.Ru\EI_Starter` (every subkey; read only, through text::utf8 as the game's reads) and writes the
  file. Nothing is merged later: the file is the settings from then on.
- **Who uses it**: CWinRegistry (win_registry.cpp: the options loader and writer; `Open` makes the file's key,
  `Close` writes the file when a value changed, atomically through `ei_settings.ini.tmp`) and the direct readers
  through `settings_file::ReadValue` (console_addon's AutoExec lines, mod_folders, script_addon_commands' AddonPath,
  ui_widescreen, world_client_draw's draw radii, device_object's `Standalone` "PerfOverlay": the performance
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

- **Short functions** (S2): the game functions under five bytes needed no code of their own once nothing called game.exe;
  the exit function that does nothing is not registered at all (C35e: src/game/short_functions.cpp and its `Ret` went; the
  generator drops the initialisers' atexit of it). tools/gen_short_functions.py, which wrote one map line per short function
  (12 kinds, each compiling to the original's bytes), went with the map (C20l).
- **Static initialisers** (S3): src/standalone/static_init.cpp by `tools/gen_static_init.py`. A small symbolic
  interpreter decodes each initialiser into stores, copies, fills, float folds (`Log` for FLDLN2 / FYL2X; .rdata
  operands as tagged literals, S18e1), calls (cdecl, stdcall, thiscall, through an import slot), `atexit` and the
  once-only guard bit; a single-use decodable
  callee is inlined, everything else is a call. The result is one function, `RunStaticInitialisers` (plain calls,
  `std::atexit`, `Once`, `Local`; static_init.h), run by the image before WinMain. Inside the script the statements
  are written `d.Operation(...)` and `plain` turns them into that code at the end (a call without arguments is the
  call itself).
- **Calls by name** (C20 batch 2): a call or atexit of a function ported by hand (a row of
  config/static_init_ports.csv: address, qualified name, the file of the definition; C20l) is rendered by its name: `app::ConstructCameraFile();`, `Call(&Class::Method, &g_object, args)`, `std::atexit(Exit0070C0B8)` (gen_static_init's `Names`,
  `name_calls`). A call with arguments goes through static_init.h's `Call`, which converts the decoded dwords to the
  declared parameters (`Argument`: a pointer from a dword). The name is the
  definition's (its namespaces from srcscan.Scopes); a header that declares it in the same namespace is included,
  otherwise a `void __cdecl Fn()` initialiser or exit function is declared at the top of the generated file (the
  linker catches a signature that differs: MSVC's names hold the return type and convention). A method is
  `&Class::Method` of the class a header declares. A function in an anonymous namespace has no name another file can
  reach, and a function with no port none at all: the generator stops with a problem (move it out of the namespace).
  Batch 2 moved 92 out of their anonymous namespaces (right after the block, same enclosing namespaces), batch 3
  the last 28 (console_streams 18, script_exec_helpers 2, and the files of the old MFC string and collections): nothing is
  called by address any more, so Targets / ReportMissingTargets, Code's Cdecl / Stdcall / Thiscall / Import and
  AtExit by address went. The statements stay in `d.` address form until render, so unread_objects, stored_values and
  named_data.py read them unchanged (the `d.Begin` labels and the Do& template, Code and Run went with C20q1).
  When a helper gets ported later the output changes: rerun the generator (check_static_init, removed in
  C25d, re-decoded game.exe and compared).
- **Exit functions** (S3a, S16a): src/standalone/static_exit.cpp, same tool: 130 whole-function patterns
  (`EXIT_PATTERNS`: `mov ecx, obj; jmp dtor`, flag-guarded, operator delete of a counted buffer, a counted
  `Release`, DeleteCriticalSection, the eh vector destructor iterator, the WSACleanup wait, `ret`) plus 60 run-time
  atexit registrations of function-local statics (PUSH fn, CALL 006E727B within 14 instructions). Their sizes go
  into config/extra_function_sizes.csv and function_sizes.inc (the generator writes them, the check compares).
- **Compiler jump thunks** (functions.csv kind "thunk"): our callers call the target by name.
- **Dispatch-only functions** (`MOV EAX,[ECX]; JMP [EAX+n]`: 00675D40, 00469E00, 00469DE0, 00552F30): the
  callers call the virtual.

### Registration rules

- **No registrations** (C20 batch 6): nothing of ours stands at a game.exe code address, and the image has no code
  table. `EI_REPLACE` / `_METHOD` / `_SHORT` / `_STANDALONE`, the `.eireg` section, CodeAddress (its table, stubs and
  SetCodeTable), HasCode, CheckCode, check_thunks, registration_readers and standalone_gaps are gone. How the
  readers of game.exe's code addresses went (C20 batch 1's measurement, then one batch each): the code cells (825,
  never read: zero since batch 1), the static initialisers' calls and atexits (by name since batches 2-3), the code
  literals of src/ (handler tables, callbacks, factories: `&Fn`, C20f), the harness's image side (C20h), the short
  functions (EI_REWRITTEN, C20e), WinMain (batch 6: EiStandaloneMain calls `app::GameMain`). Registrations: 9,347
  before C20c, 3,527 after C20d, 809 before C20 batch 1, 72 after C20e, 1 after batch 3, 0 after batch 6.
- **No function-to-original map** (C20l): the `EI_REWRITTEN` lines (C20d-C20k: {address, &Fn} in the DLL's `.eiorg`
  section, one per address by check_registrations) went with their last readers. What still needs a game address
  of a function of ours has it where it is used: an EI_RECORD names the rewrite its replays call (check_signatures
  checks its stack arguments), gen_static_init reads config/static_init_ports.csv. The trace tools (coverage, save_trace, ui_census_report, named_data's --emit / --refs) no longer map
  game.exe's functions to ours; progress / the dashboard dropped the port counts.
- **game.exe's side**: the vector recorders (`EI_RECORD`, tests/vectors/record_*.cpp) are the only hooks;
  InstallHooks installs them in game.exe with EI_VECTORS=1, leaving out (and counting as unexpected) a second one at
  an address or one whose stolen bytes differ from the file. check_stolen.py checks each against game.exe: a known
  function start, one per address, whole instructions with no relative operand, no jump back into the stolen bytes.
- **Function sizes** (src/hook/function_sizes.inc from Ghidra, config/extra_function_sizes.csv for what Ghidra lacks
  or cuts short) are the tools' (check_stolen, check_signatures, coverage, gen_static_init, find_dead_code); the image has none since batch 6. Some Ghidra sizes run past the RET (0070CAD2,
  006E6B7D: decoded sizes).
- config/library.csv names are often wrong for STLport / MFC rows (destructors named clear, AddTail named a
  constructor, 006EC8A8 `_mbsupr` lowers, 005E0550 is CTime::operator-). Read the code; the csv is add-only. Unquoted
  commas in names broke rows once (0055C730, 00561B00: now quoted).
- Library bodies live in src/lib; game-element instantiations in src/game (game_arrays, game_lists, game_slots,
  record_*, library_*). Callers call them by name (libcrt::Hypot ...; no forwarders since C29), and
  C14a-d replace the containers with standard ones.

### The CRT (src/lib/crt*.cpp, namespace `libcrt`; S4, S4a-S4d, S16, S16a, SV7c2)

- **Locale** (C26, C34, C35b): the host CRT's "C" locale, never changed. The original's text manager Create 004DED20
  called setlocale(LC_CTYPE, the "Font CharSet" option, "english" -> "English_United States.1252") and
  `_setmbcp(_MB_CP_LOCALE)`; ours does neither (the text is UTF-8, section 4, U5), and the standalone's start-up sets
  the host's `_MB_CP_SBCS` (no lead bytes). Nothing reads the locale state: crt_format.cpp's Format takes the C
  locale's decimal point '.', no lead bytes in a format and a wide character's low byte (wctomb's "C" branch); MFC's
  resource strings are copied as bytes. ei_vectors checks the game's recorded point and class words against that
  ("@CrtFormatLocale": the point is '.', no lead-byte bit, the digits of ASCII are '0'-'9').
- **Strings and characters** (C26, C34, C35b): no CRT function of ours. Callers use std::strcmp / strncmp / strncpy
  (_mbscmp, _mbsncmp, strcmp, strncpy), `str::Compare` for std::string / string_view texts (strcmp's order up to
  the first NUL, what _mbscmp read of a CString), and text_case.h: `CompareIgnoringCase` (_stricmp, _strnicmp, _memicmp,
  _mbsicmp by the UTF-8 table), `LowerInPlace` (_mbslwr), `ToLower` / `ToUpper` (ASCII) and `IsDigit` / `IsSpace` /
  `IsAlpha` (no class past ASCII; a negative char has none). _mbsinc is p + 1; _ismbblead and isleadbyte answered 0,
  so the double-byte branches went (the console's tokens, the text reader's characters, the word wrap, the chat
  overlay, _output's format bytes, MFC's path walks, str's trims). str.h's searches and trims
  (C35i) are std::strchr / strstr / strrchr / strpbrk and text::IsSpace where CString called _mbschr / _mbsstr /
  _mbsrchr / _mbspbrk / _ismbcspace: with no lead bytes those are the byte functions (strchr takes the char's byte,
  also of _tcschr's sign-extended char) and ASCII's white space (a negative char has none). Checked old against new
  (C35i, the scratch mbs_equiv.cpp, not checked in: the host's _mbs* under the standalone's _MB_CP_SBCS and the byte
  functions side by side) on every char against texts of every byte and 2M random texts, sets and needles of ASCII
  and high bytes: 4M answers, all the same. memmove, memcpy, memset, wcslen, atoi, _itoa and _splitpath (the last
  user of the start-up's _MB_CP_SBCS) are the host's, called by their own names. Both suites goldens the same,
  though the host's compares return a difference rather than VC6's
  -1 / 1, atoi saturates where VC6's wraps, and a negative char's class is 0 rather than the words before `_pctype`.
  ei_vectors: the script lexer and loader replays call text_case.h too; none of their 772 records has a byte past ASCII,
  so the game's 1251 / 1252 answers were never needed: the records carry no per-call class answers (C35j) and
  "@GameCrtCtype" holds the game's isdigit, isspace and tolower for the ASCII bytes only, which text_case.h answers the same.
- **Heap** (crt.cpp): malloc 006EABD1 and free 006EA8AD are the host CRT's (C26: both suites goldens the same, the
  image suite twice; until then a VC6-style Win32 heap of their own, `_crtheap` 007CAF08, sizes rounded up to 16,
  through the image's import slots, SV7c2). realloc, calloc, _msize and _strdup had no callers. The harness's
  repeatable heap, jitter, fill and trace (tests/verify/harness_heap.h) act on game.exe only now: the image's blocks
  are the host heap's (its low-fragmentation front end), so a golden of the image already holds under other
  addresses in every run. Our objects are the host's `new` / `delete` (C35g): MFC's handler loop
  (`_afxNewHandler` 00796E14, initial value `AfxNewHandler` 0070058F, which threw the static CMemoryException)
  is gone, so an exhausted heap throws std::bad_alloc where the game threw a CMemoryException.
  The STLport node allocator went with the last stlp container (C14t).
- **atexit / exit**: the host CRT's (C26: image goldens the same): atexit 006E727B / _onexit 006E71FD register with
  `_crt_atexit` (the function converted to its code when registered), exit 006E8DA7 is std::exit, which runs that
  table before the host's terminators.
- **rand / srand** (crt.cpp, kept): `libcrt::Rand` is VC6's LCG (seed * 214013 + 2531011, bits 16-30) on a
  thread_local seed (`libcrt::RandState()`; the image has no _getptd 006F00D1), `libcrt::Srand` sets it. It stays
  because the game's draws are the game's: the host's rand is another generator, every golden that draws (AI
  choices, damage, the ambience) would differ, and the harness's digest ("crt.rand") and random trace (the
  observer: each draw and its caller) compare our draws with game.exe's one by one.
- **qsort** (crt.cpp `libcrt::Qsort`, kept for two callers): VC6's quicksort (the middle element as the pivot,
  selection sort up to 8, the smaller part first) leaves elements that compare equal in an order of its own; C26's
  host qsort changed 105 image goldens. Where no two elements compare equal and differ, every correct sort gives
  the same order, so those callers are std::sort (C35h): SetSun's lit sectors by (sy, sx) (one per grid cell),
  the script groups by id (the live objects' ids are unique; copies of one reference are equal), and the fake unit
  scene's corner records and pairs (dead code: equal records hold the same fields the sort's reader takes). Qsort
  stays for the sector spiral's cells by distance (BuildSectorSpiralAt: many cells share a distance and the order
  among them is the walk order) and the ambience's class votes by weight (two classes can weigh the same; the first
  three heard are taken in that order).
- **Formatting** (crt_format.cpp, C35c): one printf of ours, `libcrt::Format` (the full text) behind `str::Format` /
  `str::FormatV` (CString::Format's: the text up to the first zero byte); sprintf 006E8715, vsprintf 006ECC47,
  fprintf 006EAEB0 and printf 006E955F have no entry of their own (gamecrt::sprintf and libcrt::Vsprintf went in C35c:
  a call with a literal format and no float conversion is `std::snprintf` into its buffer, the console's "%.6g" and
  the script's FloatToStr "%.*f" are str::Format). The format is read with VC6 `_output`'s state machine (the
  `__lookuptable` copied from 007476D0), so a malformed conversion prints its characters as VC6 did and the host never
  sees one (the UCRT's invalid-parameter handler would end the game on a run-time format from the game's texts).
  Each integer, character and string conversion is the host's `snprintf` of the same flags, width and precision, with
  VC6's sizes spelled for the host ('l' and "ll" are 32 bits, "I64" 64; %C / %S wide unless 'h'; %Z narrow unless
  'w', the host's bare %Z is wide; %p as "%.8X"; an integer's precision stops at 511); %n is ours (the UCRT refuses
  it). Floats are VC6's own: not "17 correct digits": `$I10_OUTPUT` builds them from a 12-byte long double (96-bit
  `__ld12mul` with truncated partial products, the exe's power-of-ten tables 0079AE60 / 0079AFC0), then `_fptostr`
  rounds half up (the UCRT rounds exact ties to even: "%.2f" of 0.125); three exponent digits; "1#INF" / "1#IND" /
  "1#QNAN" / "1#SNAN" ("%.2f" of infinity is "1.#J"); -0 prints as 0; the sign and padding as `_output` writes them.
  The decimal point is '.'. C26 tried the host's printf for everything: the crt vectors differ (3 of 258 records: "%g"
  / "%.6g" of FLT_MAX "3.40282e+38" for VC6's "3.40282e+038", infinity "inf" for "1.#INF") and so do six image
  goldens (combat, combat_more, sound, unit_ai_cast, unit_ai_corpse, unit_ai_heal: the console's
  "skill=3.40282e+038" attack line), so the float side stays. Checked old against new (C35c, the scratch equiv.cpp,
  not checked in: main's whole `_output` and the new Format side by side on 2.2M random formats of flags, widths,
  precisions, `*`, sizes, types and broken conversions, with random, small and pointer arguments): the same bytes, count
  and %n stores. Known differences, no format of ours or of the game's texts has them: wide text with a character
  above 0xFF (no byte in the C locale) writes nothing for the conversion where VC6 wrote the padding and the
  characters before it; %B (no conversion) writes nothing where VC6 wrote the previous conversion's text again.
  atof and sscanf are the host CRT's (C26: goldens and the atof vectors the same); callers use std::atof / sscanf_s.
- **Files** (C35a: no CRT copy left): every open goes through the game's import slots, so the harness's save /
  install redirection and a mod's hooks apply. `lib::OpenFile` (src/lib/file_io.h; fopen's r / w / a, +, b / t) opens
  with CreateFileA and hands the handle to the host as a descriptor and `std::FILE*` (`_open_osfhandle`, `_fdopen`);
  the callers (sysmsg.txt, BufferedFile, lib::TextFile) use std::fread / fseek / ftell / fwrite / fclose / fgets on
  it. The game's CFile, CStdioFile and CFileFind uses are file_io.h's `lib::File` (a Win32 handle from CreateFileA,
  MFC's open flags), `lib::TextFile` (OpenFile's "r" / "w", MFC's ReadString line pieces) and `lib::FileFind`
  (FindFirstFileA / FindNextFileA, MFC's read-ahead: FindNext is false on the last entry; the paths are the
  pattern's folder through GetFullPathNameA, as CFileFind's root).
  The res archives (CMappedResource) keep a Win32 handle: CreateFileA, SetFilePointer, ReadFile, WriteFile through
  the slots, the calls VC6's _sopen / _lseek / _read / _write made on its binary descriptor. _access is
  `lib::FileExists` (GetFileAttributesA; every caller asked mode 0 or 4), _findfirst the resource stream's own
  FindFirstFileA (now closed). Dropped with
  VC6's _sopen: the text-mode update open's trailing Ctrl-Z cut and the read-only attribute of a created file (no
  caller asked either). Fact from S4b1: a truncating open of a just-written file, then a write past its end, can
  read the gap back as old bytes on this machine under load (plain CreateFileA too).
- **x87 maths** (crt_x87.cpp, kept; the one file in src/lib check_cleanup lets hold `__asm`: Hypot's NaN addition):
  the bodies of _CIacos / _CIasin / _CIpow and their stack forms 006E7334 / 006E83D4 (`libcrt::CiAcos`, `CiAsin`,
  `CiPow`) and _hypot 006FD026 (`libcrt::Hypot`), called by name (no register entries). The CRT's control word keeps
  the caller's precision and rounds to nearest, so the port computes at 53 bits and rounds each step to the caller's
  precision; pow splits y log2(x) with fma errors, ties-to-even, exp2, ldexp; `_hypothlp` runs at 64 bits with every
  step stored to a double and a denormal result cut off. errno / _matherr are not reproduced (the CRT's math function
  names 00797CC0, which only its error reporting read, are gen_image_cells' UNREAD cells). Why each stays (C35h, the
  scratch maths_equiv.cpp, not checked in: crt_x87.cpp against the host's on 5M float-valued arguments each):
  - acos / asin / pow: the game's main thread runs at 24 bits (control word 007F) and the CRT's steps round there,
    the host's are 53-bit results. At 007F, rounded to a float, pow differs in 4.1% of the arguments, acos 7.8%, asin
    18.8%; at 027F (53 bits) in a double's bits 62% / 6.9% / 16%. C26 tried the host's for the maths: acos / asin /
    pow / _hypot (with fmod, modf and _controlfp) changed 14 image goldens (#vitals of the hero in effects_bow,
    net_pair_quest, rebirth_fixes_ui, sound, spells_hero, ui_camp_make, ui_gallery_camp, ui_gallery_deals, units,
    whose console prints "44.8744" for "44.8745": unit_vitals' CiPow; #unit of the base's units in
    terrain_water_waves, ui_base_dialogue, ui_base_topic, ui_gallery_base, zone_base_bz1g).
  - _hypot: std::hypot (the UCRT's, SSE2 doubles) differs from the port in a double's last bit for 11.7% of the
    arguments (never once rounded to a float), and costs 3.5 times as much (about 270 ns a call against 80, the
    scratch hypot_time.cpp: the UCRT's _hypot sets and restores the control word around each call, the cost SV10i
    took out of the camera's ClampAboveObjects: Hypot was 13.5% of the town's main thread then). With the callers on
    std::hypot the image suite's goldens were the same (121, C35h), so _hypot was not among C26's 14; kept for the
    cost and the bits.
  - fmod (_CIfmod 006E95AA, fmod 006E95A0), modf 006EAD0B, floor and ceil alone kept every golden and are the host's
    (std::fmod and so on).
  - _controlfp 006EBBB4 has no port of its own (C20r): its one caller, `SectorData::Light`, sets chop rounding on the x87
    word and restores it (SV10k below).
  - `libcrt::Ftol` (crt.h, inline, 146 callers) is no CRT code: _ftol's answer in C++, the low 32 bits of the 64-bit
    truncation (the compiler's int64 conversion). A cast to int differs past int's range (MSVC's gives 0x80000000
    where _ftol gives the low bits: 3e9 is 3000000000 as unsigned), and nothing bounds the callers' values (money,
    damage, the clock's elapsed milliseconds); ours equals _ftol 006E853C there and gives 0 for NaN / infinity /
    |x| >= 2^63.
  The control word (SV10i): FNSTCW, and FLDCW only when the wanted word differs (x87::ControlWord / SetControlWord),
  put back only then; frexp / ldexp of normal doubles by the bits (std::frexp / std::ldexp save, set and restore the
  word on every call: the UCRT's `_ctrlfp`, 9% of the town's main thread through Hypot). The FPU keeps bits 0-5 and
  8-12 (bit 6 reads 1, 7 and 13-15 read 0), the same bits __control87_2's abstract form round-trips, so the words are
  the same. (check_x87 is the other x87 rule: unsigned to float only through x87::FromUnsigned.) Checked old against new (build/SV10i/equiv.cpp, not checked in: main's
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
- **_controlfp without the UCRT (SV10k, C20r)**: game.exe's one call outside the CRT, terrain lighting's
  `_controlfp(_RC_CHOP, _MCW_RC)` and the restore around `SectorData::Light`, is FNSTCW (`x87::ControlWord`), the
  rounding bits set to chop and FLDCW (`x87::SetControlWord`) when that changes the word, then the saved word loaded
  back. The original converts the word to the CRT's abstract form and back (006EBBB4 / 006EBC2A / 006EBCBC); the round
  trip keeps every bit the FPU keeps except a reserved precision 01 (it becomes 64 bits), which nothing sets. The
  UCRT's `__control87_2` cost 0.2-0.4% of the town and fight under terrain lighting; neither form is in the profile.
- **Exceptions**: no file of ours: _CxxThrowException 006E7819 and MSVC 6's std::exception (vtable 00747290) went with
  crt_except.cpp (C20m), nothing calls them; the pointer throws are the compiler's ("Named data (C18)").
- Not ported (no callers): acos / asin / fmod's plain forms. The old iostream (streambuf, ios, ostream; C35d):
  the console's streams are console::Stream and ConsoleStreambuf (console.h), the doubles through str::Format "%.6g". The C initialisers `__onexitinit`, `__initstdio` and VC6's unhandled-exception filter are not
  run: the image's atexit, streams and crash filter are ours / the host's.

### MFC (none of ours; S5-S8b, S3d, S3f, S16)

The exe links VC6's MFC 6.0; WinMain does not go through CWinApp (CWinApp's constructor 0070D654 has no caller). Our
code has no MFC classes: strings are std::string, collections std::vector / std::list / std::map, files lib::File /
TextFile / FileFind, the clock lib/calendar.h, the interface's drawing lib/gdi.h on plain HDCs, threads lib/thread.h.
- **CString**: the static initialisers of CString's four fixed allocators (00700718 .. 007007AB, 007C8B80 ..
  007C8B08) and of the temporary DCs' and GDI objects' CFixedAlloc (007C8D18 / 007C8CF0) are gen_static_init.py's
  UNUSED_INITIALISERS. 00700EFE is MakeLower (it calls `_mbslwr`, str::MakeLower's); 00700EEC is the real MakeUpper,
  unreached.
- **Collections and archives**: the game's CArray / CList / CWordArray / CDWordArray / CByteArray instantiations are
  std::vector / std::list; their vtables are game.exe-only rows of config/vtables.csv, and no CArchive is left (the
  Serialize slots are gone).
- **Exceptions, state**: the simple exceptions, their string table, ReportError, the thread / process locals,
  afxData, the global locks, _afxDBCS, the halftone brush and the module state's class list: nothing of the game reads
  them, only game.exe's static initialisers and exit functions built and destroyed them (gen_static_init.py's
  UNUSED_INITIALISERS). The harness's crash report reads a CArchiveException only in game.exe's runs. The game code's
  own message boxes of MFC's kind are app::ShowLibraryMessageBox.
- **Replacements**: CTime is lib/calendar.h (VC6's time() from GetLocalTime through the import, `_mktime32`,
  `_localtime32_s` / strftime); the GDI classes and handle maps are lib/gdi.h's gdi::Font / Pen / Brush (PointFont =
  CreatePointFont, FillSolidRect); the threads are lib/thread.h's StartThread (`_beginthreadex` suspended, the priority
  set, resumed, as AfxBeginThread); the new handler is libcrt::OperatorNew, which throws the CMemoryException itself.
- Not ported, unreached: CString Delete / Insert / Replace / MakeReverse / FreeExtra / Span* / AnsiToOem / wide
  constructors / FormatMessage; CPtrList, CPtrArray.

### STLport (S9-S12g; none of ours)

Every container of ours is std (or `lib::HashMap`, src/lib/hash_map.h, whose iteration order is STLport's); the
original's node allocator (`__node_alloc`, free lists at 0079B224) has no port, our objects are the host's `new` /
`delete`. The maps are std::map with the CString keys' `_mbscmp` order as `str::MbsLess`; strings are std::string; a
camera view stack that nothing ever pushed to is deleted.
- **Plain records**: `records::IsPlain<T>` decides plain data: never CString, std::string, std::vector or arrays of
  them (an array of vectors is trivially copyable to the compiler, so a default record's lists were garbage once:
  S10b / S10d).
- **sort**: `lib::IntroSort` (src/lib/intro_sort.h: STLport's introsort, median of three, unguarded partition
  and inserts, heap sort at the depth limit, insertion sort of the first 16) wherever equal elements can differ and
  their order shows: the draw lists by `address_order::DrawBefore` and the touched models by last use (which keep
  flag 0x400; world_client_draw.cpp), the particles by depth, the picking hits (`DepthLess`), the connection screen's
  servers, the save list (`!(a < b)`: equal times compare true both ways, not a strict weak ordering), the
  objectives' quests (looks both names up, adding missing ones; a name without a record is before itself) and the
  briefing topics (`_stricmp`); the harness's draw-list sort in game.exe (harness_address_order.cpp) is the same
  template. `std::sort` where equal elements are identical: the client's file chunks (one message per index, or the
  same data), CollectChanges's replicated objects (one entry per object and priority), the battle screen's ids, the
  hero screen's names (`str::MbsLess`). The element copies do not follow STLport's (heap layout is not kept).
  libcrt::Qsort is the CRT's quicksort, a different algorithm, and shares nothing with it.
- Unregistered twins with the same instructions (no code of ours calls them): 00429DB0, 004FAF10, 0042B3C0,
  0042B3F0, 00404D50, 005E1560, 00457940, 004FC0C0, 004217C0; "unreviewed family" rows 005F8E90, 005F92A0,
  005F94B0, 005FB810.

### Compiler-written helpers of game types (S3b, S3c, S7a, S13, S13a, S13b)

The global constructors only static initialisers call (the six databases, logic settings 005CDFC0, SavedParties,
both worlds: database_construct.cpp, world_construct.cpp), the exit callees (database_destruct.cpp, the worlds'
and Terrain / Sector / particle manager destructors in world_destructors.cpp), record destructors / copies /
assignments (record_lifetime.h, gone with C14n), the database resets
(database_reset.cpp) and the script lists' by-value copies (game_lists). Bodies that are library rows are called
through the library's address or inlined from lib/, never registered on the game side. Quirks kept: the link queue's
stream copy 00479410 reads the new block's uninitialised bytes into the source; 004CC0C0 copies a command's float
arguments as dwords (ours through the FPU: only a signalling NaN differs).

### Rules from real bugs

- A NaN in a sort key makes a comparator irreflexive and STLport's unguarded scans run off the array: the draw list
  sort crashed in its comparator 00574300 (S17a: a shadow-only object's never-written view depth); CObject3D::Construct
  now zeroes it. Any comparator over a never-set float needs the same care.
- A stolen-byte count must cover whole instructions (`MOV [ESP+0],0` is eight bytes); check_stolen.py checks the
  recorders' counts.
- A system call does not trap on an inaccessible page, it fails: C25's image trace saw WriteFile fail (error 1784) on
  a save's one-byte .data constant. Page protection tricks must open a buffer's pages around the Win32 calls that
  take it.
- Leftover memory is not preserved (user 2026-10-15): where the original reads never-set bytes, ours writes a
  canonical value (zero) and the harness patches the original the same way (standalone-verification.md section 5).

### Open

S2a (short getters as named members), S11b, S16e (`find_vtables.py` should take only
function starts as slots; the dashboard's library count includes library.csv's "not a function" rows), S19d. The console's `disconnect` (005DFC10 -> GameFlow 00581620 ->
CWorldClient::DisconnectLocalPlayer 00566E00, S16c) cannot be a scenario: in net_lobby's lobby the client's next
segment then ends the game in its "packet parsing error" box, the original's too. Multiplayer client paths and many container members are reached by no
scenario (tests/coverage/coverage.csv).

## 3. Runtime: MFC, STLport, CRT later

The game's MFC and STLport uses are std containers, std::string and small classes of our own (src/lib: file_io.h,
gdi.h, thread.h, calendar.h, hash_map.h, intro_sort.h, str.h); linking real MFC would tie the build to Windows
(platform-layer.md).

What must survive:
- **Hash map order**: iteration follows STLport's bucket counts and insertion rule (`lib::HashMap`). A loop that feeds
  game logic, saves or network messages keeps that container.
- **Sort order**: STLport's `sort` leaves equal elements in another order than MSVC's (the objectives list shows it).
  Keep the original algorithm (`lib::IntroSort`) at those sites.
- **CRT behaviour**: `_ftol`, `rand` (MSVC6's generator and seed), sprintf's float digits (section 2), the pooled
  small-block allocator only where code depends on reuse order (none known).
- **Formats**: the archive encoding of strings and arrays in saves and network messages is a pair of read / write
  functions, independent of the container type.
- **x87 maths**: the exe stays a 32-bit MSVC build with the same float settings until the float checks are replaced.
## 4. UTF-8 instead of cp1251

Every string inside the game is UTF-8 (C34 dropped the old `EI_UTF8` switch: there is no 8-bit build). Text in the
asset code page (`EI_ASSET_CODEPAGE`, a CMake cache string, 1251) is converted where it crosses into the game and
back where it leaves: the assets when they are read, the saves and the network both ways. So the files the game
writes and the network's messages carry the original's bytes (saves stay byte-identical to vanilla, vanilla peers
read and send cp1251), and the system calls are the wide ones.

How text moved in the original: assets hold cp1251 bytes; the interface draws through GDI's ANSI calls with a font
whose charset is DEFAULT_CHARSET; typed characters arrive as WM_CHAR in the ANSI code page; file names (save names
typed by the player) go to the ANSI file calls.

| Ticket | Work |
|---|---|
| U1 Asset boundary | `FromAssetText(bytes, codepage)` where display text is read: the text database, .reg string values, .mob strings, credits.scr, the titles' headers. |
| U2 System boundary | The ANSI calls that take or give text routed to UTF-8 versions of the wide calls. |
| U3 Drawing and measuring | The interface's text draw and extent and its fonts: the wide GDI calls. |
| U4 Typing | Unicode WM_CHAR; the editors move the caret, delete and limit length by character. |
| U5 Audit | Identifier (left) or display text (fixed) per site: case, buffers, byte indexing, word wrap. |
| C34 Saves, network, harness | No switch; saves and network messages in the asset code page; the harness digests in one encoding. |

Byte-equality compares, hashing, copying and formatting work unchanged on UTF-8.

### The helpers (src/game/asset_text.h)

- `text::FromAssetText` (asset-code-page bytes to UTF-8; a `std::string_view` overload and an in-place `std::string*`
  one; ASCII is left alone), `text::ToAssetText(text, &out)` (back to the asset code page for a lookup: false for
  ASCII or text that is not valid UTF-8), `text::ToAssetText(text)` (the same as a string for writing: a character
  the code page has no byte for becomes '?', `WC_NO_BEST_FIT_CHARS`, never a look-alike; text that is not valid UTF-8
  stays as it is). Every byte of 1250-1252 maps to one character and back (all 255 non-zero bytes checked), so a
  round trip restores an asset's bytes.
- Lengths that mean time or a limit: `text::CharacterCount` counts characters (what the original's cp1251 bytes
  counted; a colour escape `\007C`+3 bytes by its bytes). A briefing phrase lasts 0.05 s per character
  (`CBriefingScreen::NextPhrase`).
- Cuts by character: `CharacterLength`, `CharacterBytes`, `FittingBytes`, `CopyFitting`; typing: `Typed`,
  `TypedLength`, `NextCharacter`, `PreviousCharacter`, `CharacterStart` (U4).

### U1: the asset boundary

- **Sites**: the text database (`TextSources::FindKeyText`, which `CTextManager::Open` reads through; the copies
  are cached per data address and dropped when their archive closes); `CTextManager::ListKeys` (key names) and the
  key passed to the lookup (one "pers" key holds Cyrillic); CFileRegistry's `GetString`, `GetStringBuffer`,
  `GetStringArray`; Chunk's `ReadString` / `ReadEncodedString` (.mob; Chunk's `WriteString` / `WriteStringArray`
  convert back); credits.scr (`CCreditsScreen::Build`); the titles' headers (`ui_layouts.cpp`; the pages are RTF).
  The archives also hold binary files: `GameFlow::RegisterQuests` reads a zone's quest.reg from the text cursor, so
  it opens it with `CTextManager::OpenData` (bytes as they are), and `TextSources::Find` (004DEBC0) stays the raw
  lookup.
- **Rule from a real bug** (UB1): the UTF-8 copies moved the heap's layout, and ui_gallery_mp's base screen lost
  its player list: the server's update lists the player record and the client's binding in the order of their
  entries' addresses, and a player read before the binding was listed as nobody (fixed: network.md "Original bugs
  kept (rest)", address_order.h).

### U2: the system boundary

- **Where**: src/game/system_text.h. The image's import slot of each ANSI call that takes or gives text holds its
  UTF-8 version (`text::utf8::<Name>A`, same signature; `text::SystemRoute`, asked by `ImageRoute` in
  hook/standalone_main.cpp when image_imports.cpp resolves the slots; the `TEXT_ROUTE` list, which gen_iat.py's
  `routed()` reads). So every call through `gameapi::` passes and receives UTF-8, and with it `CFile`, `CFileFind`,
  `CStdioFile` and the CRT's `fopen` / `_findfirst` / `_access` (they call through the slots). The harness takes the
  same slots afterwards and wraps the UTF-8 versions, so its saves redirect, overlay and registry snapshot work
  unchanged (they work on the bytes; advapi32's and kernel32's wide exports are taken too: harness_settings.h,
  harness_install.h).
- **Routed**: files (CreateFile, FindFirstFile / FindNextFile, DeleteFile, MoveFile, CopyFile, Create- /
  RemoveDirectory, Get- / SetFileAttributes, GetCurrentDirectory, GetFullPathName, GetModuleFileName,
  GetDiskFreeSpace, GetVolumeInformation, LoadLibrary, LoadCursorFromFile, mmioOpen), the registry (RegOpenKeyEx,
  RegCreateKeyEx, RegQueryValueEx and RegSetValueEx: string values as UTF-8, their sizes in UTF-8 bytes),
  CreateWindowEx (a non-ASCII title is set as UTF-16 with `DefWindowProcW(WM_SETTEXT)`), FindWindow, MessageBox,
  GetKeyNameText, LoadString, the window's class and messages (U4), the text draw and fonts (U3), CharUpperA /
  CharLowerA (U5). The image's log names the routes: "504 resolved (33 routed)" (32 here and the command line).
- **Direct calls** (not through the slots): our own code calls `text::utf8::<Name>A` by name (mod_folders,
  script_addon_commands, script_addon_strings, console_addon, settings_file, world_client_maps). The console's
  clipboard and addon's AutoExec lines convert with UTF-8; `GetFullPathNameA` (system_text.cpp) takes the current folder
  through the slot.
- **Input that is not UTF-8** is taken in the ANSI code page, as the ANSI call took it; output is always UTF-8.
- **Not routed**: the profile calls (`CIniFile`: GetPrivateProfile*, WritePrivateProfileString; the values
  round-trip as bytes and the config\*.ini names are ASCII; a Cyrillic install folder would need them wide, with
  the ini files' code page decided), OutputDebugString, the loader's own calls (hook/, standalone/: ei_hook.dll's
  folder, EI_GAME_ROOT through `RedirectGamePaths`).
- **Save names**: saves are folders `saves\save<n>` with the title inside info.sav, so a save's title never reaches
  a file name; the multiplayer heroes are `mp\<n>.mp`. Cyrillic file names are checked by utf8_typing's
  `utf8files` step (tests/verify/harness_utf8.h): a folder and a file with Cyrillic names made, listed, read back
  and removed through the game's slots under the saves redirect, the names checked on disk as UTF-16.

### U4: typing

- **The window is a Unicode one** (system_text.h): the image's slot of `RegisterClassExA` registers the class wide
  (`text::utf8::RegisterClassExA`), and the slots of `DefWindowProcA`, `GetMessageA`, `PeekMessageA` and
  `DispatchMessageA` take the wide calls themselves (the same arguments). Retrieval and dispatch must both be wide:
  a message taken with an A call is converted to the ANSI code page whatever the window. So WM_CHAR carries a
  UTF-16 unit; `DispatchInput` (00457970, app.cpp) passes the interface a code point (`TypedCharacter` joins a
  surrogate pair; a lone half is dropped). The harness's own window subclass (SetWindowLongPtrA,
  AlwaysActiveWindowProc) is unaffected: input never goes through the procedure, and CallWindowProcA converts what
  is sent.
- **The editors** (asset_text.h, "Typing"): the caret stays a byte offset (every draw and `Left` / `Mid` take it as
  one) at a character's start. Sites: the message box's edit field (0065C610, 0065C930: its letter class for a code
  point past ASCII is `IsCharAlphaW`), the console line (005E4180, 005E44A0; Rebirth's paste puts the caret at
  `CharacterStart` of its own sum), the chat line (005E3230, 005E3550), the save window's name (006548A0, 00655050).
- **Check**: tests/scenarios/utf8_typing.txt (no original run: game.exe's text is in the ANSI code page) types
  Cyrillic and a character past U+FFFF into the console line with the harness's `typeunicode` (each UTF-16 unit
  posted with PostMessageW) and checks the line's bytes and the caret with `consoleline` (harness_utf8.h) after
  typing, Left, Backspace, Delete, Home / Right, End and Esc.
- **Rule from a real bug**: a route must be a function of ours (`text::utf8::DefWindowProcA` calling
  `DefWindowProcW`), never `&::DefWindowProcW`: the routes table is read during ei_hook.dll's static
  initialisation, when an entry holding an import's address is not set yet (the first try routed none of them and
  typed Cyrillic came out as "???").

### U3: drawing and measuring

- **Wide text calls** (system_text.h): the image's slots of `DrawTextA`, `GetTextExtentPoint32A` and
  `CreateFontIndirectA` take `text::utf8::DrawTextA` / `GetTextExtentPoint32A` (the text, UTF-8 or else
  ANSI-code-page bytes, to UTF-16; the lengths stay byte counts) and `CreateFontIndirectA` (the LOGFONT and its face
  name to `CreateFontIndirectW`). Every interface text draw (the import slot of DrawTextA, called on the surfaces' plain HDCs: the surfaces'
  `DrawTextIn` 005FDE20, the console's lines and input), the interface's extent
  (`CInterface3D::TextExtent` 005F8340, what the editors' width limits use through `InterfaceTextExtentOriginal`)
  and the console's `PromptWidth` go through them. The harness's font slot wrapper (`HarnessCreateFontIndirect`, no
  anti-aliasing) wraps the UTF-8 version, as its file redirects do.
- **The "charset option"**: the fonts never took a charset from an option. MFC's `CreatePointFont` asks for
  `DEFAULT_CHARSET`, which the font mapper realises in the system's ANSI code page; the ANSI text calls read bytes
  in the code page of the selected font's charset. So game.exe draws the assets' cp1251 bytes as Cyrillic only on a
  system whose ANSI code page is 1251; on this machine (1252) its frames show them as Latin-1 letters ("Ãðàôèêà"
  for "Графика"). With the wide calls the charset no longer decides the glyph and `DEFAULT_CHARSET` stays. The
  "Font CharSet" option (+0x224, "english") only set the CRT's LC_CTYPE in the original (`CTextManager::Create`
  004DED20); ours leaves LC_CTYPE "C" (U5).
- `EI_FONT_CHARSET=204` in a run's environment (harness.cpp: the harness's font slot sets that charset, as a system
  with the assets' code page realises `DEFAULT_CHARSET`) makes game.exe's ANSI calls draw the assets' Cyrillic, the
  glyphs the image draws with the wide calls (U3 compared 54 of 54 gallery frames identical so).
- **Check**: utf8_typing types "λ" (outside cp1251) into the frozen menu's console and requires its frame to differ
  from the line "?" (`diffcapture`): 58 pixels at 4K, the glyph. How "😀" draws (Times New Roman has no emoji;
  whether DrawTextW falls back to another font) is not checked.
- **Still ANSI**: the titles' RichEdit (`SendMessageA`, RTF carries its own code page).

### U5: the audit

Every site that treats text as bytes, decided as an identifier (left: ASCII names, keys, file names, numbers, or
bytes compared and copied whole) or display text (fixed for UTF-8).

- **Case** (src/lib/text_case.h): a small table, ASCII, Latin-1 and the rest of code page 1252's letters, Cyrillic
  U+0400-U+045F and Ґ ґ (every letter of 1251); each pair is two UTF-8 bytes both ways, so a case change never
  changes a length. The CRT does not take case or classes from the locale: `_stricmp`, `_strnicmp`, `_memicmp` and
  `_mbsicmp` compare the table's lower-cased bytes (`text::CompareIgnoringCase`; "Р" D0 A0 and "Ѡ" D1 A0 share a
  trail, so every byte is folded, never only differing ones), `_mbslwr` (and so `CString::MakeLower`) lowers by it,
  `tolower` / `toupper` change ASCII only, and `isalpha`, `isdigit`, `isspace`, `isprint`, `_ismbcdigit` and
  `_ismbcspace` give a byte past ASCII no class (code page 1252's `isspace(0xA0)`, the trail of "Р", would split
  words). There are no double-byte lead bytes (`_ismbblead`, `isleadbyte`, `_mbsinc`, `_mbclen`; `_mbscmp` and
  `_mbsncmp` are byte compares; the standalone's start-up sets the host's `_MB_CP_SBCS` for `_splitpath` and the
  other host functions), whatever the system's ANSI code page. The "Font CharSet" option is not used:
  `CTextManager::Create` (004DED20) leaves LC_CTYPE "C". `CharUpperA` / `CharLowerA` are routed to the table. Sites:
  about 100 case-insensitive compares (map, zone, allod, material, model, spell, skill, item, keyword and file
  names, "none", the briefing's person and topic keys, the save folders) left as identifiers: they call
  `text::CompareIgnoringCase`, the table's compare. 19 `MakeLower` (keys and file names) left. 5 `CharUpperA` / `CharLowerA`:
  mod_folders' 2 path keys and MFC's `CFileFind` path left, script `StrLower` / `StrUpper` fixed. 6 per-byte folds
  (the .reg and archive name hashes, the map name hash, the archive wildcard, the keyword and part-name tests)
  left: their names are ASCII (the .reg tables' slots were laid out by the original's tools). 8
  class tests left (drive letters, digits in scripts and party files, the edit field's ASCII classes); the network's
  player name filter (`IsPrintableText` 0047ADA0) by character, each a valid UTF-8 sequence of a printable code
  point (the original passed bytes through the 1252 table's `isprint`; the login's name and password are the
  game's own ASCII texts).
- **Buffers**: about 95 fixed `char` buffers. Left: paths and file names (MAX_PATH, the figure, link and terrain
  names, archive patterns), numbers and ids, model / texture / sound names read from the network
  (`DeltaField::ReadText` empties one too long), error texts (`wvsprintfA` stops at 1 KB), the profile calls'
  buffers (ANSI, U2), the host password (`strncpy` to 20 bytes on both sides, compared as bytes). Fixed:
  `CFileRegistry::GetStringBuffer` cuts at a character's end (`text::FittingBytes`; the asset's 16-bit length no
  longer wraps); the server list's 90-byte name buffer (`text::CopyFitting`); the message box's edit text (50
  characters, up to 200 bytes) into the connection screen's 0x80-byte box buffer (`CMessageBox::CopyEditText`, cut
  at a character's end); `CStream::ReadText` cuts its UTF-8 at a character's end; `DrawTextA`'s UTF-8 route with
  `DT_MODIFYSTRING` (the server list's ellipsis) gives `DrawTextW` the four characters of room it writes past the
  text. The chat line also stays within 200 bytes (`CChatOverlay::OnChar`): a client's update carries 220 bytes
  (`Connection::updateBudget_`) and an entry past that is never counted, so a longer SayMessage would never arrive.
  Save names are std::strings (no buffer).
- **Byte indexing**: about 37 sites. Left: ASCII prefixes, markers and separators (`Left(1)`, `Left(2)`, `Mid` past
  "pers_", "constr", '|', "^<digit>", the text splitters and tokenizers that stop at ASCII bytes; a UTF-8 byte
  never equals one), the editors' carets (U4). Fixed: script `StrLen` and `StrMid` count characters
  (`text::CharacterCount`, `text::CharacterBytes`); the new game's name cut to 10 characters, not bytes
  (`CMpConnectScreen`).
- **Console padding**: none. Nothing pads text to a width (no `%-20s`, no stream width); the console tables are
  literal ASCII.
- **Word wrap**: the chat overlay's `AddLine` (005E2A30) takes a character as its whole UTF-8 sequence
  (`text::CharacterLength`), so a word wider than a line breaks between characters and every piece is measured as
  text. `CUiSurface::DrawWrappedText`, the console tokenizer and the text reader walk bytes but break only at ASCII
  spaces and newlines: left.
- **Check**: tests/scenarios/utf8_typing.txt's `utf8text` (Cyrillic compares in either case, the "Р" / "Ѡ" pair,
  `MakeLower`, `CharUpperA`, `tolower` and `isspace` of UTF-8 bytes, the name filter on a Cyrillic name, a cut
  sequence and a control character) and `chatwrap` (a long Cyrillic line with a 150-letter word added to the chat
  overlay: more than one line, each valid UTF-8, joined back to the text).

### C34: saves, network and the harness

- **Saves** (and the multiplayer heroes' mp\<n>.mp, info.sav, the zone's .mob chunks): resources, in the asset
  code page on disk as the original's. Every text a stream writes goes through `CStream::WriteString` (the asset
  code page, '?' for a character it lacks, and the terminator; it replaced about 30 `Write(text, strlen + 1)`
  sites and the files' own `WriteText` helpers), every text it reads through `CStream::ReadString` / `ReadInto`
  (to UTF-8) or `ReadText` (to UTF-8, cut to its buffer at a character's end). Chunk's `WriteString` /
  `WriteStringArray` convert as its `ReadString` does. So a save written by the image is byte-identical to
  vanilla's (the top panel's text, `ui_battle_slots.cpp`, and the save's title were the two that differed under the
  old switch), and a Cyrillic title typed outside 1251 comes back with '?' for those characters. What a stream
  writes as fixed-size bytes (`CEffectSound`'s 256-byte name) stays bytes: those are ASCII file names.
- **Network**: the same streams carry the world's messages (the deltas' `WriteStringBoth` / `ReadString`, the
  server's and client's text messages, zone names, variable names), and the session layer converts in
  net_messages.cpp's `PutString` / `TakeString` (login, player, chat, zone, host, command, line and variable
  messages) and `GameBuffer::WriteString` / `ReadString` (the game info's name and description). So vanilla peers
  send and read cp1251, and the update budget counts the original's bytes. The login's `PlayerLoginMessage` is
  fixed bytes with ASCII texts.
- **Harness digests**: game.exe's text is cp1251, ours UTF-8. The digest's reader of our objects
  (tests/verify/image/harness_digest_image.cpp) gives the reading code (harness_digest_read.inc) each text through
  `Text`, which converts it to the asset code page, so both sides hash and describe the same bytes (object names,
  script names, script string variables, party variable names, the zone).
- **Rule from a real bug**: the top panel's text holds colour escapes ("\007C" and three raw colour bytes), so the
  whole string was not valid UTF-8 and went into the save unconverted (ui_gallery_game's save differed). The
  conversions keep each escape's five bytes as they are and convert only the text between escapes, both ways.
- **Message boxes**: the image's MessageBoxA is the wide call, so the harness's user32 MessageBoxW hot patch
  sees its boxes: it answers the disc prompt there too (app_disc_prompt) and logs the text as UTF-8.
- **Check**: both suites' goldens the same (121 / 114), the original suite rerun for the digest's change.

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
(image_imports.cpp), runs the start-up actions, the static initialisers and WinMain (app::GameMain). A stray game address (a jump into game.exe's code, a read of its data that missed the seam) faults when the memory is free and is named instead of reaching other memory.

### image_data.cpp

game.exe's .rdata and .data in ei_hook.dll's own storage (docs/notes/standalone.md, "Own data"). In the standalone
image both sections (kMovedBegin-kMovedEnd: .rdata with the import address table at its start, .data with its
zero-filled tail, the .bss) are kept in g_dataStorage instead of at their addresses: the data seam (game_data.h)
adds g_dataShift to each of their addresses, the pointer cells of game.exe's .rdata and .data that point into the
range (image_cells.inc, tools/gen_image_cells.py: vtables' RTTI, the C++ exception tables, MFC's runtime classes
and maps, string pointers) get the same shift. Nothing is at the old range, so a use that missed the seam faults and is named with its data address and code address
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

### lib/file_io.h

The game's files through its import slots (CreateFileA, FindFirstFileA, GetFileAttributesA), so the harness's
redirection and a mod's hooks apply: `File` (binary, a Win32 handle; MFC's CFile open flags), `TextFile` (lines on a
host CRT stream), `FileFind` (a folder search with CFileFind's read-ahead and absolute paths), `OpenFile` (fopen) and
`FileExists` (_access). A failed read, write or seek throws `FileError` (the OS error and the path in what()), where
the game's MFC threw a CFileException*: what the game caught (CDib's reads and writes, the script reader) catches it,
the rest ends the game as the original (a write to a full disk: tests/scenarios/harness_disk_full.txt).
