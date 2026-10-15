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
| Static initialisers | 748 in the .data list 00783004-00783BB0: all generated (S3), with their 130 unported exit functions (S3a); their call targets lack 25, all library code (S4-S12; S3b ported the ten game-side ones), the exit functions' callees 47 (S4-S9, S3c) |
| Reads of the exe's code bytes | the frame's camera call target (00457595), app_frame's CALL test, Rebirth detection (live bytes against the file): each has a standalone path (S14) |
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
do not run yet. Harness pieces that patch call sites inside the original code reach the image through
hook points (S15a).

**S14 (2026-10-15).** What the image needs in place of game.exe's own module (hook build unchanged: every
change is behind `ei::Standalone()`, now cached, or in the host).
- Code-byte readers: app_main's `CallTarget` (camera 00457595, server update 00457314, client segment
  00457368) and app_frame's cursor lock (004580D7) call our rewrites directly in the image;
  `AddonPanelOffset` (ui_widescreen, addon's JMP at 005FC9E0) returns none. The harness reaches those
  calls through hook points (S15a).
- Rebirth: `InstallHooks` calls `rebirth_hook::SettingsOnly` instead of `Detect`: flags from
  ei_rebirth.ini (and EI_REBIRTH, the harness), EI_Plugin's player count from `[Rebirth] NPlayers`
  (default 7). Miles's import of LoadLibraryA (mss32.dll) refuses EI_Plugin.asi (logged); mp3dec.asi loads.
- Renderer: without EI_RENDER the image runs `new,present` (render.cpp `EnvironmentSpec`).
- Resources: the image's import slots of LoadIconA, LoadCursorA, LoadBitmapA, LoadStringA, FindResourceA,
  LoadResource lead to wrappers that map hInstance 0x400000 (and, for the string / resource calls, NULL)
  to game.exe loaded with `LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE` (checked by hand:
  string 0x65 "Evil Islands", class 0x66 "EIGAME", icon 0x69 load from it). GetModuleHandleA(NULL) gives
  0x400000 and GetModuleFileNameA(NULL or 0x400000) game.exe's path, as in a hooked run (so app_dxversion
  sees dgVoodoo's DDraw.dll beside run\vanilla's game.exe as a hooked vanilla run does). Calls our src/lib
  code makes through its own imports are not routed.
- Command line: `ei_standalone ... -- <game args>` (as typed) is WinMain's line and GetCommandLineA's
  after `"<game.exe>"`; `ei_launch --standalone` passes its game args that way.

**S15a (2026-10-15).** Harness hook points in the rewritten callers: `ei::mainloop::g_harnessHooks`
(ei/main_loop.h) holds the server update, client segment, frame camera update and cursor lock hooks;
app_main's `ServerUpdate` / `ClientSegment` / `FrameCameraUpdate` and app_frame's cursor lock call the
hook when set, before the CALL-target read. The harness sets each hook when it redirects that CALL, and
with hook points only (tests/verify/harness_hooks.h: always in the image, in game.exe with
`EI_HARNESS_HOOK_POINTS=1`) sets them without touching the original code. The countdown sites are left
alone in the image (the eight functions are ours, which follow `UpdateCountdownFor`). Hook build
unchanged: the redirections still make the installer leave WinMain (and the frame step when capturing)
to the original. Checked in game.exe with `EI_HARNESS_HOOK_POINTS=1` (WinMain and FrameStep rewrites
then run): smoke, combat, render_scene, render_terrain, ui_saveload, world_client, ui_main_menu digests
equal their vanilla goldens (the held server update writes the records), frames_basic captures on the
same frames (9, 23, 26, 34, 46); the frozen menu capture differs in 118 pixels of one animated spot
from the original WinMain's (R9b2's ground). Rebirth runs keep WinMain's original (NivalRegistry), so
that mode is for vanilla. The image: boot logs the hook-point and countdown lines, no "unexpected
code"; it stops in library code before the first frame, so capture and freeze are not seen there yet.

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
in its section. On 2026-10-15 the image lacked 35 call targets (library constructors for S4-S12, ten
compiler-generated global constructors, MFC internals: S3b) and 130 of the 311 exit functions (S3a).

