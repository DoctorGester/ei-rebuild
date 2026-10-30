# Rewrite guide

How to write and change the game's code in this repo (ports, cleanup and fixes). Read it fully before
starting; every rule here comes from a bug we already hit.

Our code runs only in the standalone image (docs/notes/standalone.md), which contains no game.exe code.
game.exe runs only as the original game, for the harness's references (goldens, function vectors), and
the harness patches it by address there. Nothing of ours shares memory or calls with game.exe's code.

## Setup

- Ghidra (with game.exe analysed), when the user has it running, answers at
  `http://127.0.0.1:8080` (`/decompile_function?address=0x...`, `/disassemble_function?address=0x...`,
  `/xrefs_to?address=0x...&limit=N`, `/get_function_by_address`, `/strings?filter=`, POST
  `/rename_function_by_address` with `function_address` and `new_name`).
- The original exe (read only, never modify anything under `F:\games`):
  `F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe`. Image base 0x400000; .text and .rdata have
  file offset == VA - 0x400000.
- Build: `build.cmd verify` and `build.cmd release` (PowerShell: `cmd /c "<repo>\build.cmd verify"`).
  Both must build without errors or warnings in files you touched.
- Games only through `python tools/work.py test <ID> [run_all args]` (docs/worker-brief.md). You may add a
  scenario under `tests/scenarios/` that exercises your area (script commands via `@...`, checks via
  `check` and `waitfor`); its golden is recorded centrally.
- Run `python tools/check_x87.py` (after `build.cmd release`) and `tools\check_signatures.cmd` (after
  `build.cmd verify`) before handing back, and `python tools/check_stolen.py` after adding a vector
  recorder. check_signatures compares the stack arguments of the declarations where game.exe's code meets ours
  with the original's RET n: the recorders (they run inside game.exe), the harness's forwarders, and the rewrites
  at a recorded address (the vectors' replays call them with arguments recorded from the original). Any other
  rewrite is free to change its signature (a std::string_view for a const char*); `--all` checks them too.

## Where code goes

- `src/game/<area>.h/.cpp`: rewritten game code. Provisional names; comments follow docs/comment-guide.md (no
  addresses or ticket ids in comments). Classes are normal C++: real virtual functions, members of their real types, no
  hand-built vtables and no layout asserts. No game.exe code shares our objects, so nothing needs the
  original memory layout; goldens don't depend on it either. Keep game.exe's byte layout only where
  bytes leave the process (save files, network packets) or come from game.exe's data image (until
  C18/C25 finish), and say so where you do. Add no hand-built vtable (`static void* Vtable()`,
  `ei::SetVtable`, slot calls by number).
