# Cleanup tooling (ticket C1, cleanup plan Phase 0)

Tooling only; no source file under `src/` changed.

## `tools/progress.py`: cleanup artefacts

After the usual rewritten/verified numbers it prints a "cleanup artefacts" block over `src/*.cpp`
and `*.h` (comments blanked first, so a mention in a comment does not count). One small function per
count; the plain "how often does this appear" counts are two regex tables at the end of the file.
`tools/srcscan.py` holds what both scripts share (loading the sources, blanking comments, finding
the `EI_ORIGINAL(0x...)` sites).

How each number is defined, so later runs stay comparable:

- **EI_ORIGINAL forwarders**: sites of `EI_ORIGINAL(0x...)`, and distinct addresses.
- **forwarded from several files**: addresses with a forwarder in more than one file.
- **naked, not forwarders**: `__declspec(naked)` whose declaration reaches `{` before `EI_ORIGINAL`
  (a hand-written asm body). The plan's "328 x87/asm helpers" counted something wider.
- **files with #pragma float_control**: real pragmas only (the baseline's 83 included
  `particle_common.h`, which only mentions it in a comment).
- **unknown_XXX fields**: occurrences of `unknown_<hex>`.
- **At<> / Field<> uses**: occurrences of `At<` and `Field<`. **files defining their own At()**:
  any file with a function `At(...) {` (the baseline's 47 matched only the exact
  `T& At(void* object, int offset)` form; that form is 53 files today).
- **ei::VCall uses**: `VCall<`.
- **game data addresses**: distinct literals `0x0073xxxx`-`0x008Fxxxx` (data, bss, vtables, strings).
- **`__fastcall Fn(T* self, void*)` shims**: non-naked `__fastcall` definitions with a body whose
  first parameter is a pointer and whose second is `void*` (the EDX dummy of the thiscall trick),
  with any further parameters; the number in brackets is the strict two-parameter form. The
  baseline's 174 used a narrower pattern that we could not reproduce, so do not compare it with
  today's number.
- **struct names defined in several files**: `struct`/`class` with a body at column 0, same name in
  more than one file.
- Also reported from the plan's table: `reinterpret_cast`, `constexpr uintptr_t kX = 0x00...`,
  `*At` view structs, anonymous namespaces, `mfc::`/`stlp::`/`gamecrt::`/`gameapi::` uses,
  `EI_REPLACE` registrations.

## `tools/gen_originals.py`: `docs/originals-report.md`

Reads `src/` and writes the report (never edits sources): every original address that has an
`EI_ORIGINAL` forwarder in more than one file, with each file, line and declared signature (the
declaration back to the previous statement, comments and `__declspec(naked)` removed, parameter
name comments kept). Each address is tagged by how far the files agree (identical, same signature
with different helper names, differing types or calling convention) and sorted into three groups:
already rewritten (a direct call replaces the forwarders), library code (goes with the library
layer) and not yet ported (one shared declaration; the future `src/game/originals.h`).

The output is deterministic. `python tools/gen_originals.py --check` exits 1 when the committed
report is stale. "Rewritten" means an `EI_REPLACE(0x...)` / `EI_REPLACE_METHOD(0x...)` with a literal
address (the macro-built registrations in `ui_hud.cpp` are not seen, as in `progress.py`).

## `tools/check_returns.py`: void declarations whose callers use EAX (T1)

