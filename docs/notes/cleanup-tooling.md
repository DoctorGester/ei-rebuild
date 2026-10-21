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
  never reads ECX takes no `this` (`GameFlowOriginal()`, `SetServerFlagOriginal(int)`: the old `this` arguments
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

## Retiring container forwarders (C13b1-C13b3x)

Measure with `python tools/direct_calls.py --report -v --cast --loose --library` after a `build.cmd verify` (it reads the map);
check_cleanup's "EI_ORIGINAL forwarder" count is the ratchet (C13b2: 1,426 to 798; C13b3: to 365; C13b3x: to 294). Forwarders to library containers go once the
storage is typed; then the call is the container's member or its ABI type's static member. The helpers this uses:

- `mfc::impl::ArrayAs<Abi>(array)` (lib/mfc_array.h) views a `CArray<T>` as the policy instantiation `Abi` registers (same element
  size, checked); `ConstructHeader` / `DestructHeader<Class>` / `ConstructArrayOf` / `DestructArrayOf` and lib/mfc_list.h's
  `ConstructListHeader<Class>` replace the per-instantiation constructors and destructors of a header written in place.
- `PlainElements<CString>` constructs and destroys its elements (`CArray<CString>` is MFC's CStringArray element code).
- STLport: a `_Vector_base(alloc)` constructor is `ConstructEmpty()` (or the three pointers nulled when the field is raw), an alloc
  proxy constructor is the null it stores, a node / block deallocation `stlp::deallocate(p, n * size)`, `resize(n)` the instantiation's
  `VectorAbi::ResizeDefault` (`recordvec::ResizeUntyped<Abi>` where a serialiser holds the vector untyped), a map's constructor /
  destructor / find / operator[] the `stlp::map` members (the map's key order must be the instantiation's: CInterface3D's help topics
  are `stlp::map<CString, int, CStringLess>`).
- Lists: `mfc::impl::ListAs<Abi>` / `NewNodeAs` / `FreeNodeAs` / `RemoveAtAs` view a CList as its registered instantiation;
  `stlp::AsNode<T>` / `AsRefNode` view a node declared by its own struct as `list<T>::Node` for `ListAbi<T, E>`'s statics.
  Element policies (`HeldObject`, `SharedRef`, `CountedRef`, `ConstructElementsAt`) pick the instantiation, not the call site.
- Trees and hashes: `stlp::TreeAbi<Map>` / `HashtableAbi<Traits>` statics over the registered instantiation (library_trees.h,
  game_hashes.h); pointer-triple vectors go through `VectorAbi` statics; serialisers by member pointer.
- A forwarder to a rewrite under another name (`Refresh` / `RefreshArea`, `CallCopyTo` / `CopyToImpl`, `ShowHelp` / `OpenHelp`) is
  dropped and the callers name the rewrite; a base-class body hidden by a derived declaration is a qualified call
  (`CMapObject::DestructBody()`, `CObject3D::Construct()`, `SessionBase::TickImpl(dt)`). Hooks are retired, so "through its address
  so the verify wrapper runs" no longer holds a forwarder; tests/verify's do stay (a vanilla run must call the game's own code).

Left on purpose: Rebirth-patched callees, the skip list, the vector recorders and replay stand-ins, harness-replaced entries,
WordsSetAtGrow and ViewStackDestroyRange.

C13b3x took check_cleanup's count from 364 to 294 (direct_calls.py: 301 in scope, 299 left, 2 convertible but kept). What made the
rest convertible:

- `ei_vectors` links the caller without the target: direct_calls.py now also accepts a target that a `tests/vectors/replay_*.cpp`
  defines, so a replay-linked caller calls the target and `tests/vectors/replay_unlinked.cpp` gets an `EI_UNLINKED("name")` stop
  for each target the replay does not link (a call exits with "not linked into the replay"). replay_crt.cpp's stand-ins are now
  definitions of `libcrt::Mbsinc` / `Mbscmp` / `Atoi` and the others, replay_script_load.cpp's of `ScriptRunWorldScript`.
- lib/mfc.cpp's CFile / CFileFind / CStringArray are no longer an `#else` branch: `ei_vectors` links mfc_file.cpp and mfc_coll.cpp.
  `mfc::CFileFind` has `FindNext` / `GetTitle` / `IsDots` / `Close` (not `FindNextFile` / `GetFileTitle`: Windows' headers define
  those as macros). mfc_file.cpp / mfc_time.cpp call the host CRT only (the hook build's game-CRT branch is gone).
- No-registration rows rewritten: a patrol's destructor (`DestroyPatrol`), net_lobby's query vector through
  `stlp::ConstructDefaultAt` / `CopyAssignTagged<Query, recordvec::QueryElement>`, ui_saveload's `ConstructCopyAt`, counted-pointer
  holders through `RefPtrRelease`, renderer_sprites' `lib::AlignedArray::SetSize`.
- One `PartyRecords` (unit.h; player_messages.cpp has `Find`), `CCamera::Update` called directly (`CameraUpdateOf` is the image's
  fallback for the frame's call target), the shape cache through `gamehash::InitShapeHandle` / `ShapesOf` and `CAIMap::CellMark`.

C13d took the count from 294 to 284 (the CRT / MFC names below and the exit function):

- A `gamecrt::` name with a port is a one-line call of the port (`strncpy` / `strncmp` / `strnicmp` / `tolower` / `isdigit` /
  `isspace` / `write`, like `mbscmp` / `srand`); ei_vectors defines the `libcrt::` names it links instead (replay_crt.cpp:
  `Tolower` / `Isdigit` / `Isspace` from the recorded ctype, `Strncpy` / `Strncmp` / `Strnicmp`). `mfc::PlexCreate` /
  `PlexFreeDataChain` call mfc_list.cpp's `impl::CreatePlex` / `FreePlexChain` (ei_vectors links mfc_list.cpp).
- A recorder that needs the original's answer has its own forwarder: record_crt.cpp's `GameIsdigit` / `GameIsspace` /
  `GameTolower` (the "@GameCrtCtype" record is the game CRT's classes).
- A function-local static's exit function is registered as the generated `ei::static_init::ExitXXXXXXXX`
  (src/game/at_exit_functions.h), not through a forwarder to its address (`Terrain::SetSun`'s sorted sectors, 006C9270).

Still forwarders after C13d, and why:

- `CrtThreadDataOriginal` (006F00D1): in a vanilla game.exe run the harness reads rand's seed through `libcrt::RandState()`, whose
  non-image path is the game CRT's thread data.
- `CWndDestroyWindow` (007051A1): unreachable in the image; porting it needs afxMapHWND (007048B7) and `CWnd::Detach` (007049A7).
- `ManagerFindObject` (00403F40): its one caller is world_client_draw.cpp, left for the renderer work (R16).
- `DrawMesh` / `DrawMesh2` (originals_render.h): `CTextureDevice::DrawMeshBlocks` / `2` take other argument lists; after R16.
- tests/verify (`HarnessLoadUnitFaces`, `HarnessMappedResourceConstruct`, blink / pick / class templates): a vanilla run must call
  the game's own code.
- crt.cpp's `operator_new` / `operator_delete` / `malloc` / `free` (replay_crt.cpp has its own heap): the harness runs inside
  game.exe for the goldens (`--no-hooks`) and frees blocks the game allocated, so its allocations must stay the game CRT's own
  code (the ports' `HeapAlloc` on the game's heap would be the same only while the game's CRT uses the system heap, which no run
  checks). `SectorMapAdd` / `GameAtExit` (replay stand-ins), `SeedAt` / `rand` (the harness replaces them).

## `__asm` and `float_control` (cleanup ticket C12)

C12 and C12a took `__asm` blocks in `src/` from 221 to 3 and left one `float_control` pair. The build is `/fp:precise` on x87. The rules the
conversions follow (they keep the goldens and `ei_vectors` unchanged):

- A `double` local or a `double` expression stands for an unrounded register: at 24-bit (main thread) and 53-bit precision control,
  storing an x87 result to a double changes nothing. Promote at the first operation (`static_cast<double>(a) - b`); `static_cast<float>`
  or a float local is the FSTP.
- Keep `x87::fsin` / `fcos` / `fexp` / `flog` / `fpatan` / `fistp` (their results are 64-bit significands; use them inside
  the expression so the compiler takes ST0 as it is: checked in the object code for `ClampPow`, `CircleScaled`, `WallScorchShape`).
  `gamecrt::Ftol` is the game's `_ftol`, `libcrt::CiPow` / `CiFmod` the bodies of `_CIpow` / `_CIfmod`, `x87::FromUnsigned` the FILD of
  an unsigned.
- FCOM status: write the plain comparison the source had (rewrite guide rule 8; the NaN audit in "SSE2 build"); `x87::CompareStatus`
  is gone. An FLD / FSTP copy of a float quiets a signalling NaN; the rebuild's copies are plain (`std::memcpy` of the four bytes,
  `std::bit_cast` for the bits): see "SSE2 build".
- An original returning in ST0 is called through a `double(__fastcall*)(self, void*, ...)` pointer (the value stays unrounded).
- Sums over vector components use the `lib/vecmath.h` Unrounded helpers (`LengthSquared2DUnrounded`, `DotUnrounded<Sum3::...>`), which
  `check_cleanup` asks for.

`vecmath.h`'s `float_control(precise, on)` pair was redundant under `/fp:precise` (0 of 723 objects compiled differently without it)
and is gone; the `precise, off` pair around the Unrounded helpers stays (39 objects differ without it). The long transcriptions
(`InvertModelMatrix`, `CrownBlend` in figure_part_draw, `EmitFirstSection`, `EmitSection` in renderer_strip; C12a) are C++ whose
object code does the same operations with the same rounding points: a value the original keeps on the FPU stack across a loop or a
reload is a `double` local (CrownBlend's unrounded red average for the first colour, `double redTarget = red` then the rounded one),
and a float the original reloads after storing is read back from the float. Check a conversion by tracing the compiled x87 stack
(`dumpbin /disasm` of the object, before and after): the compiler adds harmless qword stores of doubles and float stores/reloads
for every `static_cast<float>`. Left: the
register / stack shims `NewKeepingEdx` (renderer_init), `CallWithOne` (sound_manager) and `UpdateMotion` (unit_motion_tick, reads
the caller's frame), which go with the call-site work (C12b). `include/ei/x87.h` and `src/hook` (the hook framework) and
`src/lib/crt_x87.cpp` (the CRT's register entries) keep theirs by design.

## SSE2 build (C16 experiment)

What an `/arch:SSE2` image costs against the goldens, and which `x87::` helpers convert for free (2026-10-21). The build switches
are on branch `c16-sse2-experiment` (not merged): CMake `EI_ARCH=SSE2` and `EI_X87_PLAIN=<bits>` (helper families as plain C++, bits
in its `include/ei/x87.h`). A verify build outside `build\<cfg>` runs with `EI_BUILD_DIR=<dir>` and
`EI_HARNESS_SETTINGS=<checkout>\config\harness` (the harness looks for `..\..\config\harness` from the DLL).

**`/arch:SSE2`, helpers kept or all plain: 110 of 110 goldens differ**, all from the first record (world update 20, the menu's
scene: place / AI hashes of its walkers); every run still passes its scenario, and the console differs only in printed distances
(`1.440063m` / `1.440066m`). ei_vectors: 3 of 69 functions fail: `IsaacRandom::Range` (118 of 256 records, 1-2 float ulps),
`UnitParams::ProtectionBlock`, `UnitStats::Reduce` (10 of 256; two are NaN payloads: x87 keeps the operand's NaN, SSE gives the
default `-nan(ind)`). The cause is not the helpers but the doubles:
- A `double` local or expression stands for the x87 register (C12): at the main thread's 24-bit precision every operation rounds to
  a float significand, under SSE2 to 53 bits. Example: saves' `stats.sideCone` -4.37113883e-08 becomes 6.12303177e-17, because
  `RightAngleCosine`'s `g_pi * g_perDegree` is a 24-bit product at PC24 (cos of float 90 degrees) and an exact one in SSE2;
  `world timeSpeed` 59.9999962 becomes 60.0. About 1270 double locals, 1040 `static_cast<double>` and 3370 `D()` promotions
  (27 files) are such registers.
- Also: x87 intermediates have a 15-bit exponent; NaN payloads propagate differently; terrain lighting's chop rounding
  (`libcrt::Controlfp(0x300, 0x300)`) sets the x87 control word only, so SSE arithmetic there would round to nearest (an SSE2 build
  has to set MXCSR too; `crt.cpp`'s Controlfp, not `crt_x87.cpp`).

**Helper families, one at a time on `/arch:IA32`** (everything else as today; 110 goldens, ei_vectors 0 in every row):

| Family (sites) | Plain C++ | Goldens | Why |
|---|---|---|---|
| `fistp` (197) | `std::lrint` | same | the CRT's lrint is a FISTP that follows the x87 rounding control (chop too), under SSE2 as well; but NaN and out-of-range give 0 where FISTP gives 0x80000000: kept as the instruction: lrint also reads the FP environment per call (SV10j's helper replaces both) |
| `fsqrt` (149, done: C16a, double argument kept) | `std::sqrt` | same | on IA32 `_CIsqrt` is FSQRT at the precision control (converted, C16); under SSE2 it is SQRTSD, 53 bits |
| `fsin` / `fcos` (159) | `std::sin` / `cos` | same | the CRT's 53-bit results against FSIN / FCOS's 64; equal after the float store over 800k float arguments; not bit-exact in principle (converted, C16, separate commit) |
| `fpatan` (27) | `std::atan2` | same | as fsin (converted, separate commit) |
| `flog` / `flog10` (16) | `std::log` / `log10` | same | FYL2X ignores the precision control (converted, separate commit) |
| `fexp` (7) | `std::exp` | **97 differ** | VC6's inline exp rounds its FMUL / FSUB / FADD at PC24 (about 24 bits accurate): 64% of float arguments give another float, up to 3.6e-7 relative. One site does it all: `unit_client_path`'s heading weight `x87::fexp(rate * kDecay)` (checked alone: the same 43 records as the whole family); cunit702's `rot.w` 0.112546213 / 0.112546332 at world update 45 in gz1g, then its place |
| `FromUnsigned` (85) | `static_cast<double>` | not run | rejected by check_x87 on IA32 (the FADD 2^32 rounds at PC24); exact under SSE2 (check_x87 would need an SSE2 mode: SSE2 code also loads 2^32) |
| `CopyFloat` / `LoadStoreBits` (57) | the bits as they are (`memcpy` / `bit_cast`) | same | no signalling NaN is copied in the suite (converted, C16b, the helpers are gone). Behaviour change: a signalling NaN float copied at those sites keeps its bits, where the original's FLD / FSTP set the quiet bit (0x00400000); only a NaN with a clear quiet bit read from a file or a message is affected |
| `IsNaN` / `CompareStatus` (116) | `IsNaN` false (the plain comparison) | same | no NaN reaches those sites in the suite (converted, C16c: the NaN audit below) |

**NaN audit (C16c).** The rule: a comparison is the plain C++ one the source had (VC6's TEST AH,0x40 `==`, TEST AH,1 `<`,
TEST AH,0x41 `<=` / `>`, a C0 mask `a < b ? x : y`); where an `x87::IsNaN` / `CompareStatus` term only reproduced how FCOM counted an
unordered result (equal and less), it is gone, and a NaN compares as IEEE says. Where detecting NaN is the code's purpose (data
checks) the check stays (`x87::IsNaN`): none of the ~116 sites was one, so `IsNaN` is left only in `lib/nan_audit.h`, and
`CompareStatus` / `kC0`-`kC3` are gone from `x87.h`. Per site:
- **Plain, NaN result changes (85 `EI_NAN_AUDIT` sites, 47 files):** zero tests (`ZeroOrNaN` helpers renamed `IsZero`, the
  particle sizes / bends / alphas, lengths before a normalise, the script `Not` / flag tests / `PowerFactor`), equality (`Same`,
  `Equal`, `Differs`, zone variables against 1, quest states, hash key heights), less-than selects (`MinOf`, `MaxZero`, `AtMost`,
  the larger of two extents or reaches), the script comparisons (`IsEqual`, `IsLess`, `IsInSquare`, `IsInArea`, `GetPlayerUnits`:
  `static_cast<double>(side) == f`, the FILD compare). Each has `EI_NAN_AUDIT(values...)` (`src/lib/nan_audit.h`): in the verify and
  debug builds (`EI_NAN_AUDIT_LOG`, CMake) the first NaN per site writes `NaN audit: a NaN compared at <file>:<line>` to ei_hook.log;
  in release it is nothing.
- **Plain, NaN result the same (about 25):** where an equally plain form keeps the original's result: the ordered tests (`a > b` for
  TEST AH,0x41 jump-if-clear: `Exceeds`, the HP drop, `rate > 0`, `duration > 0`, `IsGreater`), `h >= kHeightSilent`, `limit >= distance`,
  the bounds a NaN never replaces (device_bounds `x < minX ? x : minX`, terrain_sector `z <= minZ`, ui_stats_rows `s >= largest`),
  `SetDiplomacy` (`> 0` allied, `< 0` enemies, else neutral), the client turn loop (`if (!(gap > kHalfTurn)) break;`: the plain
  `gap <= kHalfTurn` would never end on a NaN), and all of addon.dll's status tests (an IEEE compiler; they read C2:
  `Max` `!(f < m)`, `Min` `!(f > m)`, `Abs` / `Round` `x < 0`, `Random` `high > low`, `NearestUnit` `distance < radius`, `Equals` `==`).
  Four selects on integer-sourced floats (ui_objectives' map sizes, ui_hud_portraits' mana) cannot see a NaN and carry no audit.
- **Behaviour change** (NaN only): every audited comparison now takes the IEEE branch: e.g. a NaN length is normalised (a NaN vector)
  instead of left alone, a NaN zone variable counts as "not visited", a NaN script argument is "not zero" and "not equal", a NaN
  height marks a moved object changed, a NaN hash key height never matches (each lookup adds an entry), `CCamera::StopMotion` keeps
  a NaN turn / tilt rate where the inlined SetTurnRate(0) replaced it. The suite has none: goldens identical (110), and the audit
  line appears in no run's ei_hook.log.
- Not converted (follow-up C16e): the NaN-aware `!(a >= b)` helpers of the earlier port (`LessOrUnordered`, `ZeroOrUnordered`,
  `EqualOrNaN` in ui_item_strip / unit_record, `NonZero`, `AtMostOne`, ... in about 30 files), which keep the original's NaN results
  without `IsNaN`.

**Main-thread CPU per frame** (render_stats, vanilla 4K, best of three alternating runs on a shared machine, ms): SSE2 against x87
menu 2.49 / 2.47, zone1 5.13 / 6.02, fight 18.07 / 19.10, town 7.39 / 7.98, camp 3.94 / 3.98, map 0.96 / 1.03: 0-15% less in
the game scenes, within noise in the menus (the SSE2 fight's state has diverged).

**Oracle.** Bit-exact SSE emulation of the x87 is not a helper job: it means writing every main-thread double expression as float
steps (and fexp as VC6's sequence in float steps), plus the exponent range, NaN payloads and MXCSR chop, with double rounding left as
a rare residue that no proof covers. Recommended: convert the free families now (done for all but fexp; call-site renames and the
NaN / copy items are tickets), keep the x87 build as the oracle until the switch, and at the switch re-record the goldens from the
SSE2 image after checking it at the behaviour level against the x87 image (same scenario results and console events, digests within
tolerances, saves field by field with float tolerances). Multiplayer against original clients is the one place a bit-level difference
could matter (the delta-coded network floats): check it before the switch.