**S4 (2026-10-15), group 1.** src/lib/crt.cpp, namespace `libcrt` (not `crt`: ei_vectors has
`ei::vectors::crt`), registered at their addresses in both builds: strncmp, strncpy, memmove, _stricmp,
_strnicmp, _mbscmp, _mbsicmp, _mbsinc, _ismbblead, isleadbyte, tolower, toupper, isalpha, isdigit,
isspace, isprint, atoi (atol inside it), _itoa, _splitpath, qsort (VC6's: middle pivot, selection
sort up to 8, larger part stacked), and the 19 WSOCK32 import thunks (by ordinal) plus
DirectInputCreateEx's, each a call through its import slot. The CRT state is read where the exe keeps
it: the game runs under a real LC_CTYPE locale (`__lc_handle` 007CABF0 nonzero, set before the text
manager's setlocale query), so tolower / toupper take `_tolower_lk`'s path (LCMapStringW in
`__lc_codepage`) and the compares fold bytes through a table of tolower rebuilt when the locale
changes; character classes come from `_pctype`, lead bytes from `_mbctype`. Only single-byte paths
are ported (`__mb_cur_max` 1, `__ismbcodepage` 0). In the standalone image that data is the C
locale (ASCII case only) unless something sets it: who sets the hook build's locale is still open.
Library functions have no Ghidra size in function_sizes.inc: each registered one is added to
config/extra_function_sizes.csv (the installer skips an unknown start). Checks
(tests/verify/verify_crt.cpp): against the original, memmove ours only; atoi against atol 006E804D;
qsort sorts the caller's array twice (comparators such as CompareSlots read through the element's
address). 0 mismatches in 20 runs (10 scenarios, both installs), e.g. toupper 4.7M, tolower 3.7M,
_splitpath 1.6M, _strnicmp 1.3M, qsort 6.7k calls. The thunks have no wrapper (a jump through the slot).

**S4 group 2.** Registered in both builds: rand / srand (the seed is the game CRT's `_holdrand`,
`_getptd`+0x14, in the hook build, which the harness reads and seeds; a thread_local in the image),
ceil, modf, _controlfp (x87 word only), set_new_handler, the eh vector constructor / destructor
iterators (a guard object unwinds like `__ArrayUnwind`; C++ exceptions only, not SEH), and the STLport
node allocator 00405950 / 00404D70 with its refill 004060E0 and chunk_alloc 00406B70, ported exactly
because the pool is the exe's data and original code pushes and pops its free lists inline.
`EI_REPLACE_STANDALONE` (include/ei/hook.h) registers in the image only: malloc, free, operator new
(MFC handler loop), operator delete, _onexit, atexit and exit (our list, then the host CRT's exit; the
exe's __xp / __xt terminators are for the game CRT's streams). The hook build keeps the exe's heap
and atexit list, which other unported CRT code (realloc, calloc, _strdup, the stream table) shares.
acos, asin and fmod have no callers (the game calls the _CI forms) and are not ported. Full suite
(127 runs): all pass but zone_gz15h_tornado rebirth (times out on main too); 0 mismatches, e.g. modf
9.5M, _controlfp 698k, rand 351k calls; the image check installs every registration.
Group 3: memcpy (overlap as memmove), memset, strstr, wcslen, floor and the multibyte functions
mfc_string.cpp forwards to, by their single-byte path: _mbschr, _mbsrchr, _mbsstr, _mbspbrk, _mbsncmp,
_mbclen, _mbslwr (`_SBUP` bytes through `_mbcasemap` 007CC080). Suite: 0 mismatches (memcpy 118M,
_mbschr 3.7M, _mbslwr 1.7M calls). Left for S4a-S4c: sprintf / vsprintf / fprintf / printf / atof
(MSVC6 _output / _input digits), the file functions and the rest of the heap (image only), _ftol and
the _CI maths (register arguments), _CxxThrowException, the iostream operators, setlocale / _setmbcp,
_ismbcdigit / _ismbcspace. (library.csv named 006EC8A8 _mbsupr: it lowers.)