Two crashes came from rewrites declared `void` whose original leaves a value in EAX that callers
use (UnitActionPoint 0052FDF0 returned its out pointer, CUiWindow::AddSurface 005FFA90 the surface
it was given). For every `EI_REPLACE` / `EI_REPLACE_METHOD` / `EI_ORIGINAL` declared void (map +
undname; the parsing is shared with `check_signatures.py`, which now exposes `declarations()`,
`registrations()` and `candidates_for()`), the tool finds the direct `CALL rel32` sites in game.exe
(checked to be instruction starts by disassembling the containing function) and follows the code
after each call with capstone, both ways at conditional jumps, until EAX is written, a call, or
RET. Reading EAX/AX/AL/AH first flags the site. A path reaching RET (and a `JMP rel32` tail call)
means the caller hands EAX to its own callers, which are followed up to three levels. Sites inside
replaced functions are skipped. A write of AL/AX counts as a full write (MSVC's `MOV AL,..; PUSH
EAX` bool argument, `FNSTSW AX; SHL EAX,n`), and `XOR/SUB/SBB EAX,EAX`, `OR EAX,-1`, `AND EAX,0`
are writes; without those rules the first run had five false positives (Chunk::Destruct,
Chunk::WriteFloat, DriverSession::Pump, CUiWindow::ClearRegions, CWorldServer::UpdateWeather).

Not seen: calls through vtable slots and other indirect calls (UnitActionPoint is slot 53, so the
tool cannot find its crash); the summary counts void replacements with no direct call site.
`--address 005FFA90` checks an address whatever its declaration: it flags AddSurface's three sites
(0061C008, 0062BB3D, 0062BBA7, MOV reg,EAX).

build.cmd runs it after check_thunks with the build's own map. Replacement hits fail the build
unless listed in `tools/check_returns_allow.txt`; forwarder hits are warnings, counted and listed
with `-v` (our code ignores the value, and the check fails once such a function is replaced with
the void declaration). First run: 0 replacement hits, 22 forwarder warnings, e.g. constructors
returning `this` declared void (StringFromChars, MessageStringConstruct, RecordsConstruct),
FatalDirectX (returns 0 when it does not exit; callers compare it), BitStream::ReadString, SurfaceDraw::Begin/Text,
CampItems::ReturnAll, PartyAdd, SoundStop. No declarations needed fixing.

## C3, first part: shared forwarder headers

One declaration per address for 156 of the 213 forwarded addresses that the report marked identical or
"names differ" (405 sites in 151 files; the nested ones inside `namespace {}` / named namespaces moved to the
global shared header). The headers are `src/game/originals_<area>.h` (core, engine, script, render, units,
world, ai, ui, gameplay, effects by address range; library for STLport/MFC/CRT code). Each declares the
forwarders as `__declspec(naked) inline ... EI_ORIGINAL(addr)` and forward-declares the types it names (so
including them does not pull class headers or their globals into the files); the files include the header after
their own includes and use one helper name per address (the most common one; the rest were renamed in the
files, and in files that include an edited header). Rewritten targets stay forwarders (no call to the hook).
Left alone (57 addresses still listed): member-style declarations (`Str::Mid`, `SurfaceDraw::Clear`, ...),
signatures that use types defined in several places or only in a .cpp, the definitions in `src/lib/`, and
0x006E9180 (`scriptparse::Strncmp` is the same address declared elsewhere). Not touched: the 504 addresses whose
signatures differ in types or calling convention.

Void fixes: every forwarder `check_returns -v` listed (22 at the start, 38 sites in the end, because each run
only names one declaration per address) now has the real return type: constructors `this` (`StringRep*`,
`RawString*`, `void*`), `PartyAdd`/`QueueAdd`/`Text`/`PlaySound`/`FatalDirectX`/`GetText`/`ServerUpdateAt` `int`,
`FindFreeCellNear`/`AddressInit` `bool`, `SurfaceDraw::Begin` and `ReturnAll` `void*`, `TopicsInsert` `void**`;
member forwarders changed in their class stubs too. `check_returns` now reports 0 forwarder warnings.

## C3b (top 10 by site count): one declaration each

Decided from the game.exe disassembly (RET n, callers' use of EAX/ST0/AL), each now declared once:

| Address | Declaration | Header |
| --- | --- | --- |
| 006E727B | `int __cdecl GameAtExit(void(__cdecl*)())` (atexit, RET, 0/-1) | originals_library.h |
| 00455950 | `void* __fastcall ObjectWorld(const void*)` (object +0x14 -> +0xAC, slot 13, or null) | originals_engine.h |
| 0040C720 | `void __fastcall StlStringDestroy(void*)` (STLport string dtor; plain RET, the old `(void*, void*)` forms dropped the dummy EDX) | originals_library.h |
| 006FDAE9 | `CString CString::Left(int) const` (RET 8: result slot, count), a member of `mfc::CString` | lib/mfc.h, lib/mfc.cpp |
| 005C1010 / 005C0DF0 | `bool __fastcall SectorMapRemove / SectorMapAdd(CSectorMap*, void*, CEngineObject*)` (AL = done, RET 4); callers cast with `AsSectorMap` / `AsEngineObject` | originals_ai.h |
| 00700970 | `mfc::CString::~CString`; `CStringDestroy(void*)` is an inline over it | originals_library.h |
| 006FE925 | `void* __fastcall StringArrayConstruct(void*)` (returns `this`); `console::CStringArray::CStringArray()` calls it | originals_library.h |
| 005FDC00 / 005FDC60 | `ui_gdi::GameDC* __fastcall SurfaceGetDC(void*)` / `void SurfaceReleaseDC(void*)` (surface = CUiSurface) | originals_ui.h |

The class members that were forwarders for the same address (`CUnit::World`, `CMapObject::World`,
`CVisibilityFog::World`, the per-file `SurfaceDraw::Begin/End`, `ui_gdi::SurfaceDC::GetDC/ReleaseDC`,
`StripSurface`, `BoxSurfaceAt`, `SurfaceText`) are now one-line inline wrappers over the shared forwarder.
`CString::Left` callers that built the result into raw memory (`scene_material`, `world_briefing`,
`world_game`) now use a normal `CString` local. The shared forwarder replaced a duplicate local `CIacos` in
sound_manager.cpp (identical bytes) that clashed with originals_library.h.

## C3c (next 15 by site count): one declaration each

Same method as C3b. 494 of the 509 "differ" addresses are left (the 15 below were removed from the group). Decided
from the game.exe disassembly (capstone), each now declared once in `originals_<area>.h`:

| Address | Declaration | Header |
| --- | --- | --- |
| 00455940 | `void* __fastcall ManagerContextOf(const void*)` (object +0x14 -> +0xAC, plain RET) | originals_engine.h |
| 0050E9F0 | `void* __fastcall UnitMarkChanged(void*, void*, int part)` (RET 4; EAX on exit is the object's world or null, which the tail-calling rewrites hand on, so it is not void) | originals_units.h |
| 0041F460 / 004217E0 | `void* __fastcall StringInitEmpty(void*)` (RET, returns `this`) / `StringInitWithAllocator(void*, void*, const void*)` (RET 4, returns `this`) | originals_library.h |
| 00428F10 / 00425040 | `void __fastcall StringAssignSpan(void*, void*, const char*, const char*)` (RET 8) / `StringBufferFree(void*)` (RET). `StringAssignRange` was taken by 00426A80 | originals_library.h |
| 00403F40 | `CEngineObject* __fastcall ManagerFindObject(void*, void*, int)` (RET 4, object or null) | originals_core.h |
| 0057F440 | `bool __cdecl IsNetworkClientOriginal()` (no arguments: it reads the global at 0079B5D8 and never touches ECX; the callers' `0x007AFE20` / `kGameFlow` ECX arguments were dummies) | originals_world.h |
| 005935C0 | `void __fastcall WorldRemoveObject(void*, void*, int id)` (RET 4) | originals_world.h |
| 00434FE0 / 006FC0B0 / 006FC31D | `void* __fastcall OstreamManipulate(void*, void*, void*)` / `OstreamPutChar(void*, void*, char)` / `OstreamPutString(void*, void*, const char*)` (all RET 4, return the stream) | originals_library.h |
| 004800E0 | `int __cdecl FatalDirectX(long, const char*, ...)` (printf style; the `(long, const char*)` copies were the same call with no varargs) | originals_engine.h |
| 005FDE20 | `int __fastcall SurfaceDrawText(void*, void*, const char*, tagRECT*, int, uint32, int, int)` (RET 0x18) | originals_ui.h |
| 005C0BD0 | `void __fastcall SectorMapFindUnitsInRadius(void*, void*, const void* center, void* list, float)` (RET 0xC) | originals_ai.h |

Class members that forwarded the same address (`AiLogStream` / `Ostream` / `GameOstream` / `GameOStream` / `LogStream` /
`UnitLogStream` members, `SurfaceDraw::Text`, `StripSurface::Text`, `BoxSurfaceAt::Text`, `SurfaceTextAt::Text`,
`SurfaceText::Draw`, `StlString::InitEmpty/AssignRange/Destroy`, `Network::IsMultiplayer`, `SessionAt::IsMultiplayerClient`,
`World::RemoveObject`, `Unit::MarkChanged`, `CMapObject::MarkChanged`) are now one-line definitions over the shared forwarder.
`world_effect_internal.h` keeps `effect::ManagerContext` / `effect::FindObject` as inline wrappers. The release and verify builds,
check_stolen, check_x87, check_signatures and check_returns (0 problems, 0 forwarder warnings) pass.

## C3d (next 15 by site count): one declaration each

Same method as C3b/C3c; the "differ" group is 482 addresses after this (it was 497). Decided from the game.exe disassembly
(capstone), each now declared once:

| Address | Declaration | Header |
| --- | --- | --- |
| 006FE958 / 006FE9B8 | `void __fastcall StringArrayDestruct(void*)` (plain RET) / `StringArraySetSize(void*, void*, int count, int growBy)` (RET 8); `console::CStringArray` members are one-line wrappers | originals_library.h |
| 004DBE40 | `void __fastcall StringSplit(const char*, void* array, char)` (ECX text, EDX array, RET 4) | originals_library.h |
| 004DA7F0 / 004DA910 | `void* __fastcall GrowingBufferConstruct(void*)` (returns `this`, plain RET) / `void GrowingBufferDestruct(void*)`; the old `(GameBuffer*, void*)` forms passed a dummy EDX that is dropped | originals_library.h |
| 0040C5A0 / 0040C770 | `void* __fastcall StringInitFromChars(void*, void*, const char*, const void* alloc)` (RET 8, returns `this`; files keep typed `static` wrappers) / `void StringRepDestroy(void*)` (plain RET) | originals_library.h |
| 006E79A0 | `void* __cdecl GameMemmove(void*, const void*, size_t)` | originals_library.h |
| 00480080 | `int __cdecl FatalErrorOriginal(const char*, ...)` (plain RET). `FatalError` is the rewrite in app.cpp, so the shared name differs; `src/lib/crt.cpp` keeps its own copy (lib cannot include game headers) | originals_engine.h |
| 004DEFC0 / 004E0020 | `int __fastcall TextsOpenSection(void*, void*, const char*)` (RET 4, EAX 0/1) / `TextsGetString(void*, void*, const char*, mfc::CString*)` (RET 8, EAX 0/1) | originals_engine.h |
| 0055FB00 / 0058E5D0 / 00594180 | `CEngineObject* __fastcall WorldFindUnit(void*, void*, int)` (RET 4, unit or null) / `void WorldRemoveTimed(void*, void*, int)` (RET 4, EAX not a result) / `void* WorldGetPlayer(void*, void*, int)` (RET 4, player record or null) | originals_world.h |
| 00530930 | `void* __fastcall UnitPrototypeOf(const void* unit)` (plain RET, record or null); callers keep `static` typed wrappers (`UnitPrototype`, `HoverPrototype`) | originals_units.h |

Class members that forwarded the same addresses (`World::GetPlayer`, `World::FindUnitObject`, `WorldBase::FindUnit`,
`World::RemoveTimedObject`, `StlString::InitFrom`, `RawString::InitFrom`, `TextReader::Open`, `TextTable::Find`, `Texts::Open/GetString`,
`UiTexts::Find`, `WorldAt::GetPlayer`) are one-line definitions over the shared forwarders. Release and verify builds, check_stolen,
check_x87, check_signatures and check_returns (0 problems, 0 forwarder warnings) pass.

## C3e (next 15 by site count): one declaration each

Same method as C3b-C3d; the "differ" group is 468 addresses after this (it was 483). Addresses with a site in a `net_*` file or in
ui_battle_update.cpp / ui_hud.cpp were skipped (other tickets work there); the next ones by site count were taken. Decided from the
game.exe disassembly (capstone), each now declared once:

| Address | Declaration | Header |
| --- | --- | --- |
| 004C2050 | `void __fastcall PtrArraySetSize(void*, void*, int count, int growBy)` (MFC CPtrArray::SetSize, RET 8) | originals_library.h |
| 006E848B | `int __cdecl GameMbscmp(const char*, const char*)` (plain RET). `src/lib/crt.cpp` keeps a private `MbscmpOriginal` (lib cannot include game headers) under the `unsigned char` `gamecrt::mbscmp` | originals_library.h |
| 006FDA6D | `mfc::CString mfc::CString::Mid(int first) const` (RET 8: result slot, first). It is Mid(nFirst), not Right: the callers' `Right(Length - n)` and `CStringMid` all pass a start index | lib/mfc.h, lib/mfc.cpp |
| 0040A3A0 / 0040F560 | `void __fastcall RefObjectRelease(void*)` (plain RET, EAX not a result) / `float* __fastcall QuaternionAxisY(const void*, void*, float* out)` (RET 4, returns `out`; the `void` copy in world_client_draw.cpp is gone) | originals_core.h |
| 00460AF0 / 00460BC0 | `float __fastcall NamedTableGet(void*, void*, const char*)` (RET 4, ST0) / `void NamedTableSet(void*, void*, const char*, uint32_t valueBits)` (RET 8; raw dword, float callers pass `std::bit_cast`, so no FPU load touches a NaN payload) | originals_engine.h |
| 004DF8D0 | `int __fastcall TextsReadLineOriginal(void*, void*, mfc::CString*, int advance)` (RET 8, EAX 0/1) | originals_engine.h |
| 00500960 / 00468F70 | `void* __cdecl DisplayObject()` (returns the global 007AF060) / `char* __fastcall AllocAligned16(int count)` (plain RET) | originals_render.h |
| 004D23A0 | `int __fastcall ScriptLoadLine(void*, void*, const mfc::CString*, int)` (RET 8) | originals_script.h |
| 00530730 / 00531070 | `void* __fastcall UnitEquipArmourOriginal(void*, void*, void*)` (RET 4, previous item or null) / `int UnitAddWeaponOriginal(void*, void*, void*)` (RET 4, index / 1 / -1) | originals_units.h |
| 00554450 | `void __fastcall ControllerRepositionOriginal(void*)` (plain RET; the `(void*, void*)` forms passed a dummy EDX) | originals_units.h |
| 00675CA0 | `void __fastcall ObjectMarkNewOriginal(void*)` (plain RET) | originals_effects.h |

Every other site keeps its local name and signature as a one-line wrapper over the shared forwarder (generated, then the self-named
duplicates deleted), so call sites are unchanged. The `Right` members of the per-file CString views are gone; their callers use
`CString::Mid`. Release and verify builds, check_stolen, check_x87, check_signatures and check_returns pass (0 problems).

## C3f (next 15 by site count): one declaration each

Same method as C3b-C3e; the "differ" group is 458 addresses after this (it was 471). Script files (script_*.cpp) were not touched, so
00665470 (`Player::GiveItems` in script_exec.cpp) and 00700AA9 (`AssignString` in script_copy.cpp) keep one local forwarder each and stay
listed; the other 13 left the group. Decided from the game.exe disassembly (capstone), each now declared once; every other site keeps its
local name and signature as a generated one-line wrapper over the shared forwarder:

| Address | Declaration | Header |
| --- | --- | --- |
| 00426A80 / 0040B3C0 | `void* __fastcall StringAssignRangeOriginal(void*, void*, const char*, const char*)` (RET 8, returns this) / `char* StringEraseOriginal(void*, void*, char* first, char* last)` (RET 8, returns first) | originals_library.h |
| 0042A4F0 / 00425080 | `void* StringAssignTaggedOriginal(void*, void*, first, last, const void* tag)` (RET 0xC, returns this) / `void* StringEndOfStorageOriginal(void*, void*, const void*, const void*)` (RET 8, returns this) | originals_library.h |
| 00404D70 | `void __fastcall StlNodeFreeOriginal(void* p, size_t bytes)` (ECX, EDX, plain RET); `src/lib/stlp.cpp` keeps its copy | originals_library.h |
| 006E859F | `void __stdcall EhVectorDestructorIterator(void*, unsigned, int, void* dtor)` (RET 0x10; callers passing `uintptr_t` are cast) | originals_library.h |
| 006FEBCE / 006FEBFA | `void __fastcall StringArraySetAtGrowTextOriginal(void*, void*, int, const char*)` / `...StringOriginal(void*, void*, int, const mfc::CString*)` (RET 8; `console::CStringArray` and `StringArray` members are wrappers, the `const CString&` overload passes `&`) | originals_library.h |
| 00700AA9 | `void* __fastcall CStringAssignOriginal(void*, void*, const void*)` (RET 4, returns this); `src/lib/mfc.cpp` keeps `CString::operator=` | originals_library.h |
| 0044C470 | `unsigned __cdecl DebugMessageOriginal(const char*, ...)` (a bare RET; declared to return a value because bink_video.cpp masks EAX). The two printf-style callers (`figure_animation.cpp`, `unit_points.cpp`) call it directly; `src/lib/crt.cpp` keeps `DebugLog` | originals_engine.h |
| 004A54D0 | `void __fastcall StreamResizeOriginal(void*, void*, uint32_t)` (RET 4); `CMemoryStream` / `BitStream` / `SaveStream::Resize` are wrappers | originals_engine.h |
| 004ECB50 | `char* __fastcall AllocAligned32Original(int)` (ECX = bytes, plain RET, 32-byte aligned) | originals_render.h |
| 005E5200 / 00634650 | `int __fastcall CursorSetShapeOriginal(void*, void*, int)` (RET 4, previous shape or -1; `world_game.cpp`'s `RendererSetBusy` was the same call) / `void* MessageBoxConstructOriginal(void*, void*, CInterface3D*, const char*, const char*, int, int, int)` (RET 0x18) | originals_ui.h |
| 00665470 | `void __fastcall PlayerGiveItemsOriginal(void*, void*, void* items, int)` (RET 8, EAX not a result) | originals_gameplay.h |

Release and verify builds, check_stolen, check_x87, check_signatures and check_returns pass (0 problems, 0 forwarder warnings).

## C3g (next 15 by site count): one declaration each

Same method as C3b-C3f; the "differ" group is 445 addresses after this (it was 458). Addresses with a site in `net_*`, `script_*`,
ui_battle_update.cpp, ui_hud.cpp or ui_battle_commands.cpp were skipped, so the 15 are the highest site counts among the rest.
00700AF9 and 006FE90F keep their `src/lib/mfc.cpp` copy (lib cannot include game headers), so they stay listed. Decided from the
game.exe disassembly (capstone); every other site keeps its local name and signature as a one-line inline wrapper (members are
plain definitions) over the shared forwarder:

| Address | Declaration | Header |
| --- | --- | --- |
| 00600270 | `double __fastcall WindowFrameTimeOriginal(void*)` (ECX = window, ST0, plain RET) | originals_ui.h |
| 00578D10 | `void __stdcall SetServerFlagOriginal(int)` (one stack arg, RET 4, ECX unused; the `this` of the old members was ignored) | originals_ui.h |
| 00557E70 | `void __fastcall AnimationStepsDestroyOriginal(void*)` (plain RET) | originals_units.h |
| 004D9880 | `void __fastcall MatrixMultiplyOriginal(float*, void*, const float*, const float*)` (RET 8) | originals_render.h |
| 00700AF9 / 00700E91 | `void* __fastcall CStringAssignTextOriginal(void*, void*, const char*)` (RET 4, returns this) / `int ...CStringFindCharOriginal(const void*, void*, char)` (RET 4, index or -1, low byte of the arg) | originals_library.h |
| 004621A0 / 0052CA10 | `void* __fastcall PtrListNewNodeOriginal(void*, void*, void* prev, void* next)` (RET 8, returns node) / `void __fastcall PtrListRemoveAllOriginal(void*)` (plain RET) | originals_library.h |
| 005719B0 / 0059C660 / 005B16D0 / 006FEEFA / 006FF64A | `void __fastcall {Record,Dword,Point,Word,Byte}ArraySetSizeOriginal(void*, void*, int, int)` (RET 8) | originals_library.h |
| 006FC9C6 / 006FE90F | `void* __fastcall OstreamPutLongOriginal(void*, void*, long)` (RET 4, returns the stream) / `void __fastcall PlexFreeDataChainOriginal(void*)` (plain RET) | originals_library.h |

Release and verify builds, check_stolen, check_x87 and the forwarder checks in build.cmd pass (0 problems, 0 forwarder warnings).

## C3h (next 15 by site count): one declaration each

Same method as C3b-C3g; the "differ" count is 434 after this (it was 449). "Already rewritten" addresses were skipped (a direct call
replaces those); addresses with sites in ui_battle_commands/update or ui_hud were skipped (004E7840, 00578E20, 005E7060); net_* and
script_* files were edited. Decided from the game.exe disassembly (capstone); every other site keeps its local name as a one-line
inline wrapper (members are plain definitions) over the shared forwarder:

| Address | Declaration | Header |
| --- | --- | --- |
| 00405950 | `void* __fastcall NodeAllocateOriginal(size_t)` (ECX = bytes, plain RET); `src/lib/stlp.cpp` keeps its copy | originals_library.h |
| 00421800 | `void* __fastcall StringReserveOriginal(void*, void*, const void* alloc, size_t n)` (RET 8, returns this) | originals_library.h |
| 00426450 | `void __stdcall StringDeallocateOriginal(char* p, size_t n)` (RET 8; ECX/EDX are never read, the old first argument was a dummy) | originals_library.h |
| 00427830 / 00429F10 | `void __fastcall PtrListDestructOriginal(void*)` / `PtrListRemoveAllNodesOriginal(void*)` (plain RET) | originals_library.h |
| 004297A0 | `void* __fastcall MapRegistryFindOriginal(void*, void*, const void* key)` (RET 4, address of the value slot) | originals_library.h |
| 0042A3D0 | `char* __fastcall StringInitRangeOriginal(void*, void*, const char*, const char*, const void* tag)` (RET 0xC, tag unread, returns the end pointer, not this) | originals_library.h |
| 004DA5C0 | `void __fastcall GameBufferGrowOriginal(void*, void*, uint32_t size)` (RET 4); world_savefile's `BufferSeek` is the same call | originals_library.h |
| 004C1290 / 004C2290 | `void __fastcall GroupAssignOriginal(void*, void*, const void*)` (RET 4) / `ScriptGroupDestructOriginal(void*)` (plain RET; the `(void*, void*)` forms passed a dummy EDX) | originals_script.h |
| 005300D0 | `void __fastcall UnitSetControllerOriginal(void*, void*, void* player)` (RET 4) | originals_units.h |
| 00530280 | `double __fastcall UnitGroundHeightOriginal(void*, void*, float, float)` (RET 8, ST0). `UnitGround::Height` in script_eval.cpp now returns `double` so ST0 stays unrounded (its caller casts after adding z) | originals_units.h |
| 005809D0 | `bool __stdcall FlowLoadQuestsOriginal(const char*)` (RET 4, AL; reads the flow from the global 007AF788, ECX unused, so the flow arguments were dummies) | originals_world.h |
| 00661700 / 00661DB0 | `void __fastcall PartyResetOriginal(void*, void*, int, int)` (RET 8) / `void* PartyAddItemOriginal(void*, void*, void* item, int, int)` (RET 0xC; the two flags are read as bytes; returns the item or null) | originals_gameplay.h |

script_copy.cpp dropped its local `RefConstruct` (004B2500, identical to the shared one). Release and verify builds, check_stolen,
check_x87, check_signatures and check_returns pass (0 problems, 0 forwarder warnings). Behaviour unchanged; nothing was run in the game.

## C3i (next 50 by site count, in two commits): one declaration each

Same method as C3b-C3h; the "differ" count is 384 after this (it was 434). Skipped: the "already rewritten" group, 0x00500000-0x00540000,
combat_* files, ui_battle_update/commands, ui_hud, `src/lib` copies, and the cursor call 005E5270. Decided from the game.exe disassembly
(capstone, RET n and the code before each RET); every other site is a one-line wrapper (members plain definitions) over the shared
forwarder, generated and then built. Declarations are in originals_{library,engine,script,ui,gameplay,ai,render,world}.h, each with a
comment saying RET n and what EAX holds. Results that callers never use are void; a few return `this` / the out pointer / the value slot
(004A8930, 004AB720/820, 004C21E0/2680/3B00, 004C96A0, 00595BA0, 0066C180, 006AD000/1E0, 00661050, 006628D0, 004ABD00, 004ACC10, 0040C280,
0041F550, 00700D73, 0041FCD0). Changes of note: 00405720 / 0043DBE0 / 006FEB61 wrappers that returned a value now return void (the value was
never read; the originals leave junk in EAX); 0058EEA0 takes the time as raw dword bits; 005C35F0 / 006611D0 / 00665200 / 00665A10 / 0066C180
/ 0066EF80 / 0066F450 are plain RET, so the old `(void*, void*)` forms dropped their dummy EDX. Verify and release builds, check_stolen,
check_x87, check_signatures, check_returns and check_forwarders pass (0 problems). Behaviour unchanged; nothing was run in the game.

## C3j (next 50 by site count): one declaration each

Same method as C3b-C3i; the "differ" count is 333 after this (it was 384). Taken from the "not yet ported" group by site count, skipping
0x00500000-0x00540000, `src/render`, movie/cursor code, unit_armour_layers.cpp and `src/lib` copies. Decided from the game.exe disassembly
(capstone, RET n and the code before each RET, callers' use of EAX); every other site is a one-line wrapper (members plain definitions)
over the shared forwarder, generated and then built. Declarations are in originals_{world,ui,render,units,ai,engine,script}.h, each with a
comment giving RET n and EAX. Changes of note:

- 00578E20 `void* __cdecl GameFlowOriginal()` (global 007AF788, ECX unused); 00578920 `void __stdcall MenuSetOriginal(int)` and 00578940
  `void __cdecl MenuCallOriginal()` load ECX from globals, so the old `this` arguments were dummies. 005EE050 / 005EE080 / 005EE0D0 are
  `bool __stdcall` over one item pointer (ECX never read), so the `CampKinds` / `UiItemKinds` members ignore `this`.
- 005E7060 and 0058EEE0 only copy their float, so they take raw dword bits (`std::bit_cast` at float callers). 0055FC50 returns the day
  time in ST0 (`double`).
- 00595480 is `NewZone(map, mob, zone)`, all three strings (app.cpp's `int` third argument is a null pointer). 005C98C0 `AiEventsNoise`
  has four stack arguments (source, loudness, kind, at; a null `at` means the source's position); combat_actions passed `at` as an int 0.
- Constructors / copies that return `this` are `void*` (005D3760, 00556870, 00556D10, 004A5D50); plain-RET forms (006010E0, 005B14C0,
  005C6000, 004B5EF0, 005D4DF0, 004E7840/7A40, 004E6BA0) lost their dummy EDX.
- `check_cleanup` reports one problem on main (unit_armour_layers.cpp, 1 offset access, from U9); this ticket did not touch it.

Verify and release builds, check_stolen, check_x87, check_signatures, check_returns and check_forwarders pass (0 problems). Behaviour
unchanged; nothing was run in the game.

## C3k (next 50 by site count): one declaration each

Same method as C3b-C3j; the "differ" count is 283 after this (it was 333). Taken from the "not yet ported" and "library code" groups
by site count (then address), skipping "already rewritten", `src/lib` copies, sites in `ui_*`, bink_video.cpp / ui_cursor_frame.cpp /
`src/render` / harness.cpp, and the cursor call 005E5270 (I10 edits the interface files). The 0x00500000-0x00540000 range was taken this
time (U10 is merged and U11 has not started). Decided from the game.exe disassembly (capstone: RET n and the code before each RET; callers'
use of EAX through `check_returns` logic over all call sites, including the ones inside replaced functions); every other site is a one-line
wrapper (members plain definitions) over the shared forwarder, generated and then built. Declarations are in
originals_{library,units,render,engine,script,ai}.h, each with a comment giving RET n and EAX. Changes of note:

- Containers (originals_library.h): CList / CPtrList `NewNode(prev, next)` (RET 8, returns the node) is one declaration per instance, named
  with the address where the instance is anonymous: 0052CEF0 (CPtrList, data cleared), 0052CDB0 / 00429EA0 / 004CD6D0 / 004CDA30 (the element is
  constructed through ConstructElements). `ListInsert421AF0 / 423190 / 56E8A0Original(list, out, pos, value)` (RET 0xC, returns `out`);
  `CellListA::Insert` / `ObjectList::Insert` take `CEngineObject* const&`, so their wrappers pass `&value`. `StringMapFindOriginal(map, out, key)`
  (RET 8, returns `out`), `NameMapInsertOriginal(map, pair)` (RET 4, returns node + 4), `QuickItemMapAtOriginal` (originals_engine.h; RET 4,
  address of the value slot), `VectorGetAllocatorOriginal(vector, out)` (RET 4, returns `out`), `ByVtableNewNodeOriginal` (ECX not read).
- Array `SetSize(size, growBy)` instances (RET 8, no result): 0044FCD0 figures (0x220-byte elements, virtual destructor), 0046EAD0 12-byte
  elements, 0050FE70 effects, 004C59C0 `IfThen`, 004CD2B0 commands (the two verify files keep their own copy: only `src/` is scanned). Array and
  list constructors return `this` (00468D60, 00557230, 00557B60, 004C2030, 004C98C0); the plain-RET functions whose old `(T*, void*)` forms passed a
  dummy EDX are 00556D40 (ai_plan), 0050EA40 (world_save, world_units), 004C2030 (world_save, world_update), 00557230 (ai_region_reach) and
  00558C50 (world_loot).
- Counted references (originals_units.h): 0052B030 `Reset(slot)` (plain RET), 0052A8F0 `Assign(slot, object)` and 00511B90 `Store(slot, object)` (RET 4),
  0050E220 `Load(stream, context)` (RET 8), 0050E290 `Save(stream)` (RET 4); no result. 0050F7A0 / 0050F900 are the map object part of Load / Save
  (RET 4). 00516A80 returns the part (`CEngineObject*`, callers test it), 00530830 the worn item. 0052E970 takes the mask as a dword.
- 00501AF0 returns 1 in EAX every time (`int`, nobody reads it); 00500970 is plain RET and not a result. 004C8AF0 / 004C9100 `RemoveTail` return
  their `out` (RET 4). 00426860 frees `count` 12-byte nodes (ECX p, EDX count), 0043B310 `bytes`.
- `check_cleanup` fails on main for bink_video.cpp and ui_cursor_frame.cpp (R6, clean-code fix in progress); counts are identical before and
  after this ticket and no other file is reported.

Verify and release builds, check_stolen, check_x87, check_signatures, check_returns and check_forwarders pass (0 problems). Behaviour
unchanged; nothing was run in the game.

## C3l (next 50 by site count): one declaration each

Same method as C3b-C3k; the "differ" count is 261 after this (it was 283). The "not yet ported" and "library code" groups held only 74
differing addresses at the start (the other 209 differing ones are already rewritten and stay with the direct-call work), and most of
those have a site in a `ui_*` file, which this ticket did not touch (I10 works there). So the 50 taken are the next ones by site count
(then address) that have at least one site outside `ui_*`, `src/lib`, `src/render`, dx_surface.cpp, bink_video.cpp and harness.cpp,
plus the five remaining ones with no such restriction (006FF29A, 00700D4C, 00700E17, 007090C2, 0070BB04) because nothing else was left:
55 addresses, 51 new shared declarations (006E6653, 00700AA9, 00665470 and 005E5270 already had one; their other sites became wrappers).
Decided from the game.exe disassembly (capstone: RET n, the code before each RET, callers' use of EAX through the `check_returns` logic
over every call site); every other site is a one-line wrapper (members plain definitions) over the shared forwarder, generated and then
built. Declarations are in originals_{library,units,engine,ui,render,effects,gameplay}.h, each with a comment giving RET n and EAX. Changes
of note:

- Streams (originals_library.h): 006FC14E `OstreamPutUnsignedOriginal` (format "%u"), 006FC91C `OstreamPutSignedLongOriginal` ("%ld"),
  006FC7FA `OstreamPutDoubleOriginal` (RET 8: the double is on the stack); all RET n and return the stream. `Ostream::PutInt(int)` is the
  unsigned one and `PutLong(unsigned long)` / `GameOStream::operator<<(int)` the long one, so the wrappers cast. 006FC9C6 (declared in
  C3g as `OstreamPutLongOriginal`) is the "%d" one.
- CString: 00700B9F / 00700C05 `operator+` (stdcall, RET 0xC, EAX = the result slot; `CStringConcatOriginal` / `CStringConcatTextOriginal`),
  006FD4DE `CString(const char*, int)` (RET 8, returns this), 006FE1D3 TrimLeft(char), 00700D4C `+=(const char*)` (returns this), 00700E17
  GetBufferSetLength (EAX = the buffer). `src/lib/mfc.cpp` keeps its own copies of the operators and members.
- Containers: 004049D0 / 00404A40 / 00404A70 (`list<int>` end, insert, constructor; the insert returns `out`, the constructor `this`), 0068C560
  (insert on a list of pointers, returns `out`), 00573290 (a CPtrList instance's NewNode, `PtrListNewNode573290Original`), 00460500 and 0059D2E0
  (SetAtGrow, RET 8, no result), 004E2FA0 (`vector<dword>::_M_insert_overflow(pos, value, count)`, RET 0xC), 006FF29A (dword array SetSize), 005703C0 /
  0059A100 / 0059C890 (clear of a list of counted objects, the pointer array's destructor, release of a range; none leaves a result).
  006E88FC is the CRT qsort (`GameQsortOriginal`, cdecl).
- CFileFind (006E6370 constructor returns this, 006E63AD destructor, 006E63E9 Close, 006E6433 FindFile(name, flags) EAX 1 or 0, 006E6722
  GetFilePath(out) EAX = out) and CDC (007090C2 SetBkMode EAX = the previous mode, 0070BB04 FillSolidRect RET 0x14).
- Unit record lists (originals_gameplay.h): 006608D0 / 00660AB0 / 00660C90 / 00660E70 add an entry to the quick (+0x730), belt (+0x740), worn
  (+0x750) and spell (+0x760) lists and return it; 00660A50 / 00660C30 / 00660E10 / 00660FF0 remove an entry of the same four lists. The
  removers in world_quests.cpp carried the names of the other lists (`RecordRemoveQuick` was the worn list's, `RecordRemoveWorn` the belt's,
  `RecordRemoveItem` the quick list's); they are renamed to match, and `SyncRecord` / `StoreRecord` call the same functions with the same
  arguments as before.
- 00661F30 returns the item when it only lowered the stack count (else 0); 006620F0, 00665780 return 1 / 0; 00665440 is `int` 0/1 (the unit
  belongs to the player); 00665A70 / 00665B50 leave no result. 0066D490 `PlayerPathMessageOriginal` takes the limit as raw dword bits
  (`std::bit_cast` in combat_planning.cpp, which passed a float); 0066D7F0 returns 1 every time (`int`, nobody reads it).
- 005335E0 `PrototypeMapAtOriginal` (hash map operator[], returns the address of the value slot), 0051FE80 `SkillIndexOfOriginal` (index or -1),
  00581790 `NamedTableConstructOriginal` (returns this), 0068EAF0 `MaterialRegisterOriginal` (EAX 1, or 0 for a null record), 006CA830
  `LightMoveOriginal(lights, light, delta)` (map_object_companion's `AimLight` and world_effect_visual's `MoveLight` are the same call),
  005F74F0 `InterfaceCloseAllOriginal`, 005E5270 (`Renderer::SetActive` in app.cpp is the cursor's Show: `CursorShowOriginal`).
- Plain-RET functions whose old `(T*, void*)` forms passed a dummy EDX: 0059A100 (world_map) and the CFileFind ones in world_game (006E6370,
  006E63AD, 006E63E9, 006E6653).
- Still a forwarder in a `ui_*` file (27 of the 55; the shared declaration exists, so each is a one-line wrapper to write once I10 is merged):
  004049D0 / 00404A40 / 00404A70 / 004E2FA0 / 0059D2E0 / 00665440 (ui_battle_update.cpp:231, 233, 230, 181, 217, 39), 00460500 (ui_battle_commands.cpp:51),
  0051FE80 (ui_item_info.cpp:349), 005335E0 (ui_item_view.cpp:544), 00581790 (ui_global_map.cpp:184), 005F74F0 (ui_create_screen.cpp:236),
  006608D0 / 00660A50 / 00660AB0 / 00660C30 / 00660C90 / 00660E10 / 00660E70 / 00660FF0 / 00661F30 (ui_camp.cpp:1237, 1135, 1238, 1137, 1240, 1136, 1239,
  1138, 1244), 0068EAF0 (ui_window.cpp:527), 006E63E9 / 006E6653 / 006E6722 (ui_saveload.cpp:158, 154, 156), 006FC7FA (ui_hud.cpp:614), 007090C2 /
  0070BB04 (ui_console_window.cpp:424, 423). Only 0059D2E0 and 006FC7FA no longer count as "differ" in the report: their ui site differs from
  the shared declaration by the helper name alone.
- What is left in the group (261): 209 already rewritten, and of the 52 others 41 wait for a `ui_*` site (15 of them have no shared declaration yet:
  005EBDB0, 005EE110, 005F7E60, 005F8F10, 005FC730, 005FD760, 00601110, 006232D0, 006255A0, 0065AFE0, 0065C290, 0065CC00, and 007005AF / 007006E5 /
  007009DE, which also have a `src/lib` or dx_surface.cpp site) and 11 keep a `src/lib` copy (0044C470, 006E6370, 006E63AD, 006E6433, 006FE90F,
  00700AA9, 00700AF9, 00700B9F, 00700C05, 00700D4C, 00700E17; lib cannot include game headers).

Verify and release builds, check_stolen, check_x87, check_signatures, check_returns and check_forwarders pass (0 problems). Behaviour
unchanged; nothing was run in the game.