- `src/lib/`: shared compatibility code. `crt.h` (the game's own CRT: operator new/delete, string
  functions, file I/O),
  `containers.h` (helpers for the std::vector the engine's arrays are), `hash_map.h` (STLport's walk order),
  `intro_sort.h` (the game's std::sort, for the sorts whose order of equal keys is behaviour), `platform/file.h`,
  `gdi.h`, `thread.h`, `calendar.h`, `gameapi.h` (the Win32 imports our code calls; generated: the
  ones the harness or the image routes go through the image's import slots; declared by `win_api.h` over
  `win_types.h`, so no windows.h), `win_user.h` (VK_* / DT_* without windows.h). Containers and strings are standard
  types. Add to these only when needed, in small separate additions.
- `src/platform/`: the game's calls of the operating system by area (clock, message box, paths, ...), no SDK types;
  `src/platform/win32/` implements them and is the only game-side place that includes windows.h or calls
  `gameapi::` (docs/notes/platform.md; tools/check_platform.py fails on a new file that does). A new system call
  goes there, not into a game file.
- `tests/vectors/`: function vectors (recorders, replay checks; see Verification).
- Shared headers others use: `src/game/engine_object.h` (EngineObject virtual interface),
  `src/lib/vecmath.h` (Vector2/3/4, Quaternion, Matrix4 and inline helpers with named sum
  groupings, docs/notes/vector-math.md; `maths.h` includes it), `ai_map.h`, `resource*.h`,
  `terrain.h`, `base_manager.h`.

## The rules

1. **Read the disassembly, not just the decompiler.** The Ghidra MCP disassembly listing can also
   drop instructions (it omitted an FNSTSW in 006C1C00, making a min-select look like a bit test):
   when a sequence looks odd, check the raw bytes in game.exe (capstone) before reproducing it. Ghidra misreads calling conventions, misses
   register arguments and drops return values.
2. **Return values:** check what the original leaves in EAX/AL/ST0 and which callers read it: that is
   the function's result. Functions that set all of EAX to 0/1 return `BOOL` (int); ones that set
   only AL return `bool`. A float left unrounded in ST0 is rule 7's.
3. **The original's address.** A rewrite is an ordinary function or member, called by name; nothing reaches
   game.exe's code addresses (C20 batch 6) and no table maps them to ours (C20l). Its signature need not match the
   original's RET n unless a vector recorder records that address (the EI_RECORD names it; check_signatures).
4. **Virtual methods** are the compiler's (C19): nothing reaches a slot's address.
   The `...Hook` shims and `...Impl` bodies left from registering slots are C28's to fold into the
   virtuals. Constructors and destructors are C++ ones (docs/notes/standalone.md, "Real classes"). Do not give a non-virtual method the same name and signature as a base-class virtual
   (`EngineObject::Create`, `Destroy`, `Release`, `Load`, `Save`, `GetName`, ...): it silently
   becomes an override.
5. **No forwarders.** Every function of game.exe is ported: call it by name (include its header, or
   declare it there). src has no `EI_ORIGINAL` (C29; `check_cleanup` keeps it at 0). Where the harness
   needs to change what our code does, it sets a named seam our code exposes (the fixed ISAAC seed:
   `IsaacRandom::SetSeedHook`; the traced draws: `IsaacRandom::SetDrawObserver`,
   `libcrt::SetRandObserver`), never an address. Only tests/verify's game.exe side and the vector
   recorders call game.exe's code by address (`tests/verify/harness_original.h`).
6. **Memory and CRT:** `new T(...)` / `delete p` (the host's heap): a class built by hand on a raw block
   gets its constructor and destructor; an owned array is a `std::vector<T>` (`std::unique_ptr<T[]>` where its
   address is handed out and must not move, the sound voices), not `new T[n]` / `delete[]`; a block of a byte count
   other than its type's (a header with what follows it) `::operator new(bytes)` / `::operator delete`. An
   engine object whose constructor leaves fields the game reads is `new (kZeroed) T(...)` (engine_object.h).
   A heap helper the original freed by hand that becomes a local with a destructor (PlayerIterator drops its
   count of the player list) runs that destructor at the end of its scope, not where the original freed it:
   scope it so it ends before anything it holds is deleted (WorldServer::Destroy's iterator outlived the
   list CWorld::Destroy deletes and decremented the freed block, a flaky heap crash at quit). A std::vector is
   read and written within its size: STLport code that read the popped element through end() (IdPool::Allocate) or
   let a helper write past the size into the spare capacity (the sprite clipper's vertices) is undefined on a
   std::vector, so resize it first; `build.cmd asan` (docs/notes/harness.md) finds these. Containers are
   `std::`. Call the CRT functions whose game behaviour differs from the host's through
   `libcrt::` (C26 decides which those are) and Win32 through `gameapi::`.
7. **Floating point.** Write float maths as plain C++ (`float`/`double`, the standard maths functions); do NOT hand-copy x87 instruction
   sequences (no naked FPU helpers, `__asm` or `#pragma float_control`). The x87 code in the exe is a VC6 artifact; the build is
   `/arch:SSE2 /fp:precise` for all of the game code. When an integer or a branch follows from a float that sits on a rounding edge, a rare
   difference there is expected: say so in the notes rather than copying the FPU sequence.
   - The game's main thread runs the original's x87 at 24-bit precision (control word 007F, set by Direct3D and by device_object; measured at
     every frame boundary in both installs; other threads and the start-up before the device run at 53 bits). There every x87 add, subtract,
     multiply, divide and square root on floats rounds as the float operation does, so the original's float maths is plain float maths in its
     order: no `double` registers. Where an operand was wider (an int above 2^24, a double constant, FSIN / FCOS / FPATAN / FYL2X's 64-bit
     result, VC6's inline exp) use `lib/pc24.h` (`pc24::Mul` / `Add` / `Sub` / `Div`, `Sin`, `Cos`, `Atan2`, `Log`, `Exp`); the game CRT's
     pow / acos / asin / hypot are `libcrt::CiPow` / `CiAcos` / `CiAsin` / `Hypot` (src/lib/crt_maths.cpp). Code that runs before the
     device keeps its double maths (SSE2's doubles are the FPU's at 53 bits). docs/notes/cleanup-tooling.md, "The x87 files to SSE2".
   - The grouping written is the grouping compiled, and a `static_cast<float>`, a float local or a float return is rounded to float.
     `/fp:fast` had distributed and refolded sums (FearMode's `(30x + d) * (1/31)` became `30/31 x + d/31` and settled on another fixed
     point) and kept float locals unrounded; keep `/fp:precise`.
   - A product of two constants that the original computes at run time (FLD; FMUL / FDIV) is rounded to 24 bits there, while the compiler
     folds it at double precision: write it as the float (`static_cast<float>(kPi * kPerDegree)`, checking the rounding) or keep the
     operation; the goldens and vectors catch the drift. The build inlines at /Ob2 (R16c5), so a constant behind a helper function
     (`Pi() * FaceStep()`) folds too.
   - Rounding mode matters: FISTP rounds to nearest, ties to even (`lib::RoundEven`), a C++ cast truncates, `_ftol` truncates and gives the
     low half of the 64-bit integer (`static_cast<int>(static_cast<std::int64_t>(x))` where a NaN or a value beyond the int range can
     arrive). Not `std::lrint` (it reads the FP environment on every call) nor `std::lround` (halves away from zero).
   - Unsigned 32-bit values (random words, tick counts) convert exactly with `static_cast<double>`; through `pc24::` where the original's
     operation rounded at 24 bits. Test a NaN with `std::isnan` where detecting it is the code's purpose.

8. **Comparisons are plain C++ (the NaN audit, C16c).** Decode VC6's FCOM / FNSTSW / TEST AH to the comparison the
   source had: C3 (`TEST AH,0x40`) is `==`, C0 (`TEST AH,1`) `<`, C0|C3 (`TEST AH,0x41`) `<=` (its negation `>`), C0 as a
   select mask `a < b ? x : y`. The original counted a NaN operand as equal and less; the plain comparison follows IEEE, a
   behaviour change only for NaN (none reaches these places in the suite). Next to a comparison whose result changes for a
   NaN, `EI_NAN_AUDIT(values...)` (`lib/nan_audit.h`) logs the first NaN per site to ei-rebuild.log in the verify and debug
   builds. Where an equally plain form keeps the original's NaN result (`a > b`; `x < low ? x : low` for a bound a NaN never
   replaces; `if (!(gap > limit)) break;` for a loop a NaN must still end), prefer it. addon.dll was built by an IEEE
   compiler (its tests read C2): its status tests are exact plain comparisons. A select whose C0 mask picks `ifLess` is
   `a >= b ? otherwise : ifLess` (exact for NaN). The audit is complete (C16c-C16g; docs/notes/cleanup-tooling.md
   names the few original tests kept and why).
9. **Rebirth's patches are flags.** The standalone image never loads Rebirth's addon.dll or
   EI_Plugin.asi; what they change in game.exe is our code's to reproduce, inside our functions behind
   flags; a patched function is called by name like any other. The image keeps Rebirth's data constants in memory as addon writes them (00742B08,
   the interface's 800-pixel layout width, becomes its widescreen width in WideScreen's startup
   action): read such constants from memory, as the original does, instead of compiling their values
   in. A rewrite may reproduce a Rebirth patch behind its flag (`ei::rebirth::Enabled(Patch::<name>)`,
   `include/ei/rebirth.h`; what each patch does is in `docs/notes/rebirth.md` and
   `config/rebirth_patches.csv`, whose status column records which rewrite carries it).
   Flags are off by default; with a flag off the code must behave exactly as the original; the image's
   Rebirth goldens check the flags on (config/ei_rebirth_standalone.ini).
10. **Keep the original's bugs** (leaks, odd messages) and note them under "Original bugs kept on
    purpose" in your notes file.

## Clean code

New and changed code is written the way the cleanup plan (docs/cleanup-plan.md) wants it, so it
does not need a second pass:
- fields are members of the class's one declaration (in its header), not `At<>`/`Field<>` offsets or `*At` view structs; add the members you need;
- classes are normal C++ ("Where code goes"): real virtual functions, no hand-built tables, no layout
  asserts unless the bytes leave the process or come from game.exe's data image;
- virtual calls go through declared and named virtual functions, not `VCall` and not a placeholder
  `SlotNN()` (name the slot when you call it);
- no byte arithmetic on pointers with literal offsets (`reinterpret_cast<uint8_t*>(p) + 0x..`);
- game globals are named variables (C18, `tools/named_data.py`), not literal addresses in the code;
  the forwarders and `EI_RECORD` keep their address;
- no `__asm`, no `#pragma float_control` (rule 7);
- no new forwarders (rule 5);
- no one-line getter that only returns a field or a global under another spelling (C38): callers read the public
  field or the named global (`g_random`, `net::g_gameClient`, `g_logicSettings`), also through a dereference or an
  address (`g_itemDatabase.weapons`, `&g_camera`, `unit->brain.memory`). Getters stay where they are virtual
  (ClassName, the manager context's slots), compute or convert (`return this`, `&stack.back()`), give a base
  class's shared field its meaning in a subclass (a pair message's `ConnectionRef()` for `second`), or are the
  only const view of a file's state (the renderer's `FrameNumber()`). A field is named by what it means, not
  through a getter (C40);
- no member named after its offset or format id (`flag18`, `field1B`, `spareDC_`, `unknown08_`; C40): a field
  something reads is named from its setters and readers; a fixed format's field nothing reads (an archive field,
  a map chunk) goes to the record's skip block (`unreadFields`, `unreadField`, `unreadText`, ...) with the format's
  ids in its comment, read with the same byte counts; a field nothing reads outside any format is deleted with
  its clears and copies (check literal-size memset / memcpy and `offsetof` ranges first); bytes a save or packet
  carries as a block stay as padding (`unused_XX`, `reserved`);
