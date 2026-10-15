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
| Static initialisers | 748 in the .data list 00783004-00783BB0: all generated (S3), with their 130 unported exit functions (S3a); every call target and exit function callee has code in the image (S3b-S3f, S4-S12) |
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

**S4a1 (2026-10-15): function vectors for the conversions.** In the "crt" family (tests/vectors/
crt_format_vectors.h, record_crt_format.cpp, replay_crt_format.cpp): the fixture "@CrtFormatLocale" (the
decimal point 00797FE8 and _pctype's class words for 0..255, taken at the first call after the game set
LC_CTYPE; a call under another locale is not recorded), "vsprintf" (sprintf's calls too: both run the
original _output 006F17D4 on an unbounded string FILE, no trampolines) and "atof" (trampoline, 6 bytes).
The recorder walks the format as _output does (its __lookuptable read from 007476D0) and stores the
arguments read, in order (int, __int64, the double's bits, a %s text up to the precision, null); the
replay lays them out as an x86 va_list. Not recorded: %n, %Z, wide text, outputs over 4 KB, and calls
whose output overlaps the format or a text argument. crt_format.cpp now reads the locale through
GameData and links into ei_vectors (its tolower for _forcdecpt answers from "@GameCrtCtype"; hook_stub's
Standalone() is true there, so WideToBytes leaves the game CRT's errno alone). In record mode the
start-up battery's atof part is skipped (the recorder holds 006E911A, the verify wrapper has no
trampoline). Recorded from ui_hero_stats, ui_hud, items, ui_trade_hold, ui_camp, combat, spells_cast and
ui_main_menu (vanilla): vsprintf 258 records (846 distinct of 1,705 kept per run; 58 with a float: %g,
%.6g, %.1f, %*.*f, %f), atof 200 (220 distinct, all plain decimals: the game data has no exponents),
19 KB in all; every record replays the same. Corrupted copies (one byte of a %.6g output, one bit of an
atof result, the decimal point) fail.

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

**S4d (2026-10-15): the streams.** src/lib/crt_iostream.{h,cpp}, both builds (no state of their own: the
shared lock 007CAE90 and the live-ios count 007CAEC8 are the exe's): streambuf(char*, int) 006FCAD2,
~streambuf 006FCB35 and its deleting destructor 006FCAB6, the base class's virtuals the console's streambufs
inherit (sync 006FCC86, setbuf 006FCB65, seekoff 006FCD18, seekpos 006FCD1E, xsputn 006FCB99, xsgetn
006FCBEF, pbackfail 006FCD30, doallocate 006FCCBB; `Streambuf::Default*`, non-virtual so they can be
registered); ios() 006FCDB1, ~ios 006FCEDC, its deleting destructor 006FCE0F, ios::init 006FCF26;
ostream(streambuf*) 006FC42F (the eight static-initialiser calls), ~ostream 006FC502 and its deleting
destructor 006FC404, both entered with the ios as `this` (`Ios::DestructOstream` / `DeleteOstream`). The
constructors write the exe's vtables. There is no separate log streambuf: the eight ostreams 007C55A8-
007C5810 all write to console streambufs (console.h). Not ported (no callers): streambuf() 006FCA77,
ios(streambuf*) 006FCE94, ostream() 006FC3CE, ostream(const ostream&) 006FC494, ostream_withassign.
fflush 006ED534: image only (crt_file.cpp, the host stream's; a null stream flushes all), also reached from
MFC's CStdioFile::Flush 00701FB8. Check: tests/verify/verify_crt_streams.cpp, original vs ours on scratch
streambufs whose own virtuals record the calls (8 states x 5 behaviours x every operation, 807 cases) and on
scratch ios / ostreams (bytes but the critical section, the count's change, an owned streambuf deleted once);
the game's calls are shadowed (xsputn on every console line, the ostream / ios / streambuf destructors at
exit); the two deleting destructors without trampoline room against the destructor plus operator delete;
fflush in verify_crt_file's scripts. smoke and ui_main_menu, both installs: 0 mismatches. `--standalone`
boot: 006FC42F, 006FC502 and 006FCEDC no longer missing; it still stops at 00411AB0 (S12) first.
The C initialisers the image does not run, checked: 006E728D (__onexitinit: the atexit table, which only
the exe's _onexit / exit read; the image's are ours), 006F239A (__initstdio: `__piob` for the exe's
_getstream / flsall; the image's fopen / fflush are the host's; MFC's CStdioFile::Open 00701CB0 calls the
exe's 006ED1A3 / 006ED350 (_open_osfhandle / _fdopen, its descriptor and stream tables): map those to the
host as fopen is when CStdioFile is ported, S8) and 006F02F9 (VC6's unhandled-exception
filter: terminate() on an uncaught C++ exception; the image keeps the host's filter, which reports the
crash). None is needed now.

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

**S3c (2026-10-15).** The game-side callees of the exit functions. The worlds' destructors (00561B00,
0058AAD0) were S13a's. The five database destructors (src/game/database_destruct.cpp): the map
database 0045E6F0 (the maps' array through gamearrays::ZoneRecords, then the allods': each allod's
CDWordArray of map indices and its six CStrings from the last; AllodRecord now declares them), the
spell tables 00684390 (SpellDatabase::Reset now calls it, then Construct), quests 006ADF40, prints
006D00E0 and acks 006D70B0 (each table's index hash_map::Destroy, then its records through the
record_vectors.h policy, last member first). Library instantiations nobody forwarded, registered
where their families live: ~vector of pointers 0056E800 (the eleven frame lists) / 0069BE20 (the
item stock's candidates) as Vector4::DestroyBase (stlp_vector.cpp: ~vector with ~_Vector_base
inlined); ~list 004584E0 (the protection check list, bytes) / 004F0520 (the DirectDraw drivers:
description, then name) as stlp::ListAbi<T>::Destruct (static_lists.cpp); the CArray destructors
005BF870 (the spiral radius tree's keys, CArray<float>) / 005C4490 (the path smoother's node pool,
vtable 0073FE34, SmoothNodeArrayClass in game_arrays.h) as ArrayHeader<Class>::Destruct
(game_arrays.cpp). The six without a Ghidra function have rows in extra_function_sizes.csv. Checks
(ours only, tests/verify/verify_exit_destructors.cpp, the vectors in verify_stlp_vector.cpp): the
shape before; after, the indexes' counts zero, the arrays' vtables back, a list's sentinel on top
of its free list. They run at exit, after harness_report.txt: their counts are in ei_hook.log.
Group 2, the other game-side exit callees the standalone check listed (Ghidra has their sizes):
CArray destructors 00498E80 (particle draw order), 005C18E0 (spiral offsets), 004C9DB0 (the script
quest's objectives, gamearrays::Objectives); vectors 004A8C60 / 004A8D00 / 004A8D50 (pointers) and
004A8CB0 (16-byte entries: the item stock's scratch vectors), 00421260 (vector<stlp::string>, the
quest files and folders), 00583920 (the zone table, recordvec::ZoneTableVector); the deferred unit
list 0056DEF0 (SectorListDestruct: each object released) and the AI event pool's CList 005CDBA0
(AIEventPoolDestruct). Left: the console's command maps 005DC080 (a tree, S12a) and the MFC
internals (S3d): 0070CC2D, 0070CE77, 0070D5F9, 0070D61F, 0070DC48 (CThreadLocal / CProcessLocal
destructors, jmps to 0070CA59 / 0070CAD2), 0070E181 (AfxDeleteObject of the handle at +0x50 of
007C90C8), 006E690F, 006E695C, 006E6B30, 006E6B7D (base vtable stores) and 00704FE3.

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

**S8a (2026-10-15).** MFC's handle maps and the GDI objects, 35 addresses (size rows added; 0070AB1A,
0070BFD7 and 0070C028 have no Ghidra function: decoded sizes). src/lib/mfc_handle_map: CMapPtrToPtr
(constructor, destructors, InitHashTable, RemoveAll, NewAssoc, FreeAssoc (the last one empties the
map), GetAssocAt, GetValueAt, operator[], RemoveKey; its association has no hash field, unlike
CMap's), CHandleMap's constructor and FromHandle (an unknown handle gets a temporary object of the
map's class under AfxCriticalNewHandler, rethrown after the handler is put back),
CRuntimeClass::CreateObject (a CException on the way is deleted, null returned), AfxSetNewHandler
(_afxNewHandler 00796E14); mfc_exception: AfxThrowMemoryException / AfxThrowResourceException (the
static objects 007C8DE8 / 007C8C58). mfc_gdi: CDC's constructor (CDC is 0x10 bytes: m_bPrinting),
CDC::FromHandle, SelectObject (00708F5A for CPen*, 00708FE6 = slot 10 for CFont*, one body),
SetTextColor (slot 12), DrawText (slot 26), CGdiObject FromHandle / Attach / Detach / DeleteObject,
CPen(style, width, colour), CFont::CreatePointFont / CreatePointFontIndirect, and the create functions
of CTempDC (0070BFD7, fixed allocator 007C8D18, vtable 007463E4) and CTempGdiObject (0070C028,
007C8CF0, 0074645C). The maps are the module thread state's: the hook build keeps the original's
afxMapHDC / afxMapHGDIOBJ (our code calls their addresses), the image registers its own
(`EI_REPLACE_STANDALONE`, a `thread_local` map per thread, never freed) and AfxCriticalNewHandler
(no CWinThread, so no safety pool: it throws a CMemoryException). The game has no CWinApp, so the
temporary maps are never emptied (no OnIdle): every distinct surface DC leaves a CTempDC behind, in
the original as here. Checks (tests/verify/verify_mfc_handle_map.cpp, verify_mfc_gdi.cpp):
GetAssocAt / GetValueAt and CDC's constructor against the original; the point fonts run the original
into a scratch CFont and compare the LOGFONTs; the rest ours only against the original's maps (Attach
puts the object in the permanent map, Detach takes it out, FromHandle's object carries the handle) and
the DC's state (current object, text colour); a start-up check runs 4,000 random map operations
through the originals and ours side by side (every bucket chain compared after each), builds a handle
map both ways, makes temporary and permanent objects in ours, builds the image's map of GDI objects and
throws both static exceptions. smoke, console, ui_main_menu, ui_saveload(_delete), both installs: 0
mismatches (SelectObject(CFont*) 2.7k, CDC::FromHandle 1.4k, DrawText 1.1k, NewAssoc 2k calls in
console). The image installs all five image-only registrations; `--standalone` boot still stops at
005AF580. Left: S8b (resource strings, the exceptions' error messages and ReportError, CFile's name
slots 4-6; they need the module state's resource handle and game.exe's string table in the image).

**S6a (2026-10-15).** The CArray / CList / CMap functions only the exe's vtables point at, 400
addresses (src/lib/mfc_slots.*, game-element ones in src/game/game_slots.*). Scalar deleting
destructors (slot 1) are one template, `ScalarDelete<&Destruct>`, over each instantiation's
destructor (S6 / S7 policies; new ones for elements destroyed through their vtable, aligned blocks
and CStrings at fixed offsets); the four exception classes' destructors are jumps to std::exception's.
Serialize (slot 2) is afxtempl's with the default SerializeElements (each element's raw bytes,
whatever it holds) over S8's CArchive: `SerializeCArray / SerializeCList / SerializeCMap<Abi>`; the
element policy must be the one the instantiation's inlined SetSize / NewNode uses, which is not always
its destructor's (00450570 builds bone-link arrays its destructor deletes through slot 1). Checks:
destructors ours only (vtable left, header emptied, slot owner); every Serialize at start-up next to
the original (stolen bytes) on memory archives: the stored bytes, and loads into empty, growing and
shrinking objects. No suite scenario calls a Serialize. Left: S6c (33 Serialize whose elements are
counted references or script records, about 20 destructors).

**S6b (2026-10-15).** The last CArray<T> instantiations of game records, 46 addresses
(src/game/record_arrays.*, verify_record_arrays.cpp): the figure's 0x220-byte parts (a class 0x42
figure mesh: CFigureMesh::ConstructBase, then the plain mesh vtable), the scene's materials, meshes
(SceneMesh::Construct: CTransformObject's base, the name, the face groups) and nodes
(SceneNode::Construct, 00454F80's body; the two quaternions at +0x30 / +0x40 are now named), face
groups and their array header; network delta fields; music themes (element constructor and
destructor, the array destructor 006B56C0); the sound cache's wave records; the brain's behaviour
array destructor 00557890; patrols and zone exits by value (0046EAB0 / 0046EFD0, 00584EA0 /
00587DD0); the global map's records (both copies of each); the script's command, nested-call,
definition, run and block arrays, the command and nested-call copies (004CC0C0, 004C6D10), the
bindings' and nested calls' copy constructors (004CCCB0, 004CCD90) and the by-value GetAt
wrappers 004CD450 / 004C7510. Checks: element constructors that allocate nothing on a copy of the
bytes next to the original; SetSize growing ours first, then the original on a twin; the GetAts and
the bindings' copy constructor ours first, then the original into a twin whose blocks are freed;
the allocating constructors (materials, wave names, nested calls), destructors and command copies
ours only. Known difference: 004CC0C0 copies a command's float arguments as dwords, ours through
SSCCmd's assignment (the FPU): only a signalling NaN would differ. 00557890 and 006B56C0 were also
on S6c's destructor list; their deleting destructors (005587E0, 006B5980) stay there.

**S6c (2026-10-15).** S6a's leftovers that S6b's element policies made possible (record_arrays.*,
plus small members elsewhere). Destructors: the arrays of scene nodes (004514B0), nested calls
(004C5180 / 004CCF70, base vtable 0073D418), delta fields (0050FBC0), behaviours (00545BA0, a copy
of 00557890), the terrain's aligned records (006C4C40), sector vertices (006C4690: each vertex's
lights back to the pool) and the sound cache's CList<WaveRecord> (006BC870), with all their scalar
deleting destructors and those over 00557890, 006B56C0, 00454CE0; SceneMesh::Destruct (00454830;
the aligned blocks at +0x98 / +0xC8 are now members), CFigureMesh::DestructPlainMesh (00454510, class
0x42: DestructMesh without its vtable; DestructMesh now calls it); the game's inlined ~CGdiObject
(00444B30, 00444B90, 005FDDE0 and their deleting destructors: CGdiObject::Destruct in mfc_gdi).
Serialize (SerializeCArray over the policies; each start-up check against the original, Mode 2 where
a destroyed copy would free what the source owns): figure parts, scene meshes, materials, nodes,
face groups, nested calls, definitions, delta fields, behaviours (now built by their constructor:
00558E10 registered), themes, sector vertices, water levels (CArray<CArray<float>>, element helpers
00575690 / 005755C0) and sectors. Checks of the destructors are ours only; in the suite only
004C5180 and 00444B10 are reached. Left for S6d: the 19 list Serialize whose loader builds, assigns
and releases a temporary (counted references, script records) and 004C2700 (SerializeElements<CString>).

**S12a (2026-10-15).** The rest of the exe's STLport maps (src/game/library_trees.*, one
`TreeAbi` alias each, node sizes from each `_M_create_node`): the debug panel's map<string, string>,
the .mob loader's tint lists (map<string, list<16-byte record>>, dead with 00481660's branch), a
link mesh's part headers and ranges, the link loader's mesh cache (a multimap of counted
references: `equal_range`, `insert_equal`), the item screens' model and texture caches (two copies
of every member), the console's command maps and the interface's help topics (CString keys:
`map`'s third argument is now the key order, `stlp::CStringLess` is `_mbscmp < 0`,
stlp_tree_cstring.h), the "pers" texts and the global map's arrivals; 213 addresses, plus the four
connection maps' destructors (00433710 ...) and the 0x30-byte list constructors 00478D50 / 00479150 /
005F90E0 (library.csv calls them `_Rb_tree`; they build a list sentinel: stlp_list.cpp `List48`).
`TreeElement` builds a pair from key and value (`Make`), so operator[] constructs its value in
place and destroys the temporary once; `PairElement` destroys the value, then the key. New
`TreeAbi` members: the pair's destructor and scalar deleting destructor, `_M_destroy_node`,
`insert_equal`, `equal_range`; `TreeWordPairConstruct` (pair<dword, dword>). 004F4CD0
(`LinkPartHeader()`, 0x50 bytes zeroed) and 005F8FC0 (the model cache's destructor in the item
screens' unwinding: each model released, then the map) are plain functions. Six game-kind rows the
tint-list map uses got library.csv rows (00482570 ... 004827C0). Checks
(tests/verify/verify_library_trees.cpp, sharing tests/verify/stlp_tree_verify.h with the
connection maps): the per-call wrappers as for S12 group 3, and a start-up script per map
(4 x 400 steps through its registered originals - insert, hint insert, operator[], erase at a
position, clear, destroy - and ours side by side; strings with high bytes, ASCII CStrings, lists by
their elements, null references). Left in the ticket's ranges: 95 rows that are lists,
hashtables, vectors, CArrays and unwind helpers (S12c).

**S12b group 1 (2026-10-15).** STLport's sort internals moved from src/game/stl_sort.h to
src/lib/stlp_algo.h (`stlp::Sort<T, Less>`: median of three returning a reference, the pivot copied
once, unguarded partition and inserts, heap sort at depth 0, the final insertion sort), now for
element types by value too. src/game/stl_sorts.cpp registers the sorts our code forwards to, 12
addresses: the file chunks by index 0042A020 / 0042A150, CollectChanges's {object, priority} pairs
by the object's address 0043F870 / 0043F9B0, the picking hits with DepthLess 00573740 / 00575BC0 /
00576D40 (the original inlines the comparison in the scans; its sums only swap the first two terms,
so `DepthLess::Less` serves), the battle screen's ids 0060B1E0 / 0064B750 (less<int>), the
connection screen's server list by its sort key 0061EF60 / 0061F6F0 / 0061FB00 (keys 1-8 the column
pairs, names by `_mbscmp`; copies only share the CStrings). Checks (tests/verify/verify_stl_sorts.cpp),
all against the original: it sorts a copy, ours the range, the orders compared element by element
(the original's recursion re-enters the wrapper); the unguarded insertion sorts copy from 16 before
`first`; hits with a missing object or a non-finite depth go to the original alone (logged). The
suite sorts short ranges only, so a start-up check sorts 300 random arrays of ids, pairs (equal
objects, distinct priorities) and chunk pointers and 200 server lists (empty strings, keys 3-9)
through the originals and ours, a quarter with depth limits that reach the heap sort. smoke,
console, camera, combat, net_lobby, ui_mp_host: 0 mismatches. Their inner helpers (median,
partition, heap, linear insert) are reached only from the replaced loops: dead in both builds. Not
done: the objectives window's CString sort 0064F310 / 0064F9A0 / 0064FBD0 (its comparison builds
strings and looks them up in the quest map 007C87B4, allocating on every call) and the save list's
SaveEntry sort 00657DF0 / 00657F10 with copy_backward 00657D50 (its copies allocate CStringArrays).

