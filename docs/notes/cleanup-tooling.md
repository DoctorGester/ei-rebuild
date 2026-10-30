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
| `check_x87.py` | in the game sources (src/game, src/lib, src/render) an `x87::` helper, a `libcrt::Ftol` or a `D()` register (C30c): the build is SSE2 throughout, the original's x87 maths is plain float maths or `lib/pc24.h` ("The x87 files to SSE2" below) |
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
  `game/unit.h` 1,035 -> 945 s. When other workers' suites and builds share the machine even that varies by ~12%
  (three trees alternating, clean release: 1,122-1,270 s CPU and 62-99 s wall for each): too coarse for a change
  of ~1%. Time the affected objects instead: each object's cl.exe process times (one process: the front and back
  ends are DLLs in it), the trees in turn, the median of 9 (H33d's scratch `per_file.py`). A job object started
  after the process (`AssignProcessToJobObject` on a running cl) can miss children and gives low outliers.
- Header diet: a header that only holds or passes a rectangle, point or size includes `lib/rect.h` (our own `Rect`,
  `Point`, `Size`); the declarations of Windows functions (`platform/win32/win_api.h`) include the SDK's basic types
  (windef.h and minwinbase.h, ~0.02 s) instead of `<windows.h>` (~0.2-0.4 s of front end); only a
  file that calls Windows includes `<windows.h>`. `platform/win32/gameapi.h` names only the imports our code calls, declared
  over those types (`platform/win32/win_api.h`, docs/notes/standalone.md "Own import table"), so a file calling only
  `gameapi::` gets no SDK header. The key codes are platform/platform.h's, the text format bits render/text_format.h's
  (no winuser.h or `<winerror.h>`). Release objects reaching windows.h
  (directly or through a project header; ninja's deps drop the SDK's own headers, so count by the project files
  that include it): 199 -> 160 (H33d: ui.h's `PlaySound` member renamed, ui_item_info.h without gameapi.h) -> 67
  (H33f) -> 58 (H33g: the game's file users on lib/file_io), mmsystem 93 -> 5, objbase / shellapi / dsound 86 -> 3. Per object (median of 7 cl.exe CPU times, base
  -> after): ui_spin 1.14 -> 1.00, calendar 0.53 -> 0.31, ui_window 1.72 -> 1.56, net_session 1.23 -> 1.02,
  ui_item_screen 1.25 -> 1.06, ui_main_menu (win_user.h) 1.34 -> 1.16, model 0.86 -> 0.70, app (keeps windows.h)
  1.25 -> 1.25: ~0.17 s each, ~15 s of a clean build's ~1,300 s. What still includes windows.h calls Windows: the
  window, messages and input (app, app_main, present_window, bink_video, ui_console_window's clipboard,
  ui_edit_field, ui_font), files and mappings (file_io, resource_stream's mmio, settings), threads and waits
  (thread, sound_manager, ui_cursor_setup), text conversion (asset_text, platform_win32's UTF-8 routes), sockets
  (net_second_master), MulDiv (ui_scrolling_surface); src/render (D3D11 and DXGI include it anyway), src/standalone
  (the loader: exception handlers, VirtualAlloc, the environment), src/hook and import_table.h /
  import_prototypes.h (the harness key: game.exe's side).
- Files go through `platform/platform.h` (no SDK header): File (Seek's `File::begin` / `current` / `end`; ReadAt / WriteAt,
  a seek and an exact read or write that return false instead of throwing, as the archive and the seed file
  checked SetFilePointer and ReadFile; the osNoBuffer / osRandomAccess open flags), FileMapping (the mapped
  archives) and FileFind. Rebirth's code (mod_folders, console_addon, script_addon_commands) looks files up with
  `platform::IsFile` / `platform::FolderEntries` (platform/platform.h), the UTF-8 calls directly: addon.dll called its own imports,
  which the harness's read redirection (the game's slots only) leaves alone, so they must not move onto File /
  FileFind. A member type change (MappedFile, held by FileRegistry, held by WorldServer and SoundManager) moves
  their members: a per-function `dumpbin` comparison then differs in displacements, so compare with hex
  immediates masked (H33g's scratch `objdiff_norm.py`).
- gameapi.h's slot imports are `extern` references bound once in the generated gameapi.cpp (init_seg(lib)), not
  `inline auto&` references whose dynamic initialiser every including object compiled: 0-0.07 s per object (thread,
  calendar, random), a few s of a clean build; the call code is the same.
- `lib/import_table.h` and `import_prototypes.h` are in the harness's reference key (run_all `harness_files`,
  through hook/paths.cpp and tests/verify/harness_imports.h): a change there replays every original run. gameapi.h,
  gameapi.cpp and win_api.h are not (no file of game.exe's side includes them).
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
`0x0073xxxx`-`0x008Fxxxx`), `Fn(T* self, void*)` shims (the old thiscall shape, with or without `__fastcall`; none left since C64: the harness's entries for game.exe's CALLs are in tests/verify), struct names defined in several files (outside anonymous namespaces, the name as written: `miles::Sample`), and the rest listed in its output.

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
  (`MapObject::ServerCompanion()`, `Unit::GetWeapon` / `AsWeapon` (item.h), `FindItem`, `UnitWithId` for a
  unit's id, `LightFactory::LightWithId`, `AIBrain::FindPlayerMode` / `FindPatrolMode`,
  `UnitOrNull` after a class test, `ObjectSlots` where a generic lookup is a
  map object; `World::ObjectWithId` and `WorldServer::FindObject` already return `MapObject*`, and the server
  world's id maps but `timedObjects` are `MapObjectIdMap`s of `MapObject*`; a network session's message
  lists are `std::list<SharedRef<T>>` / `std::list<HeldRef<T>>` (core_refcount.h, ref.h: `element.Get()`); `FigureManagerOf(object)` / `OwningFigureManager(object)` (figure_manager.h) for the world's or
  the owning figure manager; `MapObject::ClientCompanion()` on the client as `ServerCompanion()` on the server;
  `Unit::Controller()` for the controller's own slots (also from inside UnitClientSpecific, which is not
  declared as a UnitController); `itemdb::WeaponOrArmorAt(weapon, index)` for the record head both share;
  `Texture::Manager()` / `ParticleSource::Manager()` for the owning manager; particle_common.h's `Carrier(object)`
  for the attached object's map object slots; `AsQuest` / `AsArmor` on a `CItem*` (Player::items holds `CItem*`)),
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
game_data.h, import_table.h with import_prototypes.h, and intro_sort.h (check_harness_key.py lists them).

## `__asm` and `float_control` (cleanup ticket C12)

C12 and C12a took `__asm` blocks in `src/` from 221 to 3 and left one `float_control` pair; C30e15 took the last ones
(render's `AddOnX87`, vecmath.h's pair, the `x87::` helpers' asm with include/ei/x87.h): `check_cleanup` counts 0 of
each. The rules below were the x87 build's (`/fp:precise` on x87); since C30e15 the build is SSE2 and the rules are the
rewrite guide's rule 7 and "The x87 files to SSE2" below. Kept for the transcriptions they explain:

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

`vecmath.h`'s `float_control` pairs are gone (`precise, on` was redundant under `/fp:precise`; `precise, off` around the
Unrounded helpers went in C30e15, when nothing was x87 code any more: no function of the release objects changed). The
long transcriptions
(`InvertModelMatrix`, `CrownBlend` in figure_part_draw, `EmitFirstSection`, `EmitSection` in renderer_strip; C12a) are C++ whose
object code does the same operations with the same rounding points: a value the original keeps on the FPU stack across a loop or a
reload is a `double` local (CrownBlend's unrounded red average for the first colour, `double redTarget = red` then the rounded one),
and a float the original reloads after storing is read back from the float. Check a conversion by tracing the compiled x87 stack
(`dumpbin /disasm` of the object, before and after): the compiler adds harmless qword stores of doubles and float stores/reloads
for every `static_cast<float>`. The register / stack shims
`NewKeepingEdx` (renderer_init), `CallWithOne` (sound_manager) and `UpdateMotion` (unit_motion_tick) went in C12b: their callers are
ours on the standalone, so the helper's flag is passed whole (the constructor reads its low byte), the codec's close is a plain
`__stdcall` call, and the movement tick's stale local starts at 0 (combat.md, "Movement tick"). `src/hook` (the hook
framework, game.exe's side) keeps its `__asm` by design.

## SSE2 build

### The drawing-only code on SSE2 (C30a)

Since C30e15 every game file and the renderer build with `/arch:SSE2 /fp:precise` (CMakeLists' `add_compile_options`;
the harness and the vector replays too). Before that the build was split by file (`EI_X87_GAME_SOURCES` on
`/arch:IA32`, the rest in the `ei_sse2` library); C30a put the drawing-only code on SSE2 (the interface, particles and
effects, weather, sky, lightning, the terrain's draw passes, waves and ambience, decals and prints, the old renderer's
glue, textures and surfaces, the music and the unit sound names), C30e moved the rest batch by batch (below).
- **One library, whole archive:** the game code is `ei_game`, linked as `$<LINK_LIBRARY:WHOLE_ARCHIVE,ei_game>` (every
  object kept, static initialisers too). The split needed it for a second reason, gone with the split: an inline
  function kept out of line in an SSE2 and an x87 object is one COMDAT, and the linker takes the first object's copy.
- No game object loads or resets the x87 control word but device_object (PC24) and render.cpp's `FpuGuard`
  ("The control word" below).
- **What C30a kept x87 and why** (moved by C30e12, C30e13 and C30e13a; C30e10 moved the figure and link mesh draws): picking reads the drawn view and the objects' screen
  rectangles (renderer_init's matrix stacks, the projected meshes device_mesh / device_bounds / device_shadow, the figure
  and link mesh draws, world_client_draw / _view), the screens that turn a pixel into a world point, an order or a
  choice (ui_hud, ui_hud_map, ui_hud_pick, ui_battle_input, ui_view_screen, ui_global_map*, the
  camera keys and mouse), the briefing (camera moves, phrase timing), device_object (sets the 24-bit precision),
  the terrain's heights, sectors and loading (moved in C30e14). C30e moved renderer_init, device_object, device_shadow, ui_briefing,
  ui_briefing_keys, ui_briefing_topics and ui_hud_pick: their objects do no float arithmetic (the picking maths they
  call is in the files left).
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
  (`UpdatePositional`: a silent one-shot takes no voice) and the ambient emitters' distances and intervals (Tick).
  sound_manager.cpp is on SSE2 since C30a2 because those are basic float operations on floats and small
  integers: the original's 24-bit (PC24) x87 results, which float maths reproduces exactly (docs/notes/sound.md,
  "Floating point"). This is the way to move such a file: show that each decision is +, -, *, /, sqrt and compares
  on floats in the original's order, then round with FISTP's ties to even (`_mm_cvtss_si32`, not `std::lround`). The
  callers' maths (positions, radii, ambience volumes: terrain_ambience, unit_client_effects) is on SSE2: it can move a
  3D sound's level across 0 only at an exact boundary, and only matters when all 15 voices are taken. Sample lengths
  (`WaveLength`) are kept on the voice but read by nothing; the harness ends samples by Miles' length on the virtual
  clock; no save holds music state.

### Plain float maths in the SSE2 files (C30b)

The SSE2 files (the list above and src/render) lost the x87 era's spellings by script, in two steps; the x87 files
and the shared headers' inline helpers (`D` in camera.h, ui_float.h; gone since C30e13) are untouched.
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
- The scripts (one-off, not in tools/): `renames.py`, `casts.py`, `clang_ast.py`, `files.py` in the C30b scratch
  folder named in the hand-back.

### The SSE2 files by hand (C30c)

What C30b's script left, done file by file: the SSE2 files hold plain float maths, and `check_x87.py` now fails on
an `x87::` helper (but the control word render.cpp's `FpuGuard` puts back), a `libcrt::Ftol` or a `D()` in them.
- **Whole files to float** (a one-off AST script, `floats.py` in the C30c scratch folder, then C30b's `casts.py`):
  `double` locals, parameters, returns and constants became `float`, `D(x)` / `static_cast<double>(x)` of a float
  went, of an int became `static_cast<float>`, double literals and the `EI_*` macros their float spelling. The
  particle files (callbacks, emitters, create, tick, render), lightning, effects, weather, the HUD and item screens,
  map_object_shadow (C30b's scripts first: R37f had kept main's copy), world_client_lights, terrain_ambience and
  the rest; the `Unsigned` / `Range` / `GroundHeight` helpers return float, the volatile reloads (`Load`,
  `ReadFloat`, `LoadFloat`, map_object_shadow's `g_pi` trio) went, `libcrt::Hypot` / `CiAcos` / `CiPow` became
  `std::hypot` / `acos` / `pow`, `static_cast<int>(libcrt::Ftol(e))` became `static_cast<int>(e)` (NaN now gives
  0x80000000 where `_ftol` gave 0: no NaN reaches these displays; ui_scrollbar's `Truncate` and
  world_client_lights' `HourIndex` keep the 0 where a NaN can arrive).
- **`UiContainer::FrameTime` is a float** (milliseconds * 0.001f): the product the original rounded at 24 bits, so
  the x87 callers (the briefing) get the original's value; the SSE2 callers sum it as floats.
- **Kept double on purpose:** a timeGetTime frame count (`ms * 15 / 1024`: the milliseconds since the system
  started, ui_hero_screen / ui_camp_create / ui_hero_preview / ui_model), the statistics functions' results the
  item panel truncates (the unit code returns double), the trade panel's `Reduce` share, disk space and the
  performance timers. A double-returning call of the x87 code
  (`RawSurfaceHeightAt`, `MotionCubic::Value`, `BufferRatio`) is narrowed once with `static_cast<float>`.
- **Accepted rounding differences:** ui_hud_setup's `OnArc` and the round map's outline truncate a float product,
  so cos(pi / 3) x 34 is 16 where the original's unrounded cosine at 24 bits gave 17 (a region corner one pixel
  off; main's SSE2 double maths gave 16 already); colour bytes and wave entries at an exact half round away from
  zero (`std::lround`).
- **terrain_water_waves** is the image's golden on both installs (its `# no original: vanilla rebirth` line; each
  recorded from four image runs: plain twice, EI_HEAP_JITTER=1 and 2,fill; the Rebirth one in C63g):
  terrain_animation's waves and terrain_shadow's `EdgeFade` are float maths, so a vertex's sine-table entry, crest byte or edge fade can round the other way from the
  original's 24-bit sums (the water group's speculars from record 0).
- **The shadow files (C30d, after R37c / R37d):** render_shadows, render_point_shadows, render_terrain's caster
  spheres and world_client_shadow_casters by hand: float constants, the map's fit (axes, planes, area, depths,
  `LightMatrix`), the point lights' ranges, spheres and face matrices, the casters' world matrices;
  `ShadowCasterSphere` and `ShadowMesh::model` (render.h) are float. **Kept double:** render_shadows' `M4`
  (`Invert`, its `Multiply`, `CameraEye`): the camera's clip matrix inverted and multiplied back into the game's
  clip matrices (`UnderCamera`, the fit's `fromClip`, the meshes' light matrices), a product that nearly
  cancels (a float inverse moves a still object by the camera's rounding, beyond the point shadows'
  `kKeptPlace`); and the GPU timers. The other `static_cast<float>` left in the SSE2 files are conversions,
  mostly an int argument or initialiser (MSVC warns, C4244, without the cast).
- **Counts** (code, comments left out). The 186 SSE2 files: `static_cast<float>` 839 -> 343, `static_cast<double>`
  164 -> 36, `D(` 704 -> 0, `double` 673 -> 159, `x87::` 7 -> 3. The dashboard's (src but hook and standalone):
  `static_cast<float>` 2,504 -> 2,009, `static_cast<double>` 772 -> 643, `x87::` 394 -> 390, `Ftol(` 234 -> 129.

### The x87 files to SSE2 (C30e)

The rest of the game code moved to SSE2 by C30a2's method, in the batches below; C30e15 moved the last two files and
removed the x87 build (`EI_X87_GAME_SOURCES`, `/arch:IA32`, include/ei/x87.h).

**The method.** From the D3D device's creation (device_object) the original runs the main thread at 24-bit precision
control (PC24): every x87 add, subtract, multiply, divide and square root rounds once to a float significand, as the
float operation does. A decision made of those on floats, in the original's order, is the same in float maths on
SSE2 (C30a2: 30 M random cases, 0 differ). Per file, what breaks the equality and what it takes:
- **double registers** (`D()`, double locals, returns and `static_cast<double>` of a float in arithmetic): at PC24 they
  hold float values, so they become float (C30c's `floats.py` / `casts.py` do most). Kept double: values that never
  were PC24 results (a timeGetTime count, file sizes, a double read from data or a message). Stored doubles (members,
  saves, messages) keep their type and receive the float value.
- **double constants wider than a float** in arithmetic: FLD keeps all 53 bits and the one operation rounds, where
  `x * 0.001f` rounds the constant first. 39 such literals in 29 of the files left (pi, 0.001, 0.01, 1.04, 1.61, 0.7,
  0.9, 0.6, 0.02, 1e-5, 2^-32 ...) and the named double constants they use. Exact form: `pc24::Mul / Add / Sub /
  Div` (below: the exact result as a double-double, then one rounding to 24 bits; C30e3's test, rerun against them in
  C30e16: 50 M cases, a million of them where the double then float rounding differs, 0 differ from the x87 form;
  every pair of 29 special operands, as float or double, through all four: zeros, infinities, NaNs, denormals, the
  float and double range ends, 0 of 3,364 differ).
  A constant that is exact in 24 bits (0.5, 100.0, 0.25) is a float constant.
  The double 1 / (2^32 - 1) is 2^-32 (1 + 2^-32): a float times it rounds to the float times 2^-32 (`0x1p-32f`).
  `pc24::Mul(x, word * 0x1p-32)` (the scaling exact in the double) keeps the x87 result where the product `x * word`
  alone would pass 3.4e38. A zero, infinite or NaN product is the plain double product.
  A single operation on two floats done in double and then stored as float is the float operation (53 >= 2 x 24 + 2:
  double rounding is harmless there); a chain of them in double is not.
- **ints above 2^24** in float arithmetic: FILD is exact and the operation rounds once; float maths converts first.
  Only timers, ids, random words and data words reach that (`x87::FromUnsigned`, `ms * 0.001`): check each site;
  the helpers above take the int as a double.
- **the exponent range**: x87 registers keep a 15-bit exponent, so a product beyond 3.4e38 or below 1.2e-38 inside an
  expression differs. No game value is near either; the equivalence tests cover the operands' real ranges.
- **transcendentals and wide operands: `lib/pc24.h`** (C30e4). FSIN, FCOS, FPATAN, FYL2X and F2XM1 ignore the
  precision control and give a 64-bit result that the next operation rounds; a double constant keeps its 53 bits.
  `pc24::Sin / Cos / Atan2 / Log / Log10` return an `Unrounded` (a double-double: the function to about 100 bits,
  then rounded to 64 as the FPU does; Sin, Cos and Atan2 take doubles, which floats convert to exactly), and `pc24::Round` (FSTP dword), `Add / Sub / Mul / Div` (one PC24 operation
  on two register values, a float or a double converting) give the original's float: `x87::fcos(a) * r + x` ->
  `pc24::Mul(pc24::Cos(a), r) + x`, `x * 0.001` (double) -> `pc24::Mul(x, 0.001)`. `pc24::Exp` is VC6's inline exp
  step by step. No x87 code: double-double arithmetic (two-sum, Veltkamp two-product), FSIN / FCOS's argument
  reduction by the FPU's 66-bit pi (0xC90FDAA22168C234C * 2^-66) in 128-bit integers (this CPU's FSIN uses it: the
  exhaustive test matches up to 2^63), series with double-double heads, then one rounding to 24 bits by rounding hi
  to odd first (lo's sign as the sticky bit). Two roundings matter that a plain double form misses: FSIN of a small x
  is x itself after the 64-bit rounding (so `sin x * f` can sit on a float tie), and a store into the denormals is
  one rounding from 64 bits where an operation rounds to 24 bits first. pc24.cpp is SSE2 only (an `#error` on x87);
  the helpers are out of line, so x87 objects may call them. The equivalence test (`eq/` in the C30e4 scratch folder:
  the original sequences as `__asm` at PC24 against the helpers, 24 threads) on this machine (Ryzen 9 7950X):
  | case | cases | differ | std:: form differs |
  |---|---|---|---|
  | sin, cos stored, every float | 2^32 each | 0 | 1,090,519,044 / 1,090,519,040 (large arguments: true pi) |
  | log, log10 stored, every float | 2^32 each | 0 | 5 / 1 |
  | exp (VC6's) stored, every float | 2^32 | 0 | 92,376,909 |
  | ClampPow (spells), every float | 2^32 | 0 | 4,223,839 (`std::exp(std::log(c) * 0.33f)`) |
  | sin / cos / log times and plus a float, f / sin, sin / f | 200 M each | 0 | 14,231 (log * f) .. 12.8 M |
  | atan2 stored, plus a float | 200 M each | 0 | 0 / 175 |
  | atan2 times a float | 200 M | 9 | 61,418 |
  | a float and a double: x * c, x + c, x - c, c - x, x / c, c / x | 200 M each | 0 | 15.8 M .. 49.7 M (c as a float) |
  | sin, cos of a double stored, sin times a float: CiAcos of every float in [0, 1) and -0 | 1,065 M each | 0 | - |
  | sin, cos of a double: stored, sin times / cos plus a float; 2^-60 .. 2^62, near k pi / 2 | 400 M each | 0 | - |
  | atan2 of two doubles stored, minus a float: doubles in [-1, 1]; 2^-60 .. 2^62 | 100 M each | 0 | - |
  | atan2 of FSIN's and FCOS's doubles stored, every float angle | 2^32 | 0 | - |
  | ai_motion's turn: FPATAN of FSIN's and FCOS's doubles minus a float, every float angle | 2^32 | 414 | - |

  The double forms (C30e10a) reduce the 53-bit argument by the same 66-bit pi (the 128-bit integers hold it exactly);
  the float forms go through them, rerun on every float (0 differ). Their test: C30e10a's `eq/` (C30e4's plus the
  double, Slerp and combat modes) and C30e8a's (`turn`, `atan2d`, `turncause`). The 414 are all angles within pi:
  atan2(sin a, cos a) of a float a lies within a few 64-bit ulps of a itself, so the turn's difference with a float is
  often a 24-bit tie that the 64-bit result decides, and there this CPU's FSIN / FCOS (28 of 240 on 0 .. pi: their
  doubles differ from the correctly rounded ones) or FPATAN (212: the same doubles, its 64 bits one off near a
  64-bit tie) are not correctly rounded. About 2 in 10^7 kind-4 turn orders; accepted as the 9 below. The 9: FPATAN of a tiny ratio y / x is the quotient, and this CPU's FPATAN does
  not round it correctly where it sits within a few hundredths of an ulp of a 64-bit tie (0x842c0719eb680885 for an exact ...0885.503); with operands
  of few significant bits (the test's half integers) the product with f can lie that near a float rounding boundary.
  Accepted. NaNs: of two NaN
  operands the FPU takes the larger significand (of equal ones the positive), as `Add` .. `Div` do, but a signalling float operand loses to a
  quiet one in the FPU, and a float converted to double is already quiet (nan-bits cases only, never a number).
- **x87 code in SSE2 objects:** a float returned by a call comes back in ST0 (the x86 calling convention), and the
  compiler may do the next float operation there (`pc24::Exp(...) - 0.2f` is an FSUB). One such operation on two floats
  gives the SSE form's float at any precision control; a chain (spell_effect_internal.h's `LightRiseScaled`: FDIVR then
  FMUL) relies on the main thread's PC24, as the original does.
- **ei_vectors** builds its game sources as the game does (SSE2, one executable since C30e15); its replays keep their
  FLDCW of the recorded word (007F): the x87 operations MSVC leaves on ST0 results round at it.
- **the game CRT's pow / acos / asin / hypot** (`libcrt::CiPow` ..., crt_maths.cpp, SSE2 since C30e15): the original's
  sequences at the main thread's PC24 through `lib/pc24.h` and exact double-double / integer steps (C30e15 below).
  The host CRT's `std::pow` (player_client) is the UCRT's SSE2 pow, equal to the x87 build's `__CIpow` there.
- **helpers**: `x87::fistp` / `RoundToInt` -> `lib::RoundEven` (CVTSS2SI: ties to even, 0x80000000 for a NaN, an
  infinity or out of range, as the x87 form: all 2^32 floats compared); `libcrt::Ftol` -> `static_cast<int>(static_cast<std::int64_t>(x))`
  where a NaN or a value beyond the int range can arrive (the low half; 0 for NaN, as `_ftol`: all 2^32 floats
  compared), else `static_cast<int>`; `x87::CopyBits` -> `=` (SSE2 copies keep a signalling NaN's bits);
  `x87::FromUnsigned` -> `static_cast<double>` (exact on SSE2).
- **the chop rounding**: none. The original truncates the terrain's edge fade under chop rounding
  (`SectorData::Build`); the image's `EdgeFade` (terrain_shadow) rounds to nearest with `std::lround` for every
  caller. C30e15 tried truncation for the sector build: terrain_water_waves' golden (recorded from the image) differs in
  the water sectors' speculars from record 0 (town_waves, zone1_waves), so the image keeps rounding to nearest; the
  speculars are the shader's input, not game state.
- **53-bit code**: before the device exists (start-up: options, registry, settings, the first files read) the main
  thread runs at 53 bits, where the original's double maths is SSE2's double maths: such code stays double. The other
  threads (the MP3 stream, the cursor, the master server lookup) are SSE2 already or do no float maths.
- **per batch**: the survey scripts below on the batch's objects before and after (no x87 instruction left, no SSE
  arithmetic but what the review allows), goldens unchanged on the verify suite and `--rebirth-only`, an equivalence
  test for every helper, check_x87 0.

**Survey (2026-10-29, release build, dumpbin /disasm of every x87 object).** 417 of the 587 game files were x87.
Their objects: 126 without a float instruction, 75 with float loads and stores only, 7 with int <-> float
conversions besides, 171 with float arithmetic and 38 with a transcendental or the control word. The unordered
containers' load factor (`size / max_load_factor`, `ceil`: small integers over 1.0f, exact on both) is counted apart.
The scripts (one-off, in the C30e scratch folder named in the hand-back): `survey.py` (source counts per file),
`objscan.py` (dumpbin classes per object and function), `classify.py`, `sse2check.py` (SSE arithmetic in the moved
objects), `cmake_list.py` (the grouped list), `eq/` (the conversions' test).

**Batch 1 (C30e, done): the 208 files without float arithmetic.** On SSE2 their objects hold none either: int to
float conversions (CVTDQ2PS), float to double (CVTPS2PD), bit operations the compiler vectorises (XOR, AND, OR on
memory blocks: world_guard, ai_cost_tables, rebirth), the four `lib::RoundEven` and registry's `__ftol3`. Source
changes: `x87::CopyBits` -> `=` (net_messages, script_vars), `x87::fistp` -> `lib::RoundEven` (an item whose
condition rounds to 0 or below is broken: unit_create, unit_items), renderer_init's three roundings of 50.0f -> the
colour 0xFF323232, registry's `FloatToInt` through int64. Goldens unchanged (verify and `--rebirth-only`).

**C30e3 (items, spells, world objects; 17 files).** Float maths on floats as they were (prices, conditions, charges,
the stock's material and spell draws, lights, map specs, levers, torches, traps). Through the helpers: the random
spreads (hoover_loot's gold, `ItemWeapon::RollDamage`, now a float: the spread times the ISAAC word, then 2^-32), the
item price's material units, spell_manager's `ScaleTruncate` (then through int64, as `_ftol`), the lever's end tick
minus the world tick, the magic trap's double height plus z. `CopyBits` -> `=` (item, map_spec_effect),
`x87::fistp` -> `lib::RoundEven` (gold minimum, spell budget, material alpha). Equivalence test: C30e3's `eq/`
(each old expression at PC24 against the new one, the light's constants, the wall spell's normal).

**C30e11 (map objects; 10 files).** No transcendental, wide constant or large int but two operands: the bounds, cage
interpolation, track lerps, scale steps, light reach and the companion light's distance are float maths in the
original's order (the double locals were PC24 registers); the ground height (`GridHeightAt`, a double) plus a float and
the animation time minus the frame count (a uint32 as a double) go through `pc24::Add` / `Sub`. `AnimationDatabase::TimeFactor`
(map_object_figures.h, inlined in figure_bones' x87 object too) and `Object3D::AnimationFactor` return float: at PC24
their double result already was one. `x87::fistp` -> `lib::RoundEven` (the track's key frame), `modf` on the float.
Kept: `RemoveOutdatedEffects`' `static_cast<double>(distance) < 0.001` (addon.dll's double compare, exact on both),
`MapObject::MoveSpeed`'s double return (a virtual of the units; a constant here).

**C30e2 (scripts; 10 files).** Script arguments come from mission data, so NaN, infinities and huge values are
plausible. The truncations (`Ftol`, 72 calls): the game's `_ftol` is `Truncate` (script_eval, script_exec, addon's
`TruncateX87`), `static_cast<int>(static_cast<std::int64_t>(x))`: SSE2's `__ftol3`, the low half, 0 for NaN; addon's
own CVTTSD2SI is `TruncateOrMin` (0x80000000 for NaN and out of range), unchanged. `fistp` / `RoundToInt` ->
`lib::RoundEven`, addon's `Store` and `CopyBits` -> `=`. Float maths: the distances (vecmath's `Length2D` /
`DistanceSquared2D`; IsInArea's squared distance is a float), `LightStep`, addon's `Round` / `Ceil` / `Floor` on
floats. Through the helpers: `ScaleRandom` and the FX source offset (`pc24::Mul(x, word * 0x1p-32)`),
`RoundTimeOffset` (the int plus the float), `GetWorldTime` (the tick times seconds per tick; fmod is exact), `GetZ`
(the unit's double ground height plus z). addon's sin / cos / pow stay the host CRT's calls: the x87 and the SSE2
object call the same UCRT functions (SSE2 code inside), equal on all 2^32 floats (sin, cos) and 48 M pairs (pow).
Equivalence test (`eq/` in the C30e2 scratch folder): every float for the truncations, `RoundEven`, the int cast,
Round / Ceil / Floor / Sin / Cos; random operands for the rest: 0 differ with game-sized operands (|x| 2^-10 to 2^16)
and with |x| 2^-30 to 2^60 plus zeros, infinities, NaN and the int edges. Left, by the exponent range only: a sum
of squares past 3.4e38 (coordinates beyond 1.8e19) or under 1.2e-38 (two coordinates within 1e-19), an FX spread beyond 1.7e38 (twice it
passes the float range) and denormal results; and when two different NaNs meet in one operation, which one comes out
(x87: the larger significand; SSE2: the first operand).

**C30e6 (units; 20 files).** The stats' double locals and returns were PC24 registers: unit.h's and unit_db.h's
returns (Reduce, the curves, Period20, the speeds, TotalItemWeight, WeaponSkillBonus ...) are float, the maths in the
original's order. The experience curves: `pc24::Div(pc24::Log(e / d), pc24::Log(base))` into `libcrt::CiPow`, whose
result at PC24 is a float; `CiPow(1.04, ...)`, `CiPow(1.61, ...)`. SkillPrice (the original's disassembly):
`_CIpow` for the worth, the perk power and 10^floor(log10 price), the log's 64-bit result stored to a double for the
CRT's floor (`pc24::Log10(price).hi`), so the host CRT's `std::pow` there became `libcrt::CiPow` (its vectors, SkillPrice
and SchoolPrice, replay at 0 failures). Through pc24: the ints added to a float (RaiseLevel's cost, the scripts' stat
raises), the worth exponent over Skill Val 2, the 0.7 of `BaseSpeed`, the stamina drain step (a double), the world's
double surface height plus z (`GroundHeight`, `StandPoint`, `PlaceOnGround`), the FCOS of the sight cones (pi times
1/180, both doubles). `x87::fistp` / `RoundToInt` -> `lib::RoundEven`, `Ftol` -> int64 truncation. The skill level
values (`SkillLevelRecord::Value`, a data dword the original loads exactly) are floats: percentages and stat bonuses,
far below 2^24. Kept double: `GroundHeight` / `StandPoint`'s world height and the virtual `MoveSpeed`, the 0.001 heal
threshold compare. ei_vectors builds unit_stats / unit_params_* / unit_skill_price for x87 still, with pc24.cpp in
`ei_vectors_sse2` (C30e1's split) and crt_x87.cpp for `CiPow`.

**Batches left** (209 files, 57,575 lines; source counts: `double` 1,640, `D(` 472, `x87::` 358, `Ftol(` 120).
Each one focused area, one worker session, after C30e4 where it has a transcendental:

| Ticket | Files | Lines | What keeps it x87 |
|---|---|---|---|
| C30e1 network, sessions, settings | 30 | 7,647 | net_connection_tick's `D()` (24) and timers, player_server_handlers' doubles (16), player_client's `std::pow`, options' `Ftol` |
| C30e2 scripts | 10 | 4,031 | `Ftol` 72 (script_exec 43, script_eval 15: a script argument can be a NaN), script_addon_math's CRT sin / cos / pow |
| C30e3 items, spells, world objects | 17 | 4,047 | item / item_stock / item_manager float maths (prices, conditions; item.cpp's `CopyBits` 11), `x87::` 21 |
| C30e4 (done) transcendental helpers, spell effects | 3 | 1,097 | `fexp` / `flog` (spell_effect / _update), exit_area's sin / cos; the helpers and their tests |
| C30e5 (done) server world, world effects | 21 | 6,831 | world_weather's wide constants, moshka / visual / party / units' transcendentals, world_time's fmod |
| C30e6 (done) units | 20 | 3,820 | stats doubles (unit_vitals 44, unit_create_stats 42), `flog`, `CiPow`, `flog10` |
| C30e7 (done) combat | 11 | 4,938 | doubles 208 (combat_sight 53, combat_tick 51), `flog`, `fpatan` |
| C30e8 (done; ai_motion: C30e8a, done) AI maps, paths, motion | 17 | 3,732 | ai_motion (91 doubles, 44 `x87::`), ai_refresh's `fexp` / `flog`, `x87::fistp` 49 |
| C30e9 (done) unit AI | 4 | 3,256 | doubles 268 (unit_ai_modes 135), `fsin` / `fcos` / `fpatan` |
| C30e10 geometry, figures | 24 | 3,665 | core_transform_math / quaternion `D()` 120, `CiAcos`, picking's projected meshes |
| C30e11 map objects | 10 | 2,360 | doubles 75 (map_object_figures, map_object_cage) |
| C30e12 (done) client units and world | 13 | 3,928 | unit_client_update's `fexp`, world_client_maps' `Hypot`, the view (picking) |
| C30e13, C30e13a (done) camera, picking, briefing | 22 | 5,869 | `D()` 328 (camera 122, ui_hud_map 123, camera_motion 72), `CiAcos` / `Hypot` / `CiPow`, `fexp` |
| C30e14 (done) terrain | 5 | 1,411 | terrain_sector's chop rounding, terrain_landscape's `CiAsin` / `fsin` |
| C30e15 (done) main loop, game CRT | 2 | 943 | app_main's frame timing (`fexp`, `FromUnsigned`, `Step`), crt_x87.cpp; then the IA32 option and the x87 helpers go |

**C30e1 (network, sessions, settings; 30 files, all moved).** The network clocks: `net::Pump` passes at most 5000
ms times 2^-10, a float value, so every `Tick(double dt)` adds and subtracts floats; the double members that hold
the sums (`SessionBase::time`, `Connection::clock`, `SentPacket::time`) keep their type and receive float sums, and
their differences are float differences (`Age`). net_connection_tick's `D()` registers, the round-trip averages, the
lobby's and the server's timers, the use order's distance (`(dz dz + dy dy) + dx dx` in float), the formation
offsets, the stat fractions: float maths. Through the exact helpers: `IsaacRandom::Range` (the draw times the range by
`pc24::Mul`, then 2^-32), map_database's degrees (an `atof` double times the float factor),
`ScaledExperience` (the double power times the float amount), the old disk-space product, and the divisions
`BufferRatio` (int / int, now a float return) and the console's exp share (`pc24::Div`,
lib/rounded_quotient.h: the double quotient, its remainder's sign by two-product). player_client's `std::pow(1.04,
k)`: the SSE2 call gives the same 256 doubles as the x87 build's `__CIpow`, so it stays `std::pow`. **Precision at
start-up:** options' loader runs before the device exists (53-bit: its `value * 255` and `value * (1 / 255)` stay
double maths), the saver at shut-down after it (24-bit: `value * 255` is a float product), so the two now have their
own helpers (`LoadedTimes255`, `SavedTimes255`); `Ftol` truncates through int64. **ei_vectors** builds each game
file as the game does (one SSE2 executable since C30e15; until then the x87 files in the executable, the rest linked
whole after it): Range's vectors, all recorded at control word 007F, replay against the SSE2 code. Equivalence test
(C30e1's `eq/`, each old expression built /arch:IA32 at PC24 against the new): `RoundedProduct` (now `pc24::Mul`) and
`RoundedQuotient` (now `pc24::Div`) 2^28 cases each (random and built on float ties: 36 M and 33 M where double then float differs),
Range for every draw of three ranges and 2^28 random, `ScaledExperience` 2^28 (each of the 256 powers), the map's
degrees for every 3-decimal text in [-1000, 1000], `BufferRatio` 2^27, the share 2^26, the saver's product on every
float: 0 differ. Behaviour change: a signalling NaN in a variable table or an award keeps its bits (SSE2 copies).

**C30e10 (geometry and figures; 23 of 24 files moved).** The transform's rotation helpers (core_transform_math), the
figure meshes' morphs and pose matrix, the bones, the projection: their `D()` registers and double locals held floats
at PC24, so float maths in the original's order (`MorphVector`'s upper z kept on the FPU is the stored float: its
`keepZ` switch is gone). Through `lib/pc24.h`: `ProjectionMatrix`'s and `FromAxisAngle`'s FSIN / FCOS (`Mul(Sin(half),
zFar)`, `Div(Sin(half), length)`, the cosine stored), `SetHeading`'s FPATAN plus pi / 2, `TurnUpTowards`' double 2 pi
minus the angle, `TiltTowards`' arc cosine times 0.1, the mesh draws' tick (an int, exact) plus its fraction. **The game
CRT's arc cosine** (`CiAcos`, crt_x87) returns FPATAN's result through a double at PC24 (its control word restore
stores it), so an SSE2 caller gets the same double: storing it as a float is a plain conversion, an operation on it a
`pc24::` one. `x87::fistp` -> `lib::RoundEven` (the fog alpha), `Ftol` -> int64 truncation (frame sheets, specular
bytes), `CopyBits` -> `=`. core_maths.cpp followed in C30e10a (below). Reviewed in the objects: the x87 code the
compiler leaves in SSE2 objects (a float-returning function's sum on ST0: `Dot`, `DotZYX`, `UpX`; an FSUBR on a returned float) is float operations on floats, exact at the
main thread's PC24; `WaterTweens::SetTarget`'s int steps were rounded to a float in the x87 code too (FILD, FSTP dword).
Equivalence test (C30e10's `eq/`: HEAD's functions built /arch:IA32 at PC24 against the new ones, one x87 acos shared):
WorldMatrix, Tilt / Turn / Roll / TurnUpTowards and the turn-up tail above pi, SetHeading, the Rotate product,
FromAxisAngle and Length, ProjectionMatrix, the morphs, FigureMesh::UpdateWorld, TickTime: 48 M random cases each,
0 differ. ei_vectors' maths family replays the SSE2 maths / core_maths / matrix_math (pc24.cpp in ei_vectors_sse2).

**C30e8 (AI maps, paths and motion; 16 of 17 files moved).** The paths, footprints, line of sight, cell marks, unit
shapes and the AI map's heights: float maths in the original's order. Through `lib/pc24.h`: the motion's angles
(ai_motion: `Round(Cos / Sin)` of the turn angles, `Sub(Atan2(...), startAngle)` into `NormalizeTurn`, the tracking
turn `Div(elapsed * angleTo, ticks)` over the int tick count, `Sub(time, start)` with the int start tick), the
smoothing's octant (`Mul(Atan2(by, bx), 4 / pi)`), the climb tables (`Mul(Atan2(rise, 0.5), Div(180, pi))`,
`Sin(Mul(1 / 180, pi) * slope)` squared by `Mul`), the occluders' damping (`Mul(Exp(table * a * b), passage)`) and the
line of sight's z step (an int up to 2^31 times the float inverse, `Mul`). Three forms that read FPATAN's register
as it is: `PathOrder::Start` compares the stored angle with the unrounded result (`angle.hi == stored && angle.lo ==
0`), `Build` compares FCOS's result with the double 0.9 (`Above`: hi, then lo), and `LimitSpeeds`' first angle is
kept through the second FPATAN call as a double (`Sub(a1.hi + a1.lo, a2)`: the sum is the 53-bit rounding). The
damping scale ln 0.6 is FYL2X of the double 0.6, a constant: `-0x1.058afp-1f`. `x87::fistp` -> `lib::RoundEven` (49),
the event grid's `Ftol` -> int64 truncation, `CopyBits` -> `=`. `MotionCubic::Value / Slope` and
`AIMap::LargestStepAlong` return float (their x87 doubles were PC24 floats). ai_motion followed in C30e8a:
`Build`'s turn order (kind 4) takes FPATAN of FSIN's and FCOS's results stored as doubles, `pc24::Sub(pc24::Atan2(s,
c), startAngle)` with `s` and `c` the `hi + lo` of `pc24::Sin` / `Cos` (the 53-bit rounding); `pc24::Atan2` takes
doubles since (its test above). Equivalence test (C30e8's
`eq/`: HEAD's forms built /arch:IA32 at PC24 against the new ones): Start, SetTurn, Sample (kinds 0 to 4, paths of 2 to
6 points sampled three times), Build's turn and tail, SubdivideSegment, LimitSpeeds, the climb tables (75,000 scales,
every step), land and water heights, the octant, the z step, Damp, Occlude / Refresh, LargestStepAlong, the
footprints, movement, facing and unit shapes: 4.8 M cases each (14.4 M path samples), 0 differ with game operands
and with wide ones (2^-30 to 2^31, zeros, infinities, NaN); left: which NaN comes out where two meet (the default
NaN's sign: an infinite angle, NaN and infinite coordinates) and the exponent range (an exponent of Damp's past 88,
not reachable: ln 0.6 times a fraction times 7).

**C30e10a (core_maths.cpp).** `Slerp`'s angle is the game CRT's arc cosine, a double (FPATAN's 64 bits stored to 53),
and FSIN loads it whole: `pc24::Sin(double)` (the float forms' reduction on a 53-bit argument). `1 / fsin(angle)`
rounds at PC24, so `inverseSine` is a float, `pc24::Div(1.0, Sin(angle))`; the weights are `pc24::Mul(Sin(pc24::Mul(1 -
t, angle)), inverseSine)` and `pc24::Mul(Sin(pc24::Mul(angle, t)), inverseSine)` (the original kept weight B as a
double: a PC24 product); the dot, the linear weights and the blend are float maths. `1 - cos > 0.0001` compares the
float difference with the double constant (exact either way). `SetAngleAxis` / `SetAxisAngle` call
`Quaternion::FromAxisAngle` (core_maths.cpp, C30e10): the same sequence. Equivalence test (C30e10a's `eq/`, `slerp`):
every float cosine in [0, 1) and -0 through the x87 arc cosine, a t per cosine: FSIN / FCOS stored, 1 / sin and both
weights, 0 differ for t in [0, 1] (932 M) and for any t within 2^-100 .. 2^100 (104 M); a t beyond (the products past
a float's range, where FSIN of |x| >= 2^63 returns x) differs, as the exponent range does everywhere. ei_vectors builds
core_maths.cpp in ei_vectors_sse2 (the maths family records no Slerp or SetAngleAxis).

**C30e7 (combat; 11 files).** The double locals were PC24 registers (reach, distances, the plan's squares and cones,
the attack roll, the blow heights, the chase): float maths in the original's order. Through pc24: `Level`'s
`Div(Log(experience / base), Log(step))`, `PlanAction`'s heading and turn (`Round(Atan2)`, `Sub(Atan2, heading)`), the
ints the FPU loads exactly (FILD / FIADD: the attack roll's attack, `BuildAttackHit`'s and `RandomHeight`'s random
pick times 0.01, `CastValue`'s cast time, `AttackInterval`'s base, `RefreshVitals`' wait, `TimeUntilAction`'s start
tick), the double ground and surface heights plus a float (`CanActFrom`, `FindSightTarget`, `BuildAttackSteps`,
`TimeUntilAction`, `SpellHasLineOfSight`). A drawn int doubled (the attack roll) and a tick count halved (the plan's
outer reach) round once on the FPU: the int rounded to a float, then doubled or halved, is the same float.
`Unit::MoveSpeed` (a double virtual) returns a float, used as one. Kept double: `StartMove`'s 0.02 (`lengthSq < 0.02`
compares with the double; 0.02f is below it) and the int-to-float compares (`ResolveHit`, `TakeHit`, the chase's
lead), all exact. `x87::fistp` / `RoundToInt` -> `lib::RoundEven`, `Ftol` -> int64 truncation (`Truncate`).
`TimeUntilAction` returns float. Equivalence test (C30e10a's `eq/`, `combat`: the x87 sequences at PC24 against the
new forms, 200 M random cases each): Level, the attack roll (any dword, any int), an int times a float plus or over a
float: 0 differ; where an intermediate leaves a float's normal range (e / b, the int product, a denormal quotient)
they differ, the exponent range again (level: 2.4 M of 5.8 M such cases).

**C30e5 (server world, world effects; 21 files, all moved).** The double locals and casts were PC24 registers: float
maths in the original's order (the particle step and the scorch shape, the light's range, the bird's speed, the
tornado's step, the view and hearing ranges, the exit centres, the experience weights and shares, the depth sort's
dot (`DotZYX`), the hero-start and spell-target distances; `VariableGet` returns float). Through `pc24`: a random
word (FromUnsigned's FILD is exact) times a wide double constant (the bird's flight angle, the tornado's speed, the
scorch angle, the weather's map point, offsets, bolt and spread: `pc24::Mul(double(word), c)`); the world tick, an
int past 2^24 in long games, times the seconds per tick (`World::Elapsed`; `World::TimeOfDay` is fmod(Elapsed() +
timeBase, 24), exact, for the time functions, `GameServerTimeOfDay`; the save's player times) and plus the tick
fraction (the camera path); 1 / the time-scale option and the rate times the double 1/15; a height
(`RawSurfaceHeightAt`, a double virtual) plus or minus a float (missile, particle, spell missile, rain bolt); an
effect's double `Radius()` times a float (sync); the bird's sin / cos times a radius and its atan2 minus a float, the
party's facing, PlaceInLine's sin / cos. **Kept:** `libcrt::CiAcos` (PlaceInLine) stays the game CRT's x87 call: its
64-bit result reaches SSE2 code as a double, and the doubling `pc24::Add(arc, arc)` still gives the x87's float for
every float argument (tested); the double virtuals (`Radius`, `LightRadius`, `NightLevel`, the heights) keep their
type and return float values. `Ftol` -> int64 truncation (`Truncate`: the catch-up ticks, the movie flag; the quest
money), `x87::fistp` / `RoundToInt` -> `lib::RoundEven`. x87 code MSVC leaves in the SSE2 objects: one float
operation on a call's ST0 result (the bird's floor, `LightRadius`) and NightLevel's (h - 20) * 0.25 (exact at any
precision); the out-of-line x87 copies of `DotZYX` / `vecmath::Dot` are not called (inlined as SSE). Equivalence
test (C30e5's `eq/`, the x87 sequences at PC24 against the new forms): every float's acos doubled, 1 / every int,
every float times 1/15 (2^32 each); every tick 0..2^31 times 8 game rates and plus 5 fractions; random tick times
and plus a float (200 M each); every word times the bird's and the tornado's constants (2^32 each) and 2^26 words
(every 64th) times each of the 7 constants: 0 differ. Keep such sweeps to a few minutes each (`eq words 6` 31 s; the
2^32 sweeps took 4-5 min each, the tick sweeps 25 min together).

**C30e9 (unit AI; 4 files, all moved).** The modes', senses' and target scoring's double locals and returns were PC24
registers: float maths in the original's order (`Score`, `AIAttackOption::Weight` and `OptionWeight` return float).
Through `lib/pc24.h`: the random words times a wide double (`Mul(word, scale)`: the decay's 0.4 / 2^32, the flight
jitter's 6 / 2^32, the look angle's 6.3 / 2^32), a span times a word then the double 2^-32 (1 + 2^-32), which rounds as
`* 0x1p-32f` (`Mul(span, word) * 0x1p-32f`: FearMode's jitter, the look's wait, FindStandPoint's offsets), the looks'
`Round(Sin)`, `Add(Cos, x)` and `Round(Atan2)`, UpdateMove's turn, the patrol waypoints' angle, the patrol cell
index plus a half and Score's ints plus a float (`Add(words * count, partial)`, `Add(count, ...)`: a spell's target
count comes from data and may pass 2^24; the equivalence test's wide operands caught the plain form). The path
smoothing (an int) is compared with a float through doubles (exact). `x87::RoundToInt` / `fistp` -> `lib::RoundEven`.
Equivalence test (C30e9's `eq/`: HEAD's forms built /arch:IA32 at PC24 against the new ones, one case per area: the
decay, FindFlightPoint, FearMode's Evaluate and Execute, a planned look, FindStandPoint, UpdateFollow, the guard /
suspicion / combat filter / party / path-clear tests, the patrol points, UpdateMove, Notice, IsNearPath, the senses
(Prepare to ForceSee) and the targets (Score, GroupExcess, IsUsable, CanReach, Weight)): 2.4 M cases each, 0 differ
with game operands and with wide ones (2^-12 to 2^13, zeros, infinities, NaN) but which NaN comes out and one zero
sign below; with 2^-30 to 2^32, the senses' four-factor products' exponent range (denormal exposures). The zero
signs were the `>=` selects MSVC compiles to MAXSS / MINSS (AIViewer::Refresh's reach, FearMode::Evaluate's distance,
KeepLower, Score's penalty, IsNearPath's clamp): exact through float_select's helpers since (below, "Selects on
`>=` / `<=`").

**C30e12 (client units and world; 13 files).** The double locals were PC24 registers: unit_client_update's blend
(`(1 - w) target + w current`) and animation speed, the client world's light reach test, ObjectsInRange's reach and
distance, the sun table's steps, the pick ray (`PickPoint`'s determinant and line): float maths in the original's
order. Through pc24: the heading blend's `fexp` (`pc24::Exp`), its FPATAN / FCOS / FSIN (`Round(Atan2)`,
`Round(Cos)`, `Round(Sin)`), the zone exits' FCOS / FSIN, the double ground heights plus a float (`GroundHeight` plus
`heightOffset`, ObjectsInRange's `Hypot` plus the radius, the pick's height plus half the range), the ints the FPU
loads exactly (`RestartAnimation`'s start tick, the client time's tick, the eye rectangle's texture sizes and data
bytes, the sun table's file ints times 1/255, the day hour's tick times seconds per tick), and `Level`, whose first
logarithm the x87 code kept through the second `flog` call as a double: `Div(numerator.hi + numerator.lo,
Log(base))`. Kept as they were: `std::fmod` on doubles (exact; the pulse and the day hour), the `0.01` and the
footprints' `Hypot` compares (exact), `Passed`'s uint frame as a double, AddRounded / SubRounded (one double
operation, then the float: the float operation). An explicit `static_cast<float>` of an int is rounded in the x87
code too (`static_cast<float>(tick) + tickFraction`, the blood print's offset): plain float conversions.
`x87::fistp` / `RoundToInt` -> `lib::RoundEven`, `libcrt::Ftol` -> int64 truncation; `static_cast<int>` of a double
in x87 code is `__ftol2_sse` (CVTTSD2SI: 0x80000000 for NaN), so the attack sums stay `static_cast<int>` of the float.
In the SSE2 objects: the attack's weapon bonus plus the skill bonus is one FADD on ST0 (a float returned in ST0), the
double operations are uint conversions, single operations rounded to float and compares. The client code runs on the
main thread (the other threads: the MP3 stream, the cursor, the master server lookup). Equivalence test (C30e12's
`eq/`, HEAD's forms built /arch:IA32 at PC24 against the new ones, 24 M cases each with game operands and 24 M wide):
Level, the heading blend, the animation speed, the eye rectangles, RestartAnimation, the pulse, the sun table's colour
and steps, the day hour, the client time, the blood print offset, ObjectsInRange's test: 0 differ with game operands;
wide ones differ only where a value leaves a float's normal range (the exponent range: Level's quotient, the blend's
weight below 2^-126 at a rate above 291, overflowing speeds and hours, denormal steps), never in game ranges.

**C30e13 (camera, picking, briefing; 21 of 22 files moved, camera.cpp by C30e13a).** The camera's update, pan, limits, saved views and
recorded paths, the aim along a direction, the turn upright and the lift over objects (camera_motion), the minimap's
markers, clipping and texture coordinates (ui_hud_map), the speakers' views and the briefing's look-at point, the
global map's centre and turns, the camera keys' speeds: their `D()` registers and double locals held floats at PC24,
so float maths in the original's order (camera.h's and ui_float.h's `D()` are gone; `KeyboardSpeed` is one inline
helper in ui_camera_keys.h). Through pc24: the shake (`Mul(Cos(halfPi * elapsed * 4/15), Exp(-(decay * elapsed)))`
times the amplitude plus the sum: the cosine stays in ST0 for the FMUL, checked in the old object), the minimap's
facing (`Round(Atan2(-x, -y))`), the speakers' angle (`Round(Cos)`, `Round(Sin)`), the HUD's mode marker (`600 -
Mul(Sin(angle), 73)`, `Mul(Cos(angle), 73)`), the global map's bob (`0.5f - Mul(Cos(t * 5), 0.2f)`), and the
operations on the game CRT's doubles: `_hypot` (`Div(ny, Hypot(nx, ny))` into the arc cosine, `Sub(root, Hypot(extents))`),
`_CIpow` (`Mul(CiPow(1.05, steps), distance)`, the minimap's wheel), `_CIacos` doubled (`Add(arc, arc)`), the
listener's height (`Add(RawSurfaceHeightAt, focusHeight)`), the briefing text's degrees (`Mul(kRadiansPerDegree,
ToDouble(text))`). The view screen's turn is a constant, `ei::kRadiansPerDegree * 225.0f` (the original's PC24 products
of pi, 1/180 and 225). `x87::fistp` -> `lib::RoundEven` (device_bounds: the projected meshes' screen rectangle,
picking). The `_ftol` truncations go through int64 (the global map's marker regions, frame fraction, zone states,
clock and camp flag; the paths' zone state; the briefing's experience and money lines): screen places and script
variables are not bounded, and MSVC's x87 `static_cast<int>` (`__ftol2_sse`, CVTTSD2SI) gave 0x80000000 for a NaN or
out of range where VC6's `_ftol` gives the low half, now as the original. The compares with double constants (0.001,
1e-6, 1e-18, 0.1, the shake's 20 against `_hypot`'s double) stay double compares (exact). **C30e13a:** camera.cpp
followed once `pc24::Atan2` took doubles (C30e8a): `ApplyView`'s aim back down after the lift is
`-pc24::Add(CAM_RIGHT_ANGLE, pc24::Atan2(dz, Hypot(...)))` (the original at 0040F9DA: FPATAN's result stays in ST0 for
the FADD of the PC24 product pi * 1/180 * 90, FCHS, FSTP dword). Its test (C30e13a's `eq/`, `eq 2000`, 123 s): 1,000 M
cases in game ranges (dz 0..60 over 120 x 120, and 0..4 over 2 x 2) 0 differ; 1,000 M with wide operands (2^-30 .. 2^30,
either sign, zeros) 8 differ, all dz negative and |dz| / across above 2^20 (the camera straight below its focus, the
arc tangent next to -pi/2 cancelling against the right angle: the FPU's 64-bit result decides a near-tie of the 24-bit
sum), never in the game, where the lifted camera is above its focus. x87 code MSVC leaves in the SSE2 objects (reviewed): one
float operation on `FrameTime()`'s ST0 result, stored (the briefing's timers), `MouseSpeed`'s product times a float
(main thread, PC24), out-of-line copies not called (`MarkerWriter::MapX`, `vecmath::Dot`, `DotXYZ`); the hash tables'
load factors. Equivalence test (C30e13's `eq/`, the x87 sequences at PC24 against the new forms, `eq all 300` 112 s):
the shake with game operands (decay 0..10, elapsed 0..600, amplitude 0..5, a sum so far), ny over `_hypot`, the root
less `_hypot`, the power times a distance, the bob, the minimap's facing, the speakers' cosine and sine, the arc
doubled: 300 M cases each, 0 differ, also with wide operands (2^-30 .. 2^40, zeros, infinities, NaN) but the shake's
(below);
every short's wheel exponent, the four marker angles, every three-decimal degree text in [-1000, 1000], both constants:
0 differ. `eq floats` (6 min): `_ftol`'s low half against the int64 truncation and FISTP against `RoundEven`, every
float, 0 differ. **The shake's exponent range:** where the cosine times the exponential falls below 2^-126 (the last
ticks of a shake, decay times elapsed past ~87) the x87 register keeps the exponent and `pc24::Mul` stores a denormal,
so the shake offset differs below 2^-96 (the first shake of a frame adds to 0: 52,560 of 22.7 M such cases, and 46 M
of 277 M, mostly -0 against +0, once the exponential itself underflows; with wide operands 61 of 30 M, a huge amplitude
lifting such a product back into the normal range, and the operands or the exponential out of range); added to the
camera's height (metres) it changes nothing. With a nonzero sum (later shakes) 0 of 300 M differ.

**C30e14 (terrain; 5 files, all moved).** Two precisions: `Terrain::Create` (the sine table, `BuildWavePhases`) runs
from `WorldClient::Create` at start-up, before the device, at 53 bits, so its double maths stays double (SSE2's double
maths is the FPU's there): the sine table is `pc24::Round(pc24::Sin(i * kPiDouble / 256))` (FSIN's 64 bits stored),
the wave phases keep their double locals and the four corners' sum is written in double (the x87 code summed the
floats at 53 bits; float adds differ at PC24 in every table tested). The rest runs at PC24 (map and sector loads,
heights, the sector maps, `SetWaves` from scripts): float maths in the original's order (the sector bounds, vertex
positions from the bytes, normals, water depths and levels, `SurfaceHeight`, the materials; the double constants
1/126, 1/65534 and 0.001 were float values: float constants). Through pc24: SetWaveStrength's step, the game CRT's
arc sine (its 64-bit result; SSE2 code stores it to a double) plus the turn, `Add(CiAsin(1 - s), kPi * turn)`, and the
UV tables' 1 / tiles per row and side / tile size (`Div` on the ints). `x87::fistp` -> `lib::RoundEven` (cells, sector
bounds, heights' cell), `Ftol` -> int64 truncation (the material's opacity byte: a NaN from the file gives 0), and
the chop scope around Build's edge fade removed (above: nothing x87 ran under it since C30c). In the SSE2 objects:
PlaneHeight's degenerate-triangle average `(z0 + z1 + z2) * 1/3` is an FADD, FADD, FMUL chain on its ST0 return value
(PC24 on the main thread, as the original) and SurfaceHeight's last add an FADD; SetWaveStrength's clamp goes through
`MaxKeepB` (a plain `s >= 0 ? s : 0` is MAXSS: +0 for a -0 strength; "Selects on `>=` / `<=`" below). Equivalence
test (C30e14's `eq/`: HEAD's forms built /arch:IA32 against the new, at PC24 or PC53 as each runs; whole runs under 5
minutes each): the sine table at PC53 (512, 0 differ; at PC24 266 would), the wave phases at PC53 (48,000 tables, 0;
at PC24 all would), every float in [0, 1] as the arc sine's argument plus the turn (1,065 M, 0), SetWaveStrength
(24 M game, 0; 24 M wide: only -0 strengths and directions whose squares or parts leave a float's range), every 4th
float through the material values (0 but the exponent range), 1 / every positive int and 4096^2 + 24 M wide margins
(0), every height times 12,288 ranges and every offset at 4096 x 33 grid columns (0), every normal field (0), the
water depth and level, sector bounds, plane and surface heights (24 M each, half wide: 0 but NaN payloads and the
exponent range; 4 of 12 M wide plane heights with coordinates from 2^-120 to 2^108 in one triangle are NaN on SSE2),
every float's sector cell (0) and the spiral distances at both precisions (0).

**C30e15 (main loop, game CRT; the last two files).** app_main's frame timing runs on the main thread at PC24 (the
start-up's disk space before the device, at 53 bits: plain doubles): the camera's seconds (`pc24::Mul` of the
milliseconds, a uint as a double, and the double constant), the speed's VC6 inline exp (`pc24::Exp`), the frame
length (`pc24::Div(1000.0, fps)`, then `std::ceil`), the client backlog (`pc24::Add(pc24::Mul(elapsed, speed),
backlog)`), the tick fraction and the balance (`Div`, `Sub`), the recorded tick times (`Add` of the int tick and the
fraction); `Step` (a volatile double store) and `x87::FromUnsigned` went. crt_x87.cpp is `crt_maths.cpp`, SSE2 code
without `__asm`: acos / asin are `pc24::Add` / `Sub` of 1 and x, the float root, then FPATAN's register stored to a
double (`pc24::Atan2(...).hi`: hi is the 64-bit value rounded to 53 bits; `hi + lo` compiled to an x87 FADD into the
return register, at PC24); pow is `pc24::Log2Times` (FYL2X: y log2 x rounded to 64 bits) and `pc24::TwoToThe`
(FRNDINT ties to even, the fraction and 1 + (2^f - 1) rounded to 24 bits), FSCALE by the bits; _hypot's 64-bit steps
(quotient, 1 + t^2, root, product, each stored to a double: the double rounding RN53(RN64(v)) moves hi only when hi
is odd and |lo| reaches ulp (1/2 - 2^-12)) are exact remainders, 32-bit-limb integer squares and an integer-adjusted
root. NaNs: _hypot's FLD x, FADD y (a signalling y loses to a quiet x); _CIpow's FADDP of its operands as the caller
loaded them (both quiet: the larger significand, of equal ones the positive). Then the x87 build went: `/arch:IA32`,
`EI_X87_GAME_SOURCES` and the `EI_SSE2` property, the ei_vectors split, include/ei/x87.h, `libcrt::Ftol`, vecmath.h's
`float_control` pair and Atan2, render's `AddOnX87`, the start-up's FNSTCW log, check_x87's object half. The release
objects' x87 instructions 7,947 -> 7,077, their arithmetic and control-word ones 610 -> 474 (app_main 73 -> 0, the
game CRT 59 -> 1, render.cpp 2 -> 0); what is left is MSVC's operations on values returned in ST0 ("The control
word" below), among them crt_maths' `MovingSize` and pc24's `Scale` (a power of two times a short value: exact). Equivalence test (C30e15's `eq/`: HEAD's
crt_x87.cpp and app_main forms built /arch:IA32 against the new, plus the original's pow core as `__asm`, every
thread at 007F; `eq all 100`, 3 min): every float through acos and asin (2^32 each: 6,896 and 2,188 differ in the
double's last bit, 0 once rounded to a float), 100 M random doubles in (-1, 1) (1,419, 0 as floats); the differing
ones are where this CPU's FPATAN is an ulp off at 64 bits next to a 53-bit tie (acos of 0x1.8d5002p-53: the true
value is 1024.48 64-bit units below hi, FPATAN gives 1025). pow with the game's bases, random positive and wide
operands (also y below 2^-960, which `Round64` now scales), negative bases, specials: 310 M cases, 0 differ from
HEAD's port and 0 from the original's instructions; _hypot game, wide, short-significand (64-bit ties), tiny and
special operands 310 M, 0 differ but NaN against NaN (11 of the 676 special pairs, a signalling y: HEAD's port took
the larger significand); 10 M NaN pairs (6 M with a signalling NaN) against FLD x, FADD y and FLD x, FLD y, FADDP: 0
differ. app_main: every uint32 millisecond count, every float balance, every int fps, 100 M backlogs, fractions,
balances and tick times (game and wide): 0 differ. The double users of CiAcos (the camera's arc, quaternion's Slerp,
core_transform_math, world_units) can see the last-bit cases above.

### The control word

The image keeps the main thread at 24-bit precision from the device's creation (device_object's
`__control87_2(_PC_24, _MCW_PC)`, the word 007F Direct3D 7 left in the original) although no game file is x87 code:
- MSVC's SSE2 code still does x87 arithmetic on a float or double returned on the x87 stack (the x86 calling
  convention): the release objects hold about 470 such operations (FMUL, FADD, FADDP, FDIVR ...: `pc24::Exp(...) -
  0.2f`, `LightRiseScaled`'s FDIVR then FMUL, `PlaneHeight`'s `(z0 + z1 + z2) * 1/3`). One operation on two floats
  gives the float at any precision; a chain or a double operand gives the original's value only at PC24, as the
  original computed it.
- The UCRT's `log2`, `exp2` and `cbrt` follow the precision control (sin, cos, pow, atan2, hypot, acos, asin, exp,
  log, floor, fmod, frexp, ldexp, fma, lround, lrint, atof do not: C30e15's scratch test at 007F and 027F).
- Miles and Bink ran under the original's word; their mixing and decoding are not checked at 53 bits.
render.cpp's `FpuGuard` puts the word back (only when it changed) around the renderer's library calls. The vector
recorders' FNSTCW and the harness's control-word trace are game.exe's side and stay.

### Selects on `>=` / `<=` (lib/float_select.h, C30e17)

MSVC (/fp:precise, SSE2) compiles `a >= b ? a : b` to MAXSS and `a >= b ? b : a` to MINSS (the `<=` forms likewise),
which return the second operand on a tie: where +0 meets -0 the zero's sign differs from the original's compare and
branch (NaNs come out right: MSVC orders the operands for them). The strict forms (`a < b ? a : b`, `a > b ? b : a`,
`!(a < b) ? a : b`, `std::min` / `std::max`) have an exact MINSS / MAXSS and compile to it. So:
- **A select of two floats on `>=` / `<=` goes through float_select.h**, never as a conditional expression or a
  one-line if: `a >= b ? a : b` -> `MaxKeepB(a, b)`, `a >= b ? b : a` -> `MinKeepA(a, b)`, `a <= b ? a : b` ->
  `MinKeepA(b, a)`, `a <= b ? b : a` -> `MaxKeepB(b, a)`; `if (a >= b) a = b;` -> `a = MinKeepA(a, b);` and so on.
  `SelectLess(a, b, ifLess, otherwise)` is the general one: CMPLESS's mask (all ones for a >= b, zero when unordered)
  and AND / ANDNOT / OR through intrinsics, which MSVC keeps; MinKeepA / MaxKeepB call it. Their deleted template
  overloads make an int or double pair a compile error (ints stay plain: they have no signed zero).
- **Routed by script** (C30e17's scratch folder: `route_selects.py` for the ternaries, `route_ifs.py` for the one-line
  ifs, the int sites kept with `--skip` from the build's C2280 list): 107 ternaries in 49 files, 7 if-selects
  (item, spell_manager, terrain_sector, ui_stats_rows, particle_callbacks), and the two `Bits(...)` selects (combat_sight's
  OutOfSightRange, world_effect_visual's ground clamp; MSVC made MAXSS of those too).
- **The object scan** (`scan_minmax.py EXE` lists the linked image's MAXSS / MINSS by function; `scan_lines.py` recompiles
  the SSE2 objects that have one with /FAs and gives each its source line): 436 at 179 places before, 134 at 56 after,
  every one a strict form, `std::min` / `std::max`, MinKeepB / MaxKeepA, addon's `!(f < m)` folds, stb_truetype or a
  double clamp in pc24.
- **Equivalence test** (C30e17's `eq/`: the helpers and each form as its own SSE2 function against an /arch:IA32
  FLD / FCOMP / FNSTSW reference that moves results as bits): every pair of 28 special operands (+-0, +-inf, quiet and
  signalling NaNs with payloads, denormals, the smallest normals, the largest finite, 1 +- an ulp) and every quadruple
  for SelectLess: 0 differ; 50 M random sets with a's twin (equal, negated, an ulp off) as b: 0 differ. The plain `>=` /
  `<=` expressions differ on exactly the +0 / -0 pairs (2 of 784; 113,572 of 50 M), the strict ones on none.

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
- Also: x87 intermediates have a 15-bit exponent; NaN payloads propagate differently; the terrain's edge fade was truncated
  under chop rounding (the image's rounds to nearest since C30c).

**Helper families, one at a time on `/arch:IA32`** (everything else as today; 110 goldens, ei_vectors 0 in every row):

| Family (sites) | Plain C++ | Goldens | Why |
|---|---|---|---|
| `fistp` (197) | `std::lrint` | same | the CRT's lrint is a FISTP that follows the x87 rounding control (chop too), under SSE2 as well; but NaN and out-of-range give 0 where FISTP gives 0x80000000: kept as the instruction: lrint also reads the FP environment per call (SV10j's helper replaces both) |
| `fsqrt` (149, done: C16a, double argument kept) | `std::sqrt` | same | on IA32 `_CIsqrt` is FSQRT at the precision control (converted, C16); under SSE2 it is SQRTSD, 53 bits |
| `fsin` / `fcos` (159) | `std::sin` / `cos` | same | the CRT's 53-bit results against FSIN / FCOS's 64; equal after the float store over 800k float arguments; not bit-exact in principle (converted, C16, separate commit) |
| `fpatan` (27) | `std::atan2` | same | as fsin (converted, separate commit) |
| `flog` / `flog10` (16) | `std::log` / `log10` | same | FYL2X ignores the precision control (converted, separate commit) |
| `fexp` (7) | `std::exp` | **97 differ** | VC6's inline exp rounds its FMUL / FSUB / FADD at PC24 (about 24 bits accurate): 64% of float arguments give another float, up to 3.6e-7 relative. One site does it all: `unit_client_update`'s heading weight `x87::fexp(rate * kDecay)` (checked alone: the same 43 records as the whole family); cunit702's `rot.w` 0.112546213 / 0.112546332 at world update 45 in gz1g, then its place |
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
  `unit_client_update`'s turn loop (C16c) and addon's `Max` / `Min`.
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
  crt_maths' pow range test (plain `>=`: never NaN there, NaN arguments return first). The new renderer's own guards
  (render_shadows) are plain. Goldens 111 the same, no audit line in any run.
- **Complete.** No `IsNaN` / `CompareStatus` term, NaN-keeping helper, equal-or-NaN or differs form, or negated
  float comparison is left, except the kept ones named above with their reasons (ai_path's free-cell scale,
  `HealSlots`, `unit_skill_price`, the loop breaks `if (!(gap > limit)) break;` / terrain_decal's settle, addon's
  IEEE tests, script_addon's CVTTSD2SI emulation). A NaN bit test stays only in `lib/nan_audit.h` and the CRT emulation (`crt_maths`).

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