- kinds, class ids, flag words and save versions are named by the one enum or constant in the owning header (C45),
  not by a local copy of its values or a bare number: `ClassId` (engine_object.h; GetClassId returns it), `ItemKind`,
  `QuickItemCategory`, `LootKind`, `LootRecordType` (item_db.h), `UnitOrderKind`, `UseSource`, `AIModeType`,
  `AIOptionKind` (unit_ai.h), `AIRemark` (ai_alarm.h), `LandscapeFlag`, `SectorStage` (terrain.h), the
  `kVersion*` a load tests (save_versions.h). A manager's creation type is not a class id (the map object factory
  makes class kClassUnit from types 0x32..0x34; the spell manager's are `SpellObjectType`);
- new text is `std::string`, with `src/lib/str.h` for what CString did that std::string has no member for (Find
  with -1, Mid, Format with the game's digits, the trims; C14a, `docs/notes/core.md`, "Strings");
- containers are `std::` (`lib::HashMap` / `lib::HashMultimap`, `src/lib/hash_map.h`, where STLport's walk order is
  behaviour; `lib::IntroSort` for the sorts whose order of equal keys is behaviour). Do not declare a container
  layout per file (`struct FooArray { void* vtable; T* data; int size; ... }`, `struct ListNode { next, prev }`)
  and do not hand-inline SetSize / RemoveAt / Add: call the container's member. What the original's
  instantiations were: `docs/notes/core.md`, "Shared containers";
  no `using` alias for a container used once (spell `std::vector<Foo>` where it is used); an alias stays only where it
  names a concept used in several places. `python tools/alias_census.py` lists the aliases by use count;
- parameters and fields carry their types (C21): no `void*` that every caller and the body cast;
  objects come from `new T` (rule 6), not a `static_cast` of a raw block (tools/progress.py counts the
  pointer casts; check_cleanup does not); objects of a manager come typed from
  `BaseManager::CreateObjectAs<T>` / `FindObjectAs<T>`, item records by kind from
  `itemdb::RecordAs<T>`, and a manager's context is a `CManagerContext` (CWorld, CParty; its
  slots 19, 20 and 22 return `CMapObjectManager*`, `CPlayerManager*` and `CSpellManager*`); a window surface is drawn on through `UiSurface`'s own members (`DrawTextIn`, `FillRect`, ...), not a
  per-file view struct derived from it. A window class that shares another's layout derives from
  it (UiItemStrip : UiItemPanel, MpHeroScreen : ItemScreen) instead of holding a `UiWindow
  window` member and being punned; the item panels' owner is the ItemScreen. A container's input
  side is `UiContainer::ContainerInput()`. An item screen's slots 42-54 are called through
  `ItemScreen::Virtual*` (VirtualHeroRecord, VirtualPrice, ...), a panel's Build through
  `UiWindow::VirtualBuild`, not a `VirtualSlot` with a local function type; three pointers that
  are a vector are a `std::vector<T>` member. The interface's active screen is
  `Interface3D::ActiveWindowAs<Screen>()`; a counted helper a world holds is a `Ref<T>` field
  released with `DestroyOwned`; the new connections a player source hands over are a `JoinedLinks`
  (`std::list<netlink::NetLink*>`); a window's handled flag is
  its `handled` member (UiInputSide). A counted object is tested with `IsLive(object)` (core_refcount.h: set and
  not detached), not a per-file `Live(const void*)`; the server session's connections are `Connection*`
  and its player records `PlayerMessage*` (FindGuestConnection, FindPlayer, Kick, Ban); a file
  written by a transform slot is a `platform::File*`, and so is a chunk's `Chunk::File()`. A use order's spots
  are a `UseSpotArray` (map_object.h, `std::vector<Vector3>`: slot 44, the levers, FindUseSpot); a pair message's
  connection is a `Ref<Connection>`; a session's message lists are
  `std::list<SharedRef<T>>` / `std::list<HeldRef<T>>`; a map object's
  slots are called on the typed object or through `ObjectSlots(object)`, not a hand-written vtable call or a cast per file.