**S4a (2026-10-15).** sprintf 006E8715, vsprintf 006ECC47, fprintf 006EAEB0, printf 006E955F and atof
006E911A registered in both builds (src/lib/crt_format.cpp). One `Output<Sink>` is VC6's _output
(the __lookuptable state machine copied from 007476D0, flags, `%I64`, `%Z`, wide text through wctomb,
"(null)", text kept between conversions). Floats are not "17 correct digits": `$I10_OUTPUT` builds them
from a 12-byte long double (96-bit `__ld12mul` with its truncated partial products, the exe's power-of-ten
tables 0079AE60 / 0079AFC0 copied), then `_fptostr` rounds that digit string half up; `_cftoe2` writes
three exponent digits, infinities and NaNs come out as "1#INF" / "1#IND" / "1#QNAN" / "1#SNAN" (so
"%.2f" of infinity is "1.#J"), -0 prints as 0. atof is `__strgtold12` (at most 24 digits; with more,
the original bumps the 24th digit itself when it is 5 or more; exponents beyond 5200 saturate; 'd' / 'D'
accepted) then `_ld12tod` (rounds up only when a bit after the one following the rounding bit is set,
so ties and some values above them round down; double format at 0079ADE0: min exponent -1023).
fprintf / printf format in memory and write once through the game's fwrite (S4b owns the stream code).
The decimal point is read from 00797FE8 (LC_NUMERIC), classes from `_pctype`; single-byte paths only.
Checks (tests/verify/verify_crt_format.cpp, all against the original): sprintf / vsprintf / fprintf /
printf run the original _output 006F17D4 into a scratch string FILE and compare count and bytes; atof
through its trampoline, bits. A start-up check runs ~135k cases (58 float formats on ~6,000 doubles
incl. denormals, NaNs and random bit patterns, integer / char / string conversions, atof on formatted
and random digit strings): 0 mismatches. Full suite (128 runs): 0 mismatches, only the pre-existing
zone_gz15h_tornado rebirth timeout; fprintf / printf are not reached (debug output). Follow-up: S4a1
(a function-vector family for the conversions).

**S4b (2026-10-15).** Image only (`EI_REPLACE_STANDALONE`; the hook build keeps the exe's FILE table,
descriptors and heap, which MFC's CStdioFile shares). Heap (crt.cpp): realloc 006ED69A, calloc 006F0317
(count * size unchecked, as VC6), _msize 006EEC7B, _strdup 006EEC50 on the host heap, retried through the
CRT's new handler under `_newmode` like malloc. Nothing else reaches the CRT heap from our side (other
callers are CRT / MFC internals: _beginthread(ex) calloc, CMemFile 0070AEEA / 0070AEF7, the MFC thread
state's _msize). Files (src/lib/crt_file.cpp): _open 006EB35C is VC6's _sopen (flags to CreateFileA's
access / sharing / disposition / attributes, `_fmode` 007CAC80, `_umaskval` 007CAADC, a trailing Ctrl-Z
dropped from a text file opened for update) through the game's import slots, so the harness's
saves / install redirection applies in the image too; the handle becomes a host descriptor
(`_open_osfhandle`, same translation and append flags). fopen 006E9107 parses the mode as `_openfile`
(an unknown or repeated letter ends it; `_commode` 007CADA4) and wraps the descriptor with `_fdopen`, so
the game's CrtFile* is the host's FILE*. _read, _write, _lseek, _close, _filelength, fclose, fseek, ftell,
fread, fwrite are the host's (a bad descriptor or stream returns the error instead of the host's
invalid-parameter abort); &_iob[0..2] map to the host's std streams; `gamecrt::fileno` is a function
(host `_fileno` in the image). _access 006EAF20 (write bit only) and _findfirst 006EB832 (attributes,
local 32-bit times, size, name; the game never closes the handle) go through the game's slots;
`gamecrt::findfirst` / `FindData` replace resource_stream.cpp's own forwarder. Check
(tests/verify/verify_crt_file.cpp, a start-up check against the originals on scratch files under %TEMP%):
27 fopen modes on text / Ctrl-Z / missing files with the same reads, seeks, tells and writes (results,
bytes, the file afterwards), 12 _open flag sets, bad descriptors, _access, _findfirst; the heap ours only.
smoke and ui_saveload, both installs: 0 mismatches. The image installs all 18; `--standalone` boot still
stops at 0040AA80 (hash table resize, S11) before any file code.

