# Cleanup tooling

Tools (all in `tools/`; each file's docstring says how to run it) and what the cleanups learnt. The plan is
`docs/cleanup-plan.md`; the rules for new code are in `docs/rewrite-guide.md`.

## Checks

`build.cmd` runs `check_cleanup`, `comments.py`, the data checks and the three check executables through
`tools/build_checks.py`, side by side (a failure stops the build; the list and why each runs are in build_checks.py;
`--times` prints each one's seconds). The others are run by hand before handing work back.

| Tool | What it catches |
|---|---|
| `check_stolen.py` | the stolen-byte count of every `EI_RECORD` (vector recorders, which run inside game.exe) ends inside an instruction, is under 5, or copies IP-relative instructions (a wrong count crashes only when the function first runs); a recorder not at a known function start, or a second one at an address |
| `check_x87.py` (after `release`) | unsigned 32-bit to float done the modern way (`FILD` + `FADD 2^32` rounds at the precision control) in the x87 code: use `x87::FromUnsigned` (the SSE2 code is left out, "SSE2 build" below) |
| `check_signatures.py` / `check_signatures.cmd` (after `verify`) | a recorder, a forwarder or the rewrite an `EI_RECORD` names (the vectors' replays call it with recorded arguments) whose stack arguments differ from the original's `RET n`; other functions are free (argument order is not checked) |
| `check_cleanup.py` | per file, the counts of offset access (`At<>` / `Field<>`), `VCall`, `__asm`, `float_control`, raw game addresses and `*At` views may only go down (`config/cleanup_baseline.csv`; a new file starts at zero; `--update` after a cleanup) |
| `progress.py` | the cleanup artefact counts (below) |
| `find_dead_code.py` | updates `config/dead_code.csv`: keeps every hand-added row, rewrites its own generated rows in place, never removes (prints generated rows no longer dead); refuses arguments |

## Build time

Measured on the 16-core machine (32 threads), quiet, 2026-10-29: a clean `build.cmd verify` is about 59 s (release, play
and asan about 56-58 s, debug 53 s), a no-op 3 s, one touched .cpp 7 s, a touched `game/maths.h` 48 s. The compile
is the whole of it: ninja runs 34 jobs and keeps them all busy until the last few seconds; about 1,700 s of compiler time,
577 `src/game` files at 2-5 s each. In a file the front end is ~90% (headers ~1.1 s: `<string>` 0.19 s,
`game/map_object_figures.h`, `ai_map.h`, `unit.h`, `ai_event_table.h` ~0.35 s each), the back end ~0.1 s. The link
of ei-rebuild.exe is ~2 s, the checks (`tools/build_checks.py`) ~1.6 s, vcvars 0.8 s.
- How to see it: `.ninja_log` in the build folder (start and end ms per step); `-Bt+ -d1reportTime` on one cl command
  (from `ninja -t commands ei-rebuild.exe`) gives the front / back end split and every header's inclusive time.
  `ninja -t deps` lists every object's project headers. On a machine other builds share, compare the CPU time of
  the whole ninja run (a job object's accounting), not its wall time: three alternating clean verify builds gave
  1,443-1,479 s before the header diet below and 1,312-1,318 s after (-10%; wall ~55 -> ~50 s), a touched
  `game/unit.h` 1,035 -> 945 s.
- Header diet: a header that only holds or passes RECT / POINT / SIZE / HWND / HANDLE / CRITICAL_SECTION includes
  `lib/win_types.h` (windef.h and minwinbase.h, ~0.02 s) instead of `<windows.h>` (~0.2 s); a file that calls Windows
  or uses its constants includes `<windows.h>` itself, and `lib/gameapi.h` (windows.h, mmsystem, shellapi, objbase,
  dsound, ~140 dynamic initialisers) only where it calls `gameapi::`. ~210 of ~750 objects still reach windows.h,
  140 gameapi.h. Left: `game/ui.h` (160 objects) keeps windows.h because its member `PlaySound` is a mmsystem.h
  macro (every file sees `PlaySoundA`; without windows.h first the names would differ); `game/ui_item_info.h` keeps
  gameapi.h for `TrimItemCache`'s `gameapi::timeGetTime` (a template body).
- `lib/import_table.h`, `import_prototypes.h` and `gameapi.h` are in the harness's reference key (run_all
  `harness_files`): a change there replays every original run. Leave them as they are for build time.
- An include change can change how MSVC inlines `std::basic_string` members, as the precompiled header did: model.h
  without `script.h` changed ~25 objects, party.h without `map_object_figures.h` 3. Check each include change with a
  per-function `dumpbin /disasm` comparison of the objects (only `__LINE__` immediates may differ).
- The game headers form a dense web (most files reach unit.h, world.h, model.h through several paths); a header
  edge mostly needs its target by value or as a base. Dropping an edge pays only when few includers need the target:
  model.h's `chunk_archive.h` (a forward declaration of ChunkArchive) took it out of ~500 objects.
- Tried, no gain: the 64-bit hosted compiler (`vcvarsamd64_x86`: the same code, the same time); `/Z7` (+4-8 s).
- A precompiled header of the standard headers saves ~18% of a clean build (58 -> 47 s) but is not used: it changes
  the inlining of `std::basic_string` members in ~120 objects (code compared with `dumpbin /disasm` per function), so
  x87 code around them could round differently. Game headers in it break the harness's views (tests/verify) and
  per-file names.
- ei-rebuild.exe links with `/OPT:REF /OPT:NOICF`: unreferenced code goes (static objects' initialisers stay: .CRT$XCU
  references them). `/OPT:ICF` would save another 6% but gives identical functions one address.

## `progress.py` cleanup artefacts

Counted over `src/*.cpp` and `*.h` with comments blanked first (`tools/srcscan.py`): naked functions, files with
`#pragma float_control`, unknown_XXX fields,
`At<>` / `Field<>` uses, files defining their own `At()`, `ei::VCall` uses, game data addresses (distinct literals
`0x0073xxxx`-`0x008Fxxxx`), `Fn(T* self, void*)` shims (the old thiscall shape, with or without `__fastcall`; src has two left, the harness hooks' stand-ins), struct names defined in several files, and the rest listed in its output.

## Redundant pointer casts (`cast_scan.py`, C21)

`python tools/cast_scan.py --jobs 4` lists the pointer `static_cast`s the compiler finds redundant: the operand already
has the type (SAME), only const is added (CONST), or an implicit upcast (UP). It builds a rewritten copy of the tree
under `build/cast_scan` (every `static_cast<T*>(` becomes a call whose overloads for the three kinds are deleted, so
each one is an error at its call site) and writes `build/cast_scan/sites.txt`; the worktree is not touched. MSVC folds
repeated diagnostics with the same text and location, so the deleted overloads (an error at the call site) are what
makes every site show, not a `static_assert` or `[[deprecated]]` in the header. Dropping them:
- SAME: always (`(&x)->f()` becomes `x.f()`).
- CONST: not the ones adding `volatile` (the x87 reads of `effects.cpp`, `particle_common.h`), nor ones that pick a
  const overload.
- UP: only when no class between declares the called name. The UI constructors hide `UiContainer::Construct`
  (write `w->UiContainer::Construct(ui)`); an `auto*` local keeps the base type it had (`CEngineObject* texture = ...`).
- A cast in a template body shows for one instantiation only: leave it.
- The forced header is C++20-only (`_MSVC_LANG` guard): CMake's compiler check builds a test file with the
  default standard under the same `/FI`. One site does not build (`world_server.cpp`, a volatile
  pointer-to-pointer CONST cast); the scan lists the rest.
- Downcasts that stay should go through one named accessor where the class is known by construction
  (`CMapObject::ServerCompanion()`, `CUnit::GetWeapon` / `AsWeapon` (item.h), `FindItem`, `UnitWithId` for a
  unit's id, `LightFactory::LightWithId`, `AIBrain::FindPlayerMode` / `FindPatrolMode`,
  `UnitOrNull` after a class test, `ObjectSlots` where a generic lookup is a
  map object; `CWorld::ObjectWithId` and `WorldServer::FindObject` already return `CMapObject*`, and the server
  world's id maps but `timedObjects` are `MapObjectIdMap`s of `CMapObject*`; a network session's message
  lists are `std::list<SharedRef<T>>` / `std::list<HeldRef<T>>` (core_refcount.h, ref.h: `element.Get()`); `FigureManagerOf(object)` / `OwningFigureManager(object)` (figure_manager.h) for the world's or
  the owning figure manager; `CMapObject::ClientCompanion()` on the client as `ServerCompanion()` on the server;
  `CUnit::Controller()` for the controller's own slots (also from inside UnitClientSpecific, which is not
  declared as a UnitController); `itemdb::WeaponOrArmorAt(weapon, index)` for the record head both share;
  `CTexture::Manager()` / `ParticleSource::Manager()` for the owning manager; particle_common.h's `Carrier(object)`
  for the attached object's map object slots; `AsQuest` / `AsArmor` on a `CItem*` (CPlayer::items holds `CItem*`)),
  not a cast at each use. `UnitOrNull` replaces a class test only where the original tests the class first and the
  pointer is never null there (the null test it adds changes nothing); keep the cast where the original
  calls something between the test and the use.
- A raw block constructed by hand (`operator_new_as<T>()` then `Construct()` / placement new, `Destruct()` then
  `operator_delete`) is `new T(...)` / `delete p` once T has its constructor and destructor (C35g: the sky, the
  hit numbers, the console, the cursor images, the sectors, the figure models, the part sets, the regions);
  `count` plain elements are a `std::vector<T>(count)` (C58: the AI grids' `SearchGrid::data`, the memory
  stream's buffer, the screen copies; `std::unique_ptr<T[]>` for the sound voices, whose addresses Miles holds). A block of a byte count other than its type's (a DIB header with its
  masks, a CPlex with its elements, an .mmp file) is `::operator new(bytes)`. crt.h has no heap wrappers left
  (C35m deleted gamecrt::operator_new / operator_new_as / operator_new_array / operator_delete).
- A deleting destructor through slot 0 is `delete p` on the object's own type when its class (or a base)
  declares the virtual destructor (EngineObject's slot 0): no upcast to EngineObject first.

## Forwarders (C3, C13, C29)

src has no `EI_ORIGINAL` forwarder since C29 (from 1,426 at C13b2; the macro is the harness's only, so C29a dropped the
always-zero counts from progress.py, dashboard_data.py, standalone_gaps.py (gone since C20 batch 6) and check_cleanup): our code calls every game function by name, the type
puns the forwarders hid are `reinterpret_cast`s at the call, and the harness changes the image's behaviour through named seams
(`IsaacRandom::SetSeedHook` / `SetDrawObserver`, `libcrt::SetRandObserver`). The shared headers (`originals_<area>.h`),
`gen_originals.py`, `check_forwarders.py`, `direct_calls.py`, `RedirectCode` / `CodeLink` and the vector replay's stand-ins went
with them. What is left calls game.exe's own code in its runs: tests/verify (`harness_original.h`; each forwarder sits behind
`ei::Standalone()` with our function on the other side) and record_crt.cpp's recorders. ei_vectors links the callers without
some targets: `tests/vectors/replay_unlinked.cpp` stops each one the replay never reaches (`EI_UNLINKED`) or stands in for it.
Since H30 nothing of ours runs in game.exe's runs (the harness calls game.exe's own heap, rand seed and functions there;
the host heap once crashed 104 of the original's runs when the harness freed blocks the game allocated), so crt.cpp's
heap and rand seed are the image's only. Since H30b nothing of ours decides what the harness reads there either: it
reads game.exe's objects through its own views (tests/verify/exe_objects.h, our member names at game.exe's offsets as
they were at the split) and calls game.exe's virtuals at game.exe's slots, and reaches our classes only in the image
(tests/verify/image). A cleanup that reorders, retypes or renames members of ours (C14a and later) changes neither
the original's runs nor the reference cache's key (tools/check_harness_key.py); when a cleanup renames a member, the
view may keep the old name. Of src/ game.exe's side still builds from src/hook (but standalone_main.cpp), rebirth.cpp,
image_data.cpp with image_cells.inc, game_data.h, gameapi.h with import_table.h, and intro_sort.h.

## `__asm` and `float_control` (cleanup ticket C12)

C12 and C12a took `__asm` blocks in `src/` from 221 to 3 and left one `float_control` pair. The build is `/fp:precise` on x87. The rules the
conversions follow (they keep the goldens and `ei_vectors` unchanged):

- A `double` local or a `double` expression stands for an unrounded register: at 24-bit (main thread) and 53-bit precision control,
  storing an x87 result to a double changes nothing. Promote at the first operation (`static_cast<double>(a) - b`); `static_cast<float>`
  or a float local is the FSTP.
- Keep `x87::fsin` / `fcos` / `fexp` / `flog` / `fpatan` / `fistp` (their results are 64-bit significands; use them inside
  the expression so the compiler takes ST0 as it is: checked in the object code for `ClampPow`, `CircleScaled`, `WallScorchShape`).
  `libcrt::Ftol` is the game's `_ftol`, `libcrt::CiPow` the body of `_CIpow` (`_CIfmod` is std::fmod since C26), `x87::FromUnsigned` the FILD of
  an unsigned.
- FCOM status: write the plain comparison the source had (rewrite guide rule 8; the NaN audit in "SSE2 build"); `x87::CompareStatus`
  is gone. An FLD / FSTP copy of a float quiets a signalling NaN. Floats are copied with `a = b` (a Vector3 the same, a float cleared
  `f = 0.0f`); a float that arrives from a file, a message or a script slot and must keep its bits goes through `x87::CopyBits`
  (include/ei/x87.h; `check_cleanup` counts raw 4-byte `memcpy` / `memset` of a float or vector, C43), a float seen as its bits is
  `std::bit_cast`: see "SSE2 build".
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
for every `static_cast<float>`. The register / stack shims
`NewKeepingEdx` (renderer_init), `CallWithOne` (sound_manager) and `UpdateMotion` (unit_motion_tick) went in C12b: their callers are
ours on the standalone, so the helper's flag is passed whole (the constructor reads its low byte), the codec's close is a plain
`__stdcall` call, and the movement tick's stale local starts at 0 (combat.md, "Movement tick"). `include/ei/x87.h` and `src/hook` (the hook framework) and
`src/lib/crt_x87.cpp` (Hypot's x87 addition of a NaN) keep theirs by design.

## SSE2 build

### The drawing-only code on SSE2 (C30a)

The build is split by file: the renderer (`ei_render`) and the drawing-only game code (`ei_draw`, the list
`EI_SSE2_GAME_SOURCES` in CMakeLists.txt: the interface, particles and effects, weather, sky, lightning, the terrain's
draw passes, waves and ambience, decals and prints, the old renderer's glue, textures and surfaces, the music and the
unit sound names) build with `/arch:SSE2`; everything else, the harness and the vector replays stay `/arch:IA32`. One
compile option chooses (`$<TARGET_PROPERTY:EI_SSE2>`): cl warns (D9025) when a second `/arch` overrides the first, so
the two cannot be stacked per file.
- **Why a whole-archive library:** an inline function kept out of line in both an SSE2 and an x87 object is one COMDAT,
  and the linker takes the first object's copy. `ei_draw` is linked as `$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_draw>`: every
  object kept (static initialisers too), all read after ei_hook's own, so the x87 code always gets its x87 copy. An
  OBJECT library or `$<TARGET_OBJECTS>` puts its objects first on Ninja's link line: the wrong way round.
- **Checked (verify build, 2026-10-29):** of the 9,448 functions the map takes from SSE2 objects none is also defined
  by an x87 object, and no SSE2 object loads or resets the x87 control word (FLDCW, FNINIT, `_control87`) other than
  render.cpp's `FpuGuard` putting back a saved word: SSE2 code leaves the main thread's 24-bit precision alone, and calls
  into x87 code see it unchanged. (One-off script: dumpbin /disasm and /symbols over the objects plus ei_hook.map.)
- `check_x87.py` skips the SSE2 objects and the CRT's `ftol3.obj` (`__ltod3` and its kin, which only SSE2 code calls:
  their 2^32 is SSE arithmetic, not a FADD at the precision control).
- **What stays x87 and why** (CMakeLists' `EI_X87_DRAW_SOURCES`): picking reads the drawn view and the objects' screen
  rectangles (renderer_init's matrix stacks, the projected meshes device_mesh / device_bounds / device_shadow, the figure
  and link mesh draws, world_client_draw / _view), the screens that turn a pixel into a world point, an order or a
  choice (ui_hud, ui_hud_map, ui_hud_pick, ui_battle_hover, ui_battle_commands, ui_view_screen, ui_global_map*, the
  camera keys and mouse), the briefing (camera moves, phrase timing), device_object (sets the 24-bit precision),
  the terrain's heights, sectors and loading, and sound_manager (below).
- **Hit tests in whole pixels (C30a3):** the item and figure strips went back to x87 in C30a because the figure strip's
  click `(x - 50) * 0.01f + position`, truncated, picked the neighbouring figure in double (`ui_hero_screen` record 11:
  x 250 is 200 pixels in, 200 * 0.01f is 1.99999996 in double and 2.0 at PC24). The rule since: a hit test is integer
  (pixels and cell widths, a float position snapped once: `UiItemStrip::SlotAt`), and a held button's repeat counts
  whole milliseconds of the frame (`UiContainer::FrameTicks`: the strip arrows' `scrollTicks`, the stats panel's and
  the trade panel's `holdTicks`), not a float sum of seconds. Results match the original's but on an exact border
  (pixel or millisecond). With that ui_item_strip_setup, ui_item_strip, ui_hero_screen, ui_hero_screen_click,
  ui_stats_buttons and ui_trade_panel_input are on SSE2 and through C30b's rewrite; the trade panel's experience
  sums (`Experience`, `PurchaseCost`) are float maths, the original's PC24 sums exactly.
- **Sound:** a speech's handle reaches the game: `UnitClientSpecific::Say` / `SpeechFilter::Finished` clear the server
  unit's talking flag (`MapObjectFlag::Blocked`) when the speech's voice is no longer busy, and Play2D answers -1 when
  every voice plays at the speech's priority or above. Whether a voice is free follows from Play3D's level
  (`UpdatePositional`, float: a silent one-shot takes no voice), so sound_manager.cpp stays x87. The callers' maths
  (positions, radii, ambience volumes: terrain_ambience, unit_client_sounds) is on SSE2: it can move a 3D sound's level
  across 0 only at an exact boundary, and only matters when all 15 voices are taken. Sample lengths (`WaveLength`) are
  kept on the voice but read by nothing; the harness ends samples by Miles' length on the virtual clock; no save
  holds music state.

### Plain float maths in the SSE2 files (C30b)

The SSE2 files (the list above and src/render) lost the x87 era's spellings by script, in two steps; the x87 files
and the shared headers' inline helpers (`D` in particle_common.h, camera.h) are untouched.
- **The helpers** (a text rewrite): `x87::fsin / fcos / fpatan / flog / fexp` -> `std::sin / cos / atan2 / log / exp`,
  `x87::fistp(static_cast<float>(e))` -> `std::lround(e)`, `x87::FromUnsigned(u)` -> `static_cast<double>(u)` (exact on
  SSE2; a float would drop the low bits of millisecond times), `x87::CopyBits(a, b)` -> `a = b`. `std::lround` rounds
  halves away from zero where FISTP rounds them to even: an exact .5 now goes up (ui_camp's
  `std::lround(item->condition) <= 0` sell test is the one site whose integer is more than a colour or a count).
- **The casts** (driven by clang's AST: zig's clang in clang-cl mode, i686-msvc, `-ast-dump=json`; offsets are bytes,
  and a location carrying `includedFrom` is a header's, not the file's): a `static_cast<float>` of a float dropped; of an
  int dropped where it is an operand of an arithmetic or comparison operator whose other side is a float that is not
  a cast too (two dropped in one division made `value / maximum` an int division); of a double expression rewritten
  as float maths when every leaf can be a float: `D(x)` / `static_cast<double>(x)` of a float -> `x`, of an int ->
  `static_cast<float>(i)`, double literals -> their float spelling, the std maths of such -> the float overload; and a
  local `double` that is only read and whose initialiser converts became a `float`. Parentheses stay where the cast
  sat in an operator. Re-parsed until nothing changes (1,769 edits in 66 files, before terrain_shadow went back).
- **Counts** (code, comments left out; the 186 SSE2 files): `static_cast<float>` 2,226 -> 815, `static_cast<double>`
  322 -> 141, `D(` 2,750 -> 704, `double` 1,099 -> 613, `x87::` 193 -> 6 (render.cpp's `FpuGuard`, terrain_animation,
  terrain_shadow).
- **Left for hand review (C30c)**, by why the script stopped: a double variable, member or constant among the leaves
  (335: `kPi`-style `constexpr double` constants, double locals that are written, double members), an int cast whose
  context is not a float operator (241: initialisers, arguments, an int against a double), a double-returning call
  (107: `Unsigned`, `Range`, `FrameTime`, the `*Unrounded` vector helpers, `fmod`), a double macro (81: the particle
  files' `EI_*` constants), a 32-bit unsigned or 64-bit int (14: times and random words would lose bits as floats),
  3 others; ui_connect_screen (clang rejects bit_flags.h's `operator|` template there) and the two files below were
  not run. The file comments that say "`double` for values the original keeps on the FPU stack" describe what is left.
- **Golden that sent files back:** `terrain_water_waves` (the water sectors' vertices are in its dumps):
  terrain_animation (positions and speculars: `std::lround` without the float rounding picks another sine-table
  entry and colour byte) and terrain_shadow (speculars from record 0: `EdgeFade`'s alpha, FISTP's half-to-even
  against `std::lround`). Both are as C30a left them, x87 helpers included; any rewrite there needs that golden
  re-recorded or the exact rounding kept.
- The scripts (one-off, not in tools/): `renames.py`, `casts.py`, `clang_ast.py`, `files.py` in the C30b scratch
  folder named in the hand-back.

### The whole image on SSE2 (C16 experiment)

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
- Also: x87 intermediates have a 15-bit exponent; NaN payloads propagate differently; the terrain's edge fade's chop rounding
  (`SectorData::Light` in terrain_sector.cpp) sets the x87 control word only, so SSE arithmetic there would round to nearest
  (an SSE2 build has to set MXCSR there too).

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
| `CopyFloat` / `LoadStoreBits` (57) | the bits as they are (`memcpy` / `bit_cast`) | same | no signalling NaN is copied in the suite (converted, C16b, the helpers are gone; C43: plain assignments, `x87::CopyBits` where the bits come from outside). Behaviour change: a signalling NaN float copied at those sites keeps its bits, where the original's FLD / FSTP set the quiet bit (0x00400000); only a NaN with a clear quiet bit read from a file or a message is affected |
| `IsNaN` / `CompareStatus` (116) | `IsNaN` false (the plain comparison) | same | no NaN reaches those sites in the suite (converted, C16c: the NaN audit below) |

**NaN audit (C16c).** The rule: a comparison is the plain C++ one the source had (VC6's TEST AH,0x40 `==`, TEST AH,1 `<`,
TEST AH,0x41 `<=` / `>`, a C0 mask `a < b ? x : y`); where an `x87::IsNaN` / `CompareStatus` term only reproduced how FCOM counted an
unordered result (equal and less), it is gone, and a NaN compares as IEEE says. Where detecting NaN is the code's purpose (data
checks) the check stays (`x87::IsNaN`): none of the ~116 sites was one, so `IsNaN` is left only in `lib/nan_audit.h`, and
`CompareStatus` / `kC0`-`kC3` are gone from `x87.h`. Per site:
- **Plain, NaN result changes (84 `EI_NAN_AUDIT` sites, 46 files):** zero tests (`ZeroOrNaN` helpers renamed `IsZero`, the
  particle sizes / bends / alphas, lengths before a normalise, the script `Not` / flag tests / `PowerFactor`), equality (`Same`,
  `Equal`, `Differs`, zone variables against 1, quest states, hash key heights), less-than selects (`MinOf`, `MaxZero`, `AtMost`,
  the larger of two extents or reaches), the script comparisons (`IsEqual`, `IsLess`, `IsInSquare`, `IsInArea`, `GetPlayerUnits`:
  `static_cast<double>(side) == f`, the FILD compare). Each has `EI_NAN_AUDIT(values...)` (`src/lib/nan_audit.h`): in the verify and
  debug builds (`EI_NAN_AUDIT_LOG`, CMake) the first NaN per site writes `NaN audit: a NaN compared at <file>:<line>` to ei-rebuild.log;
  in release it is nothing.
- **Plain, NaN result the same (about 25):** where an equally plain form keeps the original's result: the ordered tests (`a > b` for
  TEST AH,0x41 jump-if-clear: `Exceeds`, the HP drop, `rate > 0`, `duration > 0`, `IsGreater`), `h >= kHeightSilent`, `limit >= distance`,
  the bounds a NaN never replaces (device_bounds `x < minX ? x : minX`, terrain_sector `z <= minZ`, ui_stats_rows `s >= largest`),
  `SetDiplomacy` (`> 0` allied, `< 0` enemies, else neutral), the client turn loop (`if (!(gap > kHalfTurn)) break;`: the plain
  `gap <= kHalfTurn` would never end on a NaN), and all of addon.dll's status tests (an IEEE compiler; they read C2:
  `Max` `!(f < m)`, `Min` `!(f > m)`, `Abs` / `Round` `x < 0`, `Random` `high > low`, `NearestUnit` `distance < radius`, `Equals` `==`).
  Four selects on integer-sourced floats (ui_objectives' map sizes, ui_hud_portraits' mana) cannot see a NaN and carry no audit.
  `AIMap`'s free-cell search (ai_path.cpp) sees a NaN in the suite (net_pair_quest, zone_gz15h_tornado: the step is 0 / 0 when
  the unit stands on the target cell, the original's behaviour); its select is exact because p and q are NaN together, so it has
  no audit (the audit fired there in the first suite run).
- **Behaviour change** (NaN only): every audited comparison now takes the IEEE branch: e.g. a NaN length is normalised (a NaN vector)
  instead of left alone, a NaN zone variable counts as "not visited", a NaN script argument is "not zero" and "not equal", a NaN
  height marks a moved object changed, a NaN hash key height never matches (each lookup adds an entry), `Camera::StopMotion` keeps
  a NaN turn / tilt rate where the inlined SetTurnRate(0) replaced it. The suite has none: goldens identical (110), and the audit
  line appears in no run's ei-rebuild.log.
- **The `!(a >= b)` helpers (C16e).** The earlier port's NaN-keeping helpers are gone (`LessOrUnordered`, `ZeroOrUnordered`,
  `NonZero`, `IsNonZero`, `Equal` / `EqualOrUnordered` / `EqualOrNaN`, `IsZeroOrNaN`, the `ZeroOrNaN` bit tests, `Below`, the `same`
  lambdas, world_client's `NaN`; 44 files). A test is the plain `<` / `==` / `!=` with `EI_NAN_AUDIT` (about 70 sites), or the
  exact plain form where it reads as naturally: a negated less-than is `a >= b`; the size-class ladder (combat) tests from the top
  (`size >= kSizeLarge` first); a select (`Select`, `SelectLess`, `PickIfLess`, `Max`, `AtMostOne`, `AtLeastZero`, `ClampToMap`,
  `MaxZero`, `Max0`, `KeepLower` / `KeepHigher`, `Higher` / `Lower`, the bounds folds) is `a >= b ? otherwise : ifLess`, which is
  the C0 mask's result for a NaN too. No audit where a NaN gives the same result either way (`if (x != 0) x = 15 / x`: 15 / NaN is
  that NaN; `1 / plane[8]`) or cannot occur (integer-sourced draws). Loops: the modifier ring's rejection loop (particle_emitters
  `PlaceModifierPoints`) now ends on a NaN length (the original drew again for ever); the others are bounded (tries, iteration
  counts). Left: the shadow / decal files (device_shadow `OneOrNaN`, terrain_shadow `AtMostZero`, terrain_decal `Lower` /
  `Raised`: R16e1's area at the time) and the inline forms (C16f, C16g).
- **The inline forms (C16f).** The earlier port's inline `!(a >= b)` tests and its equal-or-NaN tests (`!(a < b) && !(a > b)`,
  `!(a < 0 || a > 0)`, and the "differs" form `a < 0 || a > 0`) in the gameplay, AI, UI, camera and effects files (about 330
  sites, 110 files; the equal / differs forms everywhere but terrain, device and render) follow the same rule: `a < b` /
  `==` / `!=` with `EI_NAN_AUDIT`, or the exact plain form: clamps and min / max folds as selects (`t = t >= 0 ? t : 0`), an
  if / else with its branches swapped (`if (a >= b) B else A`), a ladder reordered (`world_time`'s `hours >= 8 ? 0 : 1`,
  `KeyAmplitude`'s `power < 0 ? 1 : 2` after the `> 0` test, `ui_item_info`'s `== threshold` before the below list).
  No audit for random draws (the particle rejection loops) and integer-sourced values. Loops: the angle wraps (ai_motion),
  the weather band step and the scrollbar's negative steps now end on a NaN (the original looped for ever); the rejection
  loops are bounded. Integer `!(a > b)` tests stay. Kept as the original's test, with the reason in a comment: `ai_path`'s
  `!(scale >= 1)` (the NaN step of the free-cell search occurs in the suite and must scale both components),
  `UnitStats::HealSlots`'s `!(magnitude > kMinHeal)` (the audit fired in every run: `LearnSkill` passes a NaN heal while a
  new unit's stats are set up), `unit_skill_price`'s below-4 branch (a price of 0 gives 0 / 0; written as `if (leading >= 4) return ...` first),
  `unit_client_path`'s turn loop (C16c) and addon's `Max` / `Min`.
- **The engine, network, terrain, device and render files (C16g; about 75 float sites, 42 files).** Exact plain forms
  where they read naturally: selects for the clamps and folds (`ClampUnit`, `SpeedForBalance`, the acos clamps
  `dot >= -1 ? dot : -1` in core_transform_math / terrain_ambience / view_matrices, `Extend`, `ClampToZero`, `EffectValue`,
  `RaiseVariable`, the loss bounds, wind, pan, wave strength, the sky's horizon and cosine, the clip code bits,
  terrain_decal `Lower` / `Raised`, terrain_shadow `AtMostZero` and the fade), swapped branches (the transform turns,
  `Slerp`, clip's ratio, the acknowledged-history erase, scene normals, `Silent`), `ByWeight` (`> `, then `==`) and the
  time of day (`< 6 || > 18` first) reordered, `LightRotation`'s axis as a select, the ambience vote loop's condition
  (`heard < 3 && weightSum > kMinWeight`), and terrain_decal's settle loop as `if (!(lowest < previous)) break;`
  (`Lower` never raises it, so this is the old equal test, and a NaN ends it). Plain with `EI_NAN_AUDIT` (about 35 sites):
  the equal / differs tests (`InvertModelMatrix`'s determinant, the strip's u length, map_object_shadow's two lengths,
  the field of view, the water levels; device_shadow's `OneOrNaN` is `*scale == 1`, and the hand-over passes the scale
  as it is), the timers (awards, server lists, announcement, lobby), weld / sector / morph distances, the terrain
  height's turn tests and bounds, `SetTurnRate` / `SetTiltRate`, light facing, print alphas, detail levels, water
  priority / height tests. Loop: the cloud phase loop now ends on a NaN (the original looped for ever). addon's
  `RemoveOutdatedEffects` (disassembled): the value test is TEST AH,1 (C0, plain with audit), the strength test
  TEST AH,5 / JP (IEEE: the removal under `distance < 0.001`, exact). Stay: sector_map's FixMoveObject test (addon's
  IEEE test, as `Max` / `Min`), the integer, pointer and flag `!(...)` tests, `NotOlder` (save times are integers),
  crt_x87's pow range test (plain `>=`: never NaN there, NaN arguments return first). The new renderer's own guards
  (render_shadows) are plain. Goldens 111 the same, no audit line in any run.
- **Complete.** No `IsNaN` / `CompareStatus` term, NaN-keeping helper, equal-or-NaN or differs form, or negated
  float comparison is left, except the kept ones named above with their reasons (ai_path's free-cell scale,
  `HealSlots`, `unit_skill_price`, the loop breaks `if (!(gap > limit)) break;` / terrain_decal's settle, addon's
  IEEE tests, script_addon's CVTTSD2SI emulation). A NaN bit test stays only in `lib/nan_audit.h` and the CRT emulation (`crt_x87`).

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