- one typed view instead of a cast per accessor (C15): an object embedded in another and built by
  the owner's code is a `lib::InPlace<T>` member (`src/lib/in_place.h`), not a byte array with a casting accessor. A counted
  pointer field is a `Ref<T>` (src/game/ref.h: Assign and Reset count, reads see the object, there is no plain store),
  a container element that holds its reference a `HeldRef<T>`, not a `T*` field counted by hand; a site that keeps a
  raw pointer counts with T's `AddRef()` / `Release()`, not `++refCount` (docs/notes/core.md, "Counted pointers"); a record that starts with another struct's
  fields derives from it (UnitAction : AIOrder, SectorBox : ClipBounds) and the functions that
  read only the prefix take the base.
`build.cmd` runs `tools/check_cleanup.py`: per file, the counts of these patterns may only go down
(config/cleanup_baseline.csv; a new file starts at zero). After a cleanup ticket lowers them, run
`python tools/check_cleanup.py --update`.

## Names and files

- Members have plain names, no trailing `_` (`size`, `owner`, `refCount`): `private` already marks what a class
  guards. Where a member and a parameter or local would share a name, rename the parameter or local (`newSize`,
  `ownerWindow`); never write `this->` to tell them apart. /W4's C4458 (a declaration hides a class member) finds
  the clashes.
