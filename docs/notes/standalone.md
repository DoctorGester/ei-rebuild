# Standalone exe: runtime plan and UTF-8 (planned 2026-10-13, re-checked and split 2026-10-15)

The hook DLL shares objects with the original code, so every shared type keeps the exe's layout and
allocator. A standalone exe has no original code. This file records what still ties the code to the
exe, how the first standalone exe gets around it, and what a switch from cp1251 to UTF-8 takes.

## 1. What ties the code to the exe (measured 2026-10-15)

| Tie | Count |
|---|---|
| Unported game functions | 0 (S2 ported the last 200, all under 5 bytes, as `EI_REPLACE_SHORT` kinds; 00656D69 was not a function) |
| Forwarders (`EI_ORIGINAL`) | 3,795 addresses: 2,347 to rewritten code (C13), 5 unported, 1,443 library: STLport 881, MFC 399, compiler-generated 49, CRT 37, import thunks and others 77 |
| Code pointers in the exe's data | 4,524 to rewritten code (vtables, callback tables), 391 to 359 library functions (336 MFC: the `CArray` / `CList` / `CMap` deleting destructors and `Serialize` slots) |
| Game data addresses | 3,601 distinct in 524 files: .rdata 912, import table 465, initialised .data 1,812, .bss 410 |
| Static initialisers | 748 in the .data list 00783004-00783BB0: all generated (S3); their call targets lack 35, their exit functions 130 (S3a, S3b) |
| Reads of the exe's code bytes | the frame's camera call target (00457595), app_frame's CALL test, Rebirth detection (live bytes against the file) |
| Runtime uses outside src/lib | `mfc::` 3,481, `stlp::` 1,009, `gamecrt::` 1,191 (865 new / delete), `gameapi::` 536 |
| Cleanup counts (no effect on the first exe) | C11 shims 1,034, `__asm` 221, `float_control` 114, raw addresses 1,025 |

The old renderer code the first plan counted is ported (only the 201 short functions are left).

## 2. The first standalone exe: an address image

S comes before C12 and C13 (user, 2026-10-15), so the first exe has to run with the 2,347 forwarders to
rewritten code, the 3,601 raw data addresses and the exe's vtables as they are. It does that by
rebuilding the exe's address space without its code:

- **Host and image** (S1). `ei_standalone.exe` is linked below 0x400000 and reserves 0x400000-0x7D7000
  with a zero-filled array. It copies the headers, .rdata, .data and .rsrc of game.exe on disk into place
  (data only, nothing from .text runs), fills .text with INT3, resolves the import table itself (DDraw,
  mss32 and binkw32 from Engine\, the rest from System32, so Rebirth's dinput proxy and addon.dll stay
  out), loads the same ei_hook.dll (it sees that it is not inside game.exe) and lets `InstallHooks`
  write its JMP stubs into the blank text (plain replacements only). Forwarders, function pointers in data and vtable slots then
  reach our code through the stubs. An INT3 hit logs the address's name and exits: that is the list of
  what is still missing.
- **Library code is ported like game code** (S4-S13). Bodies for the MFC / STLport / CRT pieces live in
  src/lib (layout- and allocator-compatible, as now); each instantiation our code forwards to or the exe's
  data points at is registered at its library address (`EI_REPLACE` / `EI_REPLACE_METHOD`) with a verify
  wrapper where the elements are plain data, so the hook build checks it against the original exactly as
  it checks game code. The forwarders in src/game stay (C13 makes them direct calls); no call site
  changes, so these tickets do not touch C11's shims. The allocator (operator new / delete, malloc /
  free, STLport's node allocator) is the one piece registered for the standalone image only: the hook
  build keeps the game's heap.
- **What only the image needs** (S2, S3, S14): the 201 short functions (registered, skipped by the hook
  build, stubbed in the image), the 748 static initialisers (generated from their patterns, run in list
  order before WinMain), and the few sites that read the exe's code bytes or detect Rebirth from them
  (the standalone takes its Rebirth flags from ei_rebirth.ini alone; Miles must not load EI_Plugin.asi
  into the image).
- **Boot** (S15-S17): the harness runs the standalone exe like a game (`--standalone`), then the first
  boot to the main menu, then the suite against the SV goldens.

Later, after C13 (S18): generated definitions for the data addresses, vtables as real virtual classes,
imports linked directly, plain new / delete; then the image and the read of game.exe go.

**S1 (2026-10-15).** `build\<config>\ei_standalone.exe [--exe <game.exe>] [--dll <ei_hook.dll>] [--run]`
(src/standalone/host.cpp, src/hook/standalone.cpp), run from the game folder. Without `--run` it builds
the image, writes the jumps, checks them and exits (no window): on run\vanilla's game.exe 504 imports
resolve, 6,217 jumps go in, 0 wrong. In the image a registration the hook build skips as a short
function or unknown start (147) gets its jump when the next function leaves 5 bytes. Left out: three
virtual-call thunk registrations (00675D40, 00469E00, 00469DE0; fixed by S1a) and three addresses
registered twice (00558CA0, 00558CC0, 0054EAC0; S1a kept one). `--run` calls WinMain under a handler that logs an
INT3 hit (address, containing function, caller); not run yet (S15 / S16: static initialisers,
resources and the command line are missing).

