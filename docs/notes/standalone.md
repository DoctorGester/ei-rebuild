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
locale (ASCII case only) until the game sets it (S4c: the text manager's Create, in both builds).
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

**S4b1 (2026-10-15).** The check's rare false fclose / _close mismatches under load were the machine's,
not the CRT's: a truncating open ("w" modes, _O_TRUNC) of a file just written, then a write past its
end, sometimes reads the gap back as the old bytes instead of zeros (about 1 in 4000 with 8 processes
doing it at once, reproduced with plain CreateFileA / WriteFile; hit either side; open and read-back
never failed). A case that differs now runs again on a new pair of files (3 attempts), each retry
logged as "crt file check: ... differs on attempt n" with both differences and errors; a real
difference repeats. Two stress runs (the file cases 60 times per game, 24 games 4 at once): about
10 and 4 retries, every one passing on attempt 2, 0 mismatches.

**S4c (2026-10-15), group 1: the x87 entries.** _ftol 006E853C, _CIacos 006E7320, _CIasin 006E83C0, _CIpow
006E81A0 and _CIfmod 006E95AA take ST0 / ST1 and return in EDX:EAX or ST0; our src/game helpers call them
through their addresses (`mov eax, addr; call eax`), so the image needs code there. src/lib/crt_x87.cpp:
a naked entry each (arguments to the stack, a plain C++ body; ECX / EDX kept), image only
(`EI_REPLACE_STANDALONE`: the hook build keeps the exe's x87 results and so its goldens);
check_cleanup exempts that one file's `__asm`. _ftol decodes the 80-bit value with integers (exact for every
input, the integer indefinite for NaN / infinity / |x| >= 2^63). The _CI bodies follow the CRT: its control word
(006EF665) keeps the caller's precision and rounds to nearest, so the port computes at 53 bits and rounds the
FADD / FSUB / FMUL / FSQRT results to the caller's precision (a float store at 24 bits); FPATAN is inlined by
/fp:fast as in the original; pow splits y log2(x) as y k + y log2(m) with fma errors (FYL2X's 64-bit product),
n by ties-to-even, 2^f by exp2, FSCALE by ldexp; the integer test on y rounds y / 2 at the caller's precision
(an odd y beyond 2^24 tests even at 24 bits, as in the original); overflow follows the caller's rounding
direction (DBL_MAX under chop); fmod is exact, +0 for a zero x, the indefinite for an infinite y. errno /
_matherr are not reproduced (nothing reads them). Check (tests/verify/verify_crt_x87.cpp, start-up, against
the originals still in place): 44,900 cases under five control words (24 / 53 / 64 bits, nearest / chop / up),
FPU stack depth and control word after each; _ftol and fmod bit for bit, the _CI functions bit for bit under
007F (the main thread's word, the float the callers store) and for special values; at 53 / 64 bits pow is
within 4 + |y| / 2 double units and acos / asin within 4 (1,758 results not bit-equal), denormal results
within tolerance (the original's _87except may flush them). smoke vanilla: 0 mismatches; the image installs
all five.

**S4c group 2: exceptions.** src/lib/crt_except.cpp, both builds (no state of their own): _CxxThrowException
006E7819 raises 0xE06D7363 (not continuable, {0x19930520, object, ThrowInfo}) with the thrower's own
ThrowInfo, so the exe's catch tables and ours (verify_mfc_try) both match it; MSVC 6 std::exception
{vtable 00747290, text, owned} (constructor, copy, operator=, destructor, deleting destructor, what with
"Unknown exception" 00747298). check_returns allows 006E7819 (it does not return). Check:
tests/verify/verify_crt_except.cpp (constructor, copy and what against the originals; a start-up check drives
all six, since the suite throws none).

**S4c group 3: the locale.** Who sets `__lc_handle[LC_CTYPE]`: the game, in both builds. The text manager's
Create (004DED20) calls setlocale(LC_CTYPE, the options' "Font CharSet" 0079BE94, "english" from the
options constructor unless the registry has another) and _setmbcp(_MB_CP_LOCALE); "english" resolves to
"English_United States.1252", handle 0x409, code page 1252 (the multibyte code page is already 1252 from
the CRT start-up's `_setmbcp(_MB_CP_ANSI)`, so _setmbcp changes nothing on a 1252 system). The hook build
keeps the exe's setlocale / _setmbcp (its remaining library code reads that state); the image gets ours
(src/lib/crt_locale.cpp, `EI_REPLACE_STANDALONE`): getqloc.c's name search (EnumSystemLocalesA with
the exe's abbreviation tables), _setlocale_set_cat, __init_ctype (class tables by GetStringTypeW, called
directly: the category table's pointer is exe code), _setmbcp with setSBUpLow; and the image's start
(src/hook/standalone.cpp) now runs the C initialiser __initmbctable (00783BC0) before the static
initialisers. Left out: the locks, `__setlc_active`, setlocale(LC_ALL, ...) (never called), the Win9x
paths. _ismbcdigit / _ismbcspace (both builds): `_pctype` for a byte (also `_isctype`'s answer),
GetStringTypeW in the multibyte code page for a double-byte character. Check: tests/verify/
verify_crt_locale.cpp, start-up: the class tests against the originals (every byte and 14 double-byte
values, under 1252 and 932); _setmbcp for 22 code pages from two states, setlocale for 67 steps (49
names incl. abbreviations, countries, code pages, Japanese 932, bad names; sequences ending in "C";
queries), original then ours from the same state, every byte of the state and the class tables equal.

**S4c group 4: the ostream inserters.** src/lib/crt_iostream.{h,cpp}, both builds (the streams are the
game's): MSVC 6's old iostream layouts (streambuf with its 11 virtual slots, ios, ostream with its
virtual base table and x_floatused) and operator<< for char 006FC0B0, unsigned 006FC14E, const char*
006FC31D, double 006FC7FA, long 006FC91C, int 006FC9C6, the manipulator 00434FE0, ostream::flush 006FC346
and the flush manipulator 00435000; opfx / osfx / writepad are internal (the exe's other callers keep the
exe's). Numbers go through the game's sprintf as VC6 builds the format ("%%%s.%.0ug" for double, the
precision capped at 15, 6 after the inline float inserter); the double's buffer is larger than VC6's 24
bytes (a fixed value past 10^22 overruns it there). Check: tests/verify/verify_crt_iostream.cpp shadows
every game call on scratch streams (original and ours, recording streambufs: bytes, streambuf calls, state,
width) and runs a start-up battery of 9,191 insertions (25 formats x 7 streambuf behaviours); operator<<
(const char*)'s original is rebuilt from the exe's opfx / writepad / osfx (no room for a trampoline);
flush and the manipulators are ours only. Left for the image: the streams themselves (ostream(streambuf*)
006FC42F, which the static initialisers call, the ios / streambuf constructors and destructors, the
log's streambuf), fflush for ios::stdio.

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

**S9a (2026-10-15).** The last 15 list instantiations, over game records (src/game/record_lists.h /
.cpp; stlp_list.h got `InsertDefault` (insert(pos): T() made by `E::Default`, copied, destroyed),
`InsertRange`, `PushBackDefault`, `PopBack`, and a static `Insert`, so 0062C090's unread ECX is never
used). Policies: the chunk stack's Ref (Reset as T()), the client's trade records (CParty copy /
construct / destructor body, last party destroyed first), the link queue's memory streams, the link
figure loader's pair<string, CMMIOResource*> (T(): string reserved for 8 bytes) and the titles'
lines (two CStrings). The prints' node copies start from GroundPrint's copy constructor, now ported
and registered (00571FC0: the name copied, the texture counted, the geometry arrays' headers shared,
vtable 0073F778). Quirk kept: the queue stream copy (00479410) calls the new stream's Read (slot 1)
with the source's buffer, so the source gets the new block's uninitialised bytes, as in the original.
Checks (tests/verify/verify_record_lists.cpp): 0040A8D0 against the original (same node, same Ref;
the three unused bytes are stack garbage), the rest ours only. Full suite (128 runs) ALL PASS, 0
mismatches; 12 of 16 reached (not: trade insert 00424900 / erase 00429770, which need a finished
multiplayer trade; the queue's range insert 004795B0; 00571FC0 at its address, whose only callers
are the node copies, which call ours directly). The trade nodes are TradeRecordNode in net_classes.h
(fields inline), the queue's QueueNode (net_link.h): the list types and these agree only by size.

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

**S13a (2026-10-15).** The three database resets (src/game/database_reset.cpp: units 0057CBE0,
spell tables 0057CE80, items 0057D340) destroy each table's name index and record vector through
the library (stlp::hash_map::Destroy, the record_vectors.h policies) in the original's order and
construct them again in place. The world classes' destructors (src/game/world_destructors.cpp):
bodies and slot-0 deleting destructors of CWorld (0055EC80 / 0055EC60), CWorldClient (00561B00 /
00561A00), the particle manager (005627C0 / 005627A0), Terrain (00562B80 / 00562B60), CWorldServer
(0058AAD0 / 0058A430) and Sector (005BFCD0 / 0059E080), members destroyed through mfc::CArray /
CList (vtable written back first; element policies for the server's shared objects, counted objects
and missiles), stlp hash_map / vector / list and AlignedArray. Still forwarded (originals_world.h,
S13b): the grid resizes 00572450 / 005724F0, the first script's destructor 0058A140, the AI map's
0058A450 and the eleven MFC rows the inlined second-script destructor calls. Checks
(verify_record_lifetime.cpp, verify_world_destructors.cpp) are ours only. Reached with 0 mismatches:
the three resets, both worlds' bodies (exit and the start-up checks), the Terrain, particle manager
and Sector slot-0 destructors. The other bodies run inside ours, so their own checks count nothing;
the worlds' slot-0 destructors are not called (exit functions destroy the globals).

**S7a + S13b (2026-10-15).** Script lists with the game's copy semantics (src/game/game_lists.*):
RemoveHead by value for command blocks / definitions / runs (004C69B0, 004C7A10, 004C82B0: the
element copy-constructed into the result, then RemoveAt), AddHead 004C7C90 / 004C9210 / 004C8CE0,
AddTail 004C8D80, RemoveTail 004C9100 / 004C8AF0, NewNode 004CD930 / 004CDA30 with
ConstructElements 004CEB80 / 004CECB0, the command block element destructor 004C54A0 (004C8CE0 is
AddHead; script_construct.cpp's comment says AddTail). Scene records (scene_loader.cpp): FaceGroup
ConstructElements 00454DB0, CArray<FaceGroup> destructor 00454CE0 and the SceneNode destructor
004543E0 (not a CMap). CScript::Destruct 0058A140 (world_destructors.cpp; the server calls it for
both scripts, the second was inlined) with its eleven MFC rows (game_lists.cpp, game_arrays.cpp);
DestructAIMap 0058A450 (ai_map_construct.cpp; the reset 005B5E10 is now it plus the constructor),
LocalSearch::Destruct 0058AA60, CSectorMap::DestructSectors 00599330, SectorGrid::Allocate 00572820 /
005728D0 and Resize 00572450 / 005724F0 (world_client_maps.cpp; the two list instantiations behave
alike). Checks ours only, except ConstructElements 00454DB0 / 004CEB80 / 004CECB0 against the
original on a twin. Suite (128 runs) 0 mismatches: the list copies reached (RemoveHead 004C69B0
32709 calls, RemoveTail 004C9100 166867, ...), CWorldServer::Destruct 190 (it now checks both
scripts and the AI map); SectorGrid::Resize only under Rebirth (vanilla's callers are ours). Not
reached: the scene helpers, 004CECB0, and the eleven rows and other destructors through their own
addresses (ours call each other directly). Left for S6b: wave records 006BE240 / 006BE210 /
006B90C0, the DestructElements rows 004CD900 / 004CEC80 / 004CD740.

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

**S11a (2026-10-15).** Hashtables whose values copy and destroy themselves: `hashtable` got
`CopyFrom` (`_M_copy_from`), `CopyConstruct`, `Assign`; traits `CStringFloatTraits` (hash over the
text, `_mbscmp` equality, the float copied as bits), `StringListTraits` (pair<string, list<void*>>),
`DwordObjectTraits<T, Policy>` (dword key, value by an element policy). 25 addresses: CString->float
004613C0 00461540 004615B0 004616A0 00461750 004A8A50 00580900 004A8AD0 004AA950 005E0DB0
(stlp_hash.cpp); string keys ~hashtable 004A9010 006A6C20, string->list 004A8E60 004AAEB0 0069D130;
the bridge heights' operator[] 005BCDE0 (Map8; the original's new value is the caller's stack dword,
VC6's T() for a scalar: 0 here, its one caller overwrites it); in src/game/game_hashes.*: CParty map
004A91B0 004ABA70 004AD050, the unit looks' ~hashtable 005986B0 (nested FiguredUnit tables)
00598860, the shape cache 005BCCB0 005BE090 005BE320 005BE730 (heights equal when FCOMP says equal or
unordered). Checks (verify_stlp_hash.cpp): CString->float against the original (keys' references
retained before the original's frees, so its release is undone by taking the nodes back), the copies
ours only (same buckets, keys' shared texts, value bits, new nodes); string->list clear against the
original (list nodes, sentinel, string, node trail), find_or_insert and the shape cache's operator[] /
find against the original; destructors, the CParty map (a party's copy cannot be undone) and inserts
of the shape cache (Share marks the shape copy-on-share) ours only, the freed pool blocks checked in
order. The suite reaches 21 of 25, 0 mismatches; not reached: 004A91B0 004ABA70 006A6C20 005BE090.

**S10b (2026-10-15).** The three acks.db tables' resize(n): 006DB9C0 (UnitAcks), 006DBBD0
(ModelAcks), 006DBE20 (ExtraAcks), as `VectorAbi<R, AcksElement<R>>::ResizeDefault`
(record_vectors.h). The head copies its name and the speech map by the hashtable's copy constructor
/ operator= (S11a); a default head is the name and an empty map of 100 buckets. The copied map's
values still point into the source record's speech lists, as in the original. The policy lists the
speech-list arrays itself: `records::Memberwise` takes an array of `stlp::vector` as trivially
copyable (a vector's copy assignment is implicit and trivial), so its array overloads never apply and
a default record's lists were left as garbage. Checks ours only (model text: name, map, every list);
reached at boot, 0 mismatches.

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
**S12 group 3.** The Connection's five maps (src/game/net_connection_trees.*; the members in
src/lib/stlp_tree_abi.h `TreeAbi<Map, E>`, map's `At` renamed `Subscript`): sent ids
(map<int32_t, RefPtr>; the compare is signed), objects, entries, records and attached ids, 127
addresses: constructors (copy through SGI `_M_copy`), clear / destroy, `_M_empty_initialize`,
operator[], the lookups, `_M_create_node`, `_M_insert`, `insert_unique` with and without a hint,
the three erases, the node deallocators and the shared iterator / insert-result / distance /
get_allocator helpers. The originals' operator[] for records and attached ids inserts a value it
leaves partly uninitialised. Checks (tests/verify/verify_net_connection_trees.cpp): lookups against
the original; `_M_empty_initialize` and the constructors original first, the header back to the
pool, then ours (same block, colour byte and links: bytes 1-3 of a refilled header hold the free
list's link); the rest ours only. A start-up check drives each tree 4 x 600 random steps through the
originals and ours side by side. The standalone boot gets past the protection checks' list
constructor 00455E50 (app_helpers.cpp) and stops at the CRT's 006ECE1A, called from ei_hook (S4c).
**S12 group 4.** `basic_string<char>` (src/lib/stlp_string.*, `StringAbi`): `_M_range_initialize`
(with and without the tag), the copy, `_Construct`, from-text, default (an 8-byte block) and capacity
constructors, `_M_append` / append, assign (range, string, text), erase, substr (out_of_range through
the shared forwarder `StringThrowOutOfRangeOriginal`), reserve, push_back, operator< and
`__compare`, the deleting destructor; 26 addresses. Checks (tests/verify/verify_stlp_string.cpp):
the compares against the original, the constructors original first then ours (same block, length,
capacity, bytes), the mutators ours only against the contents before (append's capacity follows
size + max(size, n) + 1), and a start-up script of 8 x 200 random steps through the originals and
ours side by side. The from-text constructor reads its allocator argument (a byte), so callers pass
a real one. Then the last two static-initialiser list constructors of S12's set
(src/game/static_lists.cpp): 004ED540 the DirectDraw driver list 007AEE64, 0056A410 the deferred unit
list 007AF5B4 (start-up check as for 00455E50). The standalone boot then stops at 00572820, the
client's sector grid storage (originals_world.h `GridAllocate`, world area, next to S13b's grid
resizes).

**S10d (2026-10-15).** record_members.h decides plain data by `records::IsPlain<T>` (trivially copyable,
but never CString, stlp::string, stlp::vector or arrays of them; a trivially copyable record with
its own CopyMember / AssignMember / DefaultMember specialises it to false), so arrays of vectors go
element by element and an element type with no policy does not compile. AcksElement copies its
lists as arrays through it (same order as before); smoke and ui_saveload: 0 mismatches.

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

**S8 (2026-10-15).** MFC files, archives, exceptions, AfxMessageBox and the map-free CDC members,
108 addresses (size rows added for each). mfc_exception: CException's constructor and Delete, the
CFileException / CArchiveException destructors, OsErrorToException, the four throws (through the
game's `_CxxThrowException` 006E7819 with the exe's throw information: S4c owns that function) and
AFX_EXCEPTION_LINK; the link chain is the original's thread state (00702107) in the hook build, a
`thread_local` registered image-only at 00702107. mfc_file: CFile, CStdioFile (stream calls on the
game CRT in the hook build, the host CRT in the image, as S4b), CFileFind, AfxFullPath / AfxGetRoot.
mfc_time: CTime's constructors, GetCurrentTime, Format (host `_mktime32` / `_localtime32_s` /
`strftime` in the image, with VC6's time() from GetLocalTime through the import slot). mfc_archive:
CArchive Read / Write / Flush / FillBuffer / counts / BYTE-WORD-DWORD operators, the CString
operators and AfxReadStringLength, CString = LPCWSTR (00700B20) and `_wcstombsz`. mfc_window:
AfxMessageBox: CWinApp's constructor (0070D654) has no caller, so the module state never has an
application and the box always has no owner and the module's file name as its title (the image
shows ei_standalone.exe there); the wrapper checks that against the original's state. mfc_gdi: CDC
SetBkMode / MoveTo / LineTo / FillSolidRect. Checks: pure functions against the original (paths,
CFileFind getters, CTime, CFile Read / Open / lengths on a twin or read twice, CStdioFile line
reads read twice), the rest ours only; start-up checks throw each exception, run files through a
temporary folder and an archive round trip with a 16-byte buffer. Left: S8a.

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