- Classes, structs and enums have no `C` prefix (`WorldClient`, `ItemScreen`, `AIMap`); where a member function
  already has the plain name (`Party()`, `World()`), that function is renamed in the same step.
- Rename a type, a member or a file wherever a better name makes the code easier to read; the rename goes
  through the whole tree in one step, the compiler as the checker.
- Files are grouped by feature, not by the original's address order or vtable slot: a class's members live in
  its own subsystem's files, and no file exists only for a slot range, a lifetime or a few small records.

## Verification

A change is held to the original's behaviour by the two checks below. Neither depends on the heap or
on memory layout, so a change of a class's layout is checked like any other change.

- **Goldens** (tests/golden, docs/notes/standalone-verification.md): a scenario recorded from the
  original game (`--no-hooks`) and replayed on the image (`work.py test <ID> --golden`), its
  state digest and saves compared field by field. Reach the function from a scenario that has a
  golden, or add one under `tests/scenarios/` (its golden is recorded centrally).
- **Function vectors** (tests/vectors): a recorder (`EI_RECORD(addr, Recorder, stolenBytes,
  originalPtr, OurFunction)` in `tests/vectors/record_<family>.cpp`; `OurFunction` is what the replay calls, its
  stack arguments checked by check_signatures) samples the original's calls in a
  `--no-hooks` run with `EI_VECTORS=1`, and `ei_vectors.exe` (build.cmd) replays them against ours
  without the game. For pure functions and ones that only write an output object. `stolenBytes`
  must cover whole instructions at the function start, at least 5, with no relative operands (no
  CALL/JMP/Jcc in them); `python tools/check_stolen.py` checks every recorder.

## Notes

Record in `docs/notes/<area>.md`:
- anything you leave tied to game.exe (an address, a byte layout, and why), also in
  docs/standalone-cleanup.md;
- original bugs you kept;
- functions you skipped and why;
- how each rewrite is checked (the scenario with a golden that reaches it, or its vectors).

Commit on your branch with clear messages. End commit messages with the co-author line used in this
repo's history.