**S1a (2026-10-15).** The three skipped registrations are functions that are themselves a bare vtable dispatch
(`MOV EAX,[ECX]; JMP [EAX+n]`: DestroyEffect to slot 18, SpecRemove / SpecRefresh to slot 7), so our member,
a tail call, compiles to the original's own bytes and the installer took it for a pointer to a virtual. They
are registered with `EI_REPLACE_DISPATCH` (hook.h; like `EI_REPLACE_SHORT` the hook build leaves them to the
original, which is the same jump; the standalone image jumps to ours). The installer exempts them from the
thunk refusal and checks that the replacement's dispatch bytes equal the original's (a different slot would
skip). The duplicates: AIRefDelete / AICallRecordDelete (world_deleting_destructors.cpp) and
AIPath::ConstructSteps (ai_path_cells.cpp) kept, with their verify wrappers; the copies in
combat_small_records.cpp and their wrappers are gone.

**S15 (2026-10-15).** `run_all --standalone [scenarios]` (also through `work.py test`, once merged: the scheduler
runs main's run_all) plays install `standalone`: `ei_launch --standalone` starts `ei_standalone.exe --exe
run\vanilla\game.exe --dll ei_hook.dll --run` (next to ei_launch) in the vanilla data folder, with the hooked
run's environment, output folder, job object and time limits, `EI_RENDER=new,present` always (no display
mode change), the host's stdout / stderr in ei_launch's (its lines are run notes). Default scenario `boot`
(main menu, quit). The harness loads with ei_hook.dll as in a hooked run (static constructor); an INT3 hit goes
into its report (`SetMissingCodeHandler`): `FAILED: standalone: no code at <address> (in the game function at
<start> | library code), [esp] <caller as module+offset>`. A registered address the harness patched before
the jumps (its fixed ISAAC seed at 0049F660) keeps the harness's jump, as in the hook build. First run: 6,416
jumps, 0 wrong, WinMain runs and stops at once on 006FE925, MFC's `CStringArray` constructor, called from
ei_hook.dll (app::GameMain; `tools/resolve_crash.py <run folder>` names it; S4-S13). Static initialisers (S3)
do not run yet. Harness pieces that patch call sites inside the original code do nothing in the image (the
frame capture at 004580D7, the held server update and client segment, the update countdown sites), so boot
has no capture yet: S15a.

**S2 (2026-10-15).** The 200 game functions under five bytes that no rewrite ported are in
src/game/short_functions.cpp, generated by tools/gen_short_functions.py from their bytes: 12 kinds
(`Ret`, `Ret4/8/12`, `ReturnZero`, `ReturnMinusOne`, `ReturnTrue`, `ReturnFalse`, `ReturnThis`,
`LoadDword<n>`, `MemberAddress<n>`, `LoadFloat<n>`; 35 instantiations), each compiling to the original's own
bytes. `EI_REPLACE_SHORT` sets `HookEntry::shortFunction`: the hook build skips them (one log line), the
image jumps to them. tools/check_short_functions.py (build.cmd) checks every registration's kind against
game.exe and our compiled code, the room for the jump, and that none is left unported. The getters read
the object as dwords (classes named in the comments by vtable, not declared yet). 00656D69 (Ghidra's
1-byte "function" `CF`) is the ModRM byte of a MOV inside RefreshList 006567A0: in dead_code.csv.

**S3 (2026-10-15).** All 748 static initialisers are generated (src/standalone/static_init.cpp by
tools/gen_static_init.py; none by hand). A small symbolic interpreter decodes each one's instructions
into operations: dword stores, copies and fills, float folds of constants (+ - * /, FLDLN2 / FYL2X as
`Log`), calls (cdecl, stdcall, thiscall, through an import slot) with their arguments, `atexit`, and
the once-only guard bit. A callee that nothing registers, referenced from one place only and decodable,
is inlined (the compiler's `call A; jmp B` halves, CArray / vector header constructors); everything else
is called at its address (our rewrite or a library port through the image's jump). An initialiser
that is itself registered (0042FAE0, the 004DDxxx debug ones) is one call. Result: 294 stores, 175
guarded atexit, 120 float folds, 77 constructor + atexit, 53 stores + atexit, 29 others. One template,
`StaticInitialisers(Do&)` (static_init.h), with three visitors: `Run` (the image, before WinMain;
`EiStandaloneMain` then calls the CRT's exit with WinMain's result, as the CRT did), `Check` (verify
build start-up check, tests/verify/verify_static_init.cpp: 1,153 stores, 121 folds bit-exact, 175
guards match the original's memory in smoke and ui_main_menu, both installs; three initialisers (2,570
values) that a later call rebuilds are left to their own checks) and `Targets` (the standalone check logs the call targets and exit functions that
are still INT3). tools/check_static_init.py (build.cmd) re-decodes game.exe: the file must match (a
helper that gets registered later changes the output: rerun the generator), and every address must be
in its section. On 2026-10-15 the image lacks 35 call targets (library constructors for S4-S12, ten
compiler-generated global constructors, MFC internals: S3b) and 130 of the 311 exit functions (S3a).

## 3. Runtime: MFC, STLport, CRT

MFC pieces in use are containers (`CArray` 470 qualified uses, lists 218, `CMap` 4), `CString` 2,546,
files and archives 91, a little GDI (`CDC`, `CPen`, `CFont`), `AfxMessageBox` and the TRY / CATCH link.
The game's WinMain does not go through `CWinApp`.

1. **First standalone exe:** section 2. `src/lib/mfc.h`, `stlp.h` and `containers.h` get full bodies,
   one header per family so the library tickets can run side by side.
2. **Later cleanup: standard types.** Only after the hook build is retired (side-by-side checks need
   the shared layout). `CArray` to `std::vector`, lists to `std::list`, `stlp::string` and `CString`
   to one string type, `stlp::vector / list / map` to their `std` twins. The framework pieces stay
   small classes of our own. Linking real MFC would be quicker but ties the build to Windows
   (platform-layer.md).

What must survive either step:

- **Hash map order** (39 `stlp::hash_map` uses): iteration order follows STLport's bucket counts and
  insertion rule. A loop over one that feeds game logic, saves or network messages keeps our copy of
  the container; the rest can use `std::unordered_map`. Check each use.
- **Sort order**: STLport's `sort` leaves equal elements in another order than MSVC's. The
  objectives list showed it is visible. Keep the original algorithm at those sites.
- **CRT behaviour**: `_ftol` (low half of a 64-bit FISTP, NaN / overflow gives 0), `rand` (MSVC6's
  generator and seed handling), `sprintf` float formatting (to check against today's CRT; the old one stops at 17 significant digits), the pooled
  small-block allocator only where code depends on reuse order (none known).
- **Formats**: the archive encoding of strings and arrays in saves and network messages is a pair of
  read / write functions, independent of the container type.
- **Struct sizes**: a `CString` is 4 bytes, a `std::string` 24. Every raw offset left (986 offset
  accesses, 223 byte-offset sums on 2026-10-13) breaks when a member changes size, so step 2 waits
  for the member cleanup.
- **x87 maths**: unchanged by this; the standalone exe stays a 32-bit MSVC build with the same
  float settings until the float checks are replaced.

## 4. UTF-8 instead of cp1251

Goal: every string inside the game is UTF-8. Original assets stay as they are and are converted when
read. Saves and multiplayer need not match the vanilla game (user, 2026-10-13), so they simply hold
UTF-8. Do it in the standalone build behind one compile-time switch, so the hook build stays cp1251
and remains the reference; frame captures of the same scenario in both builds should match pixel
for pixel, which is the check.

How text moves today: assets hold cp1251 bytes; the interface draws through GDI's ANSI calls with a
font whose charset comes from the "Font CharSet" option; typed characters arrive as WM_CHAR in the
ANSI code page; file names (save names typed by the player) go to the ANSI file calls.

| Ticket | Work | Size |
|---|---|---|
| U1 Asset boundary | One `FromAssetText(bytes, codepage)` and calls to it where display text is read: the text database (`TextSource`, texts.res), .reg string values (menus.reg, quest.reg, smessbase.reg), strings in .mob files (unit names, dialogue, script literals), credits.scr. The source code page is a setting (1251 for this release). Identifiers and file names inside assets are ASCII and need nothing. RTF (titles) carries its own code page. | 1 |
| U2 System boundary | Wide versions of the file, registry, window title, message box, clipboard and key name calls, converting at the call. 19 direct ANSI call sites outside src/lib, plus `gameapi.h`, `CFile` and `CFileFind` themselves. | 1 |
| U3 Drawing and measuring | The interface's text draw and extent (005F8340 and the draw routines), the console and the debug panel call the wide GDI functions; the charset option is dropped. Falls away if the native UI's font rasteriser comes first. | 1 |
| U4 Typing | Unicode WM_CHAR; the edit field, console line, chat line and name entry move the caret, delete and limit length by character, not byte. | 1 |
| U5 Audit | Classify each site as identifier (leave) or display text (fix): 99 case-insensitive compares and case changes (Cyrillic needs a small case table once the CRT's code page is gone), 95 fixed `char` buffers (Cyrillic doubles in UTF-8, so limits sized for names may truncate), per-byte indexing of text (about 37 sites), `%-20s`-style padding in console tables, word wrap that splits inside a long word. | 1-2 |

Total 5-6 tickets. Byte-equality compares, hashing, copying and formatting (the large majority of
string code) work unchanged on UTF-8. It cannot start before the port is complete: until then the
original code reads the same strings.