**S12b group 2.** STLport pair members (src/lib/stlp_pair.*, `PairAbi`), 10 addresses: the dword
pairs' constructor 00407DE0 / 0040A9D0, pair<string, dword>(const string&, const dword&) 005729D0
00689950 006BC140 0064F880 006A7710 (one body; 0064F880 is reached from the objectives window's
quest sort, still the original's), pair<string, string>(const string&, const string&) 00445170
(library.csv calls it the copy constructor), its destructor 00445040 (the second string first; the
row's name is "unreviewed family") and scalar deleting destructor 004457F0. Checks
(tests/verify/verify_stlp_pair.cpp): the dword pairs against the original into a scratch pair; the
string pairs' constructors original first, its blocks back on their free lists (the last allocated
first), then ours (same blocks, words, bytes, dword); the destructors ours only (the first string's
pool block on top of its free list; 004457F0 starts with a CALL inside five bytes, so no trampoline).
smoke, console, camera, combat, ui_objectives, ui_saveload, harness_digest with goldens: 14 the
same, 0 mismatches; 0040A9D0, 00689950, 0064F880 and 004457F0 not reached. The full `--golden` suite: 128 runs pass
(rebirth_flags vanilla's known first-run DistancePointPoint flake), 54 goldens the same; ui_widescreen_hud
and ui_widescreen_more (rebirth) differ at world update 80 (cunit148 #place) with all 22 S12b addresses
in disabled.txt too, so not from S12b. The sort start-up check skips itself when a sort is disabled
(no trampoline). Left for S12d: the allocating sorts (objectives, save list), pair<string, list<T>>
0069C1C0 and the unclassified container rows.

**S12c (2026-10-15).** S12a's lists, hashtables, vectors and unwind helpers, 70 addresses on the
shared shims (no new container code): lists (stlp_list.cpp) - begin / end 00478FC0 00478FD0 00478FE0,
the 0x18-byte lists' constructors 004827D0 / 004FB3C0 and destructors 004829B0 / 00482CC0 (library.csv
calls them clear), ~list of 16-byte nodes 005F9620, insert 00482980, `_M_create_node` 00482C30
(`ListAbi::CreateNode`, new), the sentinel given back while a constructor unwinds 00482790 / 004FB840
(`ListPutSentinel<32>`), iterator-- 004FB570 (`ListIteratorDecrement`), get_allocator 00479130 and
the iterator's copy 004FB3B0 (the tree shims), the link loader's list<pair<string, dword>> (insert
004FB730, create 004FB8B0, clear 004FBCF0; `StringDwordElement` in stlp_pair.h) and list<CString>'s
create 005F9C70; pair<string, dword>(const string&, const dword&) 004FB170 (library.csv: string's copy
constructor). Hashtables (stlp_hash.cpp): dword keys of 8 bytes - begin 004FB090 005E0C00, the
iterator's constructor 00482DD0 005E0D50, `_M_skip_to_next` 005E0E00, ~hashtable 00482DF0 (unwind
only); the link loader's figure sets (string keys, 20-byte nodes: pool sizes 17-24 share one free list)
- ~hashtable 004F6A50, clear 004FBC80, skip 004FBB80, dead in the hook build (their caller 004F6BD0 is
ours). Vectors (stlp_vector.cpp): the base constructor 00482A80 00619600 006316C0, ~_Vector_base of
1, 4, 8 and 12-byte elements (10 addresses), deallocate(p, n) in both forms (11), `_M_fill_insert` of
dwords 004FBA40, `_M_insert_overflow` of 8-byte pairs 006317E0 (from 006312C0) and its
`uninitialized_copy` 006325E0 / `uninitialized_fill_n` 00632610 (`VectorAbi`, new). Game types
(src/game/library_containers.*): ~list<CMemoryStream> 00478F10 (the loopback queue, unwind only),
~list of a key and two CStrings 005F9570, the briefing's shown-item `_M_create_node` 0060A010, copy of
its speakers 0060AB60 (CString operator=), the RefPtr `_Construct` 0060AA80 and `copy_backward`
0060AAA0, a held engine object's `_Construct` 004FB680, CString's destructor 005F9680 / 005FBE20 (a
jump to 00700970; library.csv calls the first exception::~exception) and scalar deleting destructor
005FBCB0 / 0060ABE0, and four unwind helpers (00445FC0 a word's CString, 00482E90 clears a dword,
005F60B0 two pointer vectors, 004F69F0 the counted root's vtable 0073C368), and 005E0550 (library.csv: a pointer iterator's operator-; it is CTime::operator-(CTimeSpan), 005E0400 calls it after a CTime constructor). Several library.csv
names for these rows are wrong (destructors named clear, a list create named hashtable
`_M_new_node`); the rows were left as they are (add-only). Checks
(tests/verify/verify_library_containers.cpp): against the original - begin / end / get_allocator /
iterator copy and --, the hashtable begin, iterator constructor and both skips, the vector base
constructor, uninitialized_copy (into a scratch buffer), CTime - span; the list constructor, insert and create
original first, its node back on the pool (LIFO), then ours (same node, same bytes); the frees with a
trampoline original first, the block taken off its free list again, then ours; `_M_fill_insert` with
room on a twin over a copy of the storage in our memory. Ours only (they destroy, release or copy
CStrings and counted pointers): the destructors (the nodes on top of their free list in order), the
pair<string, dword> list, the CString copies (the count one less when shared), the RefPtr copies
(counts), the rest by their effect. A start-up check drives all 70 through their game addresses with
scratch objects (fake counted objects that never reach zero); in the suite only the dword hashtable
members 005E0C00 / 005E0D50 / 005E0E00 (console: 1411 skips), 005E0550 (console, once) and 005FBCB0 (S12a's console map check) are
reached. smoke, console, items, world_load, models, ui_screens, ui_base_dialogue,
ui_global_map_pick, ui_camp, ui_saveload, net_lobby, ui_main_menu, ui_mp_host, both installs: 26 pass,
0 mismatches; after merging S12d the full `--golden` suite: 128 runs pass, 56 goldens the same, 0 mismatches. Not taken: S12a's CArray rows (S6b/S6c, listed in the ticket's note) and the rows of
S12d (it ported 0060A660 and 0060AB00; 005F8E90, 005F92A0, 005F94B0 and 005FB810, "unreviewed family" rows, are still unregistered). Unregistered twins outside the
ranges with the same instructions: 00429060, 00429DB0, 004298F0 (the RefPtr and held-object copies),
004FAF10 (a char vector's ~_Vector_base), 0042B3C0, 0042B3F0, 00404D50, 004E15C0, 005E1560, 00457940,
004FC0C0, 004217C0.
**S12d (2026-10-15).** The allocating sorts (src/game/stl_sorts.cpp on `stlp::Sort<T, Less, Ops>`;
stlp_algo.h now takes an element policy, `Held<T, Ops>` temporaries and `CopyBackward`, with every
by-value copy, assignment and destruction where STLport's has them): the objectives window's quests
0064F310 / 0064F9A0 / 0064FBD0 (`ByQuestOrder`: each comparison is QuestTable::Find of both names,
which adds a missing name, so the allocations are the original's; a name without a record compares
true even against itself) and the save list 00657DF0 / 00657F10 with copy_backward 00657D50
(`SaveEntryOps`: CopyConstruct / Assign / Destruct). Every save helper tests 00658020
(a.savedAt < b.savedAt) and takes the other branch, so the order is `!(a < b)`, newest first, and
equal times compare true both ways (not a strict weak ordering). pair<string, list<T>> 0069C1C0 in
stlp_pair.cpp. Checks run ours first (SV2f), the original after it on a twin: the quests on a copy of
the CStrings (the lookups only find now), the saves on key-only entries (empty strings and arrays,
the position in the game time) compared by each entry's strings, time, flag and arrays; a quest range
with a name without a record, or more than 16 saves with two equal times, is sorted by ours alone and
logged (the unguarded scans could leave it). copy_backward ours only. A start-up check sorts 340
save lists with distinct times (40 with small arrays: sizes and capacities compared). The
container rows: list begin / end 00438640 00438820 / 00437F20 00438650 00438830, the iterator's copy
004387C0 and decrement 00439A60, splice 004380C0 / 00438770 (checked on twins of both lists' nodes),
the 0x10C / 0x120-node lists' constructors 00439CE0 / 00439960, ~list 00439D40, clear 0043D420 and
erase 00438660 (operator new / delete), plain erase by node size (16: 0040AA40 00512030 0056DFB0; 24:
0040A920 00411540 004437D0; 32: 005392F0; 40: 00443730) and clear (00405500 0040B390, 24: 0040B290),
vector<uint16> insert_overflow 0043B530, vector<8 bytes> erase 00439DD0, frees by element size
(00439D80 0053B7A0 0053BEB0 0069C9D0 00613630 0056FB80), and the out-of-line `_Construct` 0043D7E0 /
0043D7F0 / 0064B620 and copies 0043B360 (memmove) / 0043DC80 (stlp_vector.h `ConstructAt`,
`CopyTrivial`, `CopyElements`). Left (S12e): the connection's lists whose elements own something
(SentPacket's items, the freed-id batches' vectors: copy constructors, default inserts, erase, clear,
~list), its find_if and counted-pointer init, two plain hashtable erases, a hashtable clear and two
constructors, three vector-member destructors and the hero screen's string sorts and vector members.

**S3d (2026-10-15).** The MFC callees of the exit functions, 21 addresses (size rows in
extra_function_sizes.csv: library functions need one even with a Ghidra function; 0070CAD2 and
006E6B7D take decoded sizes, Ghidra's run past the RET). src/lib/mfc_state: CThreadSlotData::FreeSlot
(0070C71F), CThreadLocalObject's destructor (0070CA59 and the CThreadLocal<T> jmp thunks 0070CC2D /
0070D5F9, registered at their own addresses), CProcessLocalObject's (0070CAD2; thunks 0070CE77,
0070D61F, 0070DC48), AfxDeleteObject (0070AA54) and AUX_DATA (afxData 007C90C8): its constructor
0070D1AA (a static initialiser's call target), destructor 0070E181 (the menu-dot bitmap),
UpdateSysColors 00703E12, UpdateSysMetrics 00703E56 and the two scroll-metric helpers 0070D158 /
0070D188 (cdecl on afxData in the original; ours take `this`, the same object). mfc_exception: the
four simple exceptions' destructors (006E690F, 006E695C, 006E6B30, 006E6B7D: each stores its own
vtable). mfc_window: CWnd's destructor 00704FE3 (the ticket's "CStdioFile-like" one: the four objects it
skips are wndTop / wndBottom / wndTopMost / wndNoTopMost) and CCmdTarget's 00703EC8; CWnd::DestroyWindow
007051A1 stays a forwarder (originals_library.h): no game window is a CWnd, the image reports it if
reached. The locals hang off the original's state in the hook build; in the image nothing fills them
(S8a's maps are thread_local), so the destructors find slot 0 / no object. Not registered:
0070CD25 (another ~CThreadLocal thunk, no exit function calls it). Checks
(tests/verify/verify_mfc_state.cpp): a start-up check runs FreeSlot, ~CProcessLocalObject,
AfxDeleteObject (by the process's GDI object count: GetObjectType still typed deleted bitmaps), the
exception destructors, ~CWnd and ~CCmdTarget on twins through the trampolines and ours, and AUX_DATA's
constructor both ways (also against afxData without the metrics: InstallHooks makes the process DPI
aware after the static initialiser ran); the exit wrappers check the slot freed, the object kept,
CWnd's vtable and the control site. smoke, ui_main_menu, console, ui_saveload, both installs: 0
mismatches. Seen, not caused here: on Rebirth the exit stops after the handle maps' ~CMapPtrToPtr
(an access violation in Riched20.dll, or the game still running at the harness's 3 s limit); main's
own runs end the same way (S3e). `--standalone`: the only exit callee without code left is 005DC080
(S12a); the MFC constructors the static initialisers still call (the four exceptions', CWnd(HWND),
0070B600, 0070DC4D, 00700674) are S3f. The boot still stops at 005AF580.

**S3f (2026-10-15).** The MFC call targets of the static initialisers, with the callees the image
needs, 16 addresses (size rows in extra_function_sizes.csv; 0070C3C5 and 0070DC4D take decoded sizes,
0070B5F0 / 0070B600 have no Ghidra function). mfc_exception: CException(BOOL) 0070205D and
CSimpleException (0x98 bytes: +0x0C m_bInitialized, +0x10 m_bLoaded, +0x14 the message, +0x94 the
resource id; +0x08 is never touched) with the four classes' constructors 006E68C9 / 006E6916 /
006E6AEA / 006E6B37. mfc_window: CCmdTarget's constructor 00703EAE and CWnd(HWND) 00704546.
mfc_state: CSimpleList::AddHead 0070C4E9, AfxClassInit 00700674, MFC's global locks AfxCriticalInit
0070C3C5 / AfxLockGlobals 0070C458 / AfxUnlockGlobals 0070C4C8 (17 lock types: _afxResourceLock
007C8EE0 ends where _afxCriticalWin32s 007C9078 and _afxThreadData 007C9084 start; an 18th type
overwrote _afxThreadData in a first try), _AfxInitDBCS 0070DC4D (GetCPInfo of the OEM code page),
and wingdix.cpp's halftone brush: 0070B600 registers AfxWingdixTerm 0070B5F0 with atexit
(_afxWingdixTerm 007C8D44), which deletes _afxHalftoneBrush 007C8D40. AfxGetModuleState 0070CE8C
stays the original's in the hook build (the shared `mfc::impl::AfxGetModuleState`, which
verify_mfc_window.cpp now uses too); the image registers `ImageModuleState` there (one module state
for the process, nothing but the class list, linked at m_pNextClass +0x14). Checks
(tests/verify/verify_mfc_static_init.cpp): a start-up check runs CException(BOOL), the exception
constructors, CCmdTarget's constructor, AddHead, AfxCriticalInit and _AfxInitDBCS through the
original's trampolines and ours on twins; against the original's initialisers' results: the four
static exceptions (vtable, auto-delete, id), the four static windows (whole), the five classes in the
module state's list (in reverse order), _afxDBCS and _afxWingdixTerm (ours registers the term once
more: it runs twice at exit, the second time on a null brush); the locks on two unused lock types,
one through the original and one through ours (initialised, owned twice, once, free); AfxClassInit
on two fake classes (the head put back). Per call, ours only or against the original on a copy.
smoke, ui_main_menu, console, ui_saveload, both installs: 0 mismatches (the runs fail only on main's
second registrations at 00445040 / 00445170 / 004457F0, S12a against S12b). `--standalone`: static
initialisers without code 0 call targets, 0 exit functions, 0 exit callees; the boot still stops at
005AF580. In the image AfxCriticalInit and the locks run over the exe's data copy (zero, so first
use initialises them).

**S16 (2026-10-15).** The first boot of the image, stop by stop (`work.py test S16 --standalone`, then
`tools/resolve_crash.py` on the run). Each stop was ported at its address with a check; stops in
another ticket's family took only the instantiation on the path (or waited for that ticket's merge).
In order: the AI grids' constructors 005AF580 / 005AF590; the script definitions' SetSize 004CD770
(S6b's now); the smooth table 005C4210; the console command map 005E0A90 (S12a's now); streambuf
006FCAD2 (S4d); AfxClassInit 00700674, the static exceptions 006E68C9.., MFC's static data 0070B600
(S3f's now); after that all 410 initialisers run and WinMain starts. Then list<int>::clear 00405500 and
the chunk stack's erase / clear 0040A920.. (S12d's now); _memicmp 006FD2F0; the sound emitter
constructor 006B63D0; the x87 stack forms of acos / asin / fmod 006E7334 / 006E83D4 / 006E95A0
(src/lib/crt_x87.cpp; fmod keeps the CRT loader's NaN payload); the text source node 004E15C0; the
music themes' SetSize 006B59A0 (S6b's now); AfxBeginThread 007022A0 / AfxEndThread 00702380 with
CWinThread's constructor 0070C259 / 0070C2F6 (src/lib/mfc_thread.*: image only, host _beginthreadex,
the current thread object thread-local, no message filter hook); the stream thread's entry 006B6A10;
CIniFile's destructor 00695BE0; the unit model slot's destructor 0053F010; the lever records' copy
00465B20; DirectInput (below); the figure classes' Create jumps 005A6C70 / 005A6E00; the game's
~CGdiObject copies 00444B10.. (S6c's now); sscanf 006EAEEC (crt_format.cpp: MSVC 6's `_input` for
the integer, %n, %c / %s / %[ conversions; floats and wide characters end the scan; a start-up battery
of 37 formats x 33 texts against the original; a format that starts with %n is left out, the original
then reads a stale stack byte as its last character); lower_bound<unsigned*> 00408A50 (the hashtable
constructors' prime lookup); CStringArray's GetAt by value 004698A0 / 004698C0 (record_arrays.cpp);
the script keyword table (below); the client sector grids' lists of held objects, clear 005703C0 /
00573660, erase 00572270, ~list 00572E90 / 0069BE70 (`stlp::HeldObject` destroys with the object's
Release, slot 4). The run then stops at 005593D0, the server's unit appearance cache build
(CWorldServer::SendZone, the main menu's world): 1883 bytes over about 20 unported hash_map and
vector helpers (0055A..-0055D..), filed as S16a. The main menu is not reached yet.
- Code read as data: ClassifyToken's keyword table is the immediate at 004CF69B (Rebirth rewrites
  it); the image has INT3 there, so `KeywordTable` gives the vanilla table 0078BCA0 when standalone
  (through GameData, so ei_vectors' stand-in still holds it). No other GameData read of a code
  address is left in src.
- DirectInput in the image: DirectInputCreateEx refuses an instance that is not a loaded module
  (E_INVALIDARG), so standalone.cpp routes the image's import to a wrapper that gives the host's
  module for 0x400000. c_dfDIMouse (00745D00) points at its object table in game.exe's code section
  (006E6180), which the image fills with INT3: `CDirectInputMouse::MouseDataFormat` is our copy (the
  same seven objects and the game's axis GUIDs; a start-up check compares it with the original's).
  Other dinput.lib data in .text, if any, would need the same.

**S3e (2026-10-15).** The Rebirth exit hang was not ours and not MFC's: the original code with `--no-hooks`
hung the same way (143 of the 146 cached Rebirth reference runs ended "still running 3 s after the scenario
quit"; only main-menu runs got out). ei_launch now writes `exit_hang.txt` (each thread's EIP and return
addresses as module+offset) before it ends a game hung after quit; it showed one thread left, in ntdll's
process shutdown -> addon.dll's DLL_PROCESS_DETACH (addon+1022E, +101DB) -> addon+BCE5 -> CreateProcessA
(through AcLayers' shim) -> USER32 -> win32u. addon+BCE5 starts `<system32>\CTFMon.exe` and waits up to 2 s for
it when the starter switch `Settings\CtfMon` is 1 (it is here); the same switch makes addon send WM_DESTROY to
the `CicLoaderWndClass` window (ctfmon) when it applies its patches, so every Rebirth run also ended the user's
text input service. The harness answers CtfMon 0 in every run (tests/verify/harness_starter.cpp, through the
advapi32 RegQueryValueExW slot it already answered for WideScreen; now patched in every run). After it, the full
suite (128 runs) and render_present / render_movie_present / render_device_lost on Rebirth exit by themselves,
and every Rebirth log ends with its `verify total` lines. The log's last "first call" (~CMapPtrToPtr) was only
the last hooked function reached for the first time, not where it stopped. The Riched20 access violation of
ui_main_menu-rebirth was not seen again in these runs. R9c6 and X32 are the same cause.

**S8b (2026-10-15).** MFC's resource strings, the exceptions' error messages and CFile's name slots,
17 addresses (size rows added, all Ghidra sizes). src/lib/mfc_resource: AfxLoadString 00702B92
(LoadStringA on the module state's resource handle through the exe's import slot), AfxFormatStrings
0070B1AD (lead bytes by _mbctype's _M1 bit; "%0" reads the pointer before the array, as the
original), AfxFormatStrings(UINT) 0070B173 and AfxFormatString1 0070B2AC; mfc_string:
CString::LoadString 00702B0E (`LoadResourceString`; CString(MAKEINTRESOURCE) now calls it directly);
mfc_window: AfxMessageBox(UINT) 0070ACCC; mfc_exception: CException::ReportError 0070209C (slot 4 of
all six exception vtables; the help context starts at 0 where the original's is unset), slot 3 of
CFileException 00703135 / CArchiveException 0070A795 (prompt 0xF1A0 / 0xF1B0 + cause with the name,
AFX_IDS_UNNAMED_FILE's text for none) and of the four simple exceptions 006FE346 with InitString
006FE31D; mfc_file: CFile::GetFileName / GetFileTitle / GetFilePath 00702C31 / 00702CB3 / 00702D35,
GetStatus 00702D64 (`CFileStatus`, 0x118 bytes), AfxGetFileName 0070D0D2 and AfxGetFileTitle 00701AC9
(comdlg32's GetFileTitleA through import slot 0073B748; the thunk 006FC044 has no other caller and is
not registered). The resource handle: the original's module state has instance and resource handles
null (the game never runs AfxWinInit; a start-up check confirms it), so `ImageModuleState`'s null ones
are already right: LoadStringA(NULL) loads from game.exe in a hooked run, and the image's LoadStringA
wrapper (S14) maps NULL to game.exe loaded as a data file. game.exe's table has every MFC prompt the
library loads (30 of the 32 ids the check asks for; the two missing are the check's made-up ids).
Checks (tests/verify/verify_mfc_resource.cpp), ours first (SV2f / SV2h): LoadString, the format
functions and the name getters fill the real string, the original a scratch one made afterwards; the
cause messages into the caller's buffer and a host buffer; InitString and the simple exceptions'
GetErrorMessage on a copy taken before ours ran (objects compared); GetStatus and the path parts into
host memory. Ours only: AfxLoadString (a CALL in its first five bytes; buffer against length, the
resource handle against the image's), AfxMessageBox(UINT) and ReportError (modal boxes). The suite
reaches none of them (error paths), so a start-up check drives them through their addresses: 32 ids
(also into an 8-byte buffer), nine format strings and four format ids, every file / archive cause with
and without a name, copies of the four static exceptions (unloaded, loaded, a missing id), path parts
of seven shapes and the three names of a temporary file (open, open without a name, closed). smoke,
console, ui_main_menu, ui_saveload, both installs: 0 mismatches (AfxLoadString 222 calls,
AfxFormatStrings 107, CFileException::GetErrorMessage 60). Not exercised: LoadString's growing loop
(no string in the table comes within two bytes of 256).

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

**S10c (2026-10-15).** 004AD640, the trader stock's recipe map pair destructor (pair<string, list<recipe*>>):
`PairAbi::DestroyStringList` in src/lib/stlp_pair.cpp (the list's nodes in order and the sentinel back to
the pool via `list::Destroy`, then the string's block); item_stock.cpp's `RecipePairDestroy` calls it.
Verified ours only (verify_stlp_pair.cpp: the string block on top of its free list, the sentinel on top of
the 0xC list unless the string's block shares it); reached by trader_stock (22 calls, 0 mismatches, both
installs); items passes. The size row already existed (function_sizes.inc, 120).