**S3a (2026-10-15).** The 130 exit functions no rewrite registered are generated too
(src/standalone/static_exit.cpp, same tool): each is matched as a whole-function instruction pattern
(`EXIT_PATTERNS`: 83 `mov ecx, obj; jmp dtor`, 17 guarded by a flag bit (two encodings), 8 two calls on
one object, 16 operator delete of the block kept in the dword before a buffer (4 guarded), a counted
pointer released (`Release`: count at +4, slot 0 with 1; one guarded), DeleteCriticalSection, the eh
vector destructor iterator, the Sleep-while-busy loop before WSACleanup, and 004013D0 (`ret`, 175
registrations, `EI_REPLACE_SHORT(Ret)`)). `ExitFunction(Do&, fn)` holds the bodies over the
operations (`Code`, the executing half of `Run`, plus `NonZero` / `Release`); each `ExitXXXXXXXX` is
registered with `EI_REPLACE`, so the hook build runs them at every normal exit. Their sizes go into
config/extra_function_sizes.csv and function_sizes.inc (the generator writes them, the check compares).
Library destructors stay at their addresses: the standalone check logs "exit function's callee ... has
no code yet" (MFC CString x12, operator delete x16, CByteArray, CWnd x4, 006E6BF5 x8, 006FC502 /
006FCEDC x8, the STLport vector / tree / hashtable ones: S4-S9) and the game-side compiler-generated
destructors (S3c). Checked: check_static_init (the file and the sizes against game.exe's instructions),
the standalone check (0 exit functions without code), smoke and ui_main_menu on both installs (129
hooked; vanilla exits through all of them).

**S3b (2026-10-15).** The ten compiler-generated global constructors only static initialisers call
are ported and registered at their addresses: the skill, spell, item, quest, prints and acks databases
(src/game/database_construct.cpp: empty vectors, hash_maps of 100), the logic settings' defaults
(005CDFC0, logic_settings.cpp; size 297 in extra_function_sizes), the multiplayer heroes
(SavedParties, party_file.cpp) and both worlds (world_construct.cpp: the members' constructors in the
original's order, the inlined ones written out). They run before the hooks, so
tests/verify/verify_global_constructors.cpp builds each again at WinMain entry and compares with the
original's global (fields bit for bit, heap pointers by kind, the hash_maps' three uninitialised functor
bytes skipped, including the AI map's bridge heights): 0 mismatches in smoke, ui_main_menu, net_lobby
and ui_mp_host, both installs. The image then lacks 25 call targets, all library code: S5 CWordArray
006FEE9A, CByteArray 006FF5EA, CString(LPCTSTR) 007009DE and the MFC internals (006E6BCD, 006E68C9,
006E6916, 006E6AEA, 006E6B37, 0070B600, 0070D1AA, 00704546, 0070DC4D, 00700674, 006FC42F); S6 CArray
005C1760; S7 CList 005CDAD0; S4 atexit 006E727B, the eh vector constructor iterator 006E8693, the
WSAStartup thunk 006E6106; S11 hashtables 004DC830, 0066B320; S12 STLport 00455E50, 004ED540, 0056A410,
005E0A90. The world constructors also call GridAllocate 00572820 / 005728D0, CStringArray 006FE925 and
CString::operator= 00700AF9 at their addresses.

**S9 (2026-10-15).** STLport `list<T>` (src/lib/stlp_list.h, stlp_list.cpp): one body per member,
`ListAbi<T, Policy>` shims with the instantiations' calling conventions, the element copy / destroy
as a policy (plain bytes, or the holder policies in stlp_list_refs.h: Share + Destroy, counted +
release, key + either, held engine objects, CString). 160 of the 175 forwarded instantiations are
registered: begin / end / distance, the constructors and destructors, insert / push_back / erase /
clear, remove 00427320. tests/verify/verify_stlp_list*.cpp check plain data against the original
(the original's node is unlinked and pooled again, ours must take the same node, links and bytes),
holder lists ours only (links, the copy's count or clone, the freed node on top of its free list).
The suite reaches 106 of the 160 (most server-side award and trade lists not). The node size picks
the free list: a wrong element size corrupts the pool of another size (005392C0 holds 20 bytes in a
32-byte node). Left: S9a (15 with element types not ported yet).

**S13 (2026-10-15).** Compiler-written helpers of game types: 46 of the 49 forwarded rows and 9
of the 17 data-pointed ones registered. Record destructors (item, spell, skill, quest and ack
records; save entries), copies and assignments (save entries, music themes, zone exits, face groups,
shared objects, wave records, the shape map's pair), deleting destructors (ground prints, sector
lists, shared objects, figure-map pairs, unit shape, party base, slot 1 of seven MFC collections),
the skill database's reset and the eight aligned-buffer constructors. src/game/record_lifetime.h
holds the shared pieces (`DestroyVector`, `AsCString` for CStrings declared as `char*`,
`LeadingStringDelete`). Bodies that are library rows (MFC CArray / CList destructors, STLport vector
storage, hash_map clear) are called through the library's address or inlined from lib/, never
registered here. Verify: ours-only wrappers in tests/verify/verify_record_lifetime.cpp (shape
before, result and copy equality after). Most destructors run at shutdown, after harness_report.txt
is written: read their `verify total` lines in ei_hook.log. Not reached by the suite:
SaveEntry::Assign (two or more saves sorted), ZoneExit::Assign (net zone start), FaceGroup::Assign,
SharedObject copy and delete (shared loot), the party base's and the collections' deleting
destructors. Left: S13a (three database resets, six world destructors); 00646C25 and 00656464 are
string bytes, not functions.

**S10 (2026-10-15).** STLport `vector<T>` (src/lib/stlp_vector.h, stlp_vector.cpp): STLport's members
(insert overflow, fill / range insert, erase, resize, reserve, push_back, operator=, the base
constructors and destructors) as one body each over an element policy (`PlainElement`,
`ObjectElement` for CString, `StringElement` in stlp.h, `CountedRefElement` in stlp_vector_refs.h),
`VectorAbi<T, Policy>` shims; `stlp_alloc.h` holds the allocator declarations and `Opaque<N>` for
both families. 201 of the 266 forwarded vector-family addresses are registered: 141 that never
touch an element (`_STLP_alloc_proxy::deallocate` 70 in two calling conventions, `_Vector_base`
constructors / destructors / get_allocator, by element size read from each one's size
arithmetic), 42 plain-data members, 12 CString / stlp::string members, 6 counted-reference ones.
Checks (tests/verify/verify_stlp_vector*.cpp): plain data against the original (its result is
recorded and undone: the new block back to the pool, the old one off its free list, bytes and
pointers restored; ours must take the same block and give the same elements); string and
reference vectors ours only against a model of the elements (texts, pointers, reference counts).
The suite reaches 125 of the 201, 0 mismatches. 004387D0 (no Ghidra function) got its size row.
Left: S10a (54 vectors of game records whose element copy / destroy are records' own functions,
and 11 non-vector entries named like vector code).

**S10a (2026-10-15).** 50 record vectors registered (src/game/record_vectors.h / .cpp): a policy
per record over its ported copy constructor, operator= and destructor, or memberwise
(record_members.h: `records::Memberwise<R, &R::m...>` copies, assigns and builds resize(n)'s
default member by member; CStrings and stlp::strings empty, vectors empty, plain fields left
uninitialised as the originals' stack temporaries leave them). New shims: `PushDefault`
(push_back(T()), in place when there is room), `CopyConstruct` (vector copy constructor);
`FillInsert` copies x through the policy. Policies in stlp_vector_refs.h: `CountedRefElement` got
its null default (004200C0 004AD700 004AD8D0), `SharedRefElement` (Share / ShareAssign / the
owner's Destroy; 00421D90 00426CB0). Records whose defaults are not blank: print records (vtable,
zero values), lever records (spotCount 0), HUD portrait slots (identity orientations), queries (zero
address, growing buffer). Checks (verify_record_vectors.cpp): ours-only model checks over each
record's texts; 00426B10 and 0062A0B0 (plain) against the original. 45 of 50 reached with 0
mismatches; not reached: 004200C0, 00421D90, 00426B10 (Replicated overflow), 00428A20, 004439C0.
Left (S10b): acks.db's table records 006DB9C0 006DBBD0 006DBE20 (each record holds a
`hash_map<int, SpeechList*>`, whose copy waits for S11's hashtable).

**S6 (2026-10-15).** MFC `CArray<T>` (src/lib/mfc_array.h, mfc_array.cpp; game-element ones in
src/game/game_arrays.*): mfc::CArray's bodies (mfc.h) under `ArrayAbi<T, Policy>` shims (SetSize,
SetAtGrow by reference or value, Add, Copy, RemoveAt, GetAt by value, ElementAt, Construct /
DestructElements, constructor / destructor with the instantiation's vtable as a type with
`Vtable()`). The element policy is what the instantiation's ConstructElements does after MFC's
memset: nothing (plain bytes by size), an inlined constructor (`InitElements`: identity
quaternions, empty CStrings, nested array headers, the animation step's blank values), or
`EachElements<T, Init, Destroy>` with the element's destructor called directly (zone exits and
records, script groups and objectives, effect particles, point lights, slot 4 releases of object
references, slot 0 / 1 virtual destructors, aligned blocks, nested arrays restoring their base
vtable). 171 of the 204 forwarded rows are registered (plus 25 helpers they call, 195 addresses);
0059CB50 / 0059CCE0 / 00468C50 / 00468F70 were the engine's aligned arrays (now in
aligned_arrays.cpp). Checks (tests/verify/verify_mfc_array.cpp): one alias per address; plain and
constructed elements against the original on a twin array (header and element bytes); SetSize of
owning elements against the original while nothing is destroyed (grow), ours only on shrink;
destructors, DestructElements, string / reference copies and GetAt ours only against the source.
The suite reaches 124 of the 190 checked addresses, 0 mismatches; the model and animation
file loaders' arrays (0045xxxx-00477xxx), the save's shared-object arrays and the AI map's array
destructors are not reached. Left: S6b (31: CFigure part / scene record constructors, the script
command family 004C4FD0-004CEB80, DeltaField, CMap-element arrays (S7), music themes, cache
entries, three class destructors named like CArray ones).

**S11 group 1 (2026-10-15).** STLport hashtables of dword keys (src/lib/stlp_hash.h, stlp_hash.cpp):
`hashtable<Traits>` over a `vector<Node*>` of buckets (stlp_vector.h), the 0x14-byte layout
(functor bytes, buckets, count), `HashtableAbi<Traits>` shims. 99 addresses: constructors (n and
default 100 -> 193 buckets), `_M_initialize_buckets` (0040AA80 is one; the standalone boot now gets
past it and stops at 00411AB0, a deque), resize, find, begin, operator[], find_or_insert,
erase(iterator), clear, ~hashtable, `_M_new_node` / `_M_delete_node`, the iterator members and
`__stl_next_prime` (0040AE00, 00672EC0). Node sizes from each `_M_new_node` or clear's free list:
12 bytes (16-byte list) for most, 8 for the two dword sets 004DB8A0 / 00668B70, 32 for
find_or_insert 004E2E90. Checks (tests/verify/verify_stlp_hash.cpp) against the original: pure
members compared; frees and inserts run the original, undo it (nodes popped off the LIFO free
list, the table restored) and compare ours, which must reuse the same nodes; a rehashing resize
runs the original on a clone. Destructors ours only. 76 of 99 reached by the suite, 0 mismatches.
**S11 group 2.** String keys (`StringKeyTraits<4>`: `__stl_hash_string`, size and bytes equal;
20-byte nodes on the 24-byte list, each new node copying the string with string(first, last)):
102 more addresses, 201 in all: find, `_M_skip_to_next`, erase(key), operator[](const string&) 16,
operator[](const char*) on the record tables 8 (`RecordsByName`: the record vector, then the map),
find_or_insert 15, resize 20, `_M_new_node` 20, clear 13, the two `__stl_hash_string`s. All
checked against the original: inserts undo the original's node (its string, then the node) and
re-free operator[]'s temporaries on top; clear / erase(key) take the original's frees (each
string, then its node) off their lists again, with the first word the free list overwrote. The
checks snapshot only the bucket an insert or erase touches (whole-table copies made ui_mp_host
time out). 169 of 201 reached, 0 mismatches. Group 3: the client's party-id hash_set<int>
(insert_unique 0066B400 with its resize and `_M_new_node`, clear 0066BA70): 205 in all. Left
(S11a): the 24 forwarded members whose values are CString, CParty, a list, a ref-counted handle or
a float key, and the 350-odd hash members nothing of ours calls (reached only from other
originals; dead in the standalone image unless the exe's data points at them).

**S12 group 1 (2026-10-15).** STLport `deque<T>` (src/lib/stlp_deque.h, stlp_deque.cpp; moved out of
stlp.h): the constructor, `_M_initialize_map`, `_M_create_nodes`, `_M_push_back_aux`,
`_M_reallocate_map`, ~deque, the iterator copy and the three destroy helpers (trivial elements:
nothing), `DequeAbi<T>` shims, 20 addresses for the two instantiations (camera view stack, 0x24-byte
entries in 0x1F8-byte nodes; material entries, 0x10 in 0x200). 00411D60 / 00411F70 have no Ghidra
function: size rows added. The camera's deque is built by a static initialiser before the hooks and
its push (00410E70) is dead, so tests/verify/verify_stlp_deque.cpp adds a start-up check: deques built
from the original `_M_initialize_map`, emptied from the front, refilled through the original
`_M_reallocate_map` / `_M_push_back_aux` and freed by the original destructor, next to ours (shape and
elements after every step; 384 cases, 0 mismatches). Per call: the constructor and
`_M_initialize_map` against the original on a scratch deque (same map block), the rest ours only.
The standalone boot gets past 00411AB0 and stops at 006FE8EF, MFC's `CPlex::Create`, from
`CFixedAlloc::Alloc` (mfc_string.cpp).
**S12 group 2.** The tree's node-only half (src/lib/stlp_tree.h, stlp_tree.cpp; `map<K, V>` moved there
from stlp.h, its node colour now the original's byte): `_Rb_global` increment 0043DBE0, decrement
0043FBE0, the rotations 0043E300 / 0043E350, `_Rebalance` 0043FE90 and `_Rebalance_for_erase` 0043D830
(fastcall: node in ECX, root reference in EDX), registered in both builds. The exe's tree is SGI's 3.x
one (`_M_insert(x, y, v)`, RET 0x10, no hint argument; the hint insert falls back to
`insert_unique(v)`); `map` now has the members on top (`Insert`, `InsertUnique` with and without a
hint, `InsertEqual`, `Erase` of a node / range / key, `At` for operator[], node policies like the
list's). Checks (tests/verify/verify_stlp_tree.cpp), all against the original: the iterators are pure;
rotations and rebalances save the nodes they can touch (the path to the root and the header, three
levels below each), run the original, put the nodes back and run ours (same bytes, words, result).
A start-up check fills a map<int, int> with random keys and erases them all through the
`_Rebalance_for_erase` check with the red-black invariants after each step (the suite erases little):
smoke 2,136 erases, the suite's rebalances and rotations, 0 mismatches.

**S7 (2026-10-15).** CPlex and MFC `CList<T>` / `CMap<K, V>` (src/lib/mfc_list.h, mfc_list.cpp;
game-element lists in src/game/game_lists.*): mfc::CList's bodies under `ListAbi<T, Policy>`
(NewNode, FreeNode, RemoveAt, RemoveAll, AddTail / AddHead and RemoveHead by value, destructor with
the vtable type), `ListHeader<Class>` constructors, `MapAbi<K, V>` (InitHashTable, GetAssocAt with
HashKey `key >> 4` on the low dword, NewAssoc, operator[], RemoveAll, destructor), same element
policies as S6. 104 addresses: CPlex::Create / FreeDataChain (006FE8EF / 006FE90F, no Ghidra
functions: size rows added), 84 library-only, 20 game-element. Most library.csv names for these rows
were wrong (AddTail / AddHead / ConstructElements labelled as constructors, destructors or
GetAssocAt); each node size was read from its NewNode's CPlex::Create. FreeDataChain returns 0:
the original leaves EAX as its caller had it, and AIPath::Reset (005C7D50) returns that EAX, which
its twin check compares. Checks (tests/verify/verify_mfc_list.cpp): plain lists and maps against
the original on a twin header (touched nodes saved and restored; a new block compared by position);
the last node removed, RemoveAll, destructors, CPlex and the game-element lists ours only. The
suite reaches 62 of the 104, 0 mismatches; the CMap rows (scene / model file loaders 00453B90-
00477380) and the shared-object, AI-map and armour-spell lists are not reached. Left: S7a.

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

**S5 (2026-10-15): CString and the plain collections.** The exe links VC6's MFC (strcore / strex of
MFC 6.0). Bodies in `src/lib/mfc_string.*` (`mfc::impl::CString`, `CFixedAlloc`, the five string
pools at 007C8B08..007C8B80 and 007C8D18) and `src/lib/mfc_coll.*` (`CStringArray`, and one
`PlainArray` template for `CWordArray`, `CDWordArray`, `CByteArray`, `CUIntArray`), each registered
at its library address; mfc.h's forwarders and the call sites are unchanged (C13).

- Verify (`tests/verify/verify_mfc_string.cpp`, `verify_mfc_coll.cpp`): string members that only
  build a new string or edit their own buffer run as the original on a twin and are compared
  (constructors, assignments, appends, `operator+`, finds, Mid / Left / Right, trims with a set,
  Remove, FormatV, the copy helpers); the rest (destructors, ReleaseBuffer, MakeLower, Format, the
  allocator, the reference counting helpers) run ours only with state checks. Plain-array SetSize /
  SetAtGrow compare against the original on a twin array; constructors, destructors, Serialize and
  all of CStringArray run ours only.
- 00700EFE was named `MakeUpper` in mfc.h; it calls `_mbslwr` (6EC8A8, mislabelled `_mbsupr` in
  library.csv), so it is `MakeLower` now (same address, call sites renamed). 00700EEC is the real
  MakeUpper, unreached.
- ini_file.cpp's "DWordArray" (6FF933) is `CUIntArray` (vtable 007467A0).
- Not ported, not reached by our code: CString Delete / Insert / Replace / MakeUpper / MakeReverse /
  FreeExtra / Span* / AnsiToOem / wide constructors / FormatMessage / TrimLeft(set); CPtrList,
  CPtrArray, CMapPtrToPtr. Kept as forwarders: LoadString (00702B0E) and CArchive's CString
  operators (00708447 / 00708517), both CArchive / resource code (S8).
- The CRT calls (`_mbschr`, `_mbsstr`, `_vsprintf`, ...) stay forwarders in mfc_string.cpp's
  anonymous namespace; S4's CRT port can take them over.
- `tests/vectors/replay_mfc.cpp` (SV4b) has its own CString / CPlex copy; it could use
  `mfc::impl::CString` now, not done.

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
